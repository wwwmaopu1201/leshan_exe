import { Buffer } from "node:buffer";
import fs from "node:fs";
import http from "node:http";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  COMMANDS,
  commandName,
  createDeletePatternCommandFrame,
  createDeviceFlagFrame,
  createDownloadPatternCommandFrame,
  createDownloadPatternDataFrames,
  createHeartbeatFrame,
  createReadHighPointFrame,
  createReadLowPointFrame,
  createReadPatternListFrame,
  createRealtimeStatusQueryFrame,
  createRegisterFrame,
  createSetHighPointFrame,
  createSetLowPointFrame,
  createSetSpeedFrame,
  createTimeSyncResponseFrame,
  createTransferResumeFrame,
  createUploadPatternCommandFrame,
  createPatternListFrames,
  describeFrame,
  isCommand,
  parseAlarmPayload,
  parseBottomThreadCountPayload,
  parseCurrentSpeedPayload,
  parseDeviceFlagPayload,
  parseDeviceModelPayload,
  parseDownloadPatternCommandPayload,
  parseIdleAlarmPayload,
  parseMaxSpeedPayload,
  parseNeedleCountPayload,
  parseOilPromptPayload,
  parsePatternInfoPayload,
  parsePatternListPayload,
  parseProductionCountPayload,
  parseProductionDataNewPayload,
  parseProductionDataOldPayload,
  parseRealtimeStatusPayload,
  parseSewingRangePayload,
  parseSewingStatusPayload,
  parseSingleByteResultPayload,
  parseTimeSyncPayload,
  parseTransferResumePayload,
  parseUShortValuePayload,
} from "../shared/commands.js";
import {
  encodeFrame,
  FrameParser,
  getProtocolPreset,
  PROTOCOL_PRESETS,
  toHex,
} from "../shared/protocol.js";
import { getLocalTimeZone, nowLocal } from "../shared/time.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const rootDir = path.resolve(__dirname, "../..");
const serverPage = path.join(rootDir, "public/server/index.html");
const serverScript = path.join(rootDir, "public/server/app.js");

const DEFAULT_PROTOCOL_PRESET_ID = "be-modbus-lencrc";
const TRANSFER_TYPES = {
  upload: 1,
  download: 2,
};

const bindHost = process.env.BIND_HOST || "127.0.0.1";
const tcpPort = Number(process.env.TCP_PORT || 9000);
const httpPort = Number(process.env.HTTP_PORT || 9001);
const timeZone = getLocalTimeZone();

let nextSessionId = 1;

function createPattern(patternId, patternName, dataText) {
  const content =
    (dataText?.length ?? 0) > 0
      ? dataText
      : `PATTERN:${patternName}\nID:${patternId}\nGENERATED:${nowLocal()}`;
  return {
    patternId,
    patternName,
    data: Buffer.from(content, "utf8"),
    updatedAt: nowLocal(),
  };
}

const state = {
  tcpPort,
  httpPort,
  preferredProtocolPresetId: process.env.PROTOCOL_PRESET || DEFAULT_PROTOCOL_PRESET_ID,
  sessions: new Map(),
  logs: [],
  startedAt: nowLocal(),
  serverPatterns: [
    createPattern(1, "服务器花型A", "SERVER-PATTERN-A"),
    createPattern(2, "服务器花型B", "SERVER-PATTERN-B"),
  ],
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

function normalizeNumber(value, fallback = 0) {
  const next = Number(value);
  return Number.isFinite(next) ? next : fallback;
}

function getNextPatternId(patterns) {
  return patterns.reduce((max, item) => Math.max(max, Number(item.patternId) || 0), 0) + 1;
}

function getPatternSummary(pattern) {
  return {
    patternId: pattern.patternId,
    patternName: pattern.patternName,
    size: pattern.data.length,
    dataText: pattern.data.toString("utf8"),
    updatedAt: pattern.updatedAt,
  };
}

function createSessionDeviceInfo() {
  return {
    model: null,
    flag: null,
    sewingRange: null,
    maxSpeed: null,
    currentSpeed: null,
    patternInfo: null,
    productionCount: null,
    bottomThreadCount: null,
    alarm: null,
    idleAlarm: null,
    oilPrompt: null,
    needleCount: null,
    sewingStatus: null,
    realtimeStatus: null,
    highPoint: null,
    lowPoint: null,
    timeSyncValue: null,
    lastThreadTrimAt: null,
    lastProductionDataOld: null,
    lastProductionDataNew: null,
  };
}

function getTransferSummary(session) {
  return {
    downloadToClient: session.transfers.downloadToClient
      ? {
          patternName: session.transfers.downloadToClient.pattern.patternName,
          totalFrames: session.transfers.downloadToClient.frames.length,
          nextFrameNo: session.transfers.downloadToClient.nextFrameIndex + 1,
          awaitingAckFrameNo: session.transfers.downloadToClient.awaitingAckFrameNo,
          commandAcknowledged: session.transfers.downloadToClient.commandAcknowledged,
          startedAt: session.transfers.downloadToClient.startedAt,
        }
      : null,
    uploadFromClient: session.transfers.uploadFromClient
      ? {
          patternId: session.transfers.uploadFromClient.request.patternId,
          patternName: session.transfers.uploadFromClient.request.patternName,
          receivedFrames: session.transfers.uploadFromClient.frames.size,
          totalFrames: session.transfers.uploadFromClient.totalFrames,
          status: session.transfers.uploadFromClient.status,
          startedAt: session.transfers.uploadFromClient.startedAt,
        }
      : null,
    clientPatternList: session.transfers.clientPatternList
      ? {
          receivedFrames: session.transfers.clientPatternList.frames.size,
          totalFrames: session.transfers.clientPatternList.totalFrames,
          startedAt: session.transfers.clientPatternList.startedAt,
        }
      : null,
    pendingDelete: session.transfers.pendingDelete,
    lastResumeRequest: session.transfers.lastResumeRequest,
    lastResult: session.transfers.lastResult,
  };
}

function sessionSummary(session) {
  return {
    id: session.id,
    remoteAddress: session.remoteAddress,
    remotePort: session.remotePort,
    connectedAt: session.connectedAt,
    lastMessageAt: session.lastMessageAt,
    lastHeartbeatAt: session.lastHeartbeatAt,
    lastTimeSyncAt: session.lastTimeSyncAt,
    registered: session.registered,
    registerAcknowledgedAt: session.registerAcknowledgedAt,
    protocolPresetId: session.protocolPresetId,
    protocolPresetName: session.protocolPresetName,
    lastRecoveryMessage: session.lastRecoveryMessage,
    deviceInfo: session.deviceInfo,
    clientPatterns: session.clientPatterns,
    transferSummary: getTransferSummary(session),
  };
}

function publicState() {
  return {
    bindHost,
    timeZone,
    tcpPort: state.tcpPort,
    httpPort: state.httpPort,
    startedAt: state.startedAt,
    preferredProtocolPresetId: state.preferredProtocolPresetId,
    protocolPresets: PROTOCOL_PRESETS,
    serverPatterns: state.serverPatterns.map(getPatternSummary),
    sessions: Array.from(state.sessions.values()).map(sessionSummary),
    logs: state.logs,
  };
}

function updateServerConfig({ preferredProtocolPresetId }) {
  if (preferredProtocolPresetId && getProtocolPreset(preferredProtocolPresetId)) {
    state.preferredProtocolPresetId = preferredProtocolPresetId;
  }
}

function getSessionProtocolPreset(session) {
  return (
    getProtocolPreset(session?.protocolPresetId) ||
    getProtocolPreset(state.preferredProtocolPresetId) ||
    getProtocolPreset(DEFAULT_PROTOCOL_PRESET_ID)
  );
}

function findSession(sessionId) {
  const session = state.sessions.get(Number(sessionId));
  if (!session) {
    throw new Error("未找到会话");
  }
  if (!session.socket || session.socket.destroyed) {
    throw new Error("会话已断开");
  }
  return session;
}

function requireSession(payload = {}) {
  if (!payload.sessionId) {
    throw new Error("请先选择会话");
  }

  return findSession(payload.sessionId);
}

function sendPacket(session, packet, label, protocolPreset = getSessionProtocolPreset(session)) {
  session.socket.write(packet);
  appendLog(`服务端 ${session.id} -> ${label} [${protocolPreset?.name ?? "-"}]`, {
    sessionId: session.id,
    command: label,
    raw: toHex(packet),
  });
}

function createCommandResultPacket(command, result, protocolPreset, options = {}) {
  return encodeFrame({
    type: command.type,
    code: command.code,
    totalFrames: options.totalFrames ?? 1,
    frameNo: options.frameNo ?? 1,
    payload: Buffer.from([Number(result) & 0xff]),
    protocol: protocolPreset,
  });
}

function markTransferResult(session, direction, result, detail = "") {
  session.transfers.lastResult = {
    direction,
    result,
    detail,
    time: nowLocal(),
  };
}

function findPatternByReference(patterns, patternId, patternName) {
  return (
    patterns.find((item) => item.patternId === Number(patternId)) ||
    patterns.find((item) => item.patternName === patternName) ||
    null
  );
}

function upsertServerPattern({ patternId, patternName, dataText }) {
  const nextId = Number(patternId) || getNextPatternId(state.serverPatterns);
  const nextName = String(patternName || "").trim() || `服务器花型${nextId}`;
  const existing = state.serverPatterns.find((item) => item.patternId === nextId);
  const nextPattern = createPattern(nextId, nextName, dataText);

  if (existing) {
    existing.patternName = nextPattern.patternName;
    existing.data = nextPattern.data;
    existing.updatedAt = nextPattern.updatedAt;
    return existing;
  }

  state.serverPatterns.push(nextPattern);
  return nextPattern;
}

function deleteServerPattern(patternId) {
  const nextId = Number(patternId);
  const target = state.serverPatterns.find((item) => item.patternId === nextId);
  if (!target) {
    throw new Error("服务器花型不存在");
  }
  state.serverPatterns = state.serverPatterns.filter((item) => item.patternId !== nextId);
  return target;
}

function resolvePatternRequest(patterns, payload = {}) {
  const patternId = normalizeNumber(payload.patternId, 0);
  const patternName = String(payload.patternName || "").trim();
  const matched = findPatternByReference(patterns, patternId, patternName);

  return {
    patternId: matched?.patternId || patternId,
    patternName: matched?.patternName || patternName,
  };
}

function startDownloadToClient(session, patternId) {
  const pattern = findPatternByReference(state.serverPatterns, patternId, null);
  if (!pattern) {
    throw new Error("未找到服务器花型");
  }

  const protocolPreset = getSessionProtocolPreset(session);
  const frames = createDownloadPatternDataFrames(pattern.data, protocolPreset, { chunkSize: 256 });
  session.transfers.downloadToClient = {
    pattern,
    frames,
    nextFrameIndex: 0,
    awaitingAckFrameNo: null,
    commandAcknowledged: false,
    protocolPresetId: protocolPreset.id,
    startedAt: nowLocal(),
    ackDeviceType: null,
    ackDeviceId: null,
  };

  sendPacket(
    session,
    createDownloadPatternCommandFrame({ patternName: pattern.patternName }, protocolPreset),
    COMMANDS.DOWNLOAD_PATTERN_COMMAND.name,
    protocolPreset
  );
}

function sendNextDownloadFrame(session) {
  const transfer = session.transfers.downloadToClient;
  if (!transfer || !transfer.commandAcknowledged) {
    return;
  }

  if (transfer.nextFrameIndex >= transfer.frames.length) {
    return;
  }

  const protocolPreset = getProtocolPreset(transfer.protocolPresetId) || getSessionProtocolPreset(session);
  const packet = transfer.frames[transfer.nextFrameIndex];
  transfer.awaitingAckFrameNo = transfer.nextFrameIndex + 1;
  transfer.nextFrameIndex += 1;

  sendPacket(
    session,
    packet,
    `${COMMANDS.DOWNLOAD_PATTERN_DATA.name} ${transfer.awaitingAckFrameNo}/${transfer.frames.length}`,
    protocolPreset
  );
}

function clearDownloadTransfer(session) {
  session.transfers.downloadToClient = null;
}

function requestUploadFromClient(session, payload = {}) {
  const request = resolvePatternRequest(session.clientPatterns, payload);
  if (!request.patternId && !request.patternName) {
    throw new Error("请先填写客户端花型编号或名称");
  }

  const protocolPreset = getSessionProtocolPreset(session);
  session.transfers.uploadFromClient = {
    request,
    frames: new Map(),
    totalFrames: null,
    protocolPresetId: protocolPreset.id,
    startedAt: nowLocal(),
    status: null,
  };

  sendPacket(
    session,
    createUploadPatternCommandFrame(request, protocolPreset),
    COMMANDS.UPLOAD_PATTERN_COMMAND.name,
    protocolPreset
  );
}

function completeUploadFromClient(session) {
  const transfer = session.transfers.uploadFromClient;
  if (!transfer || !transfer.totalFrames || transfer.frames.size !== transfer.totalFrames) {
    return false;
  }

  const orderedBuffers = [];
  for (let frameNo = 1; frameNo <= transfer.totalFrames; frameNo += 1) {
    orderedBuffers.push(transfer.frames.get(frameNo) || Buffer.alloc(0));
  }

  const dataBuffer = Buffer.concat(orderedBuffers);
  const savedPattern = upsertServerPattern({
    patternId: transfer.request.patternId || getNextPatternId(state.serverPatterns),
    patternName:
      transfer.request.patternName ||
      findPatternByReference(session.clientPatterns, transfer.request.patternId, null)?.patternName ||
      `上传花型${getNextPatternId(state.serverPatterns)}`,
    dataText: dataBuffer.toString("utf8"),
  });

  markTransferResult(session, "upload", 2, `已接收客户端花型 ${savedPattern.patternName}`);
  session.transfers.uploadFromClient = null;
  return true;
}

function requestDeleteClientPattern(session, payload = {}) {
  const request = resolvePatternRequest(session.clientPatterns, payload);
  if (!request.patternId && !request.patternName) {
    throw new Error("请先填写客户端花型编号或名称");
  }

  const protocolPreset = getSessionProtocolPreset(session);
  session.transfers.pendingDelete = {
    ...request,
    requestedAt: nowLocal(),
    protocolPresetId: protocolPreset.id,
  };

  sendPacket(
    session,
    createDeletePatternCommandFrame(request, protocolPreset),
    COMMANDS.DELETE_PATTERN_FILE.name,
    protocolPreset
  );
}

function requestClientPatternList(session) {
  const protocolPreset = getSessionProtocolPreset(session);
  session.transfers.clientPatternList = {
    frames: new Map(),
    totalFrames: null,
    protocolPresetId: protocolPreset.id,
    startedAt: nowLocal(),
  };

  sendPacket(session, createReadPatternListFrame(protocolPreset), COMMANDS.READ_PATTERN_LIST.name, protocolPreset);
}

function requestUploadResume(session, frameNo = 1) {
  const protocolPreset = getSessionProtocolPreset(session);
  sendPacket(
    session,
    createTransferResumeFrame(
      { transferType: TRANSFER_TYPES.upload, frameNo: normalizeNumber(frameNo, 1) },
      protocolPreset
    ),
    COMMANDS.TRANSFER_RESUME.name,
    protocolPreset
  );
}

function sendServerPatternList(session, protocolPreset = getSessionProtocolPreset(session)) {
  const packets = createPatternListFrames(
    COMMANDS.REQUEST_SERVER_PATTERN_LIST,
    state.serverPatterns,
    protocolPreset,
    { includeId: false }
  );

  packets.forEach((packet, index) => {
    sendPacket(
      session,
      packet,
      `${COMMANDS.REQUEST_SERVER_PATTERN_LIST.name} ${index + 1}/${packets.length}`,
      protocolPreset
    );
  });
}

function removeClientPatternSnapshot(session, patternId, patternName) {
  session.clientPatterns = session.clientPatterns.filter((item) => {
    if (patternId && item.patternId === patternId) {
      return false;
    }
    if (patternName && item.patternName === patternName) {
      return false;
    }
    return true;
  });
}

function handleDownloadCommandAck(session, frame) {
  const transfer = session.transfers.downloadToClient;
  if (!transfer) {
    return;
  }

  const parsed = parseDownloadPatternCommandPayload(frame.payload);
  transfer.commandAcknowledged = true;
  transfer.ackDeviceType = frame.addr1;
  transfer.ackDeviceId = frame.addr2;
  appendLog(`会话 ${session.id} 已确认下载花型 ${parsed.patternName || transfer.pattern.patternName}`, {
    sessionId: session.id,
  });
  sendNextDownloadFrame(session);
}

function handleDownloadDataAck(session, frame) {
  const transfer = session.transfers.downloadToClient;
  if (!transfer) {
    return;
  }

  const parsed = parseSingleByteResultPayload(frame.payload);
  if (!parsed) {
    return;
  }

  if (parsed.result !== 0) {
    markTransferResult(session, "download", parsed.result, `客户端拒绝接收下载数据帧 ${frame.frameNo}`);
    clearDownloadTransfer(session);
    return;
  }

  transfer.awaitingAckFrameNo = null;
  sendNextDownloadFrame(session);
}

function handleDownloadResult(session, frame) {
  const transfer = session.transfers.downloadToClient;
  if (!transfer) {
    return;
  }

  const parsed = parseSingleByteResultPayload(frame.payload);
  if (!parsed) {
    return;
  }

  const detail =
    parsed.result === 0
      ? `客户端下载成功 ${transfer.pattern.patternName}`
      : `客户端下载失败 ${transfer.pattern.patternName}`;
  markTransferResult(session, "download", parsed.result, detail);
  clearDownloadTransfer(session);
}

function handleUploadStatus(session, frame) {
  const transfer = session.transfers.uploadFromClient;
  if (!transfer) {
    return;
  }

  const parsed = parseSingleByteResultPayload(frame.payload);
  if (!parsed) {
    return;
  }

  transfer.status = parsed.result;
  if (parsed.result === 0) {
    appendLog(`会话 ${session.id} 准备上传花型 ${transfer.request.patternId} / ${transfer.request.patternName}`, {
      sessionId: session.id,
    });
    return;
  }

  if (parsed.result === 2) {
    if (!completeUploadFromClient(session)) {
      markTransferResult(session, "upload", 2, "客户端已报告上传成功，等待数据帧补齐");
    }
    return;
  }

  const messages = {
    1: "客户端拒绝上传",
    3: "客户端上传失败",
    4: "客户端上传超时",
  };
  markTransferResult(session, "upload", parsed.result, messages[parsed.result] || "上传结束");
  session.transfers.uploadFromClient = null;
}

function handleUploadData(session, frame) {
  const transfer = session.transfers.uploadFromClient;
  if (!transfer) {
    return;
  }

  const protocolPreset = frame.protocol || getSessionProtocolPreset(session);
  transfer.totalFrames = frame.totalFrames;
  transfer.frames.set(frame.frameNo, Buffer.from(frame.payload));

  sendPacket(
    session,
    createCommandResultPacket(COMMANDS.UPLOAD_PATTERN_DATA, 0, protocolPreset, {
      totalFrames: frame.totalFrames,
      frameNo: frame.frameNo,
    }),
    `${COMMANDS.UPLOAD_PATTERN_DATA.name} 应答 ${frame.frameNo}/${frame.totalFrames}`,
    protocolPreset
  );
}

function handleDeleteResult(session, frame) {
  const transfer = session.transfers.pendingDelete;
  if (!transfer || frame.payload.length < 1) {
    return;
  }

  const result = frame.payload.readUInt8(frame.payload.length - 1);
  if (result === 0) {
    removeClientPatternSnapshot(session, transfer.patternId, transfer.patternName);
  }

  const detail =
    result === 0
      ? `客户端已删除花型 ${transfer.patternId || "-"} / ${transfer.patternName || "-"}`
      : `客户端删除花型失败 ${transfer.patternId || "-"} / ${transfer.patternName || "-"}`;
  markTransferResult(session, "delete", result, detail);
  session.transfers.pendingDelete = null;
}

function handleClientPatternListFrame(session, frame) {
  const protocolPreset = frame.protocol || getSessionProtocolPreset(session);
  if (!session.transfers.clientPatternList) {
    session.transfers.clientPatternList = {
      frames: new Map(),
      totalFrames: null,
      protocolPresetId: protocolPreset.id,
      startedAt: nowLocal(),
    };
  }

  const transfer = session.transfers.clientPatternList;
  transfer.totalFrames = frame.totalFrames;
  transfer.frames.set(frame.frameNo, parsePatternListPayload(frame.payload, protocolPreset, { includeId: true }));

  if (transfer.totalFrames && transfer.frames.size === transfer.totalFrames) {
    const entries = [];
    for (let frameNo = 1; frameNo <= transfer.totalFrames; frameNo += 1) {
      entries.push(...(transfer.frames.get(frameNo) || []));
    }
    session.clientPatterns = entries;
    session.transfers.clientPatternList = null;
    markTransferResult(session, "list", 0, `已获取客户端花型列表 ${entries.length} 项`);
  }
}

function handleResumeRequest(session, frame) {
  const parsed = parseTransferResumePayload(frame.payload);
  if (!parsed) {
    return;
  }

  session.transfers.lastResumeRequest = {
    transferType: parsed.transferType,
    frameNo: parsed.frameNo,
    requestedAt: nowLocal(),
  };

  if (parsed.transferType === TRANSFER_TYPES.download && session.transfers.downloadToClient) {
    session.transfers.downloadToClient.nextFrameIndex = Math.max(0, parsed.frameNo - 1);
    session.transfers.downloadToClient.awaitingAckFrameNo = null;
    sendNextDownloadFrame(session);
    return;
  }

  appendLog(`会话 ${session.id} 请求续传 type=${parsed.transferType} frame=${parsed.frameNo}`, {
    sessionId: session.id,
  });
}

function handleProductionData(session, frame, command) {
  const protocolPreset = frame.protocol || getSessionProtocolPreset(session);
  if (isCommand(frame, COMMANDS.PRODUCTION_DATA_OLD)) {
    session.deviceInfo.lastProductionDataOld = parseProductionDataOldPayload(frame.payload, protocolPreset);
  } else {
    session.deviceInfo.lastProductionDataNew = parseProductionDataNewPayload(frame.payload, protocolPreset);
  }

  sendPacket(
    session,
    createCommandResultPacket(command, 0, protocolPreset),
    command.name,
    protocolPreset
  );
}

function performAction(action, payload = {}) {
  switch (action) {
    case "syncTime": {
      const session = requireSession(payload);
      const protocolPreset = getSessionProtocolPreset(session);
      sendPacket(session, createTimeSyncResponseFrame(new Date(), protocolPreset), COMMANDS.TIME_SYNC.name, protocolPreset);
      return;
    }
    case "queryRealtimeStatus": {
      const session = requireSession(payload);
      const protocolPreset = getSessionProtocolPreset(session);
      sendPacket(
        session,
        createRealtimeStatusQueryFrame(protocolPreset),
        COMMANDS.REALTIME_STATUS_QUERY.name,
        protocolPreset
      );
      return;
    }
    case "readHighPoint": {
      const session = requireSession(payload);
      const protocolPreset = getSessionProtocolPreset(session);
      sendPacket(session, createReadHighPointFrame(protocolPreset), COMMANDS.READ_HIGH_POINT.name, protocolPreset);
      return;
    }
    case "setHighPoint": {
      const session = requireSession(payload);
      const protocolPreset = getSessionProtocolPreset(session);
      sendPacket(
        session,
        createSetHighPointFrame(normalizeNumber(payload.value, 0), protocolPreset),
        COMMANDS.SET_HIGH_POINT.name,
        protocolPreset
      );
      return;
    }
    case "readLowPoint": {
      const session = requireSession(payload);
      const protocolPreset = getSessionProtocolPreset(session);
      sendPacket(session, createReadLowPointFrame(protocolPreset), COMMANDS.READ_LOW_POINT.name, protocolPreset);
      return;
    }
    case "setLowPoint": {
      const session = requireSession(payload);
      const protocolPreset = getSessionProtocolPreset(session);
      sendPacket(
        session,
        createSetLowPointFrame(normalizeNumber(payload.value, 0), protocolPreset),
        COMMANDS.SET_LOW_POINT.name,
        protocolPreset
      );
      return;
    }
    case "setSpeed": {
      const session = requireSession(payload);
      const protocolPreset = getSessionProtocolPreset(session);
      sendPacket(
        session,
        createSetSpeedFrame(normalizeNumber(payload.value, 0), protocolPreset),
        COMMANDS.SET_SPEED.name,
        protocolPreset
      );
      return;
    }
    case "setDeviceFlag": {
      const session = requireSession(payload);
      const protocolPreset = getSessionProtocolPreset(session);
      sendPacket(
        session,
        createDeviceFlagFrame(String(payload.flag || ""), protocolPreset, COMMANDS.SET_DEVICE_FLAG),
        COMMANDS.SET_DEVICE_FLAG.name,
        protocolPreset
      );
      return;
    }
    case "readClientPatternList": {
      const session = requireSession(payload);
      requestClientPatternList(session);
      return;
    }
    case "downloadPattern": {
      const session = requireSession(payload);
      startDownloadToClient(session, payload.patternId);
      return;
    }
    case "requestUploadPattern": {
      const session = requireSession(payload);
      requestUploadFromClient(session, payload);
      return;
    }
    case "deleteClientPattern": {
      const session = requireSession(payload);
      requestDeleteClientPattern(session, payload);
      return;
    }
    case "resumeUpload": {
      const session = requireSession(payload);
      requestUploadResume(session, payload.frameNo);
      return;
    }
    default:
      throw new Error(`未知动作: ${action}`);
  }
}

function handleFrame(session, frame) {
  session.lastMessageAt = nowLocal();

  if (frame.error) {
    appendLog(`会话 ${session.id} 收到非法报文`, {
      sessionId: session.id,
      raw: toHex(frame.raw),
      detail: frame.error,
    });
    return;
  }

  if (frame.protocol?.id) {
    session.protocolPresetId = frame.protocol.id;
    session.protocolPresetName = frame.protocol.name;
  }

  if (frame.recovered?.message) {
    session.lastRecoveryMessage = frame.recovered.message;
  }

  appendLog(
    `会话 ${session.id} <- ${describeFrame(frame)}${frame.recovered ? ` [兼容恢复: ${frame.recovered.message}]` : ""}`,
    {
      sessionId: session.id,
      command: commandName(frame.type, frame.code),
      raw: toHex(frame.raw),
      recovery: frame.recovered?.message || "",
    }
  );

  if (isCommand(frame, COMMANDS.REGISTER)) {
    session.registered = true;
    session.registerAcknowledgedAt = nowLocal();
    sendPacket(session, createRegisterFrame(frame.protocol), COMMANDS.REGISTER.name, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.HEARTBEAT)) {
    session.lastHeartbeatAt = nowLocal();
    sendPacket(session, createHeartbeatFrame(frame.protocol), COMMANDS.HEARTBEAT.name, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.TIME_SYNC)) {
    session.lastTimeSyncAt = nowLocal();
    const parsed = parseTimeSyncPayload(frame.payload);
    if (parsed) {
      session.deviceInfo.timeSyncValue = parsed.formatted;
      return;
    }

    sendPacket(session, createTimeSyncResponseFrame(new Date(), frame.protocol), COMMANDS.TIME_SYNC.name, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.DEVICE_MODEL)) {
    session.deviceInfo.model = parseDeviceModelPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.DEVICE_FLAG)) {
    session.deviceInfo.flag = parseDeviceFlagPayload(frame.payload);
    return;
  }

  if (isCommand(frame, COMMANDS.SEWING_RANGE)) {
    session.deviceInfo.sewingRange = parseSewingRangePayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.MAX_SPEED)) {
    session.deviceInfo.maxSpeed = parseMaxSpeedPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.CURRENT_SPEED)) {
    session.deviceInfo.currentSpeed = parseCurrentSpeedPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.PATTERN_INFO)) {
    session.deviceInfo.patternInfo = parsePatternInfoPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.PRODUCTION_COUNT)) {
    session.deviceInfo.productionCount = parseProductionCountPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.BOTTOM_THREAD_COUNT)) {
    session.deviceInfo.bottomThreadCount = parseBottomThreadCountPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.ALARM)) {
    session.deviceInfo.alarm = parseAlarmPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.IDLE_ALARM)) {
    session.deviceInfo.idleAlarm = parseIdleAlarmPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.OIL_PROMPT)) {
    session.deviceInfo.oilPrompt = parseOilPromptPayload(frame.payload);
    return;
  }

  if (isCommand(frame, COMMANDS.NEEDLE_COUNT)) {
    session.deviceInfo.needleCount = parseNeedleCountPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.SEWING_STATUS)) {
    session.deviceInfo.sewingStatus = parseSewingStatusPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.REALTIME_STATUS_QUERY)) {
    session.deviceInfo.realtimeStatus = parseRealtimeStatusPayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.THREAD_TRIM_COMPLETE)) {
    session.deviceInfo.lastThreadTrimAt = nowLocal();
    return;
  }

  if (isCommand(frame, COMMANDS.READ_HIGH_POINT)) {
    session.deviceInfo.highPoint = parseUShortValuePayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.READ_LOW_POINT)) {
    session.deviceInfo.lowPoint = parseUShortValuePayload(frame.payload, frame.protocol);
    return;
  }

  if (isCommand(frame, COMMANDS.SET_HIGH_POINT)) {
    session.deviceInfo.lastSetHighPointResult = parseSingleByteResultPayload(frame.payload);
    return;
  }

  if (isCommand(frame, COMMANDS.SET_LOW_POINT)) {
    session.deviceInfo.lastSetLowPointResult = parseSingleByteResultPayload(frame.payload);
    return;
  }

  if (isCommand(frame, COMMANDS.SET_SPEED)) {
    session.deviceInfo.lastSetSpeedResult = parseSingleByteResultPayload(frame.payload);
    return;
  }

  if (isCommand(frame, COMMANDS.DOWNLOAD_PATTERN_COMMAND)) {
    handleDownloadCommandAck(session, frame);
    return;
  }

  if (isCommand(frame, COMMANDS.COMMUNICATION_ERROR)) {
    handleDownloadDataAck(session, frame);
    return;
  }

  if (isCommand(frame, COMMANDS.DOWNLOAD_PATTERN_DATA)) {
    handleDownloadResult(session, frame);
    return;
  }

  if (isCommand(frame, COMMANDS.UPLOAD_PATTERN_COMMAND)) {
    handleUploadStatus(session, frame);
    return;
  }

  if (isCommand(frame, COMMANDS.UPLOAD_PATTERN_DATA)) {
    handleUploadData(session, frame);
    return;
  }

  if (isCommand(frame, COMMANDS.DELETE_PATTERN_FILE)) {
    handleDeleteResult(session, frame);
    return;
  }

  if (isCommand(frame, COMMANDS.READ_PATTERN_LIST)) {
    handleClientPatternListFrame(session, frame);
    return;
  }

  if (isCommand(frame, COMMANDS.REQUEST_SERVER_PATTERN_LIST)) {
    sendServerPatternList(session, frame.protocol || getSessionProtocolPreset(session));
    return;
  }

  if (isCommand(frame, COMMANDS.TRANSFER_RESUME)) {
    handleResumeRequest(session, frame);
    return;
  }

  if (isCommand(frame, COMMANDS.PRODUCTION_DATA_OLD)) {
    handleProductionData(session, frame, COMMANDS.PRODUCTION_DATA_OLD);
    return;
  }

  if (isCommand(frame, COMMANDS.PRODUCTION_DATA_NEW)) {
    handleProductionData(session, frame, COMMANDS.PRODUCTION_DATA_NEW);
  }
}

const tcpServer = net.createServer((socket) => {
  const id = nextSessionId;
  nextSessionId += 1;

  const session = {
    id,
    socket,
    parser: new FrameParser({ protocols: PROTOCOL_PRESETS.map((preset) => preset.id) }),
    remoteAddress: socket.remoteAddress,
    remotePort: socket.remotePort,
    connectedAt: nowLocal(),
    lastMessageAt: null,
    lastHeartbeatAt: null,
    lastTimeSyncAt: null,
    registered: false,
    registerAcknowledgedAt: null,
    protocolPresetId: null,
    protocolPresetName: null,
    lastRecoveryMessage: null,
    deviceInfo: createSessionDeviceInfo(),
    clientPatterns: [],
    transfers: {
      downloadToClient: null,
      uploadFromClient: null,
      clientPatternList: null,
      pendingDelete: null,
      lastResumeRequest: null,
      lastResult: null,
    },
  };

  state.sessions.set(id, session);
  appendLog(`会话 ${id} 已连接 ${session.remoteAddress}:${session.remotePort}`, {
    sessionId: id,
  });

  socket.on("data", (chunk) => {
    const frames = session.parser.push(chunk);
    for (const frame of frames) {
      handleFrame(session, frame);
    }
  });

  socket.on("close", () => {
    appendLog(`会话 ${id} 已断开`, { sessionId: id });
    state.sessions.delete(id);
  });

  socket.on("error", (error) => {
    appendLog(`会话 ${id} 错误: ${error.message}`, { sessionId: id });
  });
});

tcpServer.listen(tcpPort, bindHost, () => {
  appendLog(`TCP 服务已监听 ${tcpPort}`);
});

const httpServer = http.createServer(async (request, response) => {
  const url = new URL(request.url, `http://${request.headers.host}`);

  if (request.method === "GET" && url.pathname === "/") {
    sendFile(response, serverPage, "text/html; charset=utf-8");
    return;
  }

  if (request.method === "GET" && url.pathname === "/app.js") {
    sendFile(response, serverScript, "application/javascript; charset=utf-8");
    return;
  }

  if (request.method === "GET" && url.pathname === "/api/state") {
    sendJson(response, 200, publicState());
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/server-config") {
    const body = await readJsonBody(request);
    updateServerConfig(body);
    sendJson(response, 200, {
      ok: true,
      preferredProtocolPresetId: state.preferredProtocolPresetId,
    });
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
      const pattern = upsertServerPattern(body);
      sendJson(response, 200, { ok: true, pattern: getPatternSummary(pattern) });
    } catch (error) {
      sendJson(response, 400, { ok: false, error: error.message });
    }
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/patterns/delete") {
    try {
      const body = await readJsonBody(request);
      const pattern = deleteServerPattern(body.patternId);
      sendJson(response, 200, { ok: true, pattern: getPatternSummary(pattern) });
    } catch (error) {
      sendJson(response, 400, { ok: false, error: error.message });
    }
    return;
  }

  if (request.method === "POST" && url.pathname === "/api/logs/clear") {
    state.logs = [];
    sendJson(response, 200, { ok: true });
    return;
  }

  sendJson(response, 404, { error: "not found" });
});

httpServer.listen(httpPort, bindHost, () => {
  appendLog(`Web 控制台已启动 http://127.0.0.1:${httpPort}`);
  console.log(`Server UI: http://127.0.0.1:${httpPort}`);
  console.log(`TCP Port : ${tcpPort}`);
});
