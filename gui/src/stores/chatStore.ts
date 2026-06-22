/// NovelAgent GUI — 聊天状态管理（会话 + 消息 + SSE 流式）

import { create } from "zustand";
import type { ChatMessage, ChatSession } from "@/lib/types";
import * as api from "@/lib/api";
import { streamChat } from "@/lib/sse";
import { uid } from "@/lib/utils";

interface ChatState {
  sessions: ChatSession[];
  activeSessionId: string | null;
  isStreaming: boolean;
  abortController: AbortController | null;

  // 会话操作
  createSession: () => Promise<string>;
  destroySession: (id: string) => Promise<void>;
  switchSession: (id: string) => void;

  // 消息操作
  sendMessage: (text: string) => Promise<void>;
  cancelStream: () => void;

  // 内部消息更新（由 SSE 回调触发）
  _appendContent: (delta: string) => void;
  _appendReasoning: (delta: string) => void;
  _startToolCall: () => void;
  _finalizeMessage: (tokens: number, finishReason: string) => void;
  _addError: (message: string) => void;

  // 辅助
  getActiveSession: () => ChatSession | undefined;
  getActiveMessages: () => ChatMessage[];
}

export const useChatStore = create<ChatState>((set, get) => ({
  sessions: [],
  activeSessionId: null,
  isStreaming: false,
  abortController: null,

  // ── 会话操作 ──

  createSession: async () => {
    const { session_id } = await api.createSession();
    const newSession: ChatSession = {
      id: session_id,
      title: `会话 ${session_id.slice(0, 8)}`,
      messages: [],
      createdAt: Date.now(),
      updatedAt: Date.now(),
    };
    set((s) => ({
      sessions: [newSession, ...s.sessions],
      activeSessionId: session_id,
    }));
    return session_id;
  },

  destroySession: async (id) => {
    try {
      await api.destroySession(id);
    } catch { /* 忽略网络错误 */ }
    set((s) => ({
      sessions: s.sessions.filter((ses) => ses.id !== id),
      activeSessionId:
        s.activeSessionId === id
          ? s.sessions.find((ses) => ses.id !== id)?.id ?? null
          : s.activeSessionId,
    }));
  },

  switchSession: (id) => {
    set({ activeSessionId: id });
  },

  // ── 消息操作 ──

  sendMessage: async (text) => {
    const state = get();
    // 确保有活跃会话
    let sessionId = state.activeSessionId;
    if (!sessionId) {
      sessionId = await get().createSession();
    }

    const userMsg: ChatMessage = {
      id: uid(),
      role: "user",
      content: text,
      timestamp: Date.now(),
    };

    // 创建空的助手消息（用于流式填充）
    const assistantMsg: ChatMessage = {
      id: uid(),
      role: "assistant",
      content: "",
      streaming: true,
      timestamp: Date.now(),
    };

    // 追加用户消息 + 空助手消息
    set((s) => ({
      sessions: s.sessions.map((ses) =>
        ses.id === sessionId
          ? {
              ...ses,
              messages: [...ses.messages, userMsg, assistantMsg],
              updatedAt: Date.now(),
            }
          : ses
      ),
      isStreaming: true,
    }));

    // 启动 SSE 流
    const controller = streamChat(sessionId, text, {
      onContent: (delta) => get()._appendContent(delta),
      onReasoning: (delta) => get()._appendReasoning(delta),
      onToolCallStart: () => get()._startToolCall(),
      onDone: (tokens, finishReason) => get()._finalizeMessage(tokens, finishReason),
      onError: (message) => get()._addError(message),
    });

    set({ abortController: controller });
  },

  cancelStream: () => {
    const { abortController, activeSessionId } = get();
    if (abortController) {
      abortController.abort();
    }
    // 将当前流式消息标记为完成
    set((s) => ({
      isStreaming: false,
      abortController: null,
      sessions: s.sessions.map((ses) =>
        ses.id === activeSessionId
          ? {
              ...ses,
              messages: ses.messages.map((msg) =>
                msg.streaming
                  ? { ...msg, streaming: false, content: msg.content || "(已取消)" }
                  : msg
              ),
            }
          : ses
      ),
    }));
  },

  // ── 内部消息更新 ──

  _appendContent: (delta) => {
    set((s) => ({
      sessions: s.sessions.map((ses) =>
        ses.id === s.activeSessionId
          ? {
              ...ses,
              messages: ses.messages.map((msg) =>
                msg.streaming && msg.role === "assistant"
                  ? { ...msg, content: msg.content + delta }
                  : msg
              ),
            }
          : ses
      ),
    }));
  },

  _appendReasoning: (delta) => {
    const { activeSessionId, sessions } = get();
    const activeSession = sessions.find((s) => s.id === activeSessionId);
    if (!activeSession) return;

    const msgs = activeSession.messages;
    // 查找是否存在未完成的 reasoning 消息
    const lastReasoning = [...msgs].reverse().find(
      (m) => m.role === "reasoning" && m.streaming
    );

    if (lastReasoning) {
      // 追加到现有 reasoning
      set((s) => ({
        sessions: s.sessions.map((ses) =>
          ses.id === activeSessionId
            ? {
                ...ses,
                messages: ses.messages.map((msg) =>
                  msg.id === lastReasoning.id
                    ? { ...msg, content: msg.content + delta }
                    : msg
                ),
              }
            : ses
        ),
      }));
    } else {
      // 创建新的 reasoning 消息
      const reasoningMsg: ChatMessage = {
        id: uid(),
        role: "reasoning",
        content: delta,
        streaming: true,
        collapsed: false,
        timestamp: Date.now(),
      };
      set((s) => ({
        sessions: s.sessions.map((ses) =>
          ses.id === activeSessionId
            ? { ...ses, messages: [...ses.messages, reasoningMsg] }
            : ses
        ),
      }));
    }
  },

  _startToolCall: () => {
    // 将当前 reasoning 消息标记为完成
    set((s) => ({
      sessions: s.sessions.map((ses) =>
        ses.id === s.activeSessionId
          ? {
              ...ses,
              messages: ses.messages.map((msg) =>
                msg.role === "reasoning" && msg.streaming
                  ? { ...msg, streaming: false }
                  : msg
              ),
            }
          : ses
      ),
    }));
  },

  _finalizeMessage: (tokens, finishReason) => {
    set((s) => ({
      isStreaming: false,
      abortController: null,
      sessions: s.sessions.map((ses) =>
        ses.id === s.activeSessionId
          ? {
              ...ses,
              messages: ses.messages.map((msg) =>
                msg.streaming
                  ? {
                      ...msg,
                      streaming: false,
                      tokens,
                      finishReason,
                    }
                  : msg
              ),
              updatedAt: Date.now(),
            }
          : ses
      ),
    }));
  },

  _addError: (message) => {
    const errorMsg: ChatMessage = {
      id: uid(),
      role: "error",
      content: message,
      timestamp: Date.now(),
    };
    set((s) => ({
      isStreaming: false,
      abortController: null,
      sessions: s.sessions.map((ses) =>
        ses.id === s.activeSessionId
          ? {
              ...ses,
              messages: ses.messages.map((msg) =>
                msg.streaming ? { ...msg, streaming: false } : msg
              ).concat(errorMsg),
            }
          : ses
      ),
    }));
  },

  // ── 辅助 ──

  getActiveSession: () => {
    const { sessions, activeSessionId } = get();
    return sessions.find((s) => s.id === activeSessionId);
  },

  getActiveMessages: () => {
    const session = get().getActiveSession();
    return session?.messages ?? [];
  },
}));
