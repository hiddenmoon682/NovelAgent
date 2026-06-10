/// 后端进程管理 — spawn + 检测 + 生命周期控制。

import { spawn, ChildProcess } from "child_process";
import { existsSync, readFileSync } from "fs";
import http from "http";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

const DEFAULT_PORT = 18899;
const PID_FILE = ".novelagent/backend.pid";

/// 检查后端是否已在运行。
export function isBackendRunning(projectPath: string): boolean {
  try {
    const portFile = path.join(projectPath, ".novelagent", "port");
    if (!existsSync(portFile)) return false;
    const port = parseInt(readFileSync(portFile, "utf-8").trim());
    // 尝试 HTTP 请求验证
    return new Promise<boolean>((resolve) => {
      const req = http.get(
        `http://localhost:${port}/api/health`,
        (res: any) => {
          resolve(res.statusCode === 200);
        }
      );
      req.on("error", () => resolve(false));
      req.setTimeout(1000, () => {
        req.destroy();
        resolve(false);
      });
    }) as any;
  } catch {
    return false;
  }
}

/// 获取后端端口号。
export function getBackendPort(projectPath: string): number {
  try {
    const portFile = path.join(projectPath, ".novelagent", "port");
    if (existsSync(portFile)) {
      return parseInt(readFileSync(portFile, "utf-8").trim());
    }
  } catch {}
  return DEFAULT_PORT;
}

/// 启动后端进程。
export function spawnBackend(
  projectPath: string,
  port: number = DEFAULT_PORT
): ChildProcess {
  const exePath = path.join(__dirname, "..", "..", "..", "build", "novelagent.exe");
  const args = ["backend", "-p", projectPath, "--port", String(port)];

  const child = spawn(exePath, args, {
    stdio: ["ignore", "pipe", "pipe"],
    windowsHide: true,
  });

  child.stdout?.on("data", (data: Buffer) => {
    // 后端通过 stdout 输出日志，在开发模式下可取消注释下面行来调试
    // process.stderr.write(data);
  });

  child.stderr?.on("data", (data: Buffer) => {
    process.stderr.write(data);
  });

  child.on("close", (code: number) => {
    if (code !== 0) {
      process.stderr.write(`\n后端进程异常退出 (code ${code})\n`);
    }
  });

  return child;
}
