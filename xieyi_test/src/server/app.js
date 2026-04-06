import { Buffer } from "node:buffer";
import fs from "node:fs";
import http from "node:http";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  createAlarmFrame,
  createBottomThreadCountFrame,
  createCurrentSpeedFrame,
  COMMANDS,
  commandName,
  createDeletePatternCommandFrame,
  createDeviceFlagFrame,
  createDeviceModelFrame,
  createDownloadPatternCommandFrame,
  createDownloadPatternDataFrames,
  createHeartbeatFrame,
  createHighPointFrame,
  createLowPointFrame,
  createMaxSpeedFrame,
  createPatternInfoFrame,
  createProductionCountFrame,
  createReadHighPointFrame,
  createReadLowPointFrame,
  createReadPatternListFrame,
  createRealtimeStatusQueryFrame,
  createRealtimeStatusResponseFrame,
  createRegisterFrame,
  createRequestServerPatternListFrame,
  createSewingRangeFrame,
  createSetHighPointFrame,
  createSetHighPointResultFrame,
  createSetLowPointFrame,
  createSetLowPointResultFrame,
  createSetSpeedFrame,
  createSetSpeedResultFrame,
  createTimeSyncRequestFrame,
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
const logDir = path.join(rootDir, "logs/server");

const DEFAULT_PROTOCOL_PRESET_ID = "be-modbus-lencrc";
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

const bindHost = process.env.BIND_HOST || "0.0.0.0";
const tcpPort = Number(process.env.TCP_PORT || 38400);
const httpPort = Number(process.env.HTTP_PORT || 9001);
const timeZone = getLocalTimeZone();

let nextSessionId = 1;

function createLogFileTimestamp(date = new Date()) {
  const year = date.getFullYear();
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  const hour = String(date.getHours()).padStart(2, "0");
  const minute = String(date.getMinutes()).padStart(2, "0");
  const second = String(date.getSeconds()).padStart(2, "0");
  return `${year}${month}${day}-${hour}${minute}${second}`;
}

fs.mkdirSync(logDir, { recursive: true });
const logFilePath = path.join(logDir, `server-${createLogFileTimestamp()}.log`);

function createPattern(patternId, patternName, dataText, metadata = {}) {
  const content =
    (dataText?.length ?? 0) > 0
      ? dataText
      : `PATTERN:${patternName}\nID:${patternId}\nGENERATED:${nowLocal()}`;
  return {
    patternId,
    patternName,
    data: Buffer.from(content, "utf8"),
    updatedAt: nowLocal(),
    sourcePatternId: Number(metadata.sourcePatternId) || 0,
    sourcePatternName: String(metadata.sourcePatternName || "").trim(),
  };
}

const state = {
  tcpPort,
  httpPort,
  logFilePath,
  preferredProtocolPresetId: DEFAULT_PROTOCOL_PRESET_ID,
  verboseProtocolLogs: process.env.VERBOSE_PROTOCOL_LOGS === "1",
  activeDeviceMode: process.env.ACTIVE_DEVICE_MODE !== "0",
  sessions: new Map(),
  logs: [],
  protocolLogs: [],
  startedAt: nowLocal(),
  serverPatterns: [
    createPattern(1, "服务器花型A", "SERVER-PATTERN-A"),
    createPattern(2, "服务器花型B", "SERVER-PATTERN-B"),
  ],
};

function createLogEntry(message, extra = {}) {
  const atMs = Date.now();
  return {
    id: `${atMs}-${Math.random().toString(16).slice(2, 8)}`,
    atMs,
    time: nowLocal(),
    message,
    ...extra,
  };
}

function writeLogEntryToDisk(entry) {
  const parts = [`[${entry.time}]`, `[${entry.channel}]`, entry.message];
  if (entry.detail) {
    parts.push(`detail=${entry.detail}`);
  }
  if (entry.command) {
    parts.push(`command=${entry.command}`);
  }
  if (entry.sessionId) {
    parts.push(`session=${entry.sessionId}`);
  }
  if (entry.recovery) {
    parts.push(`recovery=${entry.recovery}`);
  }
  if (entry.raw) {
    parts.push(`raw=${entry.raw}`);
  }

  try {
    fs.appendFileSync(state.logFilePath, `${parts.join(" | ")}\n`, "utf8");
  } catch (error) {
    console.error(`写入服务端日志文件失败: ${error.message}`);
  }
}

function appendLog(message, extra = {}) {
  const entry = createLogEntry(message, { channel: "event", ...extra });
  state.logs.unshift(entry);
  state.logs = state.logs.slice(0, 400);
  writeLogEntryToDisk(entry);
}

function appendProtocolLog(message, extra = {}) {
  const entry = createLogEntry(message, { channel: "protocol", ...extra });
  state.protocolLogs.unshift(entry);
  state.protocolLogs = state.protocolLogs.slice(0, 800);
  writeLogEntryToDisk(entry);
}

function appendWireLog(session, direction, buffer) {
  writeLogEntryToDisk(
    createLogEntry(`会话 ${session.id} TCP ${direction} ${buffer.length}字节`, {
      channel: "tcp",
      sessionId: session.id,
      raw: toHex(buffer),
    })
  );
}

function getVisibleLogs() {
  if (!state.verboseProtocolLogs) {
    return state.logs;
  }

  return [...state.logs, ...state.protocolLogs]
    .sort((left, right) => right.atMs - left.atMs)
    .slice(0, 800);
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
    sourcePatternId: Number(pattern.sourcePatternId) || 0,
    sourcePatternName: String(pattern.sourcePatternName || "").trim(),
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

function createActiveDeviceInfo() {
  return {
    model: {
      model: 4346,
      deviceId: 10001,
      name: "服务端下位机模拟器",
    },
    flag: "MB-SERVER-0001",
    sewingRange: {
      xRange: 1200,
      yRange: 800,
    },
    maxSpeed: {
      maxSpeed: 3200,
    },
    currentSpeed: {
      currentSpeed: 1800,
    },
    patternInfo: {
      patternId: 1,
      patternName: "默认花型A",
    },
    productionCount: {
      totalCount: 0,
      currentCount: 0,
    },
    bottomThreadCount: {
      totalLength: 30000,
      remainLength: 18000,
    },
    alarm: {
      alarmCode: 0,
    },
    idleAlarm: {
      minutes: 0,
      status: 0,
    },
    oilPrompt: {
      prompt: 0,
    },
    needleCount: {
      needleCount: 0,
    },
    sewingStatus: {
      status: SEWING_EVENT_STATUS.stop,
      needleNumber: 0,
      stopReason: 0,
    },
    realtimeStatus: {
      status: REALTIME_STATUS.idle,
      patternId: 1,
      patternName: "默认花型A",
    },
    highPoint: {
      value: 60,
    },
    lowPoint: {
      value: 20,
    },
    timeSyncValue: null,
    lastThreadTrimAt: null,
    lastProductionDataOld: null,
    lastProductionDataNew: null,
  };
}

function createActiveDevicePatterns() {
  return [
    { patternId: 1, patternName: "默认花型A" },
    { patternId: 2, patternName: "样例花型B" },
  ];
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
    connected: session.connected,
    connectedAt: session.connectedAt,
    disconnectedAt: session.disconnectedAt,
    disconnectReason: session.disconnectReason,
    lastMessageAt: session.lastMessageAt,
    lastHeartbeatAt: session.lastHeartbeatAt,
    lastTimeSyncAt: session.lastTimeSyncAt,
    registered: session.registered,
    registerAcknowledgedAt: session.registerAcknowledgedAt,
    activeDeviceMode: session.activeDeviceMode,
    protocolPresetId: session.protocolPresetId,
    protocolPresetName: session.protocolPresetName,
    lastRecoveryMessage: session.lastRecoveryMessage,
    deviceInfo: session.deviceInfo,
    clientPatterns: session.clientPatterns,
    transferSummary: getTransferSummary(session),
  };
}

function publicState() {
  const activeSessionCount = Array.from(state.sessions.values()).filter((session) => session.connected).length;
  return {
    bindHost,
    timeZone,
    tcpPort: state.tcpPort,
    httpPort: state.httpPort,
    logFilePath: state.logFilePath,
    startedAt: state.startedAt,
    preferredProtocolPresetId: state.preferredProtocolPresetId,
    verboseProtocolLogs: state.verboseProtocolLogs,
    activeDeviceMode: state.activeDeviceMode,
    protocolPresets: PROTOCOL_PRESETS,
    serverPatterns: state.serverPatterns.map(getPatternSummary),
    sessions: Array.from(state.sessions.values()).map(sessionSummary),
    activeSessionCount,
    logs: getVisibleLogs(),
  };
}

function updateServerConfig({ verboseProtocolLogs }) {
  if (typeof verboseProtocolLogs === "boolean" && state.verboseProtocolLogs !== verboseProtocolLogs) {
    state.verboseProtocolLogs = verboseProtocolLogs;
    appendLog(`详细协议日志已${verboseProtocolLogs ? "开启" : "关闭"}`);
  }
}

function getSessionProtocolPreset(session) {
  return (
    getProtocolPreset(session?.protocolPresetId) ||
    getProtocolPreset(state.preferredProtocolPresetId) ||
    getProtocolPreset(DEFAULT_PROTOCOL_PRESET_ID)
  );
}

function getSessionPatternInfo(session) {
  return session.deviceInfo.patternInfo || { patternId: 0, patternName: "" };
}

function ensureSessionActiveDeviceState(session) {
  if (!session.activeDeviceMode) {
    return;
  }

  if (!session.deviceInfo.model) {
    session.deviceInfo = createActiveDeviceInfo();
  }
}

function stopSessionActiveDeviceLoops(session) {
  if (session.activeDevice.registerRetryTimer) {
    clearInterval(session.activeDevice.registerRetryTimer);
    session.activeDevice.registerRetryTimer = null;
  }

  if (session.activeDevice.heartbeatTimer) {
    clearInterval(session.activeDevice.heartbeatTimer);
    session.activeDevice.heartbeatTimer = null;
  }

  if (session.activeDevice.postRegisterInitTimer) {
    clearTimeout(session.activeDevice.postRegisterInitTimer);
    session.activeDevice.postRegisterInitTimer = null;
  }

  if (session.activeDevice.autoPatternListTimer) {
    clearTimeout(session.activeDevice.autoPatternListTimer);
    session.activeDevice.autoPatternListTimer = null;
  }
}

function sendActiveDeviceRegister(session, protocolPreset = getSessionProtocolPreset(session)) {
  session.activeDevice.lastRegisterSentMs = Date.now();
  sendPacket(session, createRegisterFrame(protocolPreset), COMMANDS.REGISTER.name, protocolPreset);
}

function sendActiveDeviceHeartbeat(session, protocolPreset = getSessionProtocolPreset(session)) {
  session.activeDevice.lastHeartbeatSentMs = Date.now();
  sendPacket(session, createHeartbeatFrame(protocolPreset), COMMANDS.HEARTBEAT.name, protocolPreset);
}

function sendActiveDeviceRealtimeStatus(session, protocolPreset = getSessionProtocolPreset(session)) {
  const patternInfo = getSessionPatternInfo(session);
  sendPacket(
    session,
    createRealtimeStatusResponseFrame(
      {
        status: session.deviceInfo.realtimeStatus?.status ?? REALTIME_STATUS.idle,
        patternId: patternInfo.patternId,
        patternName: patternInfo.patternName,
      },
      protocolPreset
    ),
    COMMANDS.REALTIME_STATUS_QUERY.name,
    protocolPreset
  );
}

function sendActiveDeviceSnapshot(session, protocolPreset = getSessionProtocolPreset(session)) {
  ensureSessionActiveDeviceState(session);
  const patternInfo = getSessionPatternInfo(session);
  sendPacket(
    session,
    createPatternInfoFrame(patternInfo, protocolPreset),
    COMMANDS.PATTERN_INFO.name,
    protocolPreset
  );
  sendPacket(
    session,
    createAlarmFrame(session.deviceInfo.alarm?.alarmCode ?? 0, protocolPreset),
    COMMANDS.ALARM.name,
    protocolPreset
  );
  sendPacket(
    session,
    createDeviceFlagFrame(session.deviceInfo.flag || "", protocolPreset),
    COMMANDS.DEVICE_FLAG.name,
    protocolPreset
  );
  sendPacket(
    session,
    createDeviceModelFrame(
      {
        model: session.deviceInfo.model?.model ?? 0,
        deviceId: session.deviceInfo.model?.deviceId ?? 0,
        name: session.deviceInfo.model?.name || "服务端下位机模拟器",
      },
      protocolPreset
    ),
    COMMANDS.DEVICE_MODEL.name,
    protocolPreset
  );
  sendPacket(
    session,
    createSewingRangeFrame(
      {
        xRange: session.deviceInfo.sewingRange?.xRange ?? 0,
        yRange: session.deviceInfo.sewingRange?.yRange ?? 0,
      },
      protocolPreset
    ),
    COMMANDS.SEWING_RANGE.name,
    protocolPreset
  );
  sendPacket(
    session,
    createMaxSpeedFrame(session.deviceInfo.maxSpeed?.maxSpeed ?? 0, protocolPreset),
    COMMANDS.MAX_SPEED.name,
    protocolPreset
  );
  sendPacket(
    session,
    createProductionCountFrame(
      {
        totalCount: session.deviceInfo.productionCount?.totalCount ?? 0,
        currentCount: session.deviceInfo.productionCount?.currentCount ?? 0,
      },
      protocolPreset
    ),
    COMMANDS.PRODUCTION_COUNT.name,
    protocolPreset
  );
  sendPacket(
    session,
    createBottomThreadCountFrame(
      {
        totalLength: session.deviceInfo.bottomThreadCount?.totalLength ?? 0,
        remainLength: session.deviceInfo.bottomThreadCount?.remainLength ?? 0,
      },
      protocolPreset
    ),
    COMMANDS.BOTTOM_THREAD_COUNT.name,
    protocolPreset
  );
  sendPacket(
    session,
    createHighPointFrame(session.deviceInfo.highPoint?.value ?? 0, protocolPreset),
    COMMANDS.READ_HIGH_POINT.name,
    protocolPreset
  );
  sendPacket(
    session,
    createLowPointFrame(session.deviceInfo.lowPoint?.value ?? 0, protocolPreset),
    COMMANDS.READ_LOW_POINT.name,
    protocolPreset
  );
  sendPacket(session, createTimeSyncRequestFrame(protocolPreset), COMMANDS.TIME_SYNC.name, protocolPreset);
}

function sendActiveDeviceHandshake(session, trigger = "首发", protocolPreset = getSessionProtocolPreset(session)) {
  ensureSessionActiveDeviceState(session);
  appendLog(`会话 ${session.id} 主动模拟下位机注册 ${trigger} [${protocolPreset.name}]`, {
    sessionId: session.id,
  });
  sendActiveDeviceRegister(session, protocolPreset);
}

function scheduleActiveDevicePostRegisterInit(session, protocolPreset = getSessionProtocolPreset(session)) {
  if (!session.activeDeviceMode || session.activeDevice.postRegisterInitSent) {
    return;
  }

  session.activeDevice.postRegisterInitSent = true;

  if (session.activeDevice.postRegisterInitTimer) {
    clearTimeout(session.activeDevice.postRegisterInitTimer);
  }

  session.activeDevice.postRegisterInitTimer = setTimeout(() => {
    session.activeDevice.postRegisterInitTimer = null;
    if (!session.socket || session.socket.destroyed) {
      return;
    }

    appendLog(`会话 ${session.id} 注册完成，开始发送下位机信息`, {
      sessionId: session.id,
    });
    sendActiveDeviceSnapshot(session, protocolPreset);
    sendActiveDeviceHeartbeat(session, protocolPreset);
  }, 150);

  if (session.activeDevice.autoPatternListTimer) {
    clearTimeout(session.activeDevice.autoPatternListTimer);
  }

  session.activeDevice.autoPatternListTimer = setTimeout(() => {
    session.activeDevice.autoPatternListTimer = null;
    if (!session.socket || session.socket.destroyed || session.activeDevice.peerPatternListRequestedAt) {
      return;
    }

    requestClientPatternList(session);
  }, 1200);
}

function startSessionActiveDeviceLoops(session) {
  stopSessionActiveDeviceLoops(session);
  session.activeDevice.heartbeatTimer = setInterval(() => {
    if (!session.socket || session.socket.destroyed) {
      stopSessionActiveDeviceLoops(session);
      return;
    }
    if (!session.registered) {
      return;
    }
    sendActiveDeviceHeartbeat(session);
  }, 10000);

  session.activeDevice.registerRetryTimer = setInterval(() => {
    if (!session.socket || session.socket.destroyed) {
      stopSessionActiveDeviceLoops(session);
      return;
    }
    if (!session.registered) {
      sendActiveDeviceHandshake(session, "重试");
    }
  }, 5000);
}

function setSessionActiveDeviceMode(session, enabled) {
  session.activeDeviceMode = enabled;
  if (!enabled) {
    stopSessionActiveDeviceLoops(session);
    return;
  }

  if (!session.connected || !session.socket || session.socket.destroyed) {
    return;
  }

  ensureSessionActiveDeviceState(session);
  session.activeDevice.postRegisterInitSent = false;
  session.activeDevice.peerPatternListRequestedAt = 0;
  sendActiveDeviceHandshake(session);
  startSessionActiveDeviceLoops(session);
}

function markSessionRegistered(session, reason = "") {
  if (!session.registered) {
    session.registered = true;
    session.registerAcknowledgedAt = nowLocal();
    appendLog(`会话 ${session.id} 协议已建立${reason ? `: ${reason}` : ""}`, {
      sessionId: session.id,
    });
  }

  if (session.activeDevice.registerRetryTimer) {
    clearInterval(session.activeDevice.registerRetryTimer);
    session.activeDevice.registerRetryTimer = null;
  }

  if (session.activeDeviceMode) {
    scheduleActiveDevicePostRegisterInit(session);
  }
}

function findSession(sessionId) {
  const session = state.sessions.get(Number(sessionId));
  if (!session) {
    throw new Error("未找到会话");
  }
  if (!session.connected || !session.socket || session.socket.destroyed) {
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
  appendWireLog(session, "TX", packet);
  appendProtocolLog(`服务端 ${session.id} -> ${label} [${protocolPreset?.name ?? "-"}]`, {
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

function findServerPatternByUploadIdentity(sourcePatternId, sourcePatternName) {
  const nextSourceId = Number(sourcePatternId) || 0;
  const nextSourceName = String(sourcePatternName || "").trim();
  if (!nextSourceId && !nextSourceName) {
    return null;
  }

  return (
    state.serverPatterns.find((item) => {
      const itemSourceId = Number(item.sourcePatternId) || 0;
      const itemSourceName = String(item.sourcePatternName || "").trim();
      return itemSourceId === nextSourceId && itemSourceName === nextSourceName;
    }) ||
    state.serverPatterns.find(
      (item) =>
        !Number(item.sourcePatternId || 0) &&
        item.patternId === nextSourceId &&
        String(item.patternName || "").trim() === nextSourceName
    ) ||
    null
  );
}

function resolveAvailableServerPatternId(preferredId = 0, existing = null) {
  const nextPreferredId = Number(preferredId) || 0;
  if (
    nextPreferredId > 0 &&
    !state.serverPatterns.some((item) => item !== existing && item.patternId === nextPreferredId)
  ) {
    return nextPreferredId;
  }

  return getNextPatternId(state.serverPatterns);
}

function upsertServerPattern({
  patternId,
  patternName,
  dataText,
  sourcePatternId = 0,
  sourcePatternName = "",
  matchMode = "serverId",
}) {
  const normalizedSourceId = Number(sourcePatternId) || 0;
  const normalizedSourceName = String(sourcePatternName || patternName || "").trim();
  const existing =
    matchMode === "uploadIdentity"
      ? findServerPatternByUploadIdentity(normalizedSourceId, normalizedSourceName)
      : state.serverPatterns.find((item) => item.patternId === Number(patternId));
  const nextId =
    matchMode === "uploadIdentity"
      ? resolveAvailableServerPatternId(existing ? existing.patternId : patternId, existing)
      : Number(patternId) || getNextPatternId(state.serverPatterns);
  const nextName = String(patternName || "").trim() || `服务器花型${nextId}`;
  const nextPattern = createPattern(nextId, nextName, dataText, {
    sourcePatternId: normalizedSourceId,
    sourcePatternName: normalizedSourceName,
  });

  if (existing) {
    existing.patternName = nextPattern.patternName;
    existing.data = nextPattern.data;
    existing.updatedAt = nextPattern.updatedAt;
    existing.sourcePatternId = nextPattern.sourcePatternId;
    existing.sourcePatternName = nextPattern.sourcePatternName;
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

function rememberClientPattern(session, payload = {}) {
  const patternId = normalizeNumber(payload.patternId, 0);
  const patternName = String(payload.patternName || "").replace(/\0+$/g, "").trim();
  if (!patternId && !patternName) {
    return null;
  }

  const matched = findPatternByReference(session.clientPatterns, patternId, patternName);
  if (matched) {
    if (patternId) {
      matched.patternId = patternId;
    }
    if (patternName) {
      matched.patternName = patternName;
    }
    return matched;
  }

  const nextPattern = {
    patternId,
    patternName: patternName || `花型${patternId || session.clientPatterns.length + 1}`,
  };
  session.clientPatterns.push(nextPattern);
  return nextPattern;
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
  const sourcePatternId = transfer.request.patternId || 0;
  const sourcePatternName =
    transfer.request.patternName ||
    findPatternByReference(session.clientPatterns, transfer.request.patternId, null)?.patternName ||
    "";
  const savedPattern = upsertServerPattern({
    patternId: transfer.request.patternId || 0,
    patternName: sourcePatternName || `上传花型${getNextPatternId(state.serverPatterns)}`,
    dataText: dataBuffer.toString("utf8"),
    sourcePatternId,
    sourcePatternName,
    matchMode: "uploadIdentity",
  });

  markTransferResult(
    session,
    "upload",
    2,
    `已接收客户端花型 ${savedPattern.patternName} -> 服务器花型ID ${savedPattern.patternId}`
  );
  session.transfers.uploadFromClient = null;
  return true;
}

function buildDeleteAttemptPlan(session, request) {
  const attempts = [];

  const pushAttempt = ({
    label,
    patternId = request.patternId,
    patternName = request.patternName,
    nameEncoding = "unicodeFixed",
    includeId = true,
  }) => {
    attempts.push({
      label,
      patternId,
      patternName,
      nameEncoding,
      includeId,
      addr1: 0,
      addr2: 0,
    });
  };

  if (request.patternName) {
    pushAttempt({
      label: "纯名称 Unicode变长",
      nameEncoding: "unicode",
      includeId: false,
    });
  }

  /*
  if (request.patternId && request.patternName) {
    pushAttempt({ label: "标准协议 / 编号+名称 Unicode变长", nameEncoding: "unicode" });
    pushAttempt({ label: "编号+名称 Unicode固定44字节", nameEncoding: "unicodeFixed" });
    pushAttempt({ label: "编号+名称 兼容固定44字节", nameEncoding: "compatibleFixed" });
  }

  if (request.patternId) {
    pushAttempt({ label: "仅发送花型编号", patternName: "", nameEncoding: "unicode" });
  }

  if (request.patternName) {
    pushAttempt({ label: "纯名称 Unicode固定44字节", nameEncoding: "unicodeFixed", includeId: false });
    pushAttempt({ label: "纯名称 兼容固定44字节", nameEncoding: "compatibleFixed", includeId: false });
    pushAttempt({ label: "编号0 + 名称 Unicode固定44字节", patternId: 0, nameEncoding: "unicodeFixed" });
  }
  */

  return attempts;
}

function describeDeleteResultCode(result) {
  if (result === 0) {
    return "删除成功";
  }
  if (result === 1) {
    return "删除失败";
  }
  if (result === 2) {
    return "设备返回结果 2，协议未定义，当前删除请求未被接受";
  }
  return `设备返回未定义删除结果 ${result}`;
}

function sendDeletePatternRequest(session, transfer) {
  const protocolPreset = getProtocolPreset(transfer.protocolPresetId) || getSessionProtocolPreset(session);
  const attempt = transfer.attemptPlan?.[transfer.attemptIndex];
  if (!attempt) {
    return;
  }

  appendLog(
    `会话 ${session.id} 删除花型尝试 ${transfer.attemptIndex + 1}/${transfer.attemptPlan.length}: ${attempt.label} ${transfer.patternId || "-"} / ${transfer.patternName || "-"}`,
    { sessionId: session.id }
  );

  sendPacket(
    session,
    createDeletePatternCommandFrame(
      {
        patternId: attempt.patternId,
        patternName: attempt.patternName,
        nameEncoding: attempt.nameEncoding,
        addr1: attempt.addr1,
        addr2: attempt.addr2,
        includeId: attempt.includeId,
      },
      protocolPreset
    ),
    `${COMMANDS.DELETE_PATTERN_FILE.name} [${attempt.label}]`,
    protocolPreset
  );
}

function requestDeleteClientPattern(session, payload = {}) {
  if (session.transfers.pendingDelete) {
    throw new Error("上一条删除花型指令仍在处理中，请等待结果后再试");
  }

  const request = resolvePatternRequest(session.clientPatterns, payload);
  if (!request.patternId && !request.patternName) {
    throw new Error("请先填写客户端花型编号或名称");
  }

  const protocolPreset = getSessionProtocolPreset(session);
  const attemptPlan = buildDeleteAttemptPlan(session, request);
  if (attemptPlan.length === 0) {
    throw new Error("当前删除请求缺少可用的删除参数");
  }
  session.transfers.pendingDelete = {
    ...request,
    requestedAt: nowLocal(),
    protocolPresetId: protocolPreset.id,
    attemptPlan,
    attemptIndex: 0,
  };

  sendDeletePatternRequest(session, session.transfers.pendingDelete);
}

function requestClientPatternList(session) {
  const protocolPreset = getSessionProtocolPreset(session);
  session.transfers.clientPatternList = {
    frames: new Map(),
    totalFrames: null,
    protocolPresetId: protocolPreset.id,
    startedAt: nowLocal(),
    includeId: true,
    detailName: session.activeDeviceMode ? "对端全部花型列表" : "客户端花型列表",
  };

  if (session.activeDeviceMode) {
    session.activeDevice.peerPatternListRequestedAt = Date.now();
  }

  sendPacket(
    session,
    createReadPatternListFrame(protocolPreset),
    COMMANDS.READ_PATTERN_LIST.name,
    protocolPreset
  );
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
    if (patternId) {
      return true;
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
  const attempt = transfer.attemptPlan?.[transfer.attemptIndex] || null;
  if (result === 0) {
    appendLog(
      `会话 ${session.id} 删除花型成功，生效方法: ${attempt?.label || "未知"} ${transfer.patternId || "-"} / ${transfer.patternName || "-"}`,
      { sessionId: session.id }
    );
    removeClientPatternSnapshot(session, transfer.patternId, transfer.patternName);
  } else if (transfer.attemptIndex < transfer.attemptPlan.length - 1) {
    transfer.attemptIndex += 1;
    const nextAttempt = transfer.attemptPlan[transfer.attemptIndex];
    appendLog(
      `会话 ${session.id} 删除花型返回 ${describeDeleteResultCode(result)}，当前方法 ${attempt?.label || "未知"} 未生效，切换为 ${nextAttempt.label} 继续测试 ${transfer.patternId || "-"} / ${transfer.patternName || "-"}`,
      { sessionId: session.id }
    );
    sendDeletePatternRequest(session, transfer);
    return;
  }

  const detail =
    result === 0
      ? `客户端已删除花型 ${transfer.patternId || "-"} / ${transfer.patternName || "-"}，生效方法: ${attempt?.label || "未知"}`
      : `客户端删除花型失败 ${transfer.patternId || "-"} / ${transfer.patternName || "-"} (${describeDeleteResultCode(result)})，所有测试方法均未生效，已自动刷新花型列表`;
  markTransferResult(session, "delete", result, detail);
  session.transfers.pendingDelete = null;
  requestClientPatternList(session);
}

function handleClientPatternListFrame(session, frame, options = {}) {
  const protocolPreset = frame.protocol || getSessionProtocolPreset(session);
  const {
    includeId = true,
    detailName = includeId ? "客户端花型列表" : "对端全部花型列表",
  } = options;
  if (!session.transfers.clientPatternList) {
    session.transfers.clientPatternList = {
      frames: new Map(),
      totalFrames: null,
      protocolPresetId: protocolPreset.id,
      startedAt: nowLocal(),
      includeId,
      detailName,
    };
  }

  const transfer = session.transfers.clientPatternList;
  transfer.totalFrames = frame.totalFrames;
  transfer.frames.set(
    frame.frameNo,
    parsePatternListPayload(frame.payload, protocolPreset, { includeId: transfer.includeId ?? includeId })
  );

  if (transfer.totalFrames && transfer.frames.size === transfer.totalFrames) {
    const entries = [];
    for (let frameNo = 1; frameNo <= transfer.totalFrames; frameNo += 1) {
      entries.push(...(transfer.frames.get(frameNo) || []));
    }
    session.clientPatterns = entries;
    session.transfers.clientPatternList = null;
    markTransferResult(session, "list", 0, `已获取${transfer.detailName || detailName} ${entries.length} 项`);
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

function handleFrameInActiveDeviceMode(session, frame) {
  const protocolPreset = frame.protocol || getSessionProtocolPreset(session);
  ensureSessionActiveDeviceState(session);

  if (isCommand(frame, COMMANDS.REGISTER)) {
    markSessionRegistered(session, "收到注册应答");
    sendActiveDeviceSnapshot(session, protocolPreset);
    sendActiveDeviceHeartbeat(session, protocolPreset);
    return true;
  }

  if (isCommand(frame, COMMANDS.HEARTBEAT)) {
    markSessionRegistered(session, "收到心跳");
    session.lastHeartbeatAt = nowLocal();
    if (Date.now() - session.activeDevice.lastHeartbeatSentMs > 3000) {
      sendActiveDeviceHeartbeat(session, protocolPreset);
    }
    return true;
  }

  if (isCommand(frame, COMMANDS.TIME_SYNC)) {
    session.lastTimeSyncAt = nowLocal();
    const parsed = parseTimeSyncPayload(frame.payload);
    if (parsed) {
      markSessionRegistered(session, "收到时间同步数据");
      session.deviceInfo.timeSyncValue = parsed.formatted;
      return true;
    }

    sendPacket(
      session,
      createTimeSyncResponseFrame(new Date(), protocolPreset),
      COMMANDS.TIME_SYNC.name,
      protocolPreset
    );
    return true;
  }

  if (isCommand(frame, COMMANDS.REALTIME_STATUS_QUERY)) {
    sendActiveDeviceRealtimeStatus(session, protocolPreset);
    return true;
  }

  if (isCommand(frame, COMMANDS.PATTERN_INFO)) {
    markSessionRegistered(session, "收到注册后设备信息");
    session.deviceInfo.patternInfo = parsePatternInfoPayload(frame.payload, protocolPreset);
    rememberClientPattern(session, session.deviceInfo.patternInfo);
    if (session.deviceInfo.realtimeStatus) {
      session.deviceInfo.realtimeStatus.patternId = session.deviceInfo.patternInfo?.patternId ?? 0;
      session.deviceInfo.realtimeStatus.patternName = session.deviceInfo.patternInfo?.patternName ?? "";
    }
    return true;
  }

  if (isCommand(frame, COMMANDS.ALARM)) {
    markSessionRegistered(session, "收到注册后设备信息");
    session.deviceInfo.alarm = parseAlarmPayload(frame.payload, protocolPreset);
    return true;
  }

  if (isCommand(frame, COMMANDS.DEVICE_FLAG)) {
    markSessionRegistered(session, "收到注册后设备信息");
    session.deviceInfo.flag = parseDeviceFlagPayload(frame.payload);
    return true;
  }

  if (isCommand(frame, COMMANDS.DEVICE_MODEL)) {
    markSessionRegistered(session, "收到注册后设备信息");
    session.deviceInfo.model = parseDeviceModelPayload(frame.payload, protocolPreset);
    return true;
  }

  if (isCommand(frame, COMMANDS.SEWING_RANGE)) {
    markSessionRegistered(session, "收到注册后设备信息");
    session.deviceInfo.sewingRange = parseSewingRangePayload(frame.payload, protocolPreset);
    return true;
  }

  if (isCommand(frame, COMMANDS.MAX_SPEED)) {
    markSessionRegistered(session, "收到注册后设备信息");
    session.deviceInfo.maxSpeed = parseMaxSpeedPayload(frame.payload, protocolPreset);
    return true;
  }

  if (isCommand(frame, COMMANDS.PRODUCTION_COUNT)) {
    markSessionRegistered(session, "收到注册后设备信息");
    session.deviceInfo.productionCount = parseProductionCountPayload(frame.payload, protocolPreset);
    return true;
  }

  if (isCommand(frame, COMMANDS.BOTTOM_THREAD_COUNT)) {
    markSessionRegistered(session, "收到注册后设备信息");
    session.deviceInfo.bottomThreadCount = parseBottomThreadCountPayload(frame.payload, protocolPreset);
    return true;
  }

  if (isCommand(frame, COMMANDS.READ_HIGH_POINT)) {
    if (frame.payload.length >= 2) {
      markSessionRegistered(session, "收到注册后设备信息");
      session.deviceInfo.highPoint = parseUShortValuePayload(frame.payload, protocolPreset);
      return true;
    }

    sendPacket(
      session,
      createHighPointFrame(session.deviceInfo.highPoint?.value ?? 0, protocolPreset),
      COMMANDS.READ_HIGH_POINT.name,
      protocolPreset
    );
    return true;
  }

  if (isCommand(frame, COMMANDS.SET_HIGH_POINT)) {
    const parsed = parseUShortValuePayload(frame.payload, protocolPreset);
    if (parsed) {
      session.deviceInfo.highPoint = { value: parsed.value };
    }
    sendPacket(
      session,
      createSetHighPointResultFrame(0, protocolPreset),
      COMMANDS.SET_HIGH_POINT.name,
      protocolPreset
    );
    sendPacket(
      session,
      createHighPointFrame(session.deviceInfo.highPoint?.value ?? 0, protocolPreset),
      COMMANDS.READ_HIGH_POINT.name,
      protocolPreset
    );
    return true;
  }

  if (isCommand(frame, COMMANDS.READ_LOW_POINT)) {
    if (frame.payload.length >= 2) {
      markSessionRegistered(session, "收到注册后设备信息");
      session.deviceInfo.lowPoint = parseUShortValuePayload(frame.payload, protocolPreset);
      return true;
    }

    sendPacket(
      session,
      createLowPointFrame(session.deviceInfo.lowPoint?.value ?? 0, protocolPreset),
      COMMANDS.READ_LOW_POINT.name,
      protocolPreset
    );
    return true;
  }

  if (isCommand(frame, COMMANDS.SET_LOW_POINT)) {
    const parsed = parseUShortValuePayload(frame.payload, protocolPreset);
    if (parsed) {
      session.deviceInfo.lowPoint = { value: parsed.value };
    }
    sendPacket(
      session,
      createSetLowPointResultFrame(0, protocolPreset),
      COMMANDS.SET_LOW_POINT.name,
      protocolPreset
    );
    sendPacket(
      session,
      createLowPointFrame(session.deviceInfo.lowPoint?.value ?? 0, protocolPreset),
      COMMANDS.READ_LOW_POINT.name,
      protocolPreset
    );
    return true;
  }

  if (isCommand(frame, COMMANDS.SET_SPEED)) {
    const parsed = parseUShortValuePayload(frame.payload, protocolPreset);
    if (parsed) {
      session.deviceInfo.currentSpeed = { currentSpeed: parsed.value };
    }
    sendPacket(
      session,
      createSetSpeedResultFrame(0, protocolPreset),
      COMMANDS.SET_SPEED.name,
      protocolPreset
    );
    sendPacket(
      session,
      createCurrentSpeedFrame(session.deviceInfo.currentSpeed?.currentSpeed ?? 0, protocolPreset),
      COMMANDS.CURRENT_SPEED.name,
      protocolPreset
    );
    return true;
  }

  if (isCommand(frame, COMMANDS.SET_DEVICE_FLAG)) {
    session.deviceInfo.flag = frame.payload.toString("utf8");
    sendPacket(
      session,
      createDeviceFlagFrame(session.deviceInfo.flag, protocolPreset),
      COMMANDS.DEVICE_FLAG.name,
      protocolPreset
    );
    return true;
  }

  if (isCommand(frame, COMMANDS.READ_PATTERN_LIST)) {
    if (frame.payload.length > 0) {
      markSessionRegistered(session, "收到对端花型列表");
      handleClientPatternListFrame(session, frame, {
        includeId: true,
        detailName: "对端全部花型列表",
      });
      return true;
    }

    const frames = createPatternListFrames(
      COMMANDS.READ_PATTERN_LIST,
      state.serverPatterns,
      protocolPreset,
      { includeId: true }
    );
    frames.forEach((packet, index) => {
      sendPacket(
        session,
        packet,
        `${COMMANDS.READ_PATTERN_LIST.name} ${index + 1}/${frames.length}`,
        protocolPreset
      );
    });
    return true;
  }

  if (isCommand(frame, COMMANDS.REQUEST_SERVER_PATTERN_LIST)) {
    markSessionRegistered(session, "收到对端花型列表");
    handleClientPatternListFrame(session, frame, {
      includeId: false,
      detailName: "对端全部花型列表",
    });
    return true;
  }

  if (isCommand(frame, COMMANDS.DOWNLOAD_PATTERN_COMMAND)) {
    markSessionRegistered(session, "收到文件传输命令");
    handleDownloadCommandAck(session, frame);
    return true;
  }

  if (isCommand(frame, COMMANDS.COMMUNICATION_ERROR)) {
    markSessionRegistered(session, "收到文件传输命令");
    handleDownloadDataAck(session, frame);
    return true;
  }

  if (isCommand(frame, COMMANDS.DOWNLOAD_PATTERN_DATA)) {
    markSessionRegistered(session, "收到文件传输命令");
    handleDownloadResult(session, frame);
    return true;
  }

  if (isCommand(frame, COMMANDS.UPLOAD_PATTERN_COMMAND)) {
    markSessionRegistered(session, "收到文件传输命令");
    handleUploadStatus(session, frame);
    return true;
  }

  if (isCommand(frame, COMMANDS.UPLOAD_PATTERN_DATA)) {
    markSessionRegistered(session, "收到文件传输命令");
    handleUploadData(session, frame);
    return true;
  }

  if (isCommand(frame, COMMANDS.DELETE_PATTERN_FILE)) {
    markSessionRegistered(session, "收到文件传输命令");
    handleDeleteResult(session, frame);
    return true;
  }

  if (isCommand(frame, COMMANDS.TRANSFER_RESUME)) {
    markSessionRegistered(session, "收到文件传输命令");
    handleResumeRequest(session, frame);
    return true;
  }

  if (isCommand(frame, COMMANDS.PRODUCTION_DATA_OLD)) {
    markSessionRegistered(session, "收到生产数据");
    handleProductionData(session, frame, COMMANDS.PRODUCTION_DATA_OLD);
    return true;
  }

  if (isCommand(frame, COMMANDS.PRODUCTION_DATA_NEW)) {
    markSessionRegistered(session, "收到生产数据");
    handleProductionData(session, frame, COMMANDS.PRODUCTION_DATA_NEW);
    return true;
  }

  appendLog(`会话 ${session.id} 主动下位机模式暂未处理 ${commandName(frame.type, frame.code)}`, {
    sessionId: session.id,
  });
  return true;
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

  appendProtocolLog(
    `会话 ${session.id} <- ${describeFrame(frame)}${frame.recovered ? ` [兼容恢复: ${frame.recovered.message}]` : ""}`,
    {
      sessionId: session.id,
      command: commandName(frame.type, frame.code),
      raw: toHex(frame.raw),
      recovery: frame.recovered?.message || "",
    }
  );

  if (session.activeDeviceMode) {
    return handleFrameInActiveDeviceMode(session, frame);
  }

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
    rememberClientPattern(session, session.deviceInfo.patternInfo);
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
    rememberClientPattern(session, session.deviceInfo.realtimeStatus);
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
    connected: true,
    connectedAt: nowLocal(),
    disconnectedAt: null,
    disconnectReason: null,
    pendingDisconnectReason: null,
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
    activeDeviceMode: false,
    activeDevice: {
      registerRetryTimer: null,
      heartbeatTimer: null,
      postRegisterInitTimer: null,
      autoPatternListTimer: null,
      lastRegisterSentMs: 0,
      lastHeartbeatSentMs: 0,
      peerPatternListRequestedAt: 0,
      postRegisterInitSent: false,
    },
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
  if (state.activeDeviceMode) {
    setSessionActiveDeviceMode(session, true);
  }

  socket.on("data", (chunk) => {
    appendWireLog(session, "RX", chunk);
    const frames = session.parser.push(chunk);
    for (const frame of frames) {
      handleFrame(session, frame);
    }
  });

  socket.on("close", () => {
    stopSessionActiveDeviceLoops(session);
    session.connected = false;
    session.disconnectedAt = nowLocal();
    session.disconnectReason = session.pendingDisconnectReason || "连接已断开";
    session.pendingDisconnectReason = null;
    session.socket = null;
    appendLog(`会话 ${id} 已断开`, {
      sessionId: id,
      detail: session.disconnectReason,
    });
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
    state.protocolLogs = [];
    sendJson(response, 200, { ok: true });
    return;
  }

  sendJson(response, 404, { error: "not found" });
});

httpServer.listen(httpPort, bindHost, () => {
  appendLog(`Web 控制台已启动 http://${bindHost}:${httpPort}`);
  console.log(`Server UI: http://${bindHost}:${httpPort}`);
  console.log(`TCP Port : ${tcpPort}`);
  console.log(`Server Log: ${state.logFilePath}`);
});
