/// NovelAgent GUI — 消息列表容器（自动滚到底部 + 欢迎页）

import { useEffect, useRef } from "react";
import { useChatStore } from "@/stores/chatStore";
import { MessageBubble } from "./MessageBubble";

export function ChatPanel() {
  const messages = useChatStore((s) => {
    const session = s.sessions.find((ses) => ses.id === s.activeSessionId);
    return session?.messages ?? [];
  });
  const isStreaming = useChatStore((s) => s.isStreaming);
  const bottomRef = useRef<HTMLDivElement>(null);

  // 自动滚到底部
  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages, isStreaming]);

  // 无消息时显示欢迎页
  if (messages.length === 0) {
    return (
      <div className="flex-1 flex items-center justify-center">
        <div className="text-center max-w-md px-8">
          <div className="text-5xl mb-6">📝</div>
          <h1
            className="text-xl font-bold mb-3"
            style={{ color: "var(--ctp-text)" }}
          >
            NovelAgent
          </h1>
          <p
            className="text-sm leading-relaxed mb-6"
            style={{ color: "var(--ctp-subtext0)" }}
          >
            AI 辅助写小说助手。输入你的创作想法，AI 将帮你完成角色设计、大纲规划、章节写作等任务。
          </p>
          <div className="flex flex-wrap gap-2 justify-center">
            {[
              "帮我设计一个主角",
              "列出当前章节大纲",
              "写第一章的开场段落",
              "介绍一下世界观设定",
            ].map((hint) => (
              <button
                key={hint}
                className="text-xs px-3 py-1.5 rounded-full transition-colors"
                style={{
                  background: "var(--ctp-surface0)",
                  color: "var(--ctp-subtext0)",
                }}
                onMouseEnter={(e) => {
                  e.currentTarget.style.background = "var(--ctp-surface1)";
                }}
                onMouseLeave={(e) => {
                  e.currentTarget.style.background = "var(--ctp-surface0)";
                }}
              >
                {hint}
              </button>
            ))}
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="flex-1 overflow-y-auto px-4 py-2">
      {messages.map((msg) => (
        <MessageBubble key={msg.id} message={msg} />
      ))}
      <div ref={bottomRef} />
    </div>
  );
}
