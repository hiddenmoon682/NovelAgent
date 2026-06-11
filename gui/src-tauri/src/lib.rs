/// NovelAgent Tauri — 库入口：Commands + Sidecar 管理 + 桌面 run()

use std::fs;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use tauri::{Emitter, Manager};
use tauri_plugin_shell::ShellExt;
use tokio::time::{sleep, Duration};

/// 后端就绪状态（全局）
static BACKEND_READY: AtomicBool = AtomicBool::new(false);

// ── 配置路径 ──

/// 获取配置文件目录：%APPDATA%/novelagent/
fn config_dir() -> PathBuf {
    let appdata = std::env::var("APPDATA")
        .or_else(|_| std::env::var("HOME"))
        .unwrap_or_else(|_| ".".to_string());
    PathBuf::from(appdata).join("novelagent")
}

/// 上次项目路径记录文件
fn last_project_file() -> PathBuf {
    config_dir().join("last_project.txt")
}

// ── 模块：Tauri Commands ──

mod commands {
    use super::BACKEND_READY;
    use std::sync::atomic::Ordering;

    /// 获取后端端口号
    #[tauri::command]
    pub fn get_backend_port() -> u16 {
        18899
    }

    /// 获取后端运行状态
    #[tauri::command]
    pub fn get_backend_status() -> &'static str {
        if BACKEND_READY.load(Ordering::Acquire) {
            "running"
        } else {
            "starting"
        }
    }
}

fn set_backend_ready() {
    BACKEND_READY.store(true, Ordering::Release);
}

// ── 项目路径解析 ──

/// 解析项目路径，优先级：
/// 1. 命令行参数（novelagent-gui.exe "D:\my-novel"）
/// 2. 上次使用的路径（%APPDATA%/novelagent/last_project.txt）
/// 3. 弹出文件夹选择对话框
fn resolve_project_path() -> Option<String> {
    // 1. 命令行参数
    let args: Vec<String> = std::env::args().collect();
    if args.len() >= 3 {
        let arg_path = &args[2];
        let p = PathBuf::from(arg_path);
        if p.exists() && p.is_dir() {
            eprintln!("[NovelAgent] 使用命令行项目路径: {}", arg_path);
            return Some(arg_path.clone());
        }
        eprintln!("[NovelAgent] 命令行路径不存在: {}", arg_path);
    }

    // 2. 上次使用的路径
    let last_file = last_project_file();
    if last_file.exists() {
        if let Ok(content) = fs::read_to_string(&last_file) {
            let trimmed = content.trim().to_string();
            if !trimmed.is_empty() {
                let p = PathBuf::from(&trimmed);
                if p.exists() && p.is_dir() {
                    eprintln!("[NovelAgent] 使用上次项目路径: {}", trimmed);
                    return Some(trimmed);
                }
                eprintln!("[NovelAgent] 上次路径已不存在: {}", trimmed);
            }
        }
    }

    // 3. 弹出文件夹选择对话框
    eprintln!("[NovelAgent] 弹出项目选择对话框...");
    if let Some(path) = rfd::FileDialog::new()
        .set_title("选择 NovelAgent 项目目录")
        .pick_folder()
    {
        let path_str = path.to_string_lossy().to_string();
        eprintln!("[NovelAgent] 用户选择项目路径: {}", path_str);
        // 保存到配置文件
        save_last_project(&path_str);
        return Some(path_str);
    }

    eprintln!("[NovelAgent] 用户取消了项目选择");
    None
}

/// 保存上次项目路径
fn save_last_project(path: &str) {
    let dir = config_dir();
    if let Err(e) = fs::create_dir_all(&dir) {
        eprintln!("[NovelAgent] 无法创建配置目录: {}", e);
        return;
    }
    if let Err(e) = fs::write(last_project_file(), path) {
        eprintln!("[NovelAgent] 无法保存项目路径: {}", e);
    } else {
        eprintln!("[NovelAgent] 已保存项目路径: {}", path);
    }
}

// ── 后端健康检查 ──

async fn wait_for_backend(port: u16, max_retries: u32) -> Result<(), String> {
    let url = format!("http://localhost:{}/api/health", port);
    let client = reqwest::Client::new();

    for i in 0..max_retries {
        match client.get(&url).send().await {
            Ok(resp) if resp.status().is_success() => {
                eprintln!("[NovelAgent] 后端就绪 (尝试 {}/{})", i + 1, max_retries);
                return Ok(());
            }
            Ok(resp) => {
                eprintln!("[NovelAgent] 后端返回 {} (尝试 {}/{})", resp.status(), i + 1, max_retries);
            }
            Err(e) => {
                eprintln!("[NovelAgent] 等待后端... (尝试 {}/{})", i + 1, max_retries);
                if i >= 2 {
                    eprintln!("[NovelAgent]   错误详情: {}", e);
                }
            }
        }
        sleep(Duration::from_millis(500)).await;
    }
    Err(format!("后端在 {} 次尝试后仍未就绪", max_retries))
}

// ── 应用入口 ──

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    // 在 Tauri 窗口创建前解析项目路径（会弹出对话框）
    let project_path = resolve_project_path().unwrap_or_else(|| {
        eprintln!("[NovelAgent] 未选择项目，使用默认路径");
        // 最后兜底：尝试当前目录下的 test-novel
        let default = std::env::current_dir()
            .unwrap_or_else(|_| PathBuf::from("."))
            .join("test-novel")
            .to_string_lossy()
            .to_string();
        save_last_project(&default);
        default
    });

    eprintln!("[NovelAgent] 最终项目路径: {}", project_path);

    let project_path_clone = project_path.clone();

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .setup(move |app| {
            let shell = app.shell();

            eprintln!("[NovelAgent] 启动后端: project={}, port=18899", project_path_clone);

            match shell
                .sidecar("novelagent")
                .unwrap()
                .args(["backend", "-p", &project_path_clone, "--port", "18899"])
                .spawn()
            {
                Ok((mut _rx, child)) => {
                    eprintln!("[NovelAgent] Sidecar 已启动 (pid={})", child.pid());
                    app.manage(std::sync::Mutex::new(Some(child)));

                    let handle = app.handle().clone();
                    tauri::async_runtime::spawn(async move {
                        match wait_for_backend(18899, 20).await {
                            Ok(()) => {
                                eprintln!("[NovelAgent] 后端连接成功！");
                                set_backend_ready();
                                let _ = handle.emit("backend-ready", 18899);
                            }
                            Err(e) => {
                                eprintln!("[NovelAgent] 后端启动失败: {}", e);
                                let _ = handle.emit("backend-error", e);
                            }
                        }
                    });
                }
                Err(e) => {
                    eprintln!("[NovelAgent] Sidecar 启动失败: {}", e);
                    let _ = app.handle().emit("backend-error", e.to_string());
                }
            }

            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::Destroyed = event {
                let state = window.state::<std::sync::Mutex<Option<tauri_plugin_shell::process::CommandChild>>>();
                let mut guard = state.lock().unwrap();
                if let Some(child) = guard.take() {
                    eprintln!("[NovelAgent] 终止 sidecar (pid={})", child.pid());
                    let _ = child.kill();
                }
                drop(guard);
            }
        })
        .invoke_handler(tauri::generate_handler![
            commands::get_backend_port,
            commands::get_backend_status,
        ])
        .run(tauri::generate_context!())
        .expect("启动 NovelAgent 时发生错误");
}
