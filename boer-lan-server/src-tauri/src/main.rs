#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::env;
use std::fs;
#[cfg(target_os = "windows")]
use std::os::windows::process::CommandExt;
use std::path::PathBuf;
use std::process::{Child, Command};
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};
use tauri::Manager;

struct AppState {
    backend_process: Mutex<Option<Child>>,
    port_file: PathBuf,
}

#[derive(Debug, Deserialize)]
struct TrialState {
    #[serde(rename = "firstSeenAt")]
    first_seen_at: u64,
    #[serde(rename = "lastSeenAt")]
    last_seen_at: u64,
}

#[derive(Debug, Serialize)]
struct TrialStatus {
    valid: bool,
    message: String,
    expires_at: Option<u64>,
    remaining_seconds: u64,
}

#[cfg(target_os = "windows")]
const CREATE_NO_WINDOW: u32 = 0x08000000;
const TRIAL_DURATION_SECONDS: u64 = 24 * 60 * 60;
const ROLLBACK_LEEWAY_SECONDS: u64 = 10 * 60;

fn backend_binary_name() -> &'static str {
    #[cfg(target_os = "windows")]
    {
        "backend-server.exe"
    }

    #[cfg(not(target_os = "windows"))]
    {
        "backend-server"
    }
}

fn parse_backend_port(raw: &str) -> Result<u16, String> {
    raw.trim()
        .parse::<u16>()
        .map_err(|err| format!("invalid backend port: {err}"))
        .and_then(|port| {
            if port == 0 {
                Err("invalid backend port: 0".to_string())
            } else {
                Ok(port)
            }
        })
}

fn resolve_resource_dir(resource_dir: Option<PathBuf>) -> Result<PathBuf, String> {
    let backend_name = backend_binary_name();
    let mut candidates = Vec::new();

    if let Some(dir) = resource_dir.clone() {
        candidates.push(dir);
    }

    if let Ok(exe_path) = std::env::current_exe() {
        if let Some(exe_dir) = exe_path.parent() {
            candidates.push(exe_dir.to_path_buf());
            candidates.push(exe_dir.join("resources"));
        }
    }

    for dir in candidates {
        if dir.join(backend_name).exists() {
            return Ok(dir);
        }
    }

    resource_dir.ok_or_else(|| format!("failed to locate {backend_name}"))
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

fn trial_state_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    app.path()
        .app_data_dir()
        .map(|dir| dir.join("server-trial-state.json"))
        .map_err(|err| format!("failed to resolve server trial path: {err}"))
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

    if !state_path.exists() {
        return TrialStatus {
            valid: true,
            message: "试用授权初始化中".to_string(),
            expires_at: Some(now + TRIAL_DURATION_SECONDS),
            remaining_seconds: TRIAL_DURATION_SECONDS,
        };
    }

    let state = match fs::read_to_string(&state_path)
        .map_err(|err| format!("failed to read server trial state: {err}"))
        .and_then(|content| {
            serde_json::from_str::<TrialState>(&content)
                .map_err(|err| format!("failed to parse server trial state: {err}"))
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
    };

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

    let remaining_seconds = expires_at.saturating_sub(now);
    TrialStatus {
        valid: true,
        message: format!("试用剩余 {}", format_remaining_seconds(remaining_seconds)),
        expires_at: Some(expires_at),
        remaining_seconds,
    }
}

#[tauri::command]
fn get_backend_port(state: tauri::State<AppState>) -> Result<u16, String> {
    let raw = fs::read_to_string(&state.port_file)
        .map_err(|err| format!("failed to read backend port file: {err}"))?;

    parse_backend_port(&raw)
}

#[tauri::command]
fn get_trial_status(app: tauri::AppHandle) -> TrialStatus {
    inspect_trial_status(&app)
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![get_backend_port, get_trial_status])
        .setup(|app| {
            let data_dir = app
                .path()
                .app_data_dir()
                .expect("failed to get app data dir");
            let port_file = data_dir.join("backend-port.txt");
            let app_state = AppState {
                backend_process: Mutex::new(None),
                port_file: port_file.clone(),
            };

            #[cfg(not(debug_assertions))]
            {
                let trial_status = inspect_trial_status(&app.handle());
                if trial_status.valid {
                    let resource_dir = resolve_resource_dir(app.path().resource_dir().ok())
                        .map_err(|err| -> Box<dyn std::error::Error> { err.into() })?;

                    let backend_path = resource_dir.join(backend_binary_name());
                    let config_dir = resource_dir.join("config");

                    std::fs::create_dir_all(&data_dir).expect("failed to create data directory");

                    let mut cmd = Command::new(&backend_path);
                    cmd.current_dir(&resource_dir)
                        .env("CONFIG_PATH", config_dir.join("config.yaml"))
                        .env("DATA_DIR", &data_dir)
                        .env("PORT_FILE", &port_file);

                    #[cfg(target_os = "windows")]
                    cmd.creation_flags(CREATE_NO_WINDOW);

                    match cmd.spawn() {
                        Ok(child) => {
                            println!("Backend started with PID: {}", child.id());
                            *app_state.backend_process.lock().unwrap() = Some(child);
                        }
                        Err(e) => {
                            eprintln!("Failed to start backend: {}", e);
                            return Err(Box::new(e).into());
                        }
                    }
                } else {
                    eprintln!(
                        "Trial check blocked backend startup: {}",
                        trial_status.message
                    );
                }
            }

            #[cfg(debug_assertions)]
            {
                println!(
                    "Development mode: Backend should be started manually via 'npm run dev:all'"
                );
            }

            app.manage(app_state);
            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { .. } = event {
                let app_state: tauri::State<AppState> = window.state();
                if let Some(mut child) = app_state.backend_process.lock().unwrap().take() {
                    let _ = child.kill();
                    println!("Backend stopped");
                };
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
