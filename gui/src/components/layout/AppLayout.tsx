/// NovelAgent GUI — 主布局（CSS Grid：侧边栏 + 聊天区）

import { type ReactNode, useState, useCallback, useRef, useEffect } from "react";
import { cn } from "@/lib/utils";

interface AppLayoutProps {
  topBar: ReactNode;
  sidebar: ReactNode;
  children: ReactNode;
}

export function AppLayout({ topBar, sidebar, children }: AppLayoutProps) {
  const [sidebarWidth, setSidebarWidth] = useState(280);
  const [isDragging, setIsDragging] = useState(false);
  const containerRef = useRef<HTMLDivElement>(null);

  const handleMouseDown = useCallback((e: React.MouseEvent) => {
    e.preventDefault();
    setIsDragging(true);
  }, []);

  useEffect(() => {
    if (!isDragging) return;

    const handleMouseMove = (e: MouseEvent) => {
      if (!containerRef.current) return;
      const rect = containerRef.current.getBoundingClientRect();
      const w = e.clientX - rect.left;
      setSidebarWidth(Math.max(200, Math.min(500, w)));
    };

    const handleMouseUp = () => setIsDragging(false);

    window.addEventListener("mousemove", handleMouseMove);
    window.addEventListener("mouseup", handleMouseUp);
    return () => {
      window.removeEventListener("mousemove", handleMouseMove);
      window.removeEventListener("mouseup", handleMouseUp);
    };
  }, [isDragging]);

  return (
    <div
      ref={containerRef}
      className="h-full flex flex-col"
      style={{ background: "var(--ctp-base)" }}
    >
      {/* 顶栏 */}
      <div
        className="shrink-0 border-b px-4 py-2 flex items-center gap-4"
        style={{
          background: "var(--ctp-mantle)",
          borderColor: "var(--ctp-surface0)",
          height: 40,
        }}
      >
        {topBar}
      </div>

      {/* 主体区域 */}
      <div className="flex-1 flex overflow-hidden">
        {/* 侧边栏 */}
        <div
          className="shrink-0 border-r overflow-hidden flex flex-col"
          style={{
            width: sidebarWidth,
            background: "var(--ctp-mantle)",
            borderColor: "var(--ctp-surface0)",
          }}
        >
          {sidebar}
        </div>

        {/* 拖拽手柄 */}
        <div
          className={cn(
            "w-1 cursor-col-resize shrink-0 transition-colors",
            isDragging ? "bg-blue-500" : "hover:bg-blue-500/30"
          )}
          style={{ background: isDragging ? "var(--ctp-blue)" : undefined }}
          onMouseDown={handleMouseDown}
        />

        {/* 主聊天区 */}
        <div className="flex-1 flex flex-col overflow-hidden" style={{ background: "var(--ctp-base)" }}>
          {children}
        </div>
      </div>
    </div>
  );
}
