#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::cmp::Ordering;
use std::collections::hash_map::DefaultHasher;
use std::env;
use std::fs;
use std::hash::{Hash, Hasher};
use std::path::PathBuf;
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};
use tauri::Manager;

const TRIAL_DURATION_SECONDS: u64 = 30 * 24 * 60 * 60;
const ROLLBACK_LEEWAY_SECONDS: u64 = 10 * 60;
const TRIAL_POLICY_VERSION: u32 = 2;

#[derive(Debug, Serialize, Deserialize)]
struct TrialState {
    #[serde(rename = "machineHash")]
    machine_hash: String,
    #[serde(rename = "appVersion", default)]
    app_version: Option<String>,
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

fn is_trial_bypass_enabled() -> bool {
    cfg!(debug_assertions)
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

fn normalize_version(raw: &str) -> String {
    raw.trim().trim_start_matches(['v', 'V']).to_string()
}

fn parse_version_parts(raw: &str) -> Vec<u32> {
    let normalized = normalize_version(raw);
    if normalized.is_empty() {
        return Vec::new();
    }

    normalized
        .split('.')
        .map(|part| part.parse::<u32>().unwrap_or(0))
        .collect()
}

fn compare_versions(left: &str, right: &str) -> Ordering {
    let left_parts = parse_version_parts(left);
    let right_parts = parse_version_parts(right);
    let max_len = left_parts.len().max(right_parts.len());

    for index in 0..max_len {
        let left_part = *left_parts.get(index).unwrap_or(&0);
        let right_part = *right_parts.get(index).unwrap_or(&0);
        match left_part.cmp(&right_part) {
            Ordering::Equal => continue,
            ordering => return ordering,
        }
    }

    Ordering::Equal
}

fn current_app_version(app: &tauri::AppHandle) -> String {
    normalize_version(&app.package_info().version.to_string())
}

fn trial_state_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    app.path()
        .app_data_dir()
        .map(|dir| dir.join("client-trial-state.json"))
        .map_err(|err| format!("failed to resolve client trial path: {err}"))
}

fn reset_trial_state(state: &mut TrialState, machine_hash: &str, app_version: &str, now: u64) {
    state.machine_hash = machine_hash.to_string();
    state.app_version = Some(normalize_version(app_version));
    state.first_seen_at = now;
    state.last_seen_at = now;
    state.launch_count = 0;
    state.policy_version = TRIAL_POLICY_VERSION;
}

fn should_reset_trial_state(state: &TrialState, current_app_version: &str) -> bool {
    state.policy_version < TRIAL_POLICY_VERSION
        || compare_versions(
            current_app_version,
            state.app_version.as_deref().unwrap_or_default(),
        ) == Ordering::Greater
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
    if is_trial_bypass_enabled() {
        return TrialStatus {
            valid: true,
            message: "开发模式已绕过试用校验".to_string(),
            expires_at: None,
            remaining_seconds: u64::MAX,
        };
    }

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
    let current_app_version = current_app_version(app);

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
            app_version: Some(current_app_version.clone()),
            first_seen_at: now,
            last_seen_at: now,
            launch_count: 0,
            policy_version: TRIAL_POLICY_VERSION,
        }
    };

    if !state.machine_hash.is_empty() && state.machine_hash != current_machine_hash {
        return TrialStatus {
            valid: false,
            message: "试用授权已绑定到其他设备，无法继续使用".to_string(),
            expires_at: Some(state.first_seen_at + TRIAL_DURATION_SECONDS),
            remaining_seconds: 0,
        };
    }

    if should_reset_trial_state(&state, &current_app_version) {
        reset_trial_state(&mut state, &current_machine_hash, &current_app_version, now);
    } else if compare_versions(
        state.app_version.as_deref().unwrap_or_default(),
        &current_app_version,
    ) == Ordering::Equal
        && state.app_version.as_deref() != Some(current_app_version.as_str())
    {
        state.app_version = Some(current_app_version.clone());
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

#[cfg(not(target_os = "macos"))]
fn fallback_export_path(app: &tauri::AppHandle, suggested_name: &str) -> Result<PathBuf, String> {
    app.path()
        .download_dir()
        .or_else(|_| app.path().desktop_dir())
        .or_else(|_| app.path().document_dir())
        .map(|dir| dir.join(suggested_name))
        .map_err(|err| format!("failed to resolve export path: {err}"))
}

#[cfg(target_os = "macos")]
fn prompt_save_path(suggested_name: &str) -> Result<Option<PathBuf>, String> {
    let escaped_name = suggested_name.replace('\\', "\\\\").replace('"', "\\\"");
    let output = Command::new("osascript")
        .arg("-e")
        .arg(format!(
            "set chosenFile to choose file name with prompt \"保存导出文件\" default name \"{}\"",
            escaped_name
        ))
        .arg("-e")
        .arg("POSIX path of chosenFile")
        .output()
        .map_err(|err| format!("failed to show save dialog: {err}"))?;

    if output.status.success() {
        let path = String::from_utf8_lossy(&output.stdout).trim().to_string();
        if path.is_empty() {
            return Ok(None);
        }
        return Ok(Some(PathBuf::from(path)));
    }

    let stderr = String::from_utf8_lossy(&output.stderr);
    if stderr.contains("-128") {
        return Ok(None);
    }

    Err(format!("save dialog failed: {}", stderr.trim()))
}

#[cfg(not(target_os = "macos"))]
fn prompt_save_path(_suggested_name: &str) -> Result<Option<PathBuf>, String> {
    Ok(None)
}

#[tauri::command]
fn save_export_file(
    app: tauri::AppHandle,
    suggested_name: String,
    bytes: Vec<u8>,
) -> Result<Option<String>, String> {
    #[cfg(target_os = "macos")]
    let _ = &app;

    #[cfg(target_os = "macos")]
    let path = match prompt_save_path(&suggested_name)? {
        Some(path) => path,
        None => return Ok(None),
    };

    #[cfg(not(target_os = "macos"))]
    let path = match prompt_save_path(&suggested_name)? {
        Some(path) => path,
        None => fallback_export_path(&app, &suggested_name)?,
    };

    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .map_err(|err| format!("failed to create export directory: {err}"))?;
    }
    fs::write(&path, bytes).map_err(|err| format!("failed to write export file: {err}"))?;

    Ok(Some(path.to_string_lossy().to_string()))
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![get_trial_status, save_export_file])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
