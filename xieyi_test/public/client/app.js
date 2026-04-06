const connectionFieldIds = ["tcpHost", "tcpPort"];
const protocolFieldIds = ["protocolMode", "preferredProtocolPresetId"];
const deviceFieldIds = [
  "model",
  "deviceId",
  "name",
  "flag",
  "userId",
  "patternId",
  "patternName",
  "xRange",
  "yRange",
  "maxSpeed",
  "currentSpeed",
  "highPoint",
  "lowPoint",
  "oilPrompt",
  "alarmCode",
  "idleAlarmMinutes",
  "idleAlarmStatus",
  "productionTotal",
  "productionCurrent",
  "bottomThreadTotal",
  "bottomThreadRemain",
  "currentNeedleNumber",
  "completedNeedleCount",
  "stopReason",
  "sewingStatus",
  "realtimeStatus",
];
const patternDraftFieldIds = [
  "patternDraftId",
  "patternDraftName",
  "patternDraftData",
  "patternActivateNow",
];

const storageKeys = {
  connection: "leshan.client.connection.v2",
  protocol: "leshan.client.protocol.v2",
  device: "leshan.client.device.v2",
  patternDraft: "leshan.client.patternDraft.v2",
};

async function request(path, options = {}) {
  const response = await fetch(path, {
    headers: {
      "Content-Type": "application/json",
    },
    ...options,
  });

  const json = await response.json();
  if (!response.ok || json.ok === false) {
    throw new Error(json.error || `请求失败: ${path}`);
  }

  return json;
}

async function fetchState() {
  return request("/api/state", { cache: "no-store" });
}

function readStorageJson(key) {
  try {
    const raw = localStorage.getItem(key);
    return raw ? JSON.parse(raw) : null;
  } catch (error) {
    console.error("读取本地缓存失败", error);
    return null;
  }
}

function writeStorageJson(key, value) {
  try {
    localStorage.setItem(key, JSON.stringify(value));
  } catch (error) {
    console.error("写入本地缓存失败", error);
  }
}

function getInput(id) {
  return document.querySelector(`#${id}`);
}

function setFieldValue(id, value, skipActive = true) {
  const input = getInput(id);
  if (!input) {
    return;
  }

  if (skipActive && input === document.activeElement) {
    return;
  }

  if (input.type === "checkbox") {
    input.checked = Boolean(value);
  } else {
    input.value = value ?? "";
  }
}

function getFieldValue(id) {
  const input = getInput(id);
  if (!input) {
    return null;
  }

  if (input.type === "checkbox") {
    return input.checked;
  }

  return input.value;
}

function applyFormValues(values, skipActive = true) {
  if (!values) {
    return;
  }

  for (const [id, value] of Object.entries(values)) {
    setFieldValue(id, value, skipActive);
  }
}

function getConnectionStatusLabel(status) {
  const labels = {
    disconnected: "未连接",
    connecting: "连接中",
    connected: "已连接",
  };
  return labels[status] ?? status;
}

function renderSelectOptions(selectId, options, selectedValue) {
  const select = getInput(selectId);
  if (!select) {
    return;
  }

  const currentOptions = Array.from(select.options).map((item) => item.value).join("|");
  const nextOptions = options.map((item) => item.id).join("|");

  if (currentOptions !== nextOptions) {
    select.innerHTML = options
      .map((item) => `<option value="${item.id}">${item.label || item.name}</option>`)
      .join("");
  }

  setFieldValue(selectId, selectedValue, false);
}

function collectConnectionPayload() {
  return {
    tcpHost: String(getFieldValue("tcpHost") || "").trim(),
    tcpPort: Number(getFieldValue("tcpPort")),
  };
}

function collectProtocolPayload() {
  return {
    protocolMode: String(getFieldValue("protocolMode") || ""),
    preferredProtocolPresetId: String(getFieldValue("preferredProtocolPresetId") || ""),
  };
}

function collectDeviceInfoPayload() {
  return {
    model: Number(getFieldValue("model")),
    deviceId: Number(getFieldValue("deviceId")),
    name: String(getFieldValue("name") || "").trim(),
    flag: String(getFieldValue("flag") || "").trim(),
    userId: String(getFieldValue("userId") || "").trim(),
    patternId: Number(getFieldValue("patternId")),
    patternName: String(getFieldValue("patternName") || "").trim(),
    xRange: Number(getFieldValue("xRange")),
    yRange: Number(getFieldValue("yRange")),
    maxSpeed: Number(getFieldValue("maxSpeed")),
    currentSpeed: Number(getFieldValue("currentSpeed")),
    highPoint: Number(getFieldValue("highPoint")),
    lowPoint: Number(getFieldValue("lowPoint")),
    oilPrompt: Number(getFieldValue("oilPrompt")),
    alarmCode: Number(getFieldValue("alarmCode")),
    idleAlarmMinutes: Number(getFieldValue("idleAlarmMinutes")),
    idleAlarmStatus: Number(getFieldValue("idleAlarmStatus")),
    productionTotal: Number(getFieldValue("productionTotal")),
    productionCurrent: Number(getFieldValue("productionCurrent")),
    bottomThreadTotal: Number(getFieldValue("bottomThreadTotal")),
    bottomThreadRemain: Number(getFieldValue("bottomThreadRemain")),
    currentNeedleNumber: Number(getFieldValue("currentNeedleNumber")),
    completedNeedleCount: Number(getFieldValue("completedNeedleCount")),
    stopReason: Number(getFieldValue("stopReason")),
    sewingStatus: Number(getFieldValue("sewingStatus")),
    realtimeStatus: Number(getFieldValue("realtimeStatus")),
  };
}

function collectPatternDraftPayload() {
  return {
    patternDraftId: Number(getFieldValue("patternDraftId")),
    patternDraftName: String(getFieldValue("patternDraftName") || "").trim(),
    patternDraftData: String(getFieldValue("patternDraftData") || ""),
    patternActivateNow: Boolean(getFieldValue("patternActivateNow")),
  };
}

function renderSummary(state) {
  const summaryGrid = document.querySelector("#summaryGrid");
  const items = [
    ["连接状态", getConnectionStatusLabel(state.connectionStatus)],
    ["协议状态", state.protocolStatus],
    ["报文格式", state.protocolPresetLabel],
    ["时区", state.timeZone],
    ["最近注册", state.registerAcknowledgedAt || "-"],
    ["最近心跳", state.lastHeartbeatAckAt || "-"],
    ["最近时间同步", state.lastTimeSyncAt || state.deviceInfo.timeSyncValue || "-"],
    ["当前花型", `${state.deviceInfo.patternId} / ${state.deviceInfo.patternName}`],
  ];

  summaryGrid.innerHTML = items
    .map(
      ([label, value]) => `
        <div class="summary-item">
          <strong>${label}</strong>
          <div>${value}</div>
        </div>
      `
    )
    .join("");
}

function renderTransferSummary(state) {
  const wrap = document.querySelector("#transferSummary");
  const summary = state.transferSummary;
  const items = [
    [
      "下载到客户端",
      summary.downloadFromServer
        ? `${summary.downloadFromServer.patternName}<br />${summary.downloadFromServer.receivedFrames}/${summary.downloadFromServer.totalFrames || "-"}`
        : "无",
    ],
    [
      "上传到服务器",
      summary.uploadToServer
        ? `${summary.uploadToServer.patternName}<br />下一帧 ${summary.uploadToServer.nextFrameNo}/${summary.uploadToServer.totalFrames}`
        : "无",
    ],
    [
      "服务器花型列表接收",
      summary.serverPatternList
        ? `${summary.serverPatternList.receivedFrames}/${summary.serverPatternList.totalFrames || "-"}`
        : "无",
    ],
    [
      "最近续传请求",
      summary.lastResumeRequest
        ? `type=${summary.lastResumeRequest.transferType} frame=${summary.lastResumeRequest.frameNo}`
        : "无",
    ],
    [
      "最近传输结果",
      summary.lastResult
        ? `${summary.lastResult.direction} / ${summary.lastResult.result}<br />${summary.lastResult.detail || ""}`
        : "无",
    ],
  ];

  wrap.innerHTML = items
    .map(
      ([label, value]) => `
        <div class="summary-item">
          <strong>${label}</strong>
          <div>${value}</div>
        </div>
      `
    )
    .join("");
}

function renderLocalPatterns(state) {
  const tbody = document.querySelector("#localPatternsTable");
  if (!state.localPatterns.length) {
    tbody.innerHTML = `<tr><td colspan="4" class="muted">暂无本地花型</td></tr>`;
    return;
  }

  tbody.innerHTML = state.localPatterns
    .map(
      (pattern) => `
        <tr>
          <td>${pattern.patternId}</td>
          <td>${pattern.patternName}</td>
          <td>${pattern.size}</td>
          <td>
            <div class="small-actions">
              <button type="button" class="ghost pattern-fill" data-pattern-id="${pattern.patternId}">填入</button>
              <button type="button" class="ghost pattern-activate" data-pattern-id="${pattern.patternId}">设为当前</button>
            </div>
          </td>
        </tr>
      `
    )
    .join("");

  tbody.querySelectorAll(".pattern-fill").forEach((button) => {
    button.addEventListener("click", () => {
      const pattern = state.localPatterns.find((item) => item.patternId === Number(button.dataset.patternId));
      if (!pattern) {
        return;
      }
      setFieldValue("patternDraftId", pattern.patternId, false);
      setFieldValue("patternDraftName", pattern.patternName, false);
      writeStorageJson(storageKeys.patternDraft, collectPatternDraftPayload());
    });
  });

  tbody.querySelectorAll(".pattern-activate").forEach((button) => {
    button.addEventListener("click", async () => {
      await request("/api/action", {
        method: "POST",
        body: JSON.stringify({
          action: "activatePattern",
          payload: {
            patternId: Number(button.dataset.patternId),
          },
        }),
      });
      await refresh();
    });
  });
}

function renderServerPatterns(state) {
  const tbody = document.querySelector("#serverPatternsTable");
  if (!state.serverPatterns.length) {
    tbody.innerHTML = `<tr><td colspan="2" class="muted">暂无服务器花型数据</td></tr>`;
    return;
  }

  tbody.innerHTML = state.serverPatterns
    .map(
      (pattern, index) => `
        <tr>
          <td>${index + 1}</td>
          <td>${pattern.patternName}</td>
        </tr>
      `
    )
    .join("");
}

function renderLogs(state) {
  const logs = document.querySelector("#logs");
  if (!state.logs.length) {
    logs.innerHTML = `<div class="muted">暂无日志</div>`;
    return;
  }

  logs.innerHTML = state.logs
    .map(
      (log) => `
        <div class="log">
          <div><strong>${log.time}</strong></div>
          <div>${log.message}</div>
          ${log.raw ? `<div class="muted">${log.raw}</div>` : ""}
        </div>
      `
    )
    .join("");
}

function renderState(state) {
  document.querySelector("#status").textContent = `状态: ${getConnectionStatusLabel(state.connectionStatus)} | 协议: ${state.protocolStatus} | 报文: ${state.protocolPresetLabel} | 时区: ${state.timeZone}`;

  renderSelectOptions("protocolMode", state.protocolModes, state.protocolMode);
  renderSelectOptions(
    "preferredProtocolPresetId",
    state.protocolPresets.map((item) => ({ id: item.id, label: item.name })),
    state.preferredProtocolPresetId
  );

  applyFormValues(
    {
      tcpHost: state.tcpHost,
      tcpPort: state.tcpPort,
    },
    true
  );

  applyFormValues(
    {
      model: state.deviceInfo.model,
      deviceId: state.deviceInfo.deviceId,
      name: state.deviceInfo.name,
      flag: state.deviceInfo.flag,
      userId: state.deviceInfo.userId,
      patternId: state.deviceInfo.patternId,
      patternName: state.deviceInfo.patternName,
      xRange: state.deviceInfo.xRange,
      yRange: state.deviceInfo.yRange,
      maxSpeed: state.deviceInfo.maxSpeed,
      currentSpeed: state.deviceInfo.currentSpeed,
      highPoint: state.deviceInfo.highPoint,
      lowPoint: state.deviceInfo.lowPoint,
      oilPrompt: state.deviceInfo.oilPrompt,
      alarmCode: state.deviceInfo.alarmCode,
      idleAlarmMinutes: state.deviceInfo.idleAlarmMinutes,
      idleAlarmStatus: state.deviceInfo.idleAlarmStatus,
      productionTotal: state.deviceInfo.productionTotal,
      productionCurrent: state.deviceInfo.productionCurrent,
      bottomThreadTotal: state.deviceInfo.bottomThreadTotal,
      bottomThreadRemain: state.deviceInfo.bottomThreadRemain,
      currentNeedleNumber: state.deviceInfo.currentNeedleNumber,
      completedNeedleCount: state.deviceInfo.completedNeedleCount,
      stopReason: state.deviceInfo.stopReason,
      sewingStatus: state.deviceInfo.sewingStatus,
      realtimeStatus: state.deviceInfo.realtimeStatus,
    },
    true
  );

  renderSummary(state);
  renderTransferSummary(state);
  renderLocalPatterns(state);
  renderServerPatterns(state);
  renderLogs(state);

  writeStorageJson(storageKeys.connection, collectConnectionPayload());
  writeStorageJson(storageKeys.protocol, collectProtocolPayload());
  writeStorageJson(storageKeys.device, collectDeviceInfoPayload());
  writeStorageJson(storageKeys.patternDraft, collectPatternDraftPayload());
}

async function refresh() {
  const state = await fetchState();
  renderState(state);
}

function restoreCachedSettings() {
  applyFormValues(readStorageJson(storageKeys.connection), false);
  applyFormValues(readStorageJson(storageKeys.protocol), false);
  applyFormValues(readStorageJson(storageKeys.device), false);
  applyFormValues(readStorageJson(storageKeys.patternDraft), false);
}

async function syncCachedSettingsToServer() {
  await request("/api/connection-config", {
    method: "POST",
    body: JSON.stringify(collectConnectionPayload()),
  });
  await request("/api/protocol-config", {
    method: "POST",
    body: JSON.stringify(collectProtocolPayload()),
  });
  await request("/api/device-info", {
    method: "POST",
    body: JSON.stringify(collectDeviceInfoPayload()),
  });
}

async function connect() {
  await syncCachedSettingsToServer();
  await request("/api/connect", {
    method: "POST",
    body: JSON.stringify(collectConnectionPayload()),
  });
  await refresh();
}

async function disconnect() {
  await request("/api/disconnect", {
    method: "POST",
    body: JSON.stringify({}),
  });
  await refresh();
}

async function reportDeviceNow() {
  await request("/api/device-info", {
    method: "POST",
    body: JSON.stringify({
      ...collectDeviceInfoPayload(),
      reportNow: true,
    }),
  });
  await refresh();
}

async function runAction(action, payload = {}) {
  await syncCachedSettingsToServer();
  await request("/api/action", {
    method: "POST",
    body: JSON.stringify({ action, payload }),
  });
  await refresh();
}

async function savePattern() {
  const draft = collectPatternDraftPayload();
  await request("/api/patterns", {
    method: "POST",
    body: JSON.stringify({
      patternId: draft.patternDraftId,
      patternName: draft.patternDraftName,
      dataText: draft.patternDraftData,
      activateNow: draft.patternActivateNow,
    }),
  });
  await refresh();
}

async function deletePattern() {
  const draft = collectPatternDraftPayload();
  await request("/api/patterns/delete", {
    method: "POST",
    body: JSON.stringify({ patternId: draft.patternDraftId }),
  });
  await refresh();
}

async function activatePattern() {
  const draft = collectPatternDraftPayload();
  await runAction("activatePattern", { patternId: draft.patternDraftId });
}

async function clearLogs() {
  await request("/api/logs/clear", {
    method: "POST",
    body: JSON.stringify({}),
  });
  await refresh();
}

function bindInputPersistence(fieldIds, storageKey, onChange) {
  for (const id of fieldIds) {
    const input = getInput(id);
    input.addEventListener("input", () => {
      if (storageKey === storageKeys.connection) {
        writeStorageJson(storageKey, collectConnectionPayload());
      } else if (storageKey === storageKeys.protocol) {
        writeStorageJson(storageKey, collectProtocolPayload());
      } else if (storageKey === storageKeys.device) {
        writeStorageJson(storageKey, collectDeviceInfoPayload());
      } else if (storageKey === storageKeys.patternDraft) {
        writeStorageJson(storageKey, collectPatternDraftPayload());
      }
    });

    if (onChange) {
      input.addEventListener("change", onChange);
    }
  }
}

document.querySelector("#connectBtn").addEventListener("click", connect);
document.querySelector("#disconnectBtn").addEventListener("click", disconnect);
document.querySelector("#reportDeviceBtn").addEventListener("click", reportDeviceNow);
document.querySelector("#savePatternBtn").addEventListener("click", savePattern);
document.querySelector("#deletePatternBtn").addEventListener("click", deletePattern);
document.querySelector("#activatePatternBtn").addEventListener("click", activatePattern);
document.querySelector("#clearLogsBtn").addEventListener("click", clearLogs);
document.querySelector("#resumeUploadBtn").addEventListener("click", () =>
  runAction("resumeUpload", { frameNo: Number(getFieldValue("resumeUploadFrame")) || 1 })
);
document.querySelector("#resumeDownloadBtn").addEventListener("click", () =>
  runAction("resumeDownload", { frameNo: Number(getFieldValue("resumeDownloadFrame")) || 1 })
);

document.querySelectorAll("[data-action]").forEach((button) => {
  button.addEventListener("click", () => {
    runAction(button.dataset.action);
  });
});

bindInputPersistence(connectionFieldIds, storageKeys.connection, async () => {
  await request("/api/connection-config", {
    method: "POST",
    body: JSON.stringify(collectConnectionPayload()),
  });
});

bindInputPersistence(protocolFieldIds, storageKeys.protocol, async () => {
  await request("/api/protocol-config", {
    method: "POST",
    body: JSON.stringify(collectProtocolPayload()),
  });
  await refresh();
});

bindInputPersistence(deviceFieldIds, storageKeys.device, async () => {
  await request("/api/device-info", {
    method: "POST",
    body: JSON.stringify(collectDeviceInfoPayload()),
  });
});

bindInputPersistence(patternDraftFieldIds, storageKeys.patternDraft);

async function init() {
  restoreCachedSettings();
  try {
    await syncCachedSettingsToServer();
    await refresh();
  } catch (error) {
    console.error("初始化客户端页面失败", error);
  }
}

init();
setInterval(refresh, 1000);
