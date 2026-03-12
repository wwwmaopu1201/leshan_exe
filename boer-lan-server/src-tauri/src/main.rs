#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::fs;
use std::path::PathBuf;
use std::process::{Child, Command};
use std::sync::Mutex;
use tauri::Manager;

struct AppState {
    backend_process: Mutex<Option<Child>>,
    port_file: PathBuf,
}

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

#[tauri::command]
fn get_backend_port(state: tauri::State<AppState>) -> Result<u16, String> {
    let raw = fs::read_to_string(&state.port_file)
        .map_err(|err| format!("failed to read backend port file: {err}"))?;

    parse_backend_port(&raw)
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .invoke_handler(tauri::generate_handler![get_backend_port])
        .setup(|app| {
            let data_dir = app.path().app_data_dir().expect("failed to get app data dir");
            let port_file = data_dir.join("backend-port.txt");
            let app_state = AppState {
                backend_process: Mutex::new(None),
                port_file: port_file.clone(),
            };

            // 只在生产模式（非 debug 模式）下启动 Go 后端
            // 开发模式下通过 npm run dev:all 手动启动
            #[cfg(not(debug_assertions))]
            {
                // 获取资源目录和数据目录
                let resource_dir = resolve_resource_dir(app.path().resource_dir().ok())
                    .map_err(|err| -> Box<dyn std::error::Error> { err.into() })?;

                // Go 后端可执行文件路径
                let backend_path = resource_dir.join(backend_binary_name());

                // 配置文件目录
                let config_dir = resource_dir.join("config");

                // 确保数据目录存在
                std::fs::create_dir_all(&data_dir).expect("failed to create data directory");

                // 启动 Go 后端
                let mut cmd = Command::new(&backend_path);
                cmd.current_dir(&resource_dir)
                    .env("CONFIG_PATH", config_dir.join("config.yaml"))
                    .env("DATA_DIR", &data_dir)
                    .env("PORT_FILE", &port_file);

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
            }

            #[cfg(debug_assertions)]
            {
                println!("Development mode: Backend should be started manually via 'npm run dev:all'");
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
