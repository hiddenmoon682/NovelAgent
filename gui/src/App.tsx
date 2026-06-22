/// NovelAgent GUI — 根组件（布局外壳 + 状态协调）

import { useEffect, useState } from "react";
import { AppLayout } from "@/components/layout/AppLayout";
import { TopBar } from "@/components/layout/TopBar";
import { Sidebar } from "@/components/layout/Sidebar";
import { ChatPanel } from "@/components/chat/ChatPanel";
import { ChatInputBar } from "@/components/chat/ChatInputBar";
import { useChatStore } from "@/stores/chatStore";
import { useBackendStore } from "@/stores/backendStore";

const BACKEND_URL = "http://localhost:18899";

function App() {
  const backendStatus = useBackendStore((s) => s.status);
  const setBackendStatus = useBackendStore((s) => s.setStatus);
  const isStreaming = useChatStore((s) => s.isStreaming);
  const activeSessionId = useChatStore((s) => s.activeSessionId);
  const sendMessage = useChatStore((s) => s.sendMessage);
  const cancelStream = useChatStore((s) => s.cancelStream);

  // 后端健康检查轮询
  useEffect(() => {
    let cancelled = false;
    let retries = 0;
    const maxRetries = 30;

    const check = async () => {
      try {
        const resp = await fetch(`${BACKEND_URL}/api/health`);
        if (resp.ok && !cancelled) {
          setBackendStatus("running");
          return; // 成功后停止轮询，改用长间隔
        }
      } catch {
        // 后端尚未就绪
      }
      if (!cancelled) {
        retries++;
        if (retries >= maxRetries) {
          setBackendStatus("crashed");
        }
      }
    };

    // 快速轮询（每 500ms，最多 30 次）
    const fastInterval = setInterval(() => {
      if (retries >= maxRetries || backendStatus === "running") {
        clearInterval(fastInterval);
        return;
      }
      check();
    }, 500);

    // 慢速保活轮询（每 15 秒）
    const slowInterval = setInterval(() => {
      if (backendStatus === "running") {
        fetch(`${BACKEND_URL}/api/health`).catch(() => {
          setBackendStatus("crashed");
        });
      }
    }, 15000);

    return () => {
      cancelled = true;
      clearInterval(fastInterval);
      clearInterval(slowInterval);
    };
  }, []);

  const handleSend = (text: string) => {
    sendMessage(text);
  };

  const handleCancel = () => {
    cancelStream();
  };

  return (
    <AppLayout
      topBar={<TopBar />}
      sidebar={<Sidebar />}
    >
      <ChatPanel />
      <ChatInputBar
        onSend={handleSend}
        onCancel={handleCancel}
        disabled={backendStatus !== "running"}
        isStreaming={isStreaming}
      />
    </AppLayout>
  );
}

export default App;
