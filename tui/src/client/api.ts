/// HTTP + SSE 客户端 — 与 C++ 后端通信（审查修复版）。

import http from "http";
import { ServerEvent, parseSSELine } from "./protocol";

export interface ServerState {
  status: string;
  sessions: number;
  clients: number;
}

export interface ProjectStatus {
  title: string;
  chapters: number;
  characters: number;
  settings: number;
  status: string;
  word_count: number;
  target_words: number;
}

/// HTTP GET 请求（带超时）。
function httpGet(url: string, timeoutMs = 30000): Promise<any> {
  return new Promise((resolve, reject) => {
    const req = http.get(url, (res) => {
      let data = "";
      res.on("data", (chunk) => (data += chunk));
      res.on("end", () => {
        try { resolve(JSON.parse(data)); }
        catch (e) { reject(e); }
      });
    });
    req.on("error", reject);
    req.setTimeout(timeoutMs, () => {
      req.destroy();
      reject(new Error(`HTTP GET 超时 (${timeoutMs}ms): ${url}`));
    });
  });
}

/// HTTP POST 请求（JSON body，带超时）。
function httpPost(url: string, body: object, timeoutMs = 30000): Promise<any> {
  return new Promise((resolve, reject) => {
    const json = JSON.stringify(body);
    const req = http.request(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "Content-Length": String(Buffer.byteLength(json, "utf8")),
      },
    }, (res) => {
      let data = "";
      res.on("data", (chunk) => (data += chunk));
      res.on("end", () => {
        try { resolve(JSON.parse(data)); }
        catch (e) { resolve({ raw: data }); }
      });
    });
    req.on("error", reject);
    req.setTimeout(timeoutMs, () => {
      req.destroy();
      reject(new Error(`HTTP POST 超时 (${timeoutMs}ms): ${url}`));
    });
    req.write(json);
    req.end();
  });
}

/// API 客户端。
export class ApiClient {
  private baseUrl: string;

  constructor(port: number = 18899) {
    this.baseUrl = `http://localhost:${port}`;
  }

  async health(): Promise<ServerState> {
    return httpGet(`${this.baseUrl}/api/health`);
  }

  async createSession(): Promise<string> {
    const r = await httpPost(`${this.baseUrl}/api/session/create`, {});
    return r.session_id;
  }

  async destroySession(sessionId: string): Promise<void> {
    await httpPost(`${this.baseUrl}/api/session/destroy`, { session_id: sessionId });
  }

  async projectStatus(): Promise<ProjectStatus> {
    return httpGet(`${this.baseUrl}/api/project/status`);
  }

  /// 发送聊天消息（SSE 流式），通过 onEvent 回调逐事件输出。
  /// Fix #2: 缓冲区使用 [DONE] 信号强制 flush，处理跨帧边界。
  /// Fix #4: 添加 120s 超时（LLM 最长响应时间）。
  chat(
    sessionId: string,
    message: string,
    onEvent: (event: ServerEvent) => void,
    onDone: () => void,
    onError: (err: Error) => void
  ): void {
    const json = JSON.stringify({ session_id: sessionId, message });
    const req = http.request(`${this.baseUrl}/api/chat`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        "Content-Length": String(Buffer.byteLength(json, "utf8")),
      },
    }, (res) => {
      let buffer = "";
      res.on("data", (chunk: string) => {
        buffer += chunk;
        // SSE 事件以 \n\n 分隔
        const lines = buffer.split("\n\n");
        buffer = lines.pop() || "";
        for (const line of lines) {
          const trimmed = line.trim();
          if (!trimmed) continue;
          // Fix #2: [DONE] 信号强制 flush 并结束
          if (trimmed === "data: [DONE]") {
            onDone();
            return;
          }
          const event = parseSSELine(trimmed);
          if (event) {
            if (event.type === "done") onDone();
            else onEvent(event);
          }
        }
      });
      res.on("end", () => {
        // 处理剩余缓冲区
        if (buffer.trim()) {
          const trimmed = buffer.trim();
          if (trimmed !== "data: [DONE]") {
            const event = parseSSELine(trimmed);
            if (event) onEvent(event);
          }
        }
        onDone();
      });
    });

    req.on("error", onError);
    // Fix #4: 120 秒超时（LLM 调用最长等待时间）
    req.setTimeout(120000, () => {
      req.destroy();
      onError(new Error("请求超时 (120s)"));
    });
    req.write(json);
    req.end();
  }
}
