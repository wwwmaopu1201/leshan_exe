#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::collections::hash_map::DefaultHasher;
use std::env;
use std::fs;
use std::hash::{Hash, Hasher};
use std::path::PathBuf;
use std::time::{SystemTime, UNIX_EPOCH};
use tauri::Manager;

const TRIAL_DURATION_SECONDS: u64 = 3 * 24 * 60 * 60;
const ROLLBACK_LEEWAY_SECONDS: u64 = 10 * 60;
const TRIAL_POLICY_VERSION: u32 = 2;

#[derive(Debug, Serialize, Deserialize)]
struct TrialState {
    #[serde(rename = "machineHash")]
    machine_hash: String,
    #[serde(rename = "firstSeenAt")]
    first_seen_at: u64,
    #[serde(rename = "lastSeenAt")]
    last_seen_at: u64,
    #[serde(rename = "launchCount")]
    launch_count: u64,
    #[serde(rename = "policyVersion", default)]
    policy_version: u32,
}

#[derive(Debug, Serialize)]
struct TrialStatus {
    valid: bool,
    message: String,
    expires_at: Option<u64>,
    remaining_seconds: u64,
}

fn now_seconds() -> Result<u64, String> {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .map_err(|err| format!("failed to resolve current time: {err}"))
}

fn format_remaining_seconds(remaining_seconds: u64) -> String {
    if remaining_seconds == 0 {
        return "不足 1 分钟".to_string();
    }

    const DAY_SECONDS: u64 = 24 * 60 * 60;
    const HOUR_SECONDS: u64 = 60 * 60;

    if remaining_seconds >= DAY_SECONDS {
        return format!("{} 天", (remaining_seconds + DAY_SECONDS - 1) / DAY_SECONDS);
    }

    if remaining_seconds >= HOUR_SECONDS {
        return format!(
            "{} 小时",
            (remaining_seconds + HOUR_SECONDS - 1) / HOUR_SECONDS
        );
    }

    format!("{} 分钟", ((remaining_seconds + 59) / 60).max(1))
}

fn machine_hash() -> String {
    let hostname = env::var("COMPUTERNAME")
        .or_else(|_| env::var("HOSTNAME"))
        .unwrap_or_default();
    let username = env::var("USERNAME")
        .or_else(|_| env::var("USER"))
        .unwrap_or_default();
    let home_dir = env::var("USERPROFILE")
        .or_else(|_| env::var("HOME"))
        .unwrap_or_default();
    let seed = format!("boer-lan-client|{}|{}|{}", hostname, username, home_dir);
    let mut hasher = DefaultHasher::new();
    seed.hash(&mut hasher);
    format!("{:016x}", hasher.finish())
}

fn trial_state_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    app.path()
        .app_data_dir()
        .map(|dir| dir.join("client-trial-state.json"))
        .map_err(|err| format!("failed to resolve client trial path: {err}"))
}

fn write_state(path: &PathBuf, state: &TrialState) -> Result<(), String> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("failed to create client trial dir: {err}"))?;
    }
    let content = serde_json::to_string(state)
        .map_err(|err| format!("failed to encode client trial state: {err}"))?;
    fs::write(path, content).map_err(|err| format!("failed to write client trial state: {err}"))
}

fn inspect_trial_status(app: &tauri::AppHandle) -> TrialStatus {
    let state_path = match trial_state_path(app) {
        Ok(path) => path,
        Err(err) => {
            return TrialStatus {
                valid: false,
                message: format!("试用校验失败：{err}"),
                expires_at: None,
                remaining_seconds: 0,
            }
        }
    };

    let now = match now_seconds() {
        Ok(value) => value,
        Err(err) => {
            return TrialStatus {
                valid: false,
                message: format!("试用校验失败：{err}"),
                expires_at: None,
                remaining_seconds: 0,
            }
        }
    };

    let current_machine_hash = machine_hash();

    let mut state = if state_path.exists() {
        match fs::read_to_string(&state_path)
            .map_err(|err| format!("failed to read client trial state: {err}"))
            .and_then(|content| {
                serde_json::from_str::<TrialState>(&content)
                    .map_err(|err| format!("failed to parse client trial state: {err}"))
            }) {
            Ok(state) => state,
            Err(err) => {
                return TrialStatus {
                    valid: false,
                    message: format!("试用状态损坏：{err}"),
                    expires_at: None,
                    remaining_seconds: 0,
                }
            }
        }
    } else {
        TrialState {
            machine_hash: current_machine_hash.clone(),
            first_seen_at: now,
            last_seen_at: now,
            launch_count: 0,
            policy_version: TRIAL_POLICY_VERSION,
        }
    };

    if state.machine_hash != current_machine_hash {
        return TrialStatus {
            valid: false,
            message: "试用授权已绑定到其他设备，无法继续使用".to_string(),
            expires_at: Some(state.first_seen_at + TRIAL_DURATION_SECONDS),
            remaining_seconds: 0,
        };
    }

    if state.policy_version < TRIAL_POLICY_VERSION {
        state.first_seen_at = now;
        state.last_seen_at = now;
        state.launch_count = 0;
        state.policy_version = TRIAL_POLICY_VERSION;
    }

    if now + ROLLBACK_LEEWAY_SECONDS < state.last_seen_at {
        return TrialStatus {
            valid: false,
            message: "检测到系统时间被回拨，试用已失效".to_string(),
            expires_at: Some(state.first_seen_at + TRIAL_DURATION_SECONDS),
            remaining_seconds: 0,
        };
    }

    let expires_at = state.first_seen_at + TRIAL_DURATION_SECONDS;
    if now >= expires_at {
        return TrialStatus {
            valid: false,
            message: "试用已过期，请联系供应商".to_string(),
            expires_at: Some(expires_at),
            remaining_seconds: 0,
        };
    }

    state.last_seen_at = now;
    state.launch_count += 1;
    if let Err(err) = write_state(&state_path, &state) {
        return TrialStatus {
            valid: false,
            message: format!("试用状态写入失败：{err}"),
            expires_at: Some(expires_at),
            remaining_seconds: 0,
        };
    }

    let remaining_seconds = expires_at.saturating_sub(now);
    TrialStatus {
        valid: true,
        message: format!("试用剩余 {}", format_remaining_seconds(remaining_seconds)),
        expires_at: Some(expires_at),
        remaining_seconds,
    }
}

#[tauri::command]
fn get_trial_status(app: tauri::AppHandle) -> TrialStatus {
    inspect_trial_status(&app)
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![get_trial_status])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
