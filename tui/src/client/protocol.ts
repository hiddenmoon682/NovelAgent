/// WebSocket 消息协议类型定义。

// 前端 → 后端
export type ClientMessage =
  | { type: "chat"; session_id: string; message: string }
  | { type: "create_session" }
  | { type: "destroy_session"; session_id: string }
  | { type: "clear_session"; session_id: string }
  | { type: "execute"; command: string };

// 后端 → 前端
export type ServerEvent =
  | { type: "content"; delta: string }
  | { type: "reasoning"; delta: string }
  | { type: "tool_call_start" }
  | { type: "tool_call"; name: string; args: string }
  | { type: "tool_result"; summary: string }
  | { type: "done"; tokens: number; finish_reason: string }
  | { type: "error"; message: string }
  | { type: "pong" };

/// 解析 SSE data 行为 ServerEvent
export function parseSSELine(line: string): ServerEvent | null {
  if (!line.startsWith("data: ")) return null;
  try {
    const json = JSON.parse(line.slice(6));
    return json as ServerEvent;
  } catch {
    return null;
  }
}
