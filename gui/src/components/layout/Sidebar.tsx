/// NovelAgent GUI — 侧边栏（对话 / 项目 / 角色 标签页）

import { useState } from "react";
import { useChatStore } from "@/stores/chatStore";
import { useProjectStore } from "@/stores/projectStore";
import { cn, truncate, formatDate } from "@/lib/utils";
import type { ChapterInfo, CharacterInfo } from "@/lib/types";

type TabKey = "chats" | "project" | "characters";

export function Sidebar() {
  const [activeTab, setActiveTab] = useState<TabKey>("chats");
  const sessions = useChatStore((s) => s.sessions);
  const activeSessionId = useChatStore((s) => s.activeSessionId);
  const switchSession = useChatStore((s) => s.switchSession);
  const createSession = useChatStore((s) => s.createSession);
  const destroySession = useChatStore((s) => s.destroySession);

  const projectStatus = useProjectStore((s) => s.status);
  const chapters = useProjectStore((s) => s.chapters);
  const characters = useProjectStore((s) => s.characters);

  const tabs: { key: TabKey; label: string; icon: string }[] = [
    { key: "chats", label: "对话", icon: "💬" },
    { key: "project", label: "项目", icon: "📁" },
    { key: "characters", label: "角色", icon: "👤" },
  ];

  return (
    <div className="flex flex-col h-full">
      {/* 标签栏 */}
      <div
        className="flex border-b shrink-0"
        style={{ borderColor: "var(--ctp-surface0)" }}
      >
        {tabs.map((tab) => (
          <button
            key={tab.key}
            onClick={() => setActiveTab(tab.key)}
            className={cn(
              "flex-1 py-2 text-xs font-medium transition-colors",
              activeTab === tab.key
                ? "border-b-2"
                : "hover:opacity-80"
            )}
            style={{
              color: activeTab === tab.key
                ? "var(--ctp-blue)"
                : "var(--ctp-subtext0)",
              borderColor: activeTab === tab.key
                ? "var(--ctp-blue)"
                : "transparent",
            }}
          >
            {tab.icon} {tab.label}
          </button>
        ))}
      </div>

      {/* 内容区 */}
      <div className="flex-1 overflow-y-auto p-2">
        {activeTab === "chats" && (
          <ChatsTab
            sessions={sessions}
            activeId={activeSessionId}
            onSwitch={switchSession}
            onCreate={createSession}
            onDestroy={destroySession}
          />
        )}
        {activeTab === "project" && (
          <ProjectTab status={projectStatus} chapters={chapters} />
        )}
        {activeTab === "characters" && (
          <CharactersTab characters={characters} />
        )}
      </div>
    </div>
  );
}

// ── 对话标签页 ──

function ChatsTab({
  sessions,
  activeId,
  onSwitch,
  onCreate,
  onDestroy,
}: {
  sessions: import("@/lib/types").ChatSession[];
  activeId: string | null;
  onSwitch: (id: string) => void;
  onCreate: () => Promise<string>;
  onDestroy: (id: string) => Promise<void>;
}) {
  return (
    <div className="flex flex-col gap-1">
      <button
        onClick={() => onCreate()}
        className="w-full text-left px-3 py-2 rounded-lg text-sm transition-colors flex items-center gap-2"
        style={{
          color: "var(--ctp-blue)",
          background: "var(--ctp-surface0)",
        }}
      >
        ＋ 新建会话
      </button>

      {sessions.length === 0 && (
        <p
          className="text-xs px-3 py-4 text-center"
          style={{ color: "var(--ctp-overlay0)" }}
        >
          暂无会话，点击上方新建
        </p>
      )}

      {sessions.map((session) => {
        const isActive = session.id === activeId;
        const lastMsg = session.messages[session.messages.length - 1];
        return (
          <div
            key={session.id}
            className="group relative"
          >
            <button
              onClick={() => onSwitch(session.id)}
              className="w-full text-left px-3 py-2 rounded-lg text-sm transition-colors"
              style={{
                color: isActive
                  ? "var(--ctp-text)"
                  : "var(--ctp-subtext0)",
                background: isActive
                  ? "var(--ctp-surface0)"
                  : "transparent",
              }}
            >
              <div className="truncate font-medium">{session.title}</div>
              <div
                className="truncate text-xs mt-0.5"
                style={{ color: "var(--ctp-overlay1)" }}
              >
                {lastMsg
                  ? truncate(lastMsg.content, 40)
                  : "新会话"}
              </div>
              <div
                className="text-xs mt-0.5"
                style={{ color: "var(--ctp-overlay0)" }}
              >
                {formatDate(session.updatedAt)}
              </div>
            </button>
            {/* 删除按钮 */}
            <button
              onClick={(e) => {
                e.stopPropagation();
                onDestroy(session.id);
              }}
              className="absolute right-2 top-2 opacity-0 group-hover:opacity-100 transition-opacity text-xs px-1.5 py-0.5 rounded"
              style={{
                color: "var(--ctp-red)",
                background: "var(--ctp-surface1)",
              }}
              title="删除会话"
            >
              ✕
            </button>
          </div>
        );
      })}
    </div>
  );
}

// ── 项目标签页 ──

function ProjectTab({
  status,
  chapters,
}: {
  status: import("@/lib/types").ProjectStatus | null;
  chapters: ChapterInfo[];
}) {
  if (!status) {
    return (
      <p
        className="text-xs px-3 py-4 text-center"
        style={{ color: "var(--ctp-overlay0)" }}
      >
        等待后端连接...
      </p>
    );
  }

  return (
    <div className="flex flex-col gap-3">
      {/* 项目信息 */}
      <div
        className="rounded-lg p-3"
        style={{ background: "var(--ctp-surface0)" }}
      >
        <h3 className="font-semibold text-sm" style={{ color: "var(--ctp-text)" }}>
          {status.title}
        </h3>
        <div className="flex gap-3 mt-2 text-xs" style={{ color: "var(--ctp-subtext0)" }}>
          <span>📄 {status.chapters} 章</span>
          <span>👤 {status.characters} 角色</span>
          <span>🌍 {status.settings} 设定</span>
        </div>
      </div>

      {/* 章节列表 */}
      <div>
        <h4
          className="text-xs font-semibold px-1 mb-1"
          style={{ color: "var(--ctp-overlay1)" }}
        >
          章节列表
        </h4>
        {chapters.length === 0 ? (
          <p className="text-xs px-3 py-2" style={{ color: "var(--ctp-overlay0)" }}>
            暂无章节
          </p>
        ) : (
          chapters.map((ch) => (
            <div
              key={ch.id}
              className="px-3 py-1.5 rounded text-xs flex items-center gap-2"
              style={{ color: "var(--ctp-subtext0)" }}
            >
              <span style={{ color: "var(--ctp-overlay1)" }}>
                {String(ch.order).padStart(2, "0")}
              </span>
              <span className="truncate flex-1">{ch.title}</span>
              <span
                className="text-xs px-1.5 py-0.5 rounded"
                style={{
                  background: ch.status === "final"
                    ? "rgba(166,227,161,0.2)"
                    : "var(--ctp-surface1)",
                  color: ch.status === "final"
                    ? "var(--ctp-green)"
                    : "var(--ctp-overlay0)",
                }}
              >
                {statusLabel(ch.status)}
              </span>
            </div>
          ))
        )}
      </div>
    </div>
  );
}

// ── 角色标签页 ──

function CharactersTab({ characters }: { characters: CharacterInfo[] }) {
  const roleLabels: Record<string, string> = {
    protagonist: "主角",
    antagonist: "反派",
    supporting: "配角",
    minor: "次要",
  };

  if (characters.length === 0) {
    return (
      <p
        className="text-xs px-3 py-4 text-center"
        style={{ color: "var(--ctp-overlay0)" }}
      >
        暂无角色
      </p>
    );
  }

  return (
    <div className="flex flex-col gap-1">
      {characters.map((c) => (
        <div
          key={c.id}
          className="px-3 py-2 rounded-lg"
        >
          <div className="flex items-center gap-2">
            <span className="font-medium text-sm truncate" style={{ color: "var(--ctp-text)" }}>
              {c.name}
            </span>
            <span
              className="text-xs px-1.5 py-0.5 rounded"
              style={{
                background: c.role === "protagonist"
                  ? "rgba(137,180,250,0.2)"
                  : "var(--ctp-surface0)",
                color: c.role === "protagonist"
                  ? "var(--ctp-blue)"
                  : "var(--ctp-overlay1)",
              }}
            >
              {roleLabels[c.role] || c.role}
            </span>
          </div>
          {c.traits.length > 0 && (
            <div className="flex flex-wrap gap-1 mt-1">
              {c.traits.slice(0, 3).map((t) => (
                <span
                  key={t}
                  className="text-xs px-1.5 py-0.5 rounded"
                  style={{
                    color: "var(--ctp-overlay1)",
                    background: "var(--ctp-surface0)",
                  }}
                >
                  {t}
                </span>
              ))}
            </div>
          )}
        </div>
      ))}
    </div>
  );
}

// ── 辅助 ──

function statusLabel(s: string): string {
  const m: Record<string, string> = {
    outlined: "大纲",
    drafting: "草稿",
    drafted: "完成",
    revised: "修订",
    final: "终稿",
  };
  return m[s] || s;
}
