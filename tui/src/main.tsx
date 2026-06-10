/// NovelAgent TUI 入口。

import React from "react";
import { render } from "ink";
import { Command } from "commander";
import { ApiClient } from "./client/api";
import {
  isBackendRunning,
  getBackendPort,
  spawnBackend,
} from "./client/backend";
import { App } from "./app";
import path from "path";

const program = new Command();

program
  .name("novelagent")
  .description("NovelAgent TUI — AI 写小说助手")
  .option("-p, --project <path>", "项目目录路径", ".")
  .option("--port <number>", "后端端口", "18899")
  .parse(process.argv);

const opts = program.opts();
const projectPath: string = path.resolve(opts.project);
const port: number = parseInt(opts.port);

async function main() {
  // 1. 清屏（CMD 不支持交替缓冲，先手动清）
  process.stdout.write("\x1b[2J\x1b[H");

  // 2. 检测后端是否已在运行（静默模式）
  const running = await isBackendRunning(projectPath);
  let backendPort = port;

  if (running) {
    backendPort = getBackendPort(projectPath);
  } else {
    spawnBackend(projectPath, port);
    await new Promise((r) => setTimeout(r, 1500));
    backendPort = port;
  }

  // 4. 启动 TUI
  const api = new ApiClient(backendPort);
  render(React.createElement(App, { api, projectPath }), { patchConsole: false });
}

main().catch((e) => {
  process.stderr.write(`启动失败: ${e.message}\n`);
  process.stderr.write(
    "请先启动后端: ./novelagent.exe backend -p ./项目\n"
  );
  process.exit(1);
});
