#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::process::{Child, Command};
use std::sync::Mutex;
use tauri::Manager;

struct AppState {
    backend_process: Mutex<Option<Child>>,
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
            let app_state = AppState {
                backend_process: Mutex::new(None),
            };

            // 只在生产模式（非 debug 模式）下启动 Go 后端
            // 开发模式下通过 npm run dev:all 手动启动
            #[cfg(not(debug_assertions))]
            {
                // 获取资源目录和数据目录
                let resource_dir = app.path().resource_dir().expect("failed to get resource dir");
                let data_dir = app.path().app_data_dir().expect("failed to get app data dir");

                // Go 后端可执行文件路径
                #[cfg(target_os = "windows")]
                let backend_path = resource_dir.join("backend-server.exe");
                #[cfg(not(target_os = "windows"))]
                let backend_path = resource_dir.join("backend-server");

                // 配置文件目录
                let config_dir = resource_dir.join("config");

                // 确保数据目录存在
                std::fs::create_dir_all(&data_dir).expect("failed to create data directory");

                // 启动 Go 后端
                let mut cmd = Command::new(&backend_path);
                cmd.current_dir(&resource_dir)
                    .env("CONFIG_PATH", config_dir.join("config.yaml"))
                    .env("DATA_DIR", &data_dir);

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
