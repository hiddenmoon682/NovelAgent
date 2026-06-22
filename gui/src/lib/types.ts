/// NovelAgent GUI — TypeScript 类型定义（匹配 C++ JSON 结构）

// ── API 响应类型 ──

export interface HealthResponse {
  status: string;
  sessions: number;
  clients: number;
}

export interface SessionCreateResponse {
  session_id: string;
}

export interface ProjectStatus {
  title: string;
  chapters: number;
  characters: number;
  settings: number;
  status: string;
}

export interface ChapterInfo {
  id: string;
  title: string;
  order: number;
  synopsis: string;
  status: string;
  scenes_count: number;
  pov_characters: string[];
}

export interface CharacterInfo {
  id: string;
  name: string;
  role: string;
  traits: string[];
  appearances_count: number;
}

export interface ExportResponse {
  content: string;
  chapters: number;
}

export interface ExecuteResponse {
  content: string;
  tokens: number;
}

// ── SSE 事件类型（来自 /api/chat 流）──

export interface SSEContentEvent {
  type: "content";
  delta: string;
}

export interface SSEReasoningEvent {
  type: "reasoning";
  delta: string;
}

export interface SSEToolCallStartEvent {
  type: "tool_call_start";
}

export interface SSEDoneEvent {
  type: "done";
  tokens: number;
  finish_reason: string;
}

export interface SSEErrorEvent {
  type: "error";
  message: string;
}

export type SSEEvent =
  | SSEContentEvent
  | SSEReasoningEvent
  | SSEToolCallStartEvent
  | SSEDoneEvent
  | SSEErrorEvent;

// ── 聊天消息类型 ──

export type MessageRole = "user" | "assistant" | "reasoning" | "tool_call" | "tool_result" | "error" | "system";

export interface ChatMessage {
  id: string;
  role: MessageRole;
  content: string;
  toolName?: string;
  tokens?: number;
  finishReason?: string;
  streaming?: boolean;    // true 表示 SSE 仍在发送中
  collapsed?: boolean;    // true 表示思考链/工具结果已折叠
  timestamp: number;
}

export interface ChatSession {
  id: string;
  title: string;
  messages: ChatMessage[];
  createdAt: number;
  updatedAt: number;
}

// ── 后端连接状态 ──

export type BackendStatus = "starting" | "running" | "crashed";
