/// NovelAgent GUI — 项目详情面板（章节列表 + 角色列表详情视图）

import { useProjectStore } from "@/stores/projectStore";

export function ProjectPanel() {
  const status = useProjectStore((s) => s.status);
  const chapters = useProjectStore((s) => s.chapters);
  const characters = useProjectStore((s) => s.characters);

  if (!status) return null;

  return (
    <div className="p-4 overflow-y-auto">
      <h2 className="text-lg font-bold mb-1" style={{ color: "var(--ctp-text)" }}>
        {status.title}
      </h2>
      <p className="text-xs mb-4" style={{ color: "var(--ctp-subtext0)" }}>
        {status.chapters} 章 · {status.characters} 角色 · {status.settings} 设定
      </p>
      <p className="text-xs" style={{ color: "var(--ctp-overlay0)" }}>
        状态: {status.status}
      </p>
    </div>
  );
}
