/// NovelAgent GUI — SSE 流式解析器（ReadableStream + 逐帧解析）

import type { SSEEvent } from "./types";
import { BASE_URL } from "./api";

/** SSE 流回调 */
export interface SSECallbacks {
  onContent: (delta: string) => void;
  onReasoning: (delta: string) => void;
  onToolCallStart: () => void;
  onDone: (tokens: number, finishReason: string) => void;
  onError: (message: string) => void;
}

/**
 * 发起 SSE 聊天流请求。
 * 用 fetch ReadableStream 逐字节读取，按 SSE 帧分割后解析 JSON 事件。
 * 返回 AbortController 用于取消流。
 */
export function streamChat(
  sessionId: string,
  message: string,
  callbacks: SSECallbacks
): AbortController {
  const controller = new AbortController();
  let buffer = "";

  const run = async () => {
    try {
      const resp = await fetch(`${BASE_URL}/api/chat`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ session_id: sessionId, message }),
        signal: controller.signal,
      });

      if (!resp.ok) {
        const text = await resp.text();
        callbacks.onError(`HTTP ${resp.status}: ${text}`);
        return;
      }

      const reader = resp.body?.getReader();
      if (!reader) {
        callbacks.onError("无法读取响应流");
        return;
      }

      const decoder = new TextDecoder("utf-8");

      while (true) {
        const { done, value } = await reader.read();
        if (done) break;

        buffer += decoder.decode(value, { stream: true });

        // 按双换行分割 SSE 帧
        const parts = buffer.split("\n\n");
        // 最后一部分可能不完整，保留在 buffer
        buffer = parts.pop() || "";

        for (const part of parts) {
          if (!part.trim()) continue;

          // SSE 心跳行
          if (part.startsWith(": heartbeat")) continue;

          // [DONE] 终止标记
          if (part.includes("[DONE]")) return;

          // 提取 data: 行
          const lines = part.split("\n");
          for (const line of lines) {
            const trimmed = line.trim();
            if (!trimmed.startsWith("data: ")) continue;

            const jsonStr = trimmed.slice(6); // 去掉 "data: " 前缀
            if (!jsonStr || jsonStr === "[DONE]") continue;

            try {
              const event: SSEEvent = JSON.parse(jsonStr);
              dispatch(event, callbacks);
            } catch {
              // 忽略格式错误的帧
              console.warn("[SSE] 解析失败:", jsonStr.slice(0, 100));
            }
          }
        }
      }

      // 处理 buffer 中可能残留的最后一条完整帧
      if (buffer.trim() && !buffer.includes("[DONE]")) {
        const lines = buffer.split("\n");
        for (const line of lines) {
          const trimmed = line.trim();
          if (trimmed.startsWith("data: ") && trimmed.length > 6) {
            const jsonStr = trimmed.slice(6);
            if (jsonStr === "[DONE]") continue;
            try {
              const event: SSEEvent = JSON.parse(jsonStr);
              dispatch(event, callbacks);
            } catch { /* ignore */ }
          }
        }
      }
    } catch (err: unknown) {
      if (err instanceof Error && err.name === "AbortError") return;
      callbacks.onError(err instanceof Error ? err.message : String(err));
    }
  };

  // 异步执行（不阻塞调用方）
  run();

  return controller;
}

/** 根据事件类型分发回调 */
function dispatch(event: SSEEvent, cb: SSECallbacks) {
  switch (event.type) {
    case "content":
      cb.onContent(event.delta);
      break;
    case "reasoning":
      cb.onReasoning(event.delta);
      break;
    case "tool_call_start":
      cb.onToolCallStart();
      break;
    case "done":
      cb.onDone(event.tokens, event.finish_reason);
      break;
    case "error":
      cb.onError(event.message);
      break;
  }
}
