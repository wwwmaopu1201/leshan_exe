import fs from "node:fs";
import http from "node:http";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  COMMANDS,
  commandName,
  createAlarmFrame,
  createBottomThreadCountFrame,
  createCommunicationErrorFrame,
  createDeletePatternResponseFromRequestPayloadFrame,
  createCurrentSpeedFrame,
  createDeviceFlagFrame,
  createDeviceModelFrame,
  createDownloadPatternCommandAckFrame,
  createDownloadPatternResultFrame,
  createHeartbeatFrame,
  createHighPointFrame,
  createIdleAlarmFrame,
  createLowPointFrame,
  createMaxSpeedFrame,
  createNeedleCountFrame,
  createOilPromptFrame,
  createPatternInfoFrame,
  createPatternListFrames,
  createProductionCountFrame,
  createProductionDataNewFrame,
  createProductionDataOldFrame,
  createReadPatternListFrame,
  createRealtimeStatusResponseFrame,
  createRegisterFrame,
  createRequestServerPatternListFrame,
  createSewingRangeFrame,
  createSewingStatusFrame,
  createSetHighPointResultFrame,
  createSetLowPointResultFrame,
  createSetSpeedResultFrame,
  createThreadTrimCompleteFrame,
  createTimeSyncRequestFrame,
  createTransferResumeFrame,
  createUploadPatternDataFrames,
  createUploadPatternStatusFrame,
  decodeUnicodeCString,
  describeFrame,
  isCommand,
  parseDeletePatternCommandPayload,
  parseDeviceModelPayload,
  parseDownloadPatternCommandPayload,
  parsePatternInfoPayload,
  parsePatternListPayload,
  parseSingleByteResultPayload,
  parseTimeSyncPayload,
  parseTransferResumePayload,
  parseUploadPatternCommandPayload,
  parseUShortValuePayload,
} from "../shared/commands.js";
import {
  FrameParser,
  getProtocolPreset,
  PROTOCOL_PRESETS,
  toHex,
} from "../shared/protocol.js";
import { formatLocalDateTime, getLocalTimeZone, nowLocal } from "../shared/time.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, "../..");
const clientPage = path.join(rootDir, "public/client/index.html");
const clientScript = path.join(rootDir, "public/client/app.js");

const DEFAULT_PROTOCOL_PRESET_ID = "be-modbus-lencrc";
const PROTOCOL_MODES = [
  { id: "fixed", label: "固定兼容模式" },
  { id: "auto", label: "自动探测" },
];
const AUTO_PROBE_PROTOCOL_PRESET_IDS = PROTOCOL_PRESETS.map((preset) => preset.id);
const TRANSFER_TYPES = {
  upload: 1,
  download: 2,
};
const REALTIME_STATUS = {
  off: 0,
  idle: 1,
  sewing: 2,
};
const SEWING_EVENT_STATUS = {
  stop: 0,
  start: 1,
  running: 2,
};

function createPattern(patternId, patternName, dataText) {
  const content = dataText?.length
    ? dataText
    : `PATTERN:${patternName}\nID:${patternId}\nGENERATED:${nowLocal()}`;
  return {
    patternId,
    patternName,
    data: Buffer.from(content, "utf8"),
    updatedAt: nowLocal(),
  };
}

const timeZone = getLocalTimeZone();

const state = {
  bindHost: process.env.BIND_HOST || "127.0.0.1",
  tcpHost: process.env.SERVER_HOST || "127.0.0.1",
  tcpPort: Number(process.env.SERVER_TCP_PORT || 9000),
  httpPort: Number(process.env.HTTP_PORT || 9002),
  protocolMode: process.env.PROTOCOL_MODE || "fixed",
  preferredProtocolPresetId: process.env.PROTOCOL_PRESET || DEFAULT_PROTOCOL_PRESET_ID,
  connectionStatus: "disconnected",
  connectedAt: null,
  lastMessageAt: null,
  registerSentAt: null,
  registerAcknowledgedAt: null,
  lastHeartbeatSentAt: null,
  lastHeartbeatAckAt: null,
  lastTimeSyncAt: null,
  logs: [],
  deviceInfo: {
    model: 4346,
    deviceId: 10001,
    name: "Mac模板机模拟器",
    flag: "MB-TEST-0001",
    xRange: 1200,
    yRange: 800,
    maxSpeed: 3200,
    currentSpeed: 1800,
    patternId: 1,
    patternName: "默认花型A",
    highPoint: 60,
    lowPoint: 20,
    oilPrompt: 0,
    alarmCode: 0,
    idleAlarmMinutes: 0,
    idleAlarmStatus: 0,
    productionTotal: 0,
    productionCurrent: 0,
    bottomThreadTotal: 30000,
    bottomThreadRemain: 18000,
    stopReason: 0,
    sewingStatus: SEWING_EVENT_STATUS.stop,
    realtimeStatus: REALTIME_STATUS.idle,
    currentNeedleNumber: 0,
    completedNeedleCount: 0,
    userId: "SR000001",
    timeSyncValue: null,
    lastStartTime: null,
    lastEndTime: null,
  },
  localPatterns: [
    createPattern(1, "默认花型A", "DEFAULT-PATTERN-A"),
    createPattern(2, "样例花型B", "SAMPLE-PATTERN-B"),
  ],
  serverPatterns: [],
  socket: null,
  parser: new FrameParser(),
  heartbeatTimer: null,
  registerRetryTimer: null,
  lastRegisterSentMs: 0,
  lastHeartbeatSentMs: 0,
  protocolProbeIndex: 0,
  lockedProtocolPresetId: null,
  activeProtocolPresetId: DEFAULT_PROTOCOL_PRESET_ID,
  transfers: {
    downloadFromServer: null,
    uploadToServer: null,
    serverPatternList: null,
    lastResumeRequest: null,
    lastResult: null,
  },
};

function appendLog(message, extra = {}) {
  state.logs.unshift({
    id: `${Date.now()}-${Math.random().toString(16).slice(2, 8)}`,
    time: nowLocal(),
    message,
    ...extra,
  });
  state.logs = state.logs.slice(0, 400);
}

function sendJson(response, statusCode, payload) {
  response.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store",
  });
  response.end(JSON.stringify(payload));
}

function sendFile(response, filename, contentType) {
  response.writeHead(200, {
    "Content-Type": contentType,
    "Cache-Control": "no-store",
  });
  response.end(fs.readFileSync(filename));
}

async function readJsonBody(request) {
  const chunks = [];
  for await (const chunk of request) {
    chunks.push(chunk);
  }

  if (chunks.length === 0) {
    return {};
  }

  return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

function createParserForCurrentState() {
  if (state.protocolMode === "fixed") {
    return new FrameParser({ protocols: [state.preferredProtocolPresetId] });
  }

  if (state.lockedProtocolPresetId) {
    return new FrameParser({ protocols: [state.lockedProtocolPresetId] });
  }

  return new FrameParser({ protocols: AUTO_PROBE_PROTOCOL_PRESET_IDS });
}

function getActiveProtocolPreset() {
  const presetId =
    state.protocolMode === "fixed"
      ? state.preferredProtocolPresetId
      : state.lockedProtocolPresetId || state.activeProtocolPresetId || DEFAULT_PROTOCOL_PRESET_ID;
  return getProtocolPreset(presetId) ?? getProtocolPreset(DEFAULT_PROTOCOL_PRESET_ID);
}

function getNextProbeProtocolPreset() {
  if (state.protocolMode === "fixed") {
    return getActiveProtocolPreset();
  }

  const presetId =
    AUTO_PROBE_PROTOCOL_PRESET_IDS[
      state.protocolProbeIndex % AUTO_PROBE_PROTOCOL_PRESET_IDS.length
    ];
  state.protocolProbeIndex += 1;
  state.activeProtocolPresetId = presetId;
  return getProtocolPreset(presetId);
}

function lockProtocolPreset(protocolPresetId) {
  if (state.protocolMode === "fixed") {
    return;
  }

  if (!protocolPresetId || state.lockedProtocolPresetId === protocolPresetId) {
    return;
  }

  state.lockedProtocolPresetId = protocolPresetId;
  state.activeProtocolPresetId = protocolPresetId;
  state.parser = createParserForCurrentState();
  appendLog(`协议已锁定: ${getProtocolPreset(protocolPresetId)?.name ?? protocolPresetId}`);
}

function resetProtocolNegotiation() {
  state.protocolProbeIndex = 0;
  state.lockedProtocolPresetId =
    state.protocolMode === "fixed" ? state.preferredProtocolPresetId : null;
  state.activeProtocolPresetId = state.preferredProtocolPresetId || DEFAULT_PROTOCOL_PRESET_ID;
  state.parser = createParserForCurrentState();
}

function getProtocolPresetLabel() {
  const activePreset = getActiveProtocolPreset();

  if (state.protocolMode === "fixed") {
    return `固定 ${activePreset?.name ?? "-"}`;
  }

  if (state.lockedProtocolPresetId) {
    return `已锁定 ${activePreset?.name ?? "-"}`;
  }

  return `自动探测，当前发送 ${activePreset?.name ?? "-"}`;
}

function getProtocolStatus() {
  if (state.connectionStatus !== "connected") {
    return "未建立协议";
  }

  if (!state.registerAcknowledgedAt) {
    return "等待注册确认";
  }

  if (!state.lastHeartbeatAckAt) {
    return "已注册，等待心跳确认";
  }

  return "已注册，心跳正常";
}

function getPatternSummary(pattern) {
  return {
    patternId: pattern.patternId,
    patternName: pattern.patternName,
    size: pattern.data.length,
    updatedAt: pattern.updatedAt,
  };
}

function getTransferSummary() {
  return {
    downloadFromServer: state.transfers.downloadFromServer
      ? {
          patternName: state.transfers.downloadFromServer.patternName,
          receivedFrames: state.transfers.downloadFromServer.frames.size,
          totalFrames: state.transfers.downloadFromServer.totalFrames,
          startedAt: state.transfers.downloadFromServer.startedAt,
        }
      : null,
    uploadToServer: state.transfers.uploadToServer
      ? {
          patternName: state.transfers.uploadToServer.pattern.patternName,
          totalFrames: state.transfers.uploadToServer.frames.length,
          nextFrameNo: state.transfers.uploadToServer.nextFrameIndex + 1,
          awaitingAck: state.transfers.uploadToServer.awaitingAckFrameNo,
          startedAt: state.transfers.uploadToServer.startedAt,
        }
      : null,
    serverPatternList: state.transfers.serverPatternList
      ? {
          receivedFrames: state.transfers.serverPatternList.frames.size,
          totalFrames: state.transfers.serverPatternList.totalFrames,
          startedAt: state.transfers.serverPatternList.startedAt,
        }
      : null,
    lastResumeRequest: state.transfers.lastResumeRequest,
    lastResult: state.transfers.lastResult,
  };
}

function publicState() {
  return {
    tcpHost: state.tcpHost,
    tcpPort: state.tcpPort,
    httpPort: state.httpPort,
    bindHost: state.bindHost,
    timeZone,
    protocolMode: state.protocolMode,
    preferredProtocolPresetId: state.preferredProtocolPresetId,
    protocolModes: PROTOCOL_MODES,
    protocolPresets: PROTOCOL_PRESETS,
    connectionStatus: state.connectionStatus,
    connectedAt: state.connectedAt,
    lastMessageAt: state.lastMessageAt,
    registerSentAt: state.registerSentAt,
    registerAcknowledgedAt: state.registerAcknowledgedAt,
    lastHeartbeatSentAt: state.lastHeartbeatSentAt,
    lastHeartbeatAckAt: state.lastHeartbeatAckAt,
    lastTimeSyncAt: state.lastTimeSyncAt,
    protocolStatus: getProtocolStatus(),
    protocolPresetLabel: getProtocolPresetLabel(),
    deviceInfo: state.deviceInfo,
    localPatterns: state.localPatterns.map(getPatternSummary),
    serverPatterns: state.serverPatterns,
    transferSummary: getTransferSummary(),
    logs: state.logs,
  };
}

function normalizeNumber(value, fallback = 0) {
  const next = Number(value);
  return Number.isFinite(next) ? next : fallback;
}

function getNextPatternId() {
  return (
    state.localPatterns.reduce((max, item) => Math.max(max, item.patternId), 0) + 1
  );
}

function findLocalPatternByReference(patternId, patternName) {
  return (
    state.localPatterns.find((item) => item.patternId === Number(patternId)) ||
    state.localPatterns.find((item) => item.patternName === patternName) ||
    null
  );
}

function normalizePatternName(patternName) {
  return String(patternName ?? "").replace(/\0+$/g, "").trim();
}

function findLocalPatternByCompatibleName(patternName) {
  const normalizedName = normalizePatternName(patternName);
  if (!normalizedName) {
    return null;
  }

  const exact = state.localPatterns.find((item) => item.patternName === normalizedName);
  if (exact) {
    return exact;
  }

  const fuzzyMatches = state.localPatterns.filter((item) => {
    const localName = normalizePatternName(item.patternName);
    return localName.startsWith(normalizedName) || normalizedName.startsWith(localName);
  });

  return fuzzyMatches[0] || null;
}

function ensureActivePatternExists() {
  let pattern = findLocalPatternByReference(state.deviceInfo.patternId, state.deviceInfo.patternName);

  if (!pattern && state.localPatterns.length > 0) {
    pattern = state.localPatterns[0];
  }

  if (pattern) {
    state.deviceInfo.patternId = pattern.patternId;
    state.deviceInfo.patternName = pattern.patternName;
  }
}

function setActivePattern(patternId) {
  const pattern = findLocalPatternByReference(patternId, null);
  if (!pattern) {
    throw new Error("未找到对应花型");
  }

  state.deviceInfo.patternId = pattern.patternId;
  state.deviceInfo.patternName = pattern.patternName;
  appendLog(`已切换当前花型 ${pattern.patternId} / ${pattern.patternName}`);
}

function upsertPattern({ patternId, patternName, dataText }) {
  const nextId = Number(patternId) || getNextPatternId();
  const nextName = patternName?.trim() || `花型${nextId}`;
  const existing = state.localPatterns.find((item) => item.patternId === nextId);
  const nextPattern = createPattern(nextId, nextName, dataText);

  if (existing) {
    existing.patternName = nextPattern.patternName;
    existing.data = nextPattern.data;
    existing.updatedAt = nextPattern.updatedAt;
  } else {
    state.localPatterns.push(nextPattern);
  }

  appendLog(`本地花型已保存 ${nextId} / ${nextName}`);
  ensureActivePatternExists();
  return findLocalPatternByReference(nextId, nextName);
}

function deletePattern(patternId) {
  const nextId = Number(patternId);
  const target = state.localPatterns.find((item) => item.patternId === nextId);
  if (!target) {
    throw new Error("本地花型不存在");
  }

  state.localPatterns = state.localPatterns.filter((item) => item.patternId !== nextId);
  appendLog(`本地花型已删除 ${target.patternId} / ${target.patternName}`);
  ensureActivePatternExists();
  return target;
}

function findPatternForDeleteCommand(frame, request) {
  const candidates = [
    { patternId: request?.patternId, patternName: request?.patternName },
    { patternId: null, patternName: decodeUnicodeCString(frame.payload) },
    {
      patternId: null,
      patternName: frame.payload.length > 2 ? decodeUnicodeCString(frame.payload.subarray(2)) : "",
    },
  ];

  for (const candidate of candidates) {
    const target = findLocalPatternByReference(candidate.patternId, candidate.patternName);
    if (target) {
      return target;
    }
  }

  return null;
}

function findPatternForUploadCommand(frame, request) {
  const candidates = [
    { patternId: request?.patternId, patternName: request?.patternName },
    { patternId: request?.patternId, patternName: "" },
    { patternId: null, patternName: decodeUnicodeCString(frame.payload) },
    {
      patternId: null,
      patternName: frame.payload.length > 2 ? decodeUnicodeCString(frame.payload.subarray(2)) : "",
    },
  ];

  for (const candidate of candidates) {
    const target = findLocalPatternByReference(candidate.patternId, candidate.patternName);
    if (target) {
      return {
        pattern: target,
        matchedBy: "exact",
        candidate,
      };
    }
  }

  for (const candidate of candidates) {
    const target = findLocalPatternByCompatibleName(candidate.patternName);
    if (target) {
      return {
        pattern: target,
        matchedBy: "name-compatible",
        candidate,
      };
    }
  }

  return null;
}

function updateConnectionConfig({ tcpHost, tcpPort }) {
  if (typeof tcpHost === "string" && tcpHost.trim()) {
    state.tcpHost = tcpHost.trim();
  }

  if (tcpPort !== undefined) {
    const nextPort = Number(tcpPort);
    if (Number.isInteger(nextPort) && nextPort > 0 && nextPort <= 65535) {
      state.tcpPort = nextPort;
    }
  }
}

function updateProtocolConfig({ protocolMode, preferredProtocolPresetId }) {
  if (protocolMode && PROTOCOL_MODES.some((item) => item.id === protocolMode)) {
    state.protocolMode = protocolMode;
  }

  if (preferredProtocolPresetId && getProtocolPreset(preferredProtocolPresetId)) {
    state.preferredProtocolPresetId = preferredProtocolPresetId;
  }

  resetProtocolNegotiation();
}

function updateDeviceInfo(nextValues) {
  state.deviceInfo = {
    ...state.deviceInfo,
    ...nextValues,
    model: normalizeNumber(nextValues.model ?? state.deviceInfo.model, state.deviceInfo.model),
    deviceId: normalizeNumber(
      nextValues.deviceId ?? state.deviceInfo.deviceId,
      state.deviceInfo.deviceId
    ),
    xRange: normalizeNumber(nextValues.xRange ?? state.deviceInfo.xRange, state.deviceInfo.xRange),
    yRange: normalizeNumber(nextValues.yRange ?? state.deviceInfo.yRange, state.deviceInfo.yRange),
    maxSpeed: normalizeNumber(
      nextValues.maxSpeed ?? state.deviceInfo.maxSpeed,
      state.deviceInfo.maxSpeed
    ),
    currentSpeed: normalizeNumber(
      nextValues.currentSpeed ?? state.deviceInfo.currentSpeed,
      state.deviceInfo.currentSpeed
    ),
    patternId: normalizeNumber(
      nextValues.patternId ?? state.deviceInfo.patternId,
      state.deviceInfo.patternId
    ),
    highPoint: normalizeNumber(
      nextValues.highPoint ?? state.deviceInfo.highPoint,
      state.deviceInfo.highPoint
    ),
    lowPoint: normalizeNumber(
      nextValues.lowPoint ?? state.deviceInfo.lowPoint,
      state.deviceInfo.lowPoint
    ),
    oilPrompt: normalizeNumber(
      nextValues.oilPrompt ?? state.deviceInfo.oilPrompt,
      state.deviceInfo.oilPrompt
    ),
    alarmCode: normalizeNumber(
      nextValues.alarmCode ?? state.deviceInfo.alarmCode,
      state.deviceInfo.alarmCode
    ),
    idleAlarmMinutes: normalizeNumber(
      nextValues.idleAlarmMinutes ?? state.deviceInfo.idleAlarmMinutes,
      state.deviceInfo.idleAlarmMinutes
    ),
    idleAlarmStatus: normalizeNumber(
      nextValues.idleAlarmStatus ?? state.deviceInfo.idleAlarmStatus,
      state.deviceInfo.idleAlarmStatus
    ),
    productionTotal: normalizeNumber(
      nextValues.productionTotal ?? state.deviceInfo.productionTotal,
      state.deviceInfo.productionTotal
    ),
    productionCurrent: normalizeNumber(
      nextValues.productionCurrent ?? state.deviceInfo.productionCurrent,
      state.deviceInfo.productionCurrent
    ),
    bottomThreadTotal: normalizeNumber(
      nextValues.bottomThreadTotal ?? state.deviceInfo.bottomThreadTotal,
      state.deviceInfo.bottomThreadTotal
    ),
    bottomThreadRemain: normalizeNumber(
      nextValues.bottomThreadRemain ?? state.deviceInfo.bottomThreadRemain,
      state.deviceInfo.bottomThreadRemain
    ),
    stopReason: normalizeNumber(
      nextValues.stopReason ?? state.deviceInfo.stopReason,
      state.deviceInfo.stopReason
    ),
    sewingStatus: normalizeNumber(
      nextValues.sewingStatus ?? state.deviceInfo.sewingStatus,
      state.deviceInfo.sewingStatus
    ),
    realtimeStatus: normalizeNumber(
      nextValues.realtimeStatus ?? state.deviceInfo.realtimeStatus,
      state.deviceInfo.realtimeStatus
    ),
    currentNeedleNumber: normalizeNumber(
      nextValues.currentNeedleNumber ?? state.deviceInfo.currentNeedleNumber,
      state.deviceInfo.currentNeedleNumber
    ),
    completedNeedleCount: normalizeNumber(
      nextValues.completedNeedleCount ?? state.deviceInfo.completedNeedleCount,
      state.deviceInfo.completedNeedleCount
    ),
    userId: String(nextValues.userId ?? state.deviceInfo.userId),
    name: String(nextValues.name ?? state.deviceInfo.name),
    flag: String(nextValues.flag ?? state.deviceInfo.flag),
    patternName: String(nextValues.patternName ?? state.deviceInfo.patternName),
    timeSyncValue: nextValues.timeSyncValue ?? state.deviceInfo.timeSyncValue,
    lastStartTime: nextValues.lastStartTime ?? state.deviceInfo.lastStartTime,
    lastEndTime: nextValues.lastEndTime ?? state.deviceInfo.lastEndTime,
  };

  ensureActivePatternExists();
}

function sendPacket(packet, label, protocolPreset = getActiveProtocolPreset()) {
  if (!state.socket || state.connectionStatus !== "connected") {
    throw new Error("TCP 未连接");
  }

  state.socket.write(packet);
  appendLog(`客户端 -> ${label} [${protocolPreset?.name ?? "-"}]`, { raw: toHex(packet) });
}

function sendRegister(label = COMMANDS.REGISTER.name, protocolPreset = getActiveProtocolPreset()) {
  state.lastRegisterSentMs = Date.now();
  state.registerSentAt = nowLocal();
  sendPacket(createRegisterFrame(protocolPreset), label, protocolPreset);
}

function sendHeartbeat(protocolPreset = getActiveProtocolPreset()) {
  if (!state.socket || state.connectionStatus !== "connected") {
    return;
  }

  state.lastHeartbeatSentMs = Date.now();
  state.lastHeartbeatSentAt = nowLocal();
  sendPacket(createHeartbeatFrame(protocolPreset), COMMANDS.HEARTBEAT.name, protocolPreset);
}

function sendTimeSyncRequest(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(createTimeSyncRequestFrame(protocolPreset), COMMANDS.TIME_SYNC.name, protocolPreset);
}

function sendCurrentSpeedReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createCurrentSpeedFrame(state.deviceInfo.currentSpeed, protocolPreset),
    COMMANDS.CURRENT_SPEED.name,
    protocolPreset
  );
}

function sendNeedleCountReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createNeedleCountFrame(state.deviceInfo.completedNeedleCount, protocolPreset),
    COMMANDS.NEEDLE_COUNT.name,
    protocolPreset
  );
}

function sendHighPointReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createHighPointFrame(state.deviceInfo.highPoint, protocolPreset),
    COMMANDS.READ_HIGH_POINT.name,
    protocolPreset
  );
}

function sendLowPointReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createLowPointFrame(state.deviceInfo.lowPoint, protocolPreset),
    COMMANDS.READ_LOW_POINT.name,
    protocolPreset
  );
}

function sendRealtimeStatusReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createRealtimeStatusResponseFrame(
      {
        status: state.deviceInfo.realtimeStatus,
        patternId: state.deviceInfo.patternId,
        patternName: state.deviceInfo.patternName,
      },
      protocolPreset
    ),
    COMMANDS.REALTIME_STATUS_QUERY.name,
    protocolPreset
  );
}

function sendSewingStatusReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createSewingStatusFrame(
      {
        status: state.deviceInfo.sewingStatus,
        needleNumber: state.deviceInfo.currentNeedleNumber,
        stopReason: state.deviceInfo.stopReason,
      },
      protocolPreset
    ),
    COMMANDS.SEWING_STATUS.name,
    protocolPreset
  );
}

function sendThreadTrimComplete(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createThreadTrimCompleteFrame(protocolPreset),
    COMMANDS.THREAD_TRIM_COMPLETE.name,
    protocolPreset
  );
}

function sendAlarmReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createAlarmFrame(state.deviceInfo.alarmCode, protocolPreset),
    COMMANDS.ALARM.name,
    protocolPreset
  );
}

function sendIdleAlarmReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createIdleAlarmFrame(
      {
        minutes: state.deviceInfo.idleAlarmMinutes,
        status: state.deviceInfo.idleAlarmStatus,
      },
      protocolPreset
    ),
    COMMANDS.IDLE_ALARM.name,
    protocolPreset
  );
}

function sendCountersReport(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createProductionCountFrame(
      {
        totalCount: state.deviceInfo.productionTotal,
        currentCount: state.deviceInfo.productionCurrent,
      },
      protocolPreset
    ),
    COMMANDS.PRODUCTION_COUNT.name,
    protocolPreset
  );
  sendPacket(
    createBottomThreadCountFrame(
      {
        totalLength: state.deviceInfo.bottomThreadTotal,
        remainLength: state.deviceInfo.bottomThreadRemain,
      },
      protocolPreset
    ),
    COMMANDS.BOTTOM_THREAD_COUNT.name,
    protocolPreset
  );
}

function sendDeviceInfo(protocolPreset = getActiveProtocolPreset()) {
  const info = state.deviceInfo;
  sendPacket(
    createPatternInfoFrame(
      {
        patternId: info.patternId,
        patternName: info.patternName,
      },
      protocolPreset
    ),
    COMMANDS.PATTERN_INFO.name,
    protocolPreset
  );
  sendAlarmReport(protocolPreset);
  sendPacket(createDeviceFlagFrame(info.flag, protocolPreset), COMMANDS.DEVICE_FLAG.name, protocolPreset);
  sendPacket(
    createDeviceModelFrame(
      {
        model: info.model,
        deviceId: info.deviceId,
        name: info.name,
      },
      protocolPreset
    ),
    COMMANDS.DEVICE_MODEL.name,
    protocolPreset
  );
  sendPacket(
    createSewingRangeFrame(
      {
        xRange: info.xRange,
        yRange: info.yRange,
      },
      protocolPreset
    ),
    COMMANDS.SEWING_RANGE.name,
    protocolPreset
  );
  sendPacket(createMaxSpeedFrame(info.maxSpeed, protocolPreset), COMMANDS.MAX_SPEED.name, protocolPreset);
  sendCountersReport(protocolPreset);
  sendHighPointReport(protocolPreset);
  sendLowPointReport(protocolPreset);
  sendTimeSyncRequest(protocolPreset);
}

function sendStartupAnnouncement(trigger = "开机上报", protocolPreset = getActiveProtocolPreset()) {
  appendLog(`客户端 -> ${trigger} [${protocolPreset?.name ?? "-"}]`);
  sendDeviceInfo(protocolPreset);
}

function sendProductionDataOld(protocolPreset = getActiveProtocolPreset()) {
  const endTime = new Date();
  const startTime = state.deviceInfo.lastStartTime ? new Date(state.deviceInfo.lastStartTime) : endTime;
  sendPacket(
    createProductionDataOldFrame(
      {
        deviceId: state.deviceInfo.deviceId,
        patternId: state.deviceInfo.patternId,
        patternName: state.deviceInfo.patternName,
        startTime,
        startNeedle: 0,
        endTime,
        endNeedle: state.deviceInfo.completedNeedleCount,
        stopReason: state.deviceInfo.stopReason,
      },
      protocolPreset
    ),
    COMMANDS.PRODUCTION_DATA_OLD.name,
    protocolPreset
  );
}

function sendProductionDataNew(protocolPreset = getActiveProtocolPreset()) {
  const endTime = new Date();
  const startTime = state.deviceInfo.lastStartTime ? new Date(state.deviceInfo.lastStartTime) : endTime;
  sendPacket(
    createProductionDataNewFrame(
      {
        deviceId: state.deviceInfo.deviceId,
        patternId: state.deviceInfo.patternId,
        patternName: state.deviceInfo.patternName,
        startTime,
        startNeedle: 0,
        endTime,
        endNeedle: state.deviceInfo.completedNeedleCount,
        userId: state.deviceInfo.userId,
        stopReason: state.deviceInfo.stopReason,
      },
      protocolPreset
    ),
    COMMANDS.PRODUCTION_DATA_NEW.name,
    protocolPreset
  );
}

function requestServerPatternList(protocolPreset = getActiveProtocolPreset()) {
  state.transfers.serverPatternList = {
    startedAt: nowLocal(),
    totalFrames: null,
    frames: new Map(),
  };
  state.serverPatterns = [];
  sendPacket(
    createRequestServerPatternListFrame(protocolPreset),
    COMMANDS.REQUEST_SERVER_PATTERN_LIST.name,
    protocolPreset
  );
}

function sendTransferResumeRequest(transferType, frameNo, protocolPreset = getActiveProtocolPreset()) {
  state.transfers.lastResumeRequest = {
    transferType,
    frameNo,
    requestedAt: nowLocal(),
  };
  sendPacket(
    createTransferResumeFrame({ transferType, frameNo }, protocolPreset),
    COMMANDS.TRANSFER_RESUME.name,
    protocolPreset
  );
}

function sendUploadStatus(status, protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createUploadPatternStatusFrame(status, protocolPreset),
    COMMANDS.UPLOAD_PATTERN_COMMAND.name,
    protocolPreset
  );
}

function markTransferResult(direction, result, detail = "") {
  state.transfers.lastResult = {
    direction,
    result,
    detail,
    time: nowLocal(),
  };
}

function startUploadToServer(pattern, protocolPreset = getActiveProtocolPreset()) {
  const frames = createUploadPatternDataFrames(pattern.data, protocolPreset, { chunkSize: 256 });
  state.transfers.uploadToServer = {
    pattern,
    frames,
    nextFrameIndex: 0,
    awaitingAckFrameNo: null,
    protocolPresetId: protocolPreset.id,
    startedAt: nowLocal(),
  };
  sendUploadStatus(0, protocolPreset);
  sendNextUploadFrame();
}

function clearUploadTransfer() {
  state.transfers.uploadToServer = null;
}

function sendNextUploadFrame() {
  const transfer = state.transfers.uploadToServer;
  if (!transfer) {
    return;
  }

  if (transfer.nextFrameIndex >= transfer.frames.length) {
    const protocolPreset = getProtocolPreset(transfer.protocolPresetId);
    sendUploadStatus(2, protocolPreset);
    markTransferResult("upload", 2, "上传成功");
    clearUploadTransfer();
    return;
  }

  const packet = transfer.frames[transfer.nextFrameIndex];
  transfer.awaitingAckFrameNo = transfer.nextFrameIndex + 1;
  transfer.nextFrameIndex += 1;
  sendPacket(
    packet,
    `${COMMANDS.UPLOAD_PATTERN_DATA.name} ${transfer.awaitingAckFrameNo}/${transfer.frames.length}`,
    getProtocolPreset(transfer.protocolPresetId)
  );
}

function handleUploadDataAck(frame) {
  const transfer = state.transfers.uploadToServer;
  if (!transfer) {
    return;
  }

  const parsed = parseSingleByteResultPayload(frame.payload);
  if (!parsed) {
    return;
  }

  if (parsed.result === 0) {
    sendNextUploadFrame();
    return;
  }

  sendUploadStatus(3, getProtocolPreset(transfer.protocolPresetId));
  markTransferResult("upload", parsed.result, "服务器拒绝上传数据帧");
  clearUploadTransfer();
}

function handleDownloadPatternCommand(frame) {
  const protocolPreset = frame.protocol || getActiveProtocolPreset();
  const parsed = parseDownloadPatternCommandPayload(frame.payload);
  state.transfers.downloadFromServer = {
    patternName: parsed.patternName || `下载花型-${nowLocal()}`,
    frames: new Map(),
    totalFrames: null,
    protocolPresetId: protocolPreset.id,
    startedAt: nowLocal(),
  };

  sendPacket(
    createDownloadPatternCommandAckFrame(
      {
        patternName: state.transfers.downloadFromServer.patternName,
        deviceType: state.deviceInfo.model,
        deviceId: state.deviceInfo.deviceId,
      },
      protocolPreset
    ),
    COMMANDS.DOWNLOAD_PATTERN_COMMAND.name,
    protocolPreset
  );
}

function completeDownloadFromServer() {
  const transfer = state.transfers.downloadFromServer;
  if (!transfer || !transfer.totalFrames || transfer.frames.size !== transfer.totalFrames) {
    return;
  }

  const orderedBuffers = [];
  for (let frameNo = 1; frameNo <= transfer.totalFrames; frameNo += 1) {
    orderedBuffers.push(transfer.frames.get(frameNo) || Buffer.alloc(0));
  }
  const data = Buffer.concat(orderedBuffers);
  const existing = findLocalPatternByReference(null, transfer.patternName);
  const patternId = existing?.patternId || getNextPatternId();
  upsertPattern({
    patternId,
    patternName: transfer.patternName,
    dataText: data.toString("utf8"),
  });

  const protocolPreset = getProtocolPreset(transfer.protocolPresetId);
  sendPacket(
    createDownloadPatternResultFrame(0, protocolPreset),
    COMMANDS.DOWNLOAD_PATTERN_DATA.name,
    protocolPreset
  );
  markTransferResult("download", 0, `已接收花型 ${transfer.patternName}`);
  state.transfers.downloadFromServer = null;
}

function handleDownloadPatternData(frame) {
  const protocolPreset = frame.protocol || getActiveProtocolPreset();
  if (!state.transfers.downloadFromServer) {
    state.transfers.downloadFromServer = {
      patternName: `未命名下载花型-${nowLocal()}`,
      frames: new Map(),
      totalFrames: null,
      protocolPresetId: protocolPreset.id,
      startedAt: nowLocal(),
    };
  }

  const transfer = state.transfers.downloadFromServer;
  transfer.totalFrames = frame.totalFrames;
  transfer.frames.set(frame.frameNo, Buffer.from(frame.payload));

  sendPacket(
    createCommunicationErrorFrame(
      {
        totalFrames: frame.totalFrames,
        frameNo: frame.frameNo,
        result: 0,
      },
      protocolPreset
    ),
    COMMANDS.COMMUNICATION_ERROR.name,
    protocolPreset
  );

  completeDownloadFromServer();
}

function handleUploadPatternCommand(frame) {
  const protocolPreset = frame.protocol || getActiveProtocolPreset();
  const request = parseUploadPatternCommandPayload(frame.payload, protocolPreset);
  const match = findPatternForUploadCommand(frame, request);
  const pattern = match?.pattern || null;

  if (!pattern) {
    sendUploadStatus(1, protocolPreset);
    markTransferResult("upload", 1, "未找到请求的花型");
    return;
  }

  if (match?.matchedBy === "name-compatible") {
    appendLog(
      `上传花型指令使用兼容匹配: 请求 ${match.candidate.patternId ?? "-"} / ${match.candidate.patternName || "-"} -> 本地 ${pattern.patternId} / ${pattern.patternName}`
    );
  }

  startUploadToServer(pattern, protocolPreset);
}

function handleDeletePatternCommand(frame) {
  const protocolPreset = frame.protocol || getActiveProtocolPreset();
  const request = parseDeletePatternCommandPayload(frame.payload, protocolPreset);
  const target = findPatternForDeleteCommand(frame, request);
  let result = 1;

  if (target) {
    const previousPatternId = state.deviceInfo.patternId;
    deletePattern(target.patternId);
    if (target.patternId === previousPatternId) {
      announceCurrentPatternIfConnected(protocolPreset);
    }
    result = 0;
  }

  sendPacket(
    createDeletePatternResponseFromRequestPayloadFrame(frame.payload, result, protocolPreset),
    COMMANDS.DELETE_PATTERN_FILE.name,
    protocolPreset
  );
}

function handleReadPatternList(frame) {
  const protocolPreset = frame.protocol || getActiveProtocolPreset();
  const frames = createPatternListFrames(
    COMMANDS.READ_PATTERN_LIST,
    state.localPatterns,
    protocolPreset,
    { includeId: true }
  );

  frames.forEach((packet, index) => {
    sendPacket(
      packet,
      `${COMMANDS.READ_PATTERN_LIST.name} ${index + 1}/${frames.length}`,
      protocolPreset
    );
  });
}

function handleServerPatternListFrame(frame) {
  const protocolPreset = frame.protocol || getActiveProtocolPreset();
  if (!state.transfers.serverPatternList) {
    state.transfers.serverPatternList = {
      startedAt: nowLocal(),
      totalFrames: null,
      frames: new Map(),
    };
  }

  const transfer = state.transfers.serverPatternList;
  transfer.totalFrames = frame.totalFrames;
  transfer.frames.set(frame.frameNo, parsePatternListPayload(frame.payload, protocolPreset, { includeId: false }));

  if (transfer.totalFrames && transfer.frames.size === transfer.totalFrames) {
    const entries = [];
    for (let frameNo = 1; frameNo <= transfer.totalFrames; frameNo += 1) {
      const next = transfer.frames.get(frameNo) || [];
      entries.push(...next);
    }
    state.serverPatterns = entries;
    appendLog(`已接收服务器花型列表，共 ${entries.length} 项`);
    state.transfers.serverPatternList = null;
  }
}

function announceCurrentPattern(protocolPreset = getActiveProtocolPreset()) {
  sendPacket(
    createPatternInfoFrame(
      {
        patternId: state.deviceInfo.patternId,
        patternName: state.deviceInfo.patternName,
      },
      protocolPreset
    ),
    COMMANDS.PATTERN_INFO.name,
    protocolPreset
  );
}

function announceCurrentPatternIfConnected(protocolPreset = getActiveProtocolPreset()) {
  if (!state.socket || state.connectionStatus !== "connected") {
    return;
  }

  announceCurrentPattern(protocolPreset);
}

function startSewing() {
  const startTime = new Date();
  updateDeviceInfo({
    sewingStatus: SEWING_EVENT_STATUS.start,
    realtimeStatus: REALTIME_STATUS.sewing,
    lastStartTime: startTime.toISOString(),
    productionCurrent: 0,
    currentNeedleNumber: 0,
    completedNeedleCount: 0,
  });
  sendSewingStatusReport();
  sendCurrentSpeedReport();
}

function markSewingRunning() {
  updateDeviceInfo({
    sewingStatus: SEWING_EVENT_STATUS.running,
    realtimeStatus: REALTIME_STATUS.sewing,
  });
  sendSewingStatusReport();
  sendCurrentSpeedReport();
}

function stopSewing() {
  const endTime = new Date();
  updateDeviceInfo({
    sewingStatus: SEWING_EVENT_STATUS.stop,
    realtimeStatus: REALTIME_STATUS.idle,
    lastEndTime: endTime.toISOString(),
    productionTotal: state.deviceInfo.productionTotal + 1,
    productionCurrent: state.deviceInfo.productionCurrent + 1,
    bottomThreadRemain: Math.max(0, state.deviceInfo.bottomThreadRemain - 100),
  });
  sendSewingStatusReport();
  sendNeedleCountReport();
  sendCountersReport();
  sendProductionDataOld();
  sendProductionDataNew();
}

function requestServerSideResume(frameNo = 1) {
  sendTransferResumeRequest(TRANSFER_TYPES.upload, frameNo);
}

function sendHandshakeAttempt(trigger) {
  const protocolPreset =
    state.protocolMode === "fixed" || state.lockedProtocolPresetId
      ? getActiveProtocolPreset()
      : getNextProbeProtocolPreset();

  appendLog(`客户端 -> 协议握手 ${trigger} [${protocolPreset?.name ?? "-"}]`);
  sendRegister(trigger === "首发" ? COMMANDS.REGISTER.name : "注册重试", protocolPreset);
  sendStartupAnnouncement("开机上报", protocolPreset);
  sendHeartbeat(protocolPreset);
}

function startHeartbeatLoop() {
  stopHeartbeatLoop();
  state.heartbeatTimer = setInterval(() => {
    sendHeartbeat();
  }, 10000);
}

function stopHeartbeatLoop() {
  if (state.heartbeatTimer) {
    clearInterval(state.heartbeatTimer);
    state.heartbeatTimer = null;
  }
}

function startRegisterRetryLoop() {
  stopRegisterRetryLoop();
  state.registerRetryTimer = setInterval(() => {
    if (state.connectionStatus === "connected" && !state.registerAcknowledgedAt) {
      sendHandshakeAttempt("重试");
    }
  }, 5000);
}

function stopRegisterRetryLoop() {
  if (state.registerRetryTimer) {
    clearInterval(state.registerRetryTimer);
    state.registerRetryTimer = null;
  }
}

function connectToServer() {
  if (state.socket && state.connectionStatus === "connected") {
    return;
  }

  resetProtocolNegotiation();
  state.socket = net.createConnection(
    { host: state.tcpHost, port: state.tcpPort },
    () => {
      state.connectionStatus = "connected";
      state.connectedAt = nowLocal();
      state.registerAcknowledgedAt = null;
      state.lastHeartbeatAckAt = null;
      state.lastTimeSyncAt = null;
      appendLog(`已连接到 ${state.tcpHost}:${state.tcpPort}`);
      state.socket.setKeepAlive(true, 10000);
      sendHandshakeAttempt("首发");
      startRegisterRetryLoop();
      startHeartbeatLoop();
    }
  );

  state.connectionStatus = "connecting";

  state.socket.on("data", (chunk) => {
    const frames = state.parser.push(chunk);
    for (const frame of frames) {
      handleFrame(frame);
    }
  });

  state.socket.on("close", () => {
    appendLog("TCP 已断开");
    state.connectionStatus = "disconnected";
    state.socket = null;
    stopHeartbeatLoop();
    stopRegisterRetryLoop();
  });

  state.socket.on("error", (error) => {
    appendLog(`连接错误: ${error.message}`);
    state.connectionStatus = "disconnected";
    state.socket = null;
    stopHeartbeatLoop();
    stopRegisterRetryLoop();
  });
}

function disconnectFromServer() {
  stopHeartbeatLoop();
  stopRegisterRetryLoop();
  if (state.socket) {
    state.socket.end();
  }
}

function clearLogs() {
  state.logs = [];
}

function handleHeartbeatFrame(frame) {
  state.lastHeartbeatAckAt = nowLocal();

  if (Date.now() - state.lastHeartbeatSentMs > 3000) {
    sendHeartbeat(frame.protocol || getActiveProtocolPreset());
  }
}

function handleRegisterFrame(frame) {
  state.registerAcknowledgedAt = nowLocal();
  stopRegisterRetryLoop();
  sendStartupAnnouncement("注册后补发", frame.protocol || getActiveProtocolPreset());
  sendHeartbeat(frame.protocol || getActiveProtocolPreset());
}

function handleTimeSyncFrame(frame) {
  state.lastTimeSyncAt = nowLocal();
  const timeInfo = parseTimeSyncPayload(frame.payload);
  if (timeInfo) {
    updateDeviceInfo({ timeSyncValue: timeInfo.formatted });
    appendLog(`客户端 <- 服务端时间 ${timeInfo.formatted} 星期${timeInfo.week}`);
  }
}

function handleReadHighPoint(frame) {
  sendPacket(
    createHighPointFrame(state.deviceInfo.highPoint, frame.protocol),
    COMMANDS.READ_HIGH_POINT.name,
    frame.protocol
  );
}

function handleSetHighPoint(frame) {
  const parsed = parseUShortValuePayload(frame.payload, frame.protocol);
  if (parsed) {
    updateDeviceInfo({ highPoint: parsed.value });
  }
  sendPacket(
    createSetHighPointResultFrame(0, frame.protocol),
    COMMANDS.SET_HIGH_POINT.name,
    frame.protocol
  );
  sendHighPointReport(frame.protocol);
}

function handleReadLowPoint(frame) {
  sendPacket(
    createLowPointFrame(state.deviceInfo.lowPoint, frame.protocol),
    COMMANDS.READ_LOW_POINT.name,
    frame.protocol
  );
}

function handleSetLowPoint(frame) {
  const parsed = parseUShortValuePayload(frame.payload, frame.protocol);
  if (parsed) {
    updateDeviceInfo({ lowPoint: parsed.value });
  }
  sendPacket(
    createSetLowPointResultFrame(0, frame.protocol),
    COMMANDS.SET_LOW_POINT.name,
    frame.protocol
  );
  sendLowPointReport(frame.protocol);
}

function handleSetSpeed(frame) {
  const parsed = parseUShortValuePayload(frame.payload, frame.protocol);
  if (parsed) {
    updateDeviceInfo({ currentSpeed: parsed.value });
  }
  sendPacket(
    createSetSpeedResultFrame(0, frame.protocol),
    COMMANDS.SET_SPEED.name,
    frame.protocol
  );
  sendCurrentSpeedReport(frame.protocol);
}

function handleSetDeviceFlag(frame) {
  updateDeviceInfo({ flag: frame.payload.toString("utf8") });
  sendPacket(
    createDeviceFlagFrame(state.deviceInfo.flag, frame.protocol),
    COMMANDS.DEVICE_FLAG.name,
    frame.protocol
  );
}

function handleTransferResumeFrame(frame) {
  const parsed = parseTransferResumePayload(frame.payload);
  if (!parsed) {
    return;
  }

  appendLog(`客户端 <- 续传响应 type=${parsed.transferType} frame=${parsed.frameNo}`);

  if (parsed.transferType === TRANSFER_TYPES.upload && state.transfers.uploadToServer) {
    state.transfers.uploadToServer.nextFrameIndex = Math.max(0, parsed.frameNo - 1);
    state.transfers.uploadToServer.awaitingAckFrameNo = null;
    sendNextUploadFrame();
  }
}

function handleFrame(frame) {
  state.lastMessageAt = nowLocal();

  if (frame.error) {
    appendLog(`客户端收到非法报文: ${frame.error}`, { raw: toHex(frame.raw) });
    return;
  }

  if (frame.protocol?.id) {
    lockProtocolPreset(frame.protocol.id);
  }

  appendLog(
    `客户端 <- ${describeFrame(frame)}${frame.recovered ? ` [兼容恢复: ${frame.recovered.message}]` : ""}`,
    {
    command: commandName(frame.type, frame.code),
    protocolPreset: frame.protocol?.name,
    recovery: frame.recovered?.message || "",
    raw: toHex(frame.raw),
    }
  );

  if (isCommand(frame, COMMANDS.HEARTBEAT)) {
    handleHeartbeatFrame(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.REGISTER)) {
    handleRegisterFrame(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.TIME_SYNC)) {
    handleTimeSyncFrame(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.REALTIME_STATUS_QUERY)) {
    sendRealtimeStatusReport(frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.READ_HIGH_POINT)) {
    handleReadHighPoint(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.SET_HIGH_POINT)) {
    handleSetHighPoint(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.READ_LOW_POINT)) {
    handleReadLowPoint(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.SET_LOW_POINT)) {
    handleSetLowPoint(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.SET_SPEED)) {
    handleSetSpeed(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.SET_DEVICE_FLAG)) {
    handleSetDeviceFlag(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.DOWNLOAD_PATTERN_COMMAND)) {
    handleDownloadPatternCommand(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.DOWNLOAD_PATTERN_DATA)) {
    handleDownloadPatternData(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.UPLOAD_PATTERN_COMMAND)) {
    handleUploadPatternCommand(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.DELETE_PATTERN_FILE)) {
    handleDeletePatternCommand(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.READ_PATTERN_LIST)) {
    handleReadPatternList(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.REQUEST_SERVER_PATTERN_LIST)) {
    handleServerPatternListFrame(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.UPLOAD_PATTERN_DATA)) {
    handleUploadDataAck(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.TRANSFER_RESUME)) {
    handleTransferResumeFrame(frame);
    return;
  }

  if (isCommand(frame, COMMANDS.PRODUCTION_DATA_OLD) || isCommand(frame, COMMANDS.PRODUCTION_DATA_NEW)) {
    const result = parseSingleByteResultPayload(frame.payload);
    if (result) {
      appendLog(`客户端 <- 生产数据接收结果 ${result.result}`);
    }
    return;
  }

  if (isCommand(frame, COMMANDS.COMMUNICATION_ERROR)) {
    const result = parseSingleByteResultPayload(frame.payload);
    if (result) {
      appendLog(`客户端 <- 通信错误反馈 ${result.result}`);
    }
    return;
  }
}

function performAction(action, payload = {}) {
  switch (action) {
    case "register":
      sendRegister();
      return;
    case "heartbeat":
      sendHeartbeat();
      return;
    case "startup":
      sendStartupAnnouncement();
      return;
    case "timeSyncRequest":
      sendTimeSyncRequest();
      return;
    case "reportDeviceInfo":
      sendDeviceInfo();
      return;
    case "reportCurrentSpeed":
      sendCurrentSpeedReport();
      return;
    case "reportNeedleCount":
      sendNeedleCountReport();
      return;
    case "reportHighPoint":
      sendHighPointReport();
      return;
    case "reportLowPoint":
      sendLowPointReport();
      return;
    case "reportRealtimeStatus":
      sendRealtimeStatusReport();
      return;
    case "reportAlarm":
      sendAlarmReport();
      return;
    case "clearAlarm":
      updateDeviceInfo({ alarmCode: 0 });
      sendAlarmReport();
      return;
    case "reportIdleAlarm":
      sendIdleAlarmReport();
      return;
    case "reportOilPrompt":
      sendPacket(createOilPromptFrame(state.deviceInfo.oilPrompt, getActiveProtocolPreset()), COMMANDS.OIL_PROMPT.name);
      return;
    case "startSewing":
      startSewing();
      return;
    case "markSewingRunning":
      markSewingRunning();
      return;
    case "stopSewing":
      stopSewing();
      return;
    case "threadTrim":
      sendThreadTrimComplete();
      return;
    case "productionOld":
      sendProductionDataOld();
      return;
    case "productionNew":
      sendProductionDataNew();
      return;
    case "requestServerPatternList":
      requestServerPatternList();
      return;
    case "resumeUpload":
      requestServerSideResume(normalizeNumber(payload.frameNo, 1));
      return;
    case "resumeDownload":
      sendTransferResumeRequest(TRANSFER_TYPES.download, normalizeNumber(payload.frameNo, 1));
      return;
    case "activatePattern":
      setActivePattern(payload.patternId);
      announceCurrentPattern();
      return;
    default:
      throw new Error(`未知动作: ${action}`);
  }
}

ensureActivePatternExists();
resetProtocolNegotiation();

const httpServer = http.createServer(async (request, response) => {
  const url = new URL(request.url, `http://${request.headers.host}`);

  if (request.method === "GET" && url.pathname === "/") {
    sendFile(response, clientPage, "text/html; charset=utf-8");
    return;
  }

  if (request.method === "GET" && url.pathname === "/app.js") {
    sendFile(response, clientScript, "application/javascript; charset=utf-8");
    return;
  }

  if (request.method === "GET" && url.pathname === "/api/state") {
    sendJson(response, 200, publicState());
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/connect") {
    const body = await readJsonBody(request);
    updateConnectionConfig(body);
    connectToServer();
    sendJson(response, 200, { ok: true });
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/disconnect") {
    disconnectFromServer();
    sendJson(response, 200, { ok: true });
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/connection-config") {
    const body = await readJsonBody(request);
    updateConnectionConfig(body);
    sendJson(response, 200, { ok: true, tcpHost: state.tcpHost, tcpPort: state.tcpPort });
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/protocol-config") {
    const body = await readJsonBody(request);
    updateProtocolConfig(body);
    sendJson(response, 200, {
      ok: true,
      protocolMode: state.protocolMode,
      preferredProtocolPresetId: state.preferredProtocolPresetId,
    });
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/device-info") {
    const body = await readJsonBody(request);
    updateDeviceInfo(body);
    if (body.reportNow) {
      sendDeviceInfo();
    }
    sendJson(response, 200, { ok: true, deviceInfo: state.deviceInfo });
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/action") {
    try {
      const body = await readJsonBody(request);
      performAction(body.action, body.payload);
      sendJson(response, 200, { ok: true });
    } catch (error) {
      sendJson(response, 400, { ok: false, error: error.message });
    }
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/patterns") {
    try {
      const body = await readJsonBody(request);
      const pattern = upsertPattern(body);
      if (body.activateNow) {
        setActivePattern(pattern.patternId);
        announceCurrentPatternIfConnected();
      }
      sendJson(response, 200, { ok: true, pattern: getPatternSummary(pattern) });
    } catch (error) {
      sendJson(response, 400, { ok: false, error: error.message });
    }
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/patterns/delete") {
    try {
      const body = await readJsonBody(request);
      const previousPatternId = state.deviceInfo.patternId;
      const deleted = deletePattern(body.patternId);
      if (deleted.patternId === previousPatternId) {
        announceCurrentPatternIfConnected();
      }
      sendJson(response, 200, { ok: true, pattern: getPatternSummary(deleted) });
    } catch (error) {
      sendJson(response, 400, { ok: false, error: error.message });
    }
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/logs/clear") {
    clearLogs();
    sendJson(response, 200, { ok: true });
    return;
  }

  sendJson(response, 404, { error: "not found" });
});

httpServer.listen(state.httpPort, state.bindHost, () => {
  console.log(`Client UI: http://127.0.0.1:${state.httpPort}`);
  appendLog(`客户端控制台已启动 http://127.0.0.1:${state.httpPort}`);
});
