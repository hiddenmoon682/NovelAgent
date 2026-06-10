/// HTTP + SSE 客户端 — 与 C++ 后端通信。

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

/// HTTP GET 请求。
function httpGet(url: string): Promise<any> {
  return new Promise((resolve, reject) => {
    http
      .get(url, (res) => {
        let data = "";
        res.on("data", (chunk) => (data += chunk));
        res.on("end", () => {
          try {
            resolve(JSON.parse(data));
          } catch (e) {
            reject(e);
          }
        });
      })
      .on("error", reject);
  });
}

/// HTTP POST 请求（JSON body）。
function httpPost(url: string, body: object): Promise<any> {
  return new Promise((resolve, reject) => {
    const json = JSON.stringify(body);
    const req = http.request(
      url,
      {
        method: "POST",
        headers: { "Content-Type": "application/json", "Content-Length": String(Buffer.byteLength(json)) },
      },
      (res) => {
        let data = "";
        res.on("data", (chunk) => (data += chunk));
        res.on("end", () => {
          try {
            resolve(JSON.parse(data));
          } catch (e) {
            resolve({ raw: data });
          }
        });
      }
    );
    req.on("error", reject);
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

  /// 健康检查。
  async health(): Promise<ServerState> {
    return httpGet(`${this.baseUrl}/api/health`);
  }

  /// 创建会话。
  async createSession(): Promise<string> {
    const r = await httpPost(`${this.baseUrl}/api/session/create`, {});
    return r.session_id;
  }

  /// 销毁会话。
  async destroySession(sessionId: string): Promise<void> {
    await httpPost(`${this.baseUrl}/api/session/destroy`, { session_id: sessionId });
  }

  /// 获取项目状态。
  async projectStatus(): Promise<ProjectStatus> {
    return httpGet(`${this.baseUrl}/api/project/status`);
  }

  /// 发送聊天消息（SSE 流式），通过 onEvent 回调逐事件输出。
  chat(
    sessionId: string,
    message: string,
    onEvent: (event: ServerEvent) => void,
    onDone: () => void,
    onError: (err: Error) => void
  ): void {
    const json = JSON.stringify({ session_id: sessionId, message });
    const req = http.request(
      `${this.baseUrl}/api/chat`,
      {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Content-Length": String(Buffer.byteLength(json)),
        },
      },
      (res) => {
        let buffer = "";
        res.on("data", (chunk: string) => {
          buffer += chunk;
          // SSE 事件以 \n\n 分隔
          const lines = buffer.split("\n\n");
          buffer = lines.pop() || ""; // 最后一个可能不完整
          for (const line of lines) {
            const event = parseSSELine(line.trim());
            if (event) onEvent(event);
          }
        });
        res.on("end", () => {
          // 处理剩余缓冲区
          if (buffer.trim()) {
            const event = parseSSELine(buffer.trim());
            if (event) onEvent(event);
          }
          onDone();
        });
      }
    );
    req.on("error", onError);
    req.write(json);
    req.end();
  }
}
