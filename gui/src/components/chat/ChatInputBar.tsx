/// NovelAgent GUI — 聊天输入区（自动扩展 + Enter 发送 + 流式取消）

import { useState, useRef, useCallback, type KeyboardEvent } from "react";
import { cn } from "@/lib/utils";

interface ChatInputBarProps {
  onSend: (text: string) => void;
  onCancel: () => void;
  disabled: boolean;
  isStreaming: boolean;
}

export function ChatInputBar({
  onSend,
  onCancel,
  disabled,
  isStreaming,
}: ChatInputBarProps) {
  const [text, setText] = useState("");
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  // 自动调整高度
  const adjustHeight = useCallback(() => {
    const el = textareaRef.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = Math.min(el.scrollHeight, 200) + "px";
  }, []);

  const handleSend = () => {
    const trimmed = text.trim();
    if (!trimmed || disabled || isStreaming) return;
    onSend(trimmed);
    setText("");
    // 重置高度
    if (textareaRef.current) {
      textareaRef.current.style.height = "auto";
    }
  };

  const handleKeyDown = (e: KeyboardEvent<HTMLTextAreaElement>) => {
    // Enter 发送，Shift+Enter 换行
    if (e.key === "Enter" && !e.shiftKey) {
      e.preventDefault();
      handleSend();
    }
  };

  return (
    <div
      className="shrink-0 border-t px-4 py-3"
      style={{
        background: "var(--ctp-mantle)",
        borderColor: "var(--ctp-surface0)",
      }}
    >
      <div className="flex items-end gap-2 max-w-4xl mx-auto">
        {/* 输入框 */}
        <div className="flex-1 relative">
          <textarea
            ref={textareaRef}
            value={text}
            onChange={(e) => {
              setText(e.target.value);
              adjustHeight();
            }}
            onKeyDown={handleKeyDown}
            placeholder={
              disabled
                ? "等待后端连接..."
                : isStreaming
                  ? "AI 正在回复中..."
                  : "输入消息... (Enter 发送, Shift+Enter 换行)"
            }
            disabled={disabled}
            rows={1}
            className={cn(
              "w-full resize-none rounded-xl px-4 py-2.5 text-sm outline-none transition-colors",
              "placeholder:opacity-50"
            )}
            style={{
              background: "var(--ctp-surface0)",
              color: "var(--ctp-text)",
              borderColor: "var(--ctp-surface2)",
              maxHeight: 200,
            }}
          />
        </div>

        {/* 发送 / 取消 按钮 */}
        {isStreaming ? (
          <button
            onClick={onCancel}
            className="shrink-0 px-4 py-2.5 rounded-xl text-sm font-medium transition-opacity hover:opacity-80"
            style={{
              background: "var(--ctp-red)",
              color: "var(--ctp-base)",
            }}
          >
            取消
          </button>
        ) : (
          <button
            onClick={handleSend}
            disabled={disabled || !text.trim()}
            className={cn(
              "shrink-0 px-4 py-2.5 rounded-xl text-sm font-medium transition-opacity",
              disabled || !text.trim()
                ? "opacity-40 cursor-not-allowed"
                : "hover:opacity-80"
            )}
            style={{
              background: "var(--ctp-blue)",
              color: "var(--ctp-base)",
            }}
          >
            发送
          </button>
        )}
      </div>

      {/* 状态提示 */}
      <div
        className="text-xs text-center mt-1.5"
        style={{ color: "var(--ctp-overlay0)" }}
      >
        {disabled
          ? "⏳ 等待 C++ 后端启动..."
          : isStreaming
            ? "AI 正在生成回复 · 点击「取消」可中断"
            : "Enter 发送 · Shift+Enter 换行"}
      </div>
    </div>
  );
}
