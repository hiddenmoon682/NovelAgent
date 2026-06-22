/// NovelAgent GUI — 顶栏（项目名称 + 后端状态指示灯 + 设置）

import { useProjectStore } from "@/stores/projectStore";
import { useBackendStore } from "@/stores/backendStore";
import { useEffect } from "react";

export function TopBar() {
  const status = useProjectStore((s) => s.status);
  const fetchAll = useProjectStore((s) => s.fetchAll);
  const backendStatus = useBackendStore((s) => s.status);

  // 后端就绪后自动加载项目数据
  useEffect(() => {
    if (backendStatus === "running") {
      fetchAll();
    }
  }, [backendStatus]);

  const statusColors: Record<string, string> = {
    starting: "#f9e2af", // yellow
    running: "#a6e3a1",  // green
    crashed: "#f38ba8",  // red
  };

  const statusLabels: Record<string, string> = {
    starting: "后端启动中...",
    running: "后端运行中",
    crashed: "后端已断开",
  };

  return (
    <>
      {/* 项目名 */}
      <div className="flex items-center gap-2 font-semibold text-sm min-w-0">
        <span style={{ color: "var(--ctp-lavender)" }}>📖</span>
        <span className="truncate" style={{ color: "var(--ctp-text)" }}>
          {status?.title || "NovelAgent"}
        </span>
      </div>

      {/* 弹性空间 */}
      <div className="flex-1" />

      {/* 后端状态指示灯 */}
      <div
        className="flex items-center gap-2 text-xs rounded-full px-2.5 py-0.5"
        style={{
          background: "var(--ctp-surface0)",
          color: "var(--ctp-subtext0)",
        }}
      >
        <span
          className="w-2 h-2 rounded-full shrink-0"
          style={{ background: statusColors[backendStatus] || "#6c7086" }}
        />
        {statusLabels[backendStatus] || backendStatus}
      </div>

      {/* 设置按钮（占位） */}
      <button
        className="p-1 rounded hover:opacity-80 transition-opacity"
        style={{ color: "var(--ctp-subtext0)" }}
        title="设置"
      >
        ⚙️
      </button>
    </>
  );
}
