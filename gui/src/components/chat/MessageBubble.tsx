/// NovelAgent GUI — 单条消息渲染（按角色切换样式）

import { useState } from "react";
import { StreamingText } from "./StreamingText";
import { formatTime } from "@/lib/utils";
import { cn } from "@/lib/utils";
import type { ChatMessage } from "@/lib/types";

interface MessageBubbleProps {
  message: ChatMessage;
}

export function MessageBubble({ message }: MessageBubbleProps) {
  const { role, content, streaming, timestamp, toolName, tokens, finishReason } = message;

  switch (role) {
    case "user":
      return <UserBubble content={content} timestamp={timestamp} />;
    case "assistant":
      return (
        <AssistantBubble
          content={content}
          streaming={!!streaming}
          timestamp={timestamp}
          tokens={tokens}
          finishReason={finishReason}
        />
      );
    case "reasoning":
      return <ReasoningBlock content={content} streaming={!!streaming} />;
    case "tool_call":
      return <ToolCallBlock toolName={toolName || "Unknown"} />;
    case "tool_result":
      return <ToolResultBlock content={content} />;
    case "error":
      return <ErrorBlock content={content} />;
    case "system":
      return <SystemBlock content={content} />;
    default:
      return null;
  }
}

// ── 用户消息（右对齐，蓝色）──

function UserBubble({ content, timestamp }: { content: string; timestamp: number }) {
  return (
    <div className="flex justify-end my-3">
      <div className="max-w-[75%]">
        <div
          className="rounded-2xl rounded-br-md px-4 py-2.5 text-sm leading-relaxed"
          style={{
            background: "var(--ctp-blue)",
            color: "var(--ctp-base)",
          }}
        >
          {content}
        </div>
        <div
          className="text-right text-xs mt-1 px-1"
          style={{ color: "var(--ctp-overlay0)" }}
        >
          {formatTime(timestamp)}
        </div>
      </div>
    </div>
  );
}

// ── 助手回复（左对齐，Markdown 渲染）──

function AssistantBubble({
  content,
  streaming,
  timestamp,
  tokens,
  finishReason,
}: {
  content: string;
  streaming: boolean;
  timestamp: number;
  tokens?: number;
  finishReason?: string;
}) {
  return (
    <div className="flex justify-start my-3">
      <div className="max-w-[85%] min-w-[40%]">
        <div
          className="rounded-2xl rounded-bl-md px-4 py-2.5 text-sm"
          style={{ background: "var(--ctp-surface0)" }}
        >
          <StreamingText content={content} isStreaming={streaming} />
        </div>
        <div
          className="flex items-center gap-2 text-xs mt-1 px-1"
          style={{ color: "var(--ctp-overlay0)" }}
        >
          <span>{formatTime(timestamp)}</span>
          {tokens !== undefined && (
            <span style={{ color: "var(--ctp-overlay1)" }}>
              · {tokens} tokens
            </span>
          )}
          {finishReason === "stop" && (
            <span style={{ color: "var(--ctp-green)" }}>✓</span>
          )}
        </div>
      </div>
    </div>
  );
}

// ── 思考链（可折叠，深灰背景）──

function ReasoningBlock({
  content,
  streaming,
}: {
  content: string;
  streaming: boolean;
}) {
  const [collapsed, setCollapsed] = useState(false);

  if (!content) return null;

  return (
    <div className="flex justify-start my-1">
      <div className="max-w-[85%] min-w-[30%]">
        <button
          onClick={() => setCollapsed(!collapsed)}
          className="flex items-center gap-1.5 text-xs px-3 py-1 rounded-t-lg w-full text-left"
          style={{
            background: "var(--ctp-surface1)",
            color: "var(--ctp-overlay1)",
          }}
        >
          <span>{collapsed ? "▶" : "▼"}</span>
          <span>💭 思考链</span>
          {streaming && (
            <span style={{ color: "var(--ctp-green)" }}>···</span>
          )}
        </button>
        {!collapsed && (
          <div
            className="rounded-b-lg px-3 py-2 text-xs leading-relaxed"
            style={{
              background: "var(--ctp-mantle)",
              color: "var(--ctp-overlay1)",
            }}
          >
            {content}
            {streaming && <span className="streaming-cursor" />}
          </div>
        )}
      </div>
    </div>
  );
}

// ── 工具调用（黄色强调边条）──

function ToolCallBlock({ toolName }: { toolName: string }) {
  return (
    <div className="flex justify-start my-1">
      <div
        className="flex items-center gap-2 px-3 py-1 rounded-lg text-xs border-l-3"
        style={{
          background: "rgba(249,226,175,0.08)",
          borderColor: "var(--ctp-yellow)",
          color: "var(--ctp-yellow)",
        }}
      >
        <span>🔧</span>
        <span className="font-mono">{toolName}</span>
      </div>
    </div>
  );
}

// ── 工具结果（灰色小字，可折叠）──

function ToolResultBlock({ content }: { content: string }) {
  const [collapsed, setCollapsed] = useState(true);

  return (
    <div className="flex justify-start my-1">
      <div className="max-w-[85%]">
        <button
          onClick={() => setCollapsed(!collapsed)}
          className="text-xs px-2 py-0.5 rounded flex items-center gap-1"
          style={{
            color: "var(--ctp-overlay0)",
            background: "var(--ctp-surface0)",
          }}
        >
          <span>{collapsed ? "▶" : "▼"}</span>
          工具结果
        </button>
        {!collapsed && (
          <pre
            className="text-xs mt-1 p-2 rounded overflow-x-auto max-h-32"
            style={{
              background: "var(--ctp-mantle)",
              color: "var(--ctp-overlay1)",
            }}
          >
            {content}
          </pre>
        )}
      </div>
    </div>
  );
}

// ── 错误消息（红色左边框）──

function ErrorBlock({ content }: { content: string }) {
  return (
    <div className="flex justify-start my-2">
      <div
        className="max-w-[85%] px-3 py-2 rounded-lg text-sm border-l-3"
        style={{
          background: "rgba(243,139,168,0.1)",
          borderColor: "var(--ctp-red)",
          color: "var(--ctp-red)",
        }}
      >
        <div className="flex items-center gap-1.5 mb-1">
          <span>⚠️</span>
          <span className="font-semibold text-xs">错误</span>
        </div>
        <p className="text-xs leading-relaxed" style={{ color: "var(--ctp-maroon)" }}>
          {content}
        </p>
      </div>
    </div>
  );
}

// ── 系统消息（灰色居中）──

function SystemBlock({ content }: { content: string }) {
  return (
    <div className="flex justify-center my-2">
      <span
        className="text-xs px-3 py-1 rounded-full"
        style={{
          background: "var(--ctp-surface0)",
          color: "var(--ctp-overlay0)",
        }}
      >
        {content}
      </span>
    </div>
  );
}
