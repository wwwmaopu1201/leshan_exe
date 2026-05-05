#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::cmp::Ordering;
use std::collections::hash_map::DefaultHasher;
use std::env;
use std::fs;
use std::hash::{Hash, Hasher};
use std::io::{Read, Write};
use std::net::{SocketAddr, TcpStream};
use std::path::PathBuf;
use std::sync::OnceLock;
use std::time::{SystemTime, UNIX_EPOCH};
use tauri::Manager;
use tauri_plugin_dialog::DialogExt;

const STARTUP_API_HOST: &str = "47.92.226.92";
const STARTUP_API_PORT: u16 = 56;
const STARTUP_API_PATH: &str = "/api.php";
const TRIAL_DURATION_SECONDS: u64 = 7 * 24 * 60 * 60;
const ROLLBACK_LEEWAY_SECONDS: u64 = 10 * 60;
const TRIAL_POLICY_VERSION: u32 = 3;

static STARTUP_API_RESULT: OnceLock<Result<(), String>> = OnceLock::new();

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

fn network_error_status() -> TrialStatus {
    TrialStatus {
        valid: false,
        message: "网络错误，请检查网络连接".to_string(),
        expires_at: None,
        remaining_seconds: 0,
    }
}

fn decode_chunked_body(raw: &str) -> Option<String> {
    let mut body = String::new();
    let mut rest = raw;

    loop {
        let (size_line, after_size) = rest.split_once("\r\n")?;
        let size_text = size_line.split(';').next()?.trim();
        let size = usize::from_str_radix(size_text, 16).ok()?;
        if size == 0 {
            return Some(body);
        }
        if after_size.len() < size + 2 {
            return None;
        }

        body.push_str(&after_size[..size]);
        rest = after_size.get(size + 2..)?;
    }
}

fn parse_startup_api_allowed(response: &str) -> Result<bool, String> {
    let (headers, body) = response
        .split_once("\r\n\r\n")
        .ok_or_else(|| "invalid startup api response".to_string())?;
    let status_line = headers.lines().next().unwrap_or_default();
    let status_code = status_line
        .split_whitespace()
        .nth(1)
        .and_then(|value| value.parse::<u16>().ok())
        .ok_or_else(|| "invalid startup api status".to_string())?;

    if !(200..300).contains(&status_code) {
        return Err(format!("startup api returned status {status_code}"));
    }

    let decoded_body = if headers.lines().any(|line| {
        line.to_ascii_lowercase()
            .contains("transfer-encoding: chunked")
    }) {
        decode_chunked_body(body).ok_or_else(|| "invalid startup api chunked body".to_string())?
    } else {
        body.to_string()
    };
    let value = decoded_body.trim().trim_start_matches('\u{feff}');

    if let Ok(parsed) = serde_json::from_str::<bool>(value) {
        return Ok(parsed);
    }

    match value.to_ascii_lowercase().as_str() {
        "true" => Ok(true),
        "false" => Ok(false),
        _ => Err("startup api returned invalid value".to_string()),
    }
}

fn request_startup_api_allowed() -> Result<(), String> {
    let address = SocketAddr::from(([47, 92, 226, 92], STARTUP_API_PORT));
    let mut stream = TcpStream::connect_timeout(&address, std::time::Duration::from_secs(5))
        .map_err(|err| format!("failed to connect startup api: {err}"))?;
    stream
        .set_read_timeout(Some(std::time::Duration::from_secs(5)))
        .map_err(|err| format!("failed to set startup api read timeout: {err}"))?;
    stream
        .set_write_timeout(Some(std::time::Duration::from_secs(5)))
        .map_err(|err| format!("failed to set startup api write timeout: {err}"))?;

    let request = format!(
        "GET {STARTUP_API_PATH} HTTP/1.1\r\nHost: {STARTUP_API_HOST}:{STARTUP_API_PORT}\r\nUser-Agent: BoerLAN-Client\r\nAccept: application/json,text/plain,*/*\r\nConnection: close\r\n\r\n"
    );
    stream
        .write_all(request.as_bytes())
        .map_err(|err| format!("failed to write startup api request: {err}"))?;

    let mut response = String::new();
    stream
        .read_to_string(&mut response)
        .map_err(|err| format!("failed to read startup api response: {err}"))?;

    match parse_startup_api_allowed(&response) {
        Ok(true) => Ok(()),
        Ok(false) => Err("startup api disabled startup".to_string()),
        Err(err) => Err(err),
    }
}

fn ensure_startup_api_allowed() -> Result<(), String> {
    STARTUP_API_RESULT
        .get_or_init(request_startup_api_allowed)
        .clone()
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
    if let Err(err) = ensure_startup_api_allowed() {
        eprintln!("Startup network validation failed: {err}");
        return network_error_status();
    }

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

async fn prompt_save_path(
    app: &tauri::AppHandle,
    suggested_name: &str,
) -> Result<Option<PathBuf>, String> {
    let (tx, mut rx) = tauri::async_runtime::channel(1);

    app.dialog()
        .file()
        .set_title("保存导出文件")
        .set_file_name(suggested_name)
        .save_file(move |file_path| {
            let result = match file_path {
                Some(file_path) => file_path
                    .into_path()
                    .map(Some)
                    .map_err(|err| format!("failed to resolve save path: {err}")),
                None => Ok(None),
            };
            let _ = tx.try_send(result);
        });

    match rx.recv().await {
        Some(result) => result,
        None => Err("save dialog closed unexpectedly".to_string()),
    }
}

#[tauri::command]
async fn save_export_file(
    app: tauri::AppHandle,
    suggested_name: String,
    bytes: Vec<u8>,
) -> Result<Option<String>, String> {
    let path = match prompt_save_path(&app, &suggested_name).await? {
        Some(path) => path,
        None => return Ok(None),
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
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![get_trial_status, save_export_file])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
