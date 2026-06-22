/// NovelAgent GUI — HTTP API 客户端（封装所有 /api/* 端点）

import type {
  HealthResponse,
  SessionCreateResponse,
  ProjectStatus,
  ChapterInfo,
  CharacterInfo,
  ExportResponse,
  ExecuteResponse,
} from "./types";

/** 后端基础 URL */
const BASE_URL = "http://localhost:18899";

/** 通用 fetch 封装，自动处理 JSON 解析和错误 */
async function request<T>(
  path: string,
  options?: RequestInit
): Promise<T> {
  const url = `${BASE_URL}${path}`;
  const resp = await fetch(url, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });

  if (!resp.ok) {
    const text = await resp.text();
    throw new Error(`API 错误 ${resp.status}: ${text}`);
  }

  return resp.json() as Promise<T>;
}

/** 健康检查 */
export async function checkHealth(): Promise<HealthResponse> {
  return request<HealthResponse>("/api/health");
}

/** 创建新会话 */
export async function createSession(): Promise<SessionCreateResponse> {
  return request<SessionCreateResponse>("/api/session/create", {
    method: "POST",
    body: "{}",
  });
}

/** 销毁会话 */
export async function destroySession(sessionId: string): Promise<void> {
  await request("/api/session/destroy", {
    method: "POST",
    body: JSON.stringify({ session_id: sessionId }),
  });
}

/** 列出所有活跃会话 ID */
export async function listSessions(): Promise<string[]> {
  return request<string[]>("/api/session/list");
}

/** 获取项目状态 */
export async function getProjectStatus(): Promise<ProjectStatus> {
  return request<ProjectStatus>("/api/project/status");
}

/** 获取章节列表 */
export async function getChapters(): Promise<ChapterInfo[]> {
  return request<ChapterInfo[]>("/api/project/chapters");
}

/** 获取角色列表 */
export async function getCharacters(): Promise<CharacterInfo[]> {
  return request<CharacterInfo[]>("/api/project/characters");
}

/** 导出项目为 Markdown */
export async function exportProject(): Promise<ExportResponse> {
  return request<ExportResponse>("/api/project/export", {
    method: "POST",
  });
}

/** 单次命令执行（非流式） */
export async function executeCommand(command: string): Promise<ExecuteResponse> {
  return request<ExecuteResponse>("/api/execute", {
    method: "POST",
    body: JSON.stringify({ command }),
  });
}

export { BASE_URL };
