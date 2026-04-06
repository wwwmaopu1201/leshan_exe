import { Buffer } from "node:buffer";
import {
  decodeInt,
  decodeString,
  decodeUInt,
  decodeUShort,
  encodeFrame,
  encodeInt,
  encodeString,
  encodeUInt,
  encodeUShort,
  resolveProtocolPreset,
} from "./protocol.js";

const UNICODE_ENCODING = "utf16le";
const PATTERN_NAME_FIXED_BYTES = 44;
const DEFAULT_FILE_CHUNK_SIZE = 256;

export const COMMANDS = {
  HEARTBEAT: { type: 0x0b2a, code: 0x0001, name: "心跳" },
  REGISTER: { type: 0x0b2a, code: 0x0002, name: "注册" },
  TIME_SYNC: { type: 0x0b2a, code: 0x0003, name: "时间同步" },
  DOWNLOAD_PATTERN_COMMAND: { type: 0x0b2a, code: 0x0004, name: "下载花型指令" },
  DOWNLOAD_PATTERN_DATA: { type: 0x0b2a, code: 0x0005, name: "下载花型数据" },
  COMMUNICATION_ERROR: { type: 0x0b2a, code: 0x0006, name: "通信错误" },
  UPLOAD_PATTERN_COMMAND: { type: 0x0b2a, code: 0x0007, name: "上传花型指令" },
  DELETE_PATTERN_FILE: { type: 0x0b2a, code: 0x0008, name: "删除花型文件" },
  READ_PATTERN_LIST: { type: 0x0b2a, code: 0x0009, name: "读取花型文件列表" },
  PRODUCTION_DATA_OLD: { type: 0x0b2a, code: 0x000b, name: "发送生产数据(旧)" },
  PRODUCTION_DATA_NEW: { type: 0x0b2a, code: 0x000c, name: "发送生产数据(新)" },
  UPLOAD_PATTERN_DATA: { type: 0x0b2a, code: 0x000d, name: "上传花型数据文件" },
  TRANSFER_RESUME: { type: 0x0b2a, code: 0x000e, name: "花型文件续传" },
  REQUEST_SERVER_PATTERN_LIST: { type: 0x0b2a, code: 0x000f, name: "下载花型文件列表" },
  SEWING_RANGE: { type: 0x0b29, code: 0x0015, name: "设备缝制范围" },
  PATTERN_INFO: { type: 0x0b29, code: 0x0016, name: "当前花型编号和名称" },
  BOTTOM_THREAD_COUNT: { type: 0x0b29, code: 0x0006, name: "底线计数" },
  PRODUCTION_COUNT: { type: 0x0b29, code: 0x0003, name: "生产量计数" },
  OIL_PROMPT: { type: 0x0b29, code: 0x0029, name: "油量提示" },
  SEWING_STATUS: { type: 0x0b29, code: 0x0032, name: "开始/停止缝制" },
  REALTIME_STATUS_QUERY: { type: 0x0b29, code: 0x0033, name: "模板机实时状态查询" },
  THREAD_TRIM_COMPLETE: { type: 0x0b29, code: 0x0034, name: "剪线完成" },
  READ_HIGH_POINT: { type: 0x0b29, code: 0x0035, name: "读取中压脚高点位置" },
  SET_HIGH_POINT: { type: 0x0b29, code: 0x0036, name: "设置中压脚高点位置" },
  READ_LOW_POINT: { type: 0x0b29, code: 0x0037, name: "读取中压脚低点位置" },
  SET_LOW_POINT: { type: 0x0b29, code: 0x0038, name: "设置中压脚低点位置" },
  SET_SPEED: { type: 0x0b29, code: 0x0039, name: "设置缝纫速度" },
  NEEDLE_COUNT: { type: 0x0b01, code: 0x001f, name: "当前花型完成针数" },
  DEVICE_MODEL: { type: 0x1302, code: 0x10fa, name: "设备型号/编号" },
  CURRENT_SPEED: { type: 0x1302, code: 0x107c, name: "当前缝纫速度" },
  DEVICE_FLAG: { type: 0x1302, code: 0x157c, name: "设备标志符" },
  SET_DEVICE_FLAG: { type: 0x1202, code: 0x157c, name: "设置设备标志符" },
  MAX_SPEED: { type: 0x1301, code: 0x00a3, name: "最高缝纫速度" },
  ALARM: { type: 0x0b97, code: 0x0001, name: "报警" },
  IDLE_ALARM: { type: 0x0b97, code: 0x0002, name: "空闲超时报警" },
};

const COMMAND_NAME_MAP = new Map(
  Object.values(COMMANDS).map((command) => [`${command.type}:${command.code}`, command.name])
);

function getByteOrder(protocol) {
  return resolveProtocolPreset(protocol).byteOrder;
}

function buildCommandFrame(command, options = {}, protocol) {
  const {
    payload = Buffer.alloc(0),
    addr1 = 0,
    addr2 = 0,
    totalFrames = 1,
    frameNo = 1,
  } = options;

  return encodeFrame({
    addr1,
    addr2,
    type: command.type,
    code: command.code,
    totalFrames,
    frameNo,
    payload,
    protocol,
  });
}

function trimZeroBytes(value) {
  return value.replace(/\0+$/g, "");
}

export function encodeUnicodeString(value = "") {
  return Buffer.from(value, UNICODE_ENCODING);
}

export function encodeUnicodeFixed(value = "", byteLength = PATTERN_NAME_FIXED_BYTES) {
  const raw = encodeUnicodeString(value);
  const buffer = Buffer.alloc(byteLength);
  raw.copy(buffer, 0, 0, Math.min(raw.length, byteLength));
  return buffer;
}

export function decodeUnicodeString(buffer) {
  return trimZeroBytes(buffer.toString(UNICODE_ENCODING));
}

export function decodeUnicodeCString(buffer) {
  let end = buffer.length;
  for (let offset = 0; offset + 1 < buffer.length; offset += 2) {
    const word = buffer.readUInt16LE(offset);
    if (word === 0x0000 || word === 0xfdfd || word === 0xffff) {
      end = offset;
      break;
    }
  }

  return buffer.subarray(0, end).toString(UNICODE_ENCODING);
}

export function encodeFixedString(value = "", byteLength, encoding = "utf8") {
  const raw = Buffer.from(value, encoding);
  const buffer = Buffer.alloc(byteLength);
  raw.copy(buffer, 0, 0, Math.min(raw.length, byteLength));
  return buffer;
}

export function decodeFixedString(buffer, encoding = "utf8") {
  return trimZeroBytes(buffer.toString(encoding));
}

export function commandName(type, code) {
  return COMMAND_NAME_MAP.get(`${type}:${code}`) ?? `0x${type.toString(16)}/0x${code.toString(16)}`;
}

export function isCommand(frame, command) {
  return frame.type === command.type && frame.code === command.code;
}

export function encodeBcdByte(value) {
  const integer = Number(value) || 0;
  return (((Math.floor(integer / 10) % 10) << 4) | (integer % 10)) & 0xff;
}

export function decodeBcdByte(byte) {
  return ((byte >> 4) & 0x0f) * 10 + (byte & 0x0f);
}

export function encodeBcdDateTime(input = new Date()) {
  const date = input instanceof Date ? input : new Date(input);
  const year = date.getFullYear() % 100;
  const week = date.getDay() === 0 ? 7 : date.getDay();

  return Buffer.from([
    encodeBcdByte(year),
    encodeBcdByte(date.getMonth() + 1),
    encodeBcdByte(date.getDate()),
    encodeBcdByte(week),
    encodeBcdByte(date.getHours()),
    encodeBcdByte(date.getMinutes()),
    encodeBcdByte(date.getSeconds()),
  ]);
}

export function parseBcdDateTime(payload, offset = 0) {
  if (payload.length < offset + 7) {
    return null;
  }

  const year = 2000 + decodeBcdByte(payload.readUInt8(offset));
  const month = decodeBcdByte(payload.readUInt8(offset + 1));
  const day = decodeBcdByte(payload.readUInt8(offset + 2));
  const week = decodeBcdByte(payload.readUInt8(offset + 3));
  const hour = decodeBcdByte(payload.readUInt8(offset + 4));
  const minute = decodeBcdByte(payload.readUInt8(offset + 5));
  const second = decodeBcdByte(payload.readUInt8(offset + 6));

  return {
    year,
    month,
    day,
    week,
    hour,
    minute,
    second,
    formatted: `${year}-${String(month).padStart(2, "0")}-${String(day).padStart(2, "0")} ${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}:${String(second).padStart(2, "0")}`,
  };
}

export function createRegisterFrame(protocol) {
  return buildCommandFrame(COMMANDS.REGISTER, {}, protocol);
}

export function createHeartbeatFrame(protocol) {
  return buildCommandFrame(COMMANDS.HEARTBEAT, {}, protocol);
}

export function createTimeSyncRequestFrame(protocol) {
  return buildCommandFrame(COMMANDS.TIME_SYNC, {}, protocol);
}

export function createTimeSyncResponseFrame(date, protocol) {
  const payload = Buffer.concat([encodeBcdDateTime(date), Buffer.from([0x00])]);
  return buildCommandFrame(COMMANDS.TIME_SYNC, { payload }, protocol);
}

export function parseTimeSyncPayload(payload) {
  const parsed = parseBcdDateTime(payload);
  if (!parsed) {
    return null;
  }

  return {
    ...parsed,
    reserve: payload.length >= 8 ? payload.readUInt8(7) : 0,
  };
}

export function createDeviceModelFrame({ model, deviceId, name }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUInt(model, byteOrder),
    encodeUInt(deviceId, byteOrder),
    encodeString(name, "utf8"),
  ]);
  return buildCommandFrame(COMMANDS.DEVICE_MODEL, { payload }, protocol);
}

export function parseDeviceModelPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 8) {
    return null;
  }

  return {
    model: decodeUInt(payload, 0, byteOrder),
    deviceId: decodeUInt(payload, 4, byteOrder),
    name: decodeString(payload.subarray(8), "utf8"),
  };
}

export function createDeviceFlagFrame(flag, protocol, command = COMMANDS.DEVICE_FLAG) {
  return buildCommandFrame(command, { payload: encodeString(flag, "utf8") }, protocol);
}

export function parseDeviceFlagPayload(payload) {
  return decodeString(payload, "utf8");
}

export function createSewingRangeFrame({ xRange, yRange }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([encodeInt(xRange, byteOrder), encodeInt(yRange, byteOrder)]);
  return buildCommandFrame(COMMANDS.SEWING_RANGE, { payload }, protocol);
}

export function parseSewingRangePayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 8) {
    return null;
  }

  return {
    xRange: decodeInt(payload, 0, byteOrder),
    yRange: decodeInt(payload, 4, byteOrder),
  };
}

export function createMaxSpeedFrame(maxSpeed, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.MAX_SPEED,
    { payload: encodeUShort(maxSpeed, byteOrder) },
    protocol
  );
}

export function parseMaxSpeedPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 2) {
    return null;
  }

  return {
    maxSpeed: decodeUShort(payload, 0, byteOrder),
  };
}

export function createCurrentSpeedFrame(currentSpeed, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.CURRENT_SPEED,
    { payload: encodeUShort(currentSpeed, byteOrder) },
    protocol
  );
}

export function parseCurrentSpeedPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 2) {
    return null;
  }

  return {
    currentSpeed: decodeUShort(payload, 0, byteOrder),
  };
}

export function createPatternInfoFrame({ patternId, patternName }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUShort(patternId, byteOrder),
    encodeString(patternName, "utf8"),
  ]);
  return buildCommandFrame(COMMANDS.PATTERN_INFO, { payload }, protocol);
}

export function parsePatternInfoPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 2) {
    return null;
  }

  return {
    patternId: decodeUShort(payload, 0, byteOrder),
    patternName: decodeString(payload.subarray(2), "utf8"),
  };
}

export function createProductionCountFrame({ totalCount, currentCount }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUInt(totalCount, byteOrder),
    encodeUInt(currentCount, byteOrder),
  ]);
  return buildCommandFrame(COMMANDS.PRODUCTION_COUNT, { payload }, protocol);
}

export function parseProductionCountPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 8) {
    return null;
  }

  return {
    totalCount: decodeUInt(payload, 0, byteOrder),
    currentCount: decodeUInt(payload, 4, byteOrder),
  };
}

export function createBottomThreadCountFrame({ totalLength, remainLength }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUInt(totalLength, byteOrder),
    encodeUInt(remainLength, byteOrder),
  ]);
  return buildCommandFrame(COMMANDS.BOTTOM_THREAD_COUNT, { payload }, protocol);
}

export function parseBottomThreadCountPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 8) {
    return null;
  }

  return {
    totalLength: decodeUInt(payload, 0, byteOrder),
    remainLength: decodeUInt(payload, 4, byteOrder),
  };
}

export function createAlarmFrame(alarmCode = 0, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.ALARM,
    { payload: encodeUShort(alarmCode, byteOrder) },
    protocol
  );
}

export function parseAlarmPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 2) {
    return null;
  }

  return {
    alarmCode: decodeUShort(payload, 0, byteOrder),
  };
}

export function createIdleAlarmFrame({ minutes, status }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([encodeUShort(minutes, byteOrder), Buffer.from([status & 0xff])]);
  return buildCommandFrame(COMMANDS.IDLE_ALARM, { payload }, protocol);
}

export function parseIdleAlarmPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 3) {
    return null;
  }

  return {
    minutes: decodeUShort(payload, 0, byteOrder),
    status: payload.readUInt8(2),
  };
}

export function createOilPromptFrame(prompt, protocol) {
  return buildCommandFrame(
    COMMANDS.OIL_PROMPT,
    { payload: Buffer.from([Number(prompt) & 0xff]) },
    protocol
  );
}

export function parseOilPromptPayload(payload) {
  if (payload.length < 1) {
    return null;
  }

  return {
    prompt: payload.readUInt8(0),
  };
}

export function createNeedleCountFrame(needleCount, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.NEEDLE_COUNT,
    { payload: encodeUInt(needleCount, byteOrder) },
    protocol
  );
}

export function parseNeedleCountPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 4) {
    return null;
  }

  return {
    needleCount: decodeUInt(payload, 0, byteOrder),
  };
}

export function createSewingStatusFrame({ status, needleNumber, stopReason }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    Buffer.from([Number(status) & 0xff]),
    encodeUInt(needleNumber, byteOrder),
    encodeUShort(stopReason, byteOrder),
  ]);
  return buildCommandFrame(COMMANDS.SEWING_STATUS, { payload }, protocol);
}

export function parseSewingStatusPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 7) {
    return null;
  }

  return {
    status: payload.readUInt8(0),
    needleNumber: decodeUInt(payload, 1, byteOrder),
    stopReason: decodeUShort(payload, 5, byteOrder),
  };
}

export function createRealtimeStatusQueryFrame(protocol) {
  return buildCommandFrame(COMMANDS.REALTIME_STATUS_QUERY, {}, protocol);
}

export function createRealtimeStatusResponseFrame({ status, patternId, patternName }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    Buffer.from([Number(status) & 0xff]),
    encodeUShort(patternId, byteOrder),
    encodeString(patternName, "utf8"),
  ]);
  return buildCommandFrame(COMMANDS.REALTIME_STATUS_QUERY, { payload }, protocol);
}

export function parseRealtimeStatusPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 3) {
    return null;
  }

  return {
    status: payload.readUInt8(0),
    patternId: decodeUShort(payload, 1, byteOrder),
    patternName: decodeString(payload.subarray(3), "utf8"),
  };
}

export function createThreadTrimCompleteFrame(protocol) {
  return buildCommandFrame(COMMANDS.THREAD_TRIM_COMPLETE, {}, protocol);
}

export function createReadHighPointFrame(protocol) {
  return buildCommandFrame(COMMANDS.READ_HIGH_POINT, {}, protocol);
}

export function createHighPointFrame(value, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.READ_HIGH_POINT,
    { payload: encodeUShort(value, byteOrder) },
    protocol
  );
}

export function createSetHighPointFrame(value, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.SET_HIGH_POINT,
    { payload: encodeUShort(value, byteOrder) },
    protocol
  );
}

export function createSetHighPointResultFrame(result, protocol) {
  return buildCommandFrame(
    COMMANDS.SET_HIGH_POINT,
    { payload: Buffer.from([Number(result) & 0xff]) },
    protocol
  );
}

export function createReadLowPointFrame(protocol) {
  return buildCommandFrame(COMMANDS.READ_LOW_POINT, {}, protocol);
}

export function createLowPointFrame(value, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.READ_LOW_POINT,
    { payload: encodeUShort(value, byteOrder) },
    protocol
  );
}

export function createSetLowPointFrame(value, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.SET_LOW_POINT,
    { payload: encodeUShort(value, byteOrder) },
    protocol
  );
}

export function createSetLowPointResultFrame(result, protocol) {
  return buildCommandFrame(
    COMMANDS.SET_LOW_POINT,
    { payload: Buffer.from([Number(result) & 0xff]) },
    protocol
  );
}

export function createSetSpeedFrame(value, protocol) {
  const byteOrder = getByteOrder(protocol);
  return buildCommandFrame(
    COMMANDS.SET_SPEED,
    { payload: encodeUShort(value, byteOrder) },
    protocol
  );
}

export function createSetSpeedResultFrame(result, protocol) {
  return buildCommandFrame(
    COMMANDS.SET_SPEED,
    { payload: Buffer.from([Number(result) & 0xff]) },
    protocol
  );
}

export function parseUShortValuePayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 2) {
    return null;
  }

  return {
    value: decodeUShort(payload, 0, byteOrder),
  };
}

export function parseSingleByteResultPayload(payload) {
  if (payload.length < 1) {
    return null;
  }

  return {
    result: payload.readUInt8(0),
  };
}

export function createDownloadPatternCommandFrame({ patternName, addr1 = 0, addr2 = 0 }, protocol) {
  return buildCommandFrame(
    COMMANDS.DOWNLOAD_PATTERN_COMMAND,
    {
      addr1,
      addr2,
      payload: encodeUnicodeString(patternName),
    },
    protocol
  );
}

export function parseDownloadPatternCommandPayload(payload) {
  return {
    patternName: decodeUnicodeCString(payload),
  };
}

export function createDownloadPatternCommandAckFrame(
  { patternName, deviceType, deviceId },
  protocol
) {
  return createDownloadPatternCommandFrame(
    { patternName, addr1: deviceType, addr2: deviceId },
    protocol
  );
}

function createChunkFrames(command, dataBuffer, protocol, options = {}) {
  const {
    chunkSize = DEFAULT_FILE_CHUNK_SIZE,
    addr1 = 0,
    addr2 = 0,
  } = options;
  const buffer = dataBuffer ?? Buffer.alloc(0);
  const frames = [];

  if (buffer.length === 0) {
    frames.push(buildCommandFrame(command, { addr1, addr2, totalFrames: 1, frameNo: 1, payload: Buffer.alloc(0) }, protocol));
    return frames;
  }

  const totalFrames = Math.max(1, Math.ceil(buffer.length / chunkSize));

  for (let index = 0; index < totalFrames; index += 1) {
    const start = index * chunkSize;
    const payload = buffer.subarray(start, Math.min(start + chunkSize, buffer.length));
    frames.push(
      buildCommandFrame(
        command,
        {
          addr1,
          addr2,
          totalFrames,
          frameNo: index + 1,
          payload,
        },
        protocol
      )
    );
  }

  return frames;
}

export function createDownloadPatternDataFrames(dataBuffer, protocol, options) {
  return createChunkFrames(COMMANDS.DOWNLOAD_PATTERN_DATA, dataBuffer, protocol, options);
}

export function createUploadPatternDataFrames(dataBuffer, protocol, options) {
  return createChunkFrames(COMMANDS.UPLOAD_PATTERN_DATA, dataBuffer, protocol, options);
}

export function createCommunicationErrorFrame({ totalFrames, frameNo, result }, protocol) {
  return buildCommandFrame(
    COMMANDS.COMMUNICATION_ERROR,
    {
      totalFrames,
      frameNo,
      payload: Buffer.from([Number(result) & 0xff]),
    },
    protocol
  );
}

export function createDownloadPatternResultFrame(result, protocol) {
  return buildCommandFrame(
    COMMANDS.DOWNLOAD_PATTERN_DATA,
    { payload: Buffer.from([Number(result) & 0xff]) },
    protocol
  );
}

export function createUploadPatternCommandFrame({ patternId, patternName }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUShort(patternId, byteOrder),
    encodeUnicodeString(patternName),
  ]);
  return buildCommandFrame(COMMANDS.UPLOAD_PATTERN_COMMAND, { payload }, protocol);
}

export function parseUploadPatternCommandPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 2) {
    return null;
  }

  return {
    patternId: decodeUShort(payload, 0, byteOrder),
    patternName: decodeUnicodeCString(payload.subarray(2)),
  };
}

export function createUploadPatternStatusFrame(status, protocol) {
  return buildCommandFrame(
    COMMANDS.UPLOAD_PATTERN_COMMAND,
    { payload: Buffer.from([Number(status) & 0xff]) },
    protocol
  );
}

export function createDeletePatternCommandFrame({ patternId, patternName }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUShort(patternId, byteOrder),
    encodeUnicodeString(patternName),
  ]);
  return buildCommandFrame(COMMANDS.DELETE_PATTERN_FILE, { payload }, protocol);
}

export function parseDeletePatternCommandPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 2) {
    return null;
  }

  return {
    patternId: decodeUShort(payload, 0, byteOrder),
    patternName: decodeUnicodeCString(payload.subarray(2)),
  };
}

export function createDeletePatternResponseFrame({ patternId, patternName, result }, protocol) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUShort(patternId, byteOrder),
    encodeUnicodeString(patternName),
    Buffer.from([Number(result) & 0xff]),
  ]);
  return buildCommandFrame(COMMANDS.DELETE_PATTERN_FILE, { payload }, protocol);
}

export function createDeletePatternResponseFromRequestPayloadFrame(requestPayload, result, protocol) {
  const payload = Buffer.concat([Buffer.from(requestPayload), Buffer.from([Number(result) & 0xff])]);
  return buildCommandFrame(COMMANDS.DELETE_PATTERN_FILE, { payload }, protocol);
}

export function createReadPatternListFrame(protocol) {
  return buildCommandFrame(COMMANDS.READ_PATTERN_LIST, {}, protocol);
}

function encodePatternListEntry(entry, includeId, protocol) {
  const byteOrder = getByteOrder(protocol);
  const nameBuffer = encodeUnicodeFixed(entry.patternName ?? entry.name ?? "");
  if (!includeId) {
    return nameBuffer;
  }

  return Buffer.concat([encodeUShort(entry.patternId ?? entry.id ?? 0, byteOrder), nameBuffer]);
}

export function createPatternListFrames(command, entries, protocol, options = {}) {
  const { includeId = true, maxPayloadBytes = 920 } = options;
  const entryBuffers = entries.map((entry) => encodePatternListEntry(entry, includeId, protocol));
  const chunks = [];
  let currentChunk = [];
  let currentBytes = 0;

  for (const entryBuffer of entryBuffers) {
    if (currentBytes + entryBuffer.length > maxPayloadBytes && currentChunk.length > 0) {
      chunks.push(Buffer.concat(currentChunk));
      currentChunk = [];
      currentBytes = 0;
    }
    currentChunk.push(entryBuffer);
    currentBytes += entryBuffer.length;
  }

  if (currentChunk.length > 0) {
    chunks.push(Buffer.concat(currentChunk));
  }

  if (chunks.length === 0) {
    chunks.push(Buffer.alloc(0));
  }

  return chunks.map((payload, index) =>
    buildCommandFrame(
      command,
      {
        totalFrames: chunks.length,
        frameNo: index + 1,
        payload,
      },
      protocol
    )
  );
}

export function parsePatternListPayload(payload, protocol, options = {}) {
  const { includeId = true } = options;
  const byteOrder = getByteOrder(protocol);
  const entrySize = includeId ? PATTERN_NAME_FIXED_BYTES + 2 : PATTERN_NAME_FIXED_BYTES;
  if (payload.length === 0 || payload.length % entrySize !== 0) {
    return [];
  }

  const entries = [];
  for (let offset = 0; offset < payload.length; offset += entrySize) {
    if (includeId) {
      entries.push({
        patternId: decodeUShort(payload, offset, byteOrder),
        patternName: decodeUnicodeString(
          payload.subarray(offset + 2, offset + 2 + PATTERN_NAME_FIXED_BYTES)
        ),
      });
    } else {
      entries.push({
        patternName: decodeUnicodeString(payload.subarray(offset, offset + PATTERN_NAME_FIXED_BYTES)),
      });
    }
  }

  return entries;
}

export function createRequestServerPatternListFrame(protocol) {
  return buildCommandFrame(COMMANDS.REQUEST_SERVER_PATTERN_LIST, {}, protocol);
}

export function createProductionDataOldFrame(
  {
    deviceId,
    patternId,
    patternName,
    startTime,
    startNeedle,
    endTime,
    endNeedle,
    stopReason,
  },
  protocol
) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUInt(deviceId, byteOrder),
    encodeUShort(patternId, byteOrder),
    encodeString(patternName, "utf8"),
    encodeBcdDateTime(startTime),
    encodeUInt(startNeedle, byteOrder),
    encodeBcdDateTime(endTime),
    encodeUInt(endNeedle, byteOrder),
    encodeUShort(stopReason, byteOrder),
  ]);
  return buildCommandFrame(COMMANDS.PRODUCTION_DATA_OLD, { payload }, protocol);
}

export function parseProductionDataOldPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  const fixedTailBytes = 7 + 4 + 7 + 4 + 2;
  if (payload.length < 6 + fixedTailBytes) {
    return null;
  }

  const patternNameEnd = payload.length - fixedTailBytes;
  const startTime = parseBcdDateTime(payload, patternNameEnd);
  const endTime = parseBcdDateTime(payload, patternNameEnd + 11);
  if (!startTime || !endTime) {
    return null;
  }

  return {
    deviceId: decodeUInt(payload, 0, byteOrder),
    patternId: decodeUShort(payload, 4, byteOrder),
    patternName: decodeString(payload.subarray(6, patternNameEnd), "utf8"),
    startTime: startTime.formatted,
    startNeedle: decodeUInt(payload, patternNameEnd + 7, byteOrder),
    endTime: endTime.formatted,
    endNeedle: decodeUInt(payload, patternNameEnd + 18, byteOrder),
    stopReason: decodeUShort(payload, patternNameEnd + 22, byteOrder),
  };
}

export function createProductionDataNewFrame(
  {
    deviceId,
    patternId,
    patternName,
    startTime,
    startNeedle,
    endTime,
    endNeedle,
    userId,
    stopReason,
  },
  protocol
) {
  const byteOrder = getByteOrder(protocol);
  const payload = Buffer.concat([
    encodeUInt(deviceId, byteOrder),
    encodeUShort(patternId, byteOrder),
    encodeFixedString(patternName, 44, "utf8"),
    encodeBcdDateTime(startTime),
    encodeUInt(startNeedle, byteOrder),
    encodeBcdDateTime(endTime),
    encodeUInt(endNeedle, byteOrder),
    encodeFixedString(userId, 8, "utf8"),
    encodeUShort(stopReason, byteOrder),
  ]);
  return buildCommandFrame(COMMANDS.PRODUCTION_DATA_NEW, { payload }, protocol);
}

export function parseProductionDataNewPayload(payload, protocol) {
  const byteOrder = getByteOrder(protocol);
  if (payload.length < 82) {
    return null;
  }

  const startTime = parseBcdDateTime(payload, 50);
  const endTime = parseBcdDateTime(payload, 61);
  if (!startTime || !endTime) {
    return null;
  }

  return {
    deviceId: decodeUInt(payload, 0, byteOrder),
    patternId: decodeUShort(payload, 4, byteOrder),
    patternName: decodeFixedString(payload.subarray(6, 50), "utf8"),
    startTime: startTime.formatted,
    startNeedle: decodeUInt(payload, 57, byteOrder),
    endTime: endTime.formatted,
    endNeedle: decodeUInt(payload, 68, byteOrder),
    userId: decodeFixedString(payload.subarray(72, 80), "utf8"),
    stopReason: decodeUShort(payload, 80, byteOrder),
  };
}

export function createTransferResumeFrame({ transferType, frameNo }, protocol) {
  return buildCommandFrame(
    COMMANDS.TRANSFER_RESUME,
    {
      payload: Buffer.from([Number(transferType) & 0xff, Number(frameNo) & 0xff]),
    },
    protocol
  );
}

export function parseTransferResumePayload(payload) {
  if (payload.length < 2) {
    return null;
  }

  return {
    transferType: payload.readUInt8(0),
    frameNo: payload.readUInt8(1),
  };
}

export function describeFrame(frame) {
  if (frame.error) {
    return `非法报文: ${frame.error}`;
  }

  return `${commandName(frame.type, frame.code)} 帧 ${frame.frameNo}/${frame.totalFrames} 数据${frame.payload.length}字节`;
}
