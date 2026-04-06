const configFieldIds = ["preferredProtocolPresetId"];
const controlFieldIds = [
  "selectedSessionId",
  "setHighPointValue",
  "setLowPointValue",
  "setSpeedValue",
  "setDeviceFlagValue",
  "targetPatternId",
  "targetPatternName",
  "resumeUploadFrame",
];
const patternDraftFieldIds = [
  "serverPatternDraftId",
  "serverPatternDraftName",
  "serverPatternDraftData",
];

const storageKeys = {
  config: "leshan.server.config.v1",
  controls: "leshan.server.controls.v1",
  patternDraft: "leshan.server.patternDraft.v1",
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

  input.value = value ?? "";
}

function getFieldValue(id) {
  const input = getInput(id);
  return input ? input.value : "";
}

function applyFormValues(values, skipActive = true) {
  if (!values) {
    return;
  }

  for (const [id, value] of Object.entries(values)) {
    setFieldValue(id, value, skipActive);
  }
}

function renderSelectOptions(selectId, options, selectedValue) {
  const select = getInput(selectId);
  if (!select) {
    return;
  }

  const currentOptions = Array.from(select.options).map((item) => item.value).join("|");
  const nextOptions = options.map((item) => String(item.id)).join("|");

  if (currentOptions !== nextOptions) {
    select.innerHTML = options
      .map((item) => `<option value="${item.id}">${item.label || item.name}</option>`)
      .join("");
  }

  setFieldValue(selectId, selectedValue, false);
}

function collectConfigPayload() {
  return {
    preferredProtocolPresetId: String(getFieldValue("preferredProtocolPresetId") || ""),
  };
}

function collectControlPayload() {
  return {
    selectedSessionId: String(getFieldValue("selectedSessionId") || ""),
    setHighPointValue: Number(getFieldValue("setHighPointValue")),
    setLowPointValue: Number(getFieldValue("setLowPointValue")),
    setSpeedValue: Number(getFieldValue("setSpeedValue")),
    setDeviceFlagValue: String(getFieldValue("setDeviceFlagValue") || "").trim(),
    targetPatternId: Number(getFieldValue("targetPatternId")),
    targetPatternName: String(getFieldValue("targetPatternName") || "").trim(),
    resumeUploadFrame: Number(getFieldValue("resumeUploadFrame")),
  };
}

function collectPatternDraftPayload() {
  return {
    serverPatternDraftId: Number(getFieldValue("serverPatternDraftId")),
    serverPatternDraftName: String(getFieldValue("serverPatternDraftName") || "").trim(),
    serverPatternDraftData: String(getFieldValue("serverPatternDraftData") || ""),
  };
}

function getSelectedSessionId() {
  const selected = Number(getFieldValue("selectedSessionId"));
  return Number.isInteger(selected) && selected > 0 ? selected : null;
}

function ensureSelectedSessionId(state) {
  const currentId = getSelectedSessionId();
  const exists = state.sessions.some((item) => item.id === currentId);
  if (exists) {
    return currentId;
  }

  const fallbackId = state.sessions[0]?.id || "";
  setFieldValue("selectedSessionId", fallbackId, false);
  writeStorageJson(storageKeys.controls, collectControlPayload());
  return fallbackId || null;
}

function getSelectedSession(state) {
  const selectedSessionId = ensureSelectedSessionId(state);
  return state.sessions.find((item) => item.id === Number(selectedSessionId)) || null;
}

function formatPatternInfo(pattern) {
  return pattern ? `${pattern.patternId} / ${pattern.patternName || "-"}` : "-";
}

function formatRange(range) {
  return range ? `${range.xRange / 10} x ${range.yRange / 10} mm` : "-";
}

function formatRealtimeStatus(status) {
  return status
    ? `状态 ${status.status} / 花型 ${status.patternId} / ${status.patternName || "-"}`
    : "-";
}

function formatSewingStatus(status) {
  return status
    ? `状态 ${status.status} / 针号 ${status.needleNumber} / 停止原因 ${status.stopReason}`
    : "-";
}

function formatProductionCounts(counts) {
  return counts ? `总数 ${counts.totalCount} / 当前 ${counts.currentCount}` : "-";
}

function formatBottomThread(counts) {
  return counts ? `总量 ${counts.totalLength} / 剩余 ${counts.remainLength}` : "-";
}

function formatProductionData(data) {
  if (!data) {
    return "-";
  }

  const userText = data.userId ? ` / 用户 ${data.userId}` : "";
  return `${data.patternId} / ${data.patternName || "-"}<br />${data.startTime} -> ${data.endTime}<br />针数 ${data.startNeedle} -> ${data.endNeedle}<br />停止原因 ${data.stopReason}${userText}`;
}

function renderStatus(state) {
  const preset =
    state.protocolPresets.find((item) => item.id === state.preferredProtocolPresetId)?.name ||
    state.preferredProtocolPresetId;
  document.querySelector("#status").textContent = `状态: 在线会话 ${state.sessions.length} | 默认发送协议: ${preset} | 时区: ${state.timeZone}`;
}

function renderSessionTable(state, selectedSession) {
  const tbody = document.querySelector("#sessionsTable");

  if (!state.sessions.length) {
    tbody.innerHTML = `<tr><td colspan="4" class="muted">暂无连接会话</td></tr>`;
    return;
  }

  tbody.innerHTML = state.sessions
    .map((session) => {
      const activeClass = selectedSession?.id === session.id ? "active-row" : "";
      return `
        <tr class="${activeClass}">
          <td>
            #${session.id}<br />
            <span class="muted">${session.remoteAddress}:${session.remotePort}</span>
          </td>
          <td>
            ${session.registered ? "已注册" : "未注册"}<br />
            <span class="muted">最后消息: ${session.lastMessageAt || "-"}</span>
          </td>
          <td>${formatPatternInfo(session.deviceInfo.patternInfo)}</td>
          <td>
            <button type="button" class="ghost choose-session" data-session-id="${session.id}">选择</button>
          </td>
        </tr>
      `;
    })
    .join("");

  tbody.querySelectorAll(".choose-session").forEach((button) => {
    button.addEventListener("click", () => {
      setFieldValue("selectedSessionId", button.dataset.sessionId, false);
      writeStorageJson(storageKeys.controls, collectControlPayload());
      refresh().catch((error) => console.error(error));
    });
  });
}

function renderSessionSummary(session) {
  const container = document.querySelector("#sessionSummary");

  if (!session) {
    container.innerHTML = `<div class="muted">暂无选中会话</div>`;
    return;
  }

  const items = [
    ["会话", `#${session.id}`],
    ["远端地址", `${session.remoteAddress}:${session.remotePort}`],
    ["连接时间", session.connectedAt || "-"],
    ["注册状态", session.registered ? "已注册" : "未注册"],
    ["最后心跳", session.lastHeartbeatAt || "-"],
    ["最后时间同步", session.lastTimeSyncAt || session.deviceInfo.timeSyncValue || "-"],
    ["协议格式", session.protocolPresetName || "-"],
    ["兼容恢复", session.lastRecoveryMessage || "-"],
  ];

  container.innerHTML = items
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

function renderDeviceInfo(session) {
  const container = document.querySelector("#deviceInfoSummary");

  if (!session) {
    container.innerHTML = `<div class="muted">暂无选中会话</div>`;
    return;
  }

  const model = session.deviceInfo.model;
  const items = [
    ["设备型号", model ? model.model : "-"],
    ["设备编号", model ? model.deviceId : "-"],
    ["设备名称", model ? model.name : "-"],
    ["设备标志符", session.deviceInfo.flag || "-"],
    ["缝制范围", formatRange(session.deviceInfo.sewingRange)],
    ["最高速度", session.deviceInfo.maxSpeed?.maxSpeed ?? "-"],
    ["当前速度", session.deviceInfo.currentSpeed?.currentSpeed ?? "-"],
    ["当前花型", formatPatternInfo(session.deviceInfo.patternInfo)],
    ["高点", session.deviceInfo.highPoint?.value ?? "-"],
    ["低点", session.deviceInfo.lowPoint?.value ?? "-"],
    ["实时状态", formatRealtimeStatus(session.deviceInfo.realtimeStatus)],
    ["缝纫状态", formatSewingStatus(session.deviceInfo.sewingStatus)],
    ["生产计数", formatProductionCounts(session.deviceInfo.productionCount)],
    ["底线计数", formatBottomThread(session.deviceInfo.bottomThreadCount)],
    ["报警", session.deviceInfo.alarm?.alarmCode ?? "-"],
    ["空闲报警", session.deviceInfo.idleAlarm ? `${session.deviceInfo.idleAlarm.minutes} / ${session.deviceInfo.idleAlarm.status}` : "-"],
    ["油量提示", session.deviceInfo.oilPrompt?.prompt ?? "-"],
    ["完成针数", session.deviceInfo.needleCount?.needleCount ?? "-"],
    ["剪线完成", session.deviceInfo.lastThreadTrimAt || "-"],
    ["旧生产数据", formatProductionData(session.deviceInfo.lastProductionDataOld)],
    ["新生产数据", formatProductionData(session.deviceInfo.lastProductionDataNew)],
  ];

  container.innerHTML = items
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

function renderTransferSummary(session) {
  const container = document.querySelector("#transferSummary");

  if (!session) {
    container.innerHTML = `<div class="muted">暂无选中会话</div>`;
    return;
  }

  const summary = session.transferSummary;
  const items = [
    [
      "下载到客户端",
      summary.downloadToClient
        ? `${summary.downloadToClient.patternName}<br />下一帧 ${summary.downloadToClient.nextFrameNo}/${summary.downloadToClient.totalFrames}<br />命令确认: ${summary.downloadToClient.commandAcknowledged ? "已确认" : "等待确认"}`
        : "无",
    ],
    [
      "客户端上传到服务端",
      summary.uploadFromClient
        ? `${summary.uploadFromClient.patternId || "-"} / ${summary.uploadFromClient.patternName || "-"}<br />${summary.uploadFromClient.receivedFrames}/${summary.uploadFromClient.totalFrames || "-"}<br />状态: ${summary.uploadFromClient.status ?? "-"}`
        : "无",
    ],
    [
      "客户端花型列表",
      summary.clientPatternList
        ? `${summary.clientPatternList.receivedFrames}/${summary.clientPatternList.totalFrames || "-"}`
        : "无",
    ],
    [
      "待删除花型",
      summary.pendingDelete
        ? `${summary.pendingDelete.patternId || "-"} / ${summary.pendingDelete.patternName || "-"}`
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

  container.innerHTML = items
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

function renderClientPatterns(session) {
  const tbody = document.querySelector("#clientPatternsTable");

  if (!session || !session.clientPatterns.length) {
    tbody.innerHTML = `<tr><td colspan="3" class="muted">暂无客户端花型列表</td></tr>`;
    return;
  }

  tbody.innerHTML = session.clientPatterns
    .map(
      (pattern) => `
        <tr>
          <td>${pattern.patternId}</td>
          <td>${pattern.patternName}</td>
          <td>
            <div class="small-actions">
              <button type="button" class="ghost fill-client-pattern" data-pattern-id="${pattern.patternId}" data-pattern-name="${pattern.patternName}">填入</button>
              <button type="button" class="ghost upload-client-pattern" data-pattern-id="${pattern.patternId}" data-pattern-name="${pattern.patternName}">请求上传</button>
              <button type="button" class="warn delete-client-pattern" data-pattern-id="${pattern.patternId}" data-pattern-name="${pattern.patternName}">删除</button>
            </div>
          </td>
        </tr>
      `
    )
    .join("");

  tbody.querySelectorAll(".fill-client-pattern").forEach((button) => {
    button.addEventListener("click", () => {
      setFieldValue("targetPatternId", button.dataset.patternId, false);
      setFieldValue("targetPatternName", button.dataset.patternName, false);
      writeStorageJson(storageKeys.controls, collectControlPayload());
    });
  });

  tbody.querySelectorAll(".upload-client-pattern").forEach((button) => {
    button.addEventListener("click", () =>
      runTask(() =>
        submitAction("requestUploadPattern", {
          patternId: Number(button.dataset.patternId),
          patternName: button.dataset.patternName,
        })
      )
    );
  });

  tbody.querySelectorAll(".delete-client-pattern").forEach((button) => {
    button.addEventListener("click", () =>
      runTask(() =>
        submitAction("deleteClientPattern", {
          patternId: Number(button.dataset.patternId),
          patternName: button.dataset.patternName,
        })
      )
    );
  });
}

function renderServerPatterns(state) {
  const tbody = document.querySelector("#serverPatternsTable");

  if (!state.serverPatterns.length) {
    tbody.innerHTML = `<tr><td colspan="4" class="muted">暂无服务器花型</td></tr>`;
    return;
  }

  tbody.innerHTML = state.serverPatterns
    .map(
      (pattern) => `
        <tr>
          <td>${pattern.patternId}</td>
          <td>${pattern.patternName}</td>
          <td>${pattern.size}</td>
          <td>
            <div class="small-actions">
              <button type="button" class="ghost fill-server-pattern" data-pattern-id="${pattern.patternId}" data-pattern-name="${pattern.patternName}">填入</button>
              <button type="button" class="ghost download-server-pattern" data-pattern-id="${pattern.patternId}">下发到客户端</button>
            </div>
          </td>
        </tr>
      `
    )
    .join("");

  tbody.querySelectorAll(".fill-server-pattern").forEach((button) => {
    button.addEventListener("click", () => {
      const pattern = state.serverPatterns.find((item) => item.patternId === Number(button.dataset.patternId));
      if (!pattern) {
        return;
      }
      setFieldValue("serverPatternDraftId", pattern.patternId, false);
      setFieldValue("serverPatternDraftName", pattern.patternName, false);
      setFieldValue("serverPatternDraftData", pattern.dataText || "", false);
      writeStorageJson(storageKeys.patternDraft, collectPatternDraftPayload());
    });
  });

  tbody.querySelectorAll(".download-server-pattern").forEach((button) => {
    button.addEventListener("click", () =>
      runTask(() =>
        submitAction("downloadPattern", {
          patternId: Number(button.dataset.patternId),
        })
      )
    );
  });
}

function renderLogs(state) {
  const container = document.querySelector("#logs");

  if (!state.logs.length) {
    container.innerHTML = `<div class="muted">暂无日志</div>`;
    return;
  }

  container.innerHTML = state.logs
    .map(
      (log) => `
        <div class="log">
          <div><strong>${log.time}</strong></div>
          <div>${log.message}</div>
          ${log.detail ? `<div class="muted">${log.detail}</div>` : ""}
          ${log.recovery ? `<div class="muted">${log.recovery}</div>` : ""}
          ${log.raw ? `<div class="muted">${log.raw}</div>` : ""}
        </div>
      `
    )
    .join("");
}

function renderState(state) {
  renderStatus(state);

  renderSelectOptions(
    "preferredProtocolPresetId",
    state.protocolPresets.map((item) => ({ id: item.id, label: item.name })),
    state.preferredProtocolPresetId
  );

  const selectedSessionId = ensureSelectedSessionId(state);
  renderSelectOptions(
    "selectedSessionId",
    [{ id: "", label: "请选择会话" }, ...state.sessions.map((item) => ({ id: item.id, label: `#${item.id} ${item.remoteAddress}:${item.remotePort}` }))],
    selectedSessionId || ""
  );

  const selectedSession = getSelectedSession(state);
  renderSessionTable(state, selectedSession);
  renderSessionSummary(selectedSession);
  renderDeviceInfo(selectedSession);
  renderTransferSummary(selectedSession);
  renderClientPatterns(selectedSession);
  renderServerPatterns(state);
  renderLogs(state);

  writeStorageJson(storageKeys.config, collectConfigPayload());
  writeStorageJson(storageKeys.controls, collectControlPayload());
  writeStorageJson(storageKeys.patternDraft, collectPatternDraftPayload());
}

async function refresh() {
  const state = await fetchState();
  renderState(state);
}

function restoreCachedSettings() {
  applyFormValues(readStorageJson(storageKeys.config), false);
  applyFormValues(readStorageJson(storageKeys.controls), false);
  applyFormValues(readStorageJson(storageKeys.patternDraft), false);
}

async function syncConfigToServer() {
  await request("/api/server-config", {
    method: "POST",
    body: JSON.stringify(collectConfigPayload()),
  });
}

async function submitAction(action, payload = {}) {
  await syncConfigToServer();
  await request("/api/action", {
    method: "POST",
    body: JSON.stringify({
      action,
      payload: {
        sessionId: getSelectedSessionId(),
        ...payload,
      },
    }),
  });
}

async function saveServerPattern() {
  const draft = collectPatternDraftPayload();
  await request("/api/patterns", {
    method: "POST",
    body: JSON.stringify({
      patternId: draft.serverPatternDraftId,
      patternName: draft.serverPatternDraftName,
      dataText: draft.serverPatternDraftData,
    }),
  });
}

async function deleteServerPattern() {
  const draft = collectPatternDraftPayload();
  await request("/api/patterns/delete", {
    method: "POST",
    body: JSON.stringify({
      patternId: draft.serverPatternDraftId,
    }),
  });
}

async function clearLogs() {
  await request("/api/logs/clear", {
    method: "POST",
    body: JSON.stringify({}),
  });
}

async function runTask(task) {
  try {
    await task();
    await refresh();
  } catch (error) {
    alert(error.message);
    console.error(error);
  }
}

function bindInputPersistence(fieldIds, storageKey, onChange) {
  fieldIds.forEach((id) => {
    const input = getInput(id);
    const persist = () => {
      if (storageKey === storageKeys.config) {
        writeStorageJson(storageKey, collectConfigPayload());
      } else if (storageKey === storageKeys.controls) {
        writeStorageJson(storageKey, collectControlPayload());
      } else {
        writeStorageJson(storageKey, collectPatternDraftPayload());
      }
    };

    input.addEventListener("input", persist);
    input.addEventListener("change", persist);

    if (onChange) {
      input.addEventListener("change", onChange);
    }
  });
}

document.querySelector("#syncTimeBtn").addEventListener("click", () =>
  runTask(() => submitAction("syncTime"))
);
document.querySelector("#queryRealtimeStatusBtn").addEventListener("click", () =>
  runTask(() => submitAction("queryRealtimeStatus"))
);
document.querySelector("#readHighPointBtn").addEventListener("click", () =>
  runTask(() => submitAction("readHighPoint"))
);
document.querySelector("#readLowPointBtn").addEventListener("click", () =>
  runTask(() => submitAction("readLowPoint"))
);
document.querySelector("#readClientPatternListBtn").addEventListener("click", () =>
  runTask(() => submitAction("readClientPatternList"))
);
document.querySelector("#setHighPointBtn").addEventListener("click", () =>
  runTask(() =>
    submitAction("setHighPoint", {
      value: Number(getFieldValue("setHighPointValue")),
    })
  )
);
document.querySelector("#setLowPointBtn").addEventListener("click", () =>
  runTask(() =>
    submitAction("setLowPoint", {
      value: Number(getFieldValue("setLowPointValue")),
    })
  )
);
document.querySelector("#setSpeedBtn").addEventListener("click", () =>
  runTask(() =>
    submitAction("setSpeed", {
      value: Number(getFieldValue("setSpeedValue")),
    })
  )
);
document.querySelector("#setDeviceFlagBtn").addEventListener("click", () =>
  runTask(() =>
    submitAction("setDeviceFlag", {
      flag: getFieldValue("setDeviceFlagValue"),
    })
  )
);
document.querySelector("#requestUploadPatternBtn").addEventListener("click", () =>
  runTask(() =>
    submitAction("requestUploadPattern", {
      patternId: Number(getFieldValue("targetPatternId")),
      patternName: getFieldValue("targetPatternName"),
    })
  )
);
document.querySelector("#deleteClientPatternBtn").addEventListener("click", () =>
  runTask(() =>
    submitAction("deleteClientPattern", {
      patternId: Number(getFieldValue("targetPatternId")),
      patternName: getFieldValue("targetPatternName"),
    })
  )
);
document.querySelector("#resumeUploadBtn").addEventListener("click", () =>
  runTask(() =>
    submitAction("resumeUpload", {
      frameNo: Number(getFieldValue("resumeUploadFrame")) || 1,
    })
  )
);
document.querySelector("#saveServerPatternBtn").addEventListener("click", () =>
  runTask(saveServerPattern)
);
document.querySelector("#deleteServerPatternBtn").addEventListener("click", () =>
  runTask(deleteServerPattern)
);
document.querySelector("#clearLogsBtn").addEventListener("click", () =>
  runTask(clearLogs)
);

bindInputPersistence(configFieldIds, storageKeys.config, async () => {
  await runTask(syncConfigToServer);
});
bindInputPersistence(controlFieldIds, storageKeys.controls);
bindInputPersistence(patternDraftFieldIds, storageKeys.patternDraft);
document.querySelector("#selectedSessionId").addEventListener("change", () => {
  refresh().catch((error) => console.error(error));
});

async function init() {
  restoreCachedSettings();
  try {
    await syncConfigToServer();
    await refresh();
  } catch (error) {
    console.error("初始化服务端页面失败", error);
  }
}

init();
setInterval(refresh, 1000);
