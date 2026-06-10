/// NovelAgent Ink TUI — 光标 + IME 修正版。

import React, { useState, useEffect, useRef, useCallback } from "react";
import { Box, Text, useInput, useApp, useStdout } from "ink";
import Spinner from "ink-spinner";
import { ApiClient } from "./client/api";
import { ServerEvent } from "./client/protocol";

interface Message {
  role: "user" | "assistant" | "tool" | "thinking" | "error";
  content: string;
  toolName?: string;
}

interface Props { api: ApiClient; projectPath: string }

export const App: React.FC<Props> = ({ api, projectPath }) => {
  const { exit } = useApp();
  const { stdout } = useStdout();
  const [sessionId, setSessionId] = useState("");
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState("");
  const [loading, setLoading] = useState(false);
  const [statusLine, setStatusLine] = useState("连接中...");
  const [projectInfo, setProjectInfo] = useState("");
  const [showCursor, setShowCursor] = useState(true);

  const msgCountRef = useRef(0);
  const inputFocused = useRef(true);

  // ── 闪烁光标 ──
  useEffect(() => {
    const timer = setInterval(() => setShowCursor((v) => !v), 530);
    return () => clearInterval(timer);
  }, []);

  // ── 连接后端 ──
  useEffect(() => {
    (async () => {
      try {
        const sid = await api.createSession();
        setSessionId(sid);
        const info = await api.projectStatus();
        if (info.title)
          setProjectInfo(`${info.title} | ${info.chapters}章 ${info.characters}角色`);
        setStatusLine("就绪");
      } catch (e: any) {
        setStatusLine("无法连接后端。请先启动 novelagent backend -p ./项目");
        setMessages([{ role: "error", content: `无法连接后端: ${e.message}` }]);
      }
    })();
    return () => { if (sessionId) api.destroySession(sessionId); };
  }, []);

  // ── 发送消息 ──
  const sendMessage = useCallback(() => {
    if (!input.trim() || !sessionId) return;
    const msg = input.trim();
    setInput("");
    setMessages((prev) => [...prev, { role: "user", content: msg }]);
    setLoading(true);
    setStatusLine("思考中...");

    setMessages((prev) => {
      msgCountRef.current = prev.length;
      return [...prev, { role: "assistant", content: "" }];
    });

    api.chat(
      sessionId, msg,
      (event: ServerEvent) => {
        switch (event.type) {
          case "content":
            setMessages((prev) => {
              const idx = msgCountRef.current;
              if (idx < prev.length && prev[idx]) {
                const updated = [...prev];
                updated[idx] = { ...updated[idx], content: updated[idx].content + event.delta };
                return updated;
              }
              return prev;
            });
            break;
          case "tool_call":
            setMessages((prev) => [...prev, { role: "tool", content: `[工具] ${event.name}`, toolName: event.name }]);
            break;
          case "tool_result":
            setMessages((prev) => [...prev, { role: "tool", content: `  ${event.summary?.substring(0, 100) || "完成"}` }]);
            break;
          case "done":
            setStatusLine(`就绪 | ${event.tokens} tokens`);
            setLoading(false);
            break;
          case "error":
            setMessages((prev) => [...prev, { role: "error", content: `错误: ${event.message}` }]);
            setLoading(false);
            setStatusLine("错误");
            break;
        }
      },
      () => setLoading(false),
      (err: Error) => {
        setMessages((prev) => [...prev, { role: "error", content: `连接错误: ${err.message}` }]);
        setLoading(false);
        setStatusLine("连接断开");
      }
    );
  }, [input, sessionId, api]);

  // ── 键盘输入 ──
  useInput((inputChar, key) => {
    inputFocused.current = true;
    if (key.return) sendMessage();
    else if (key.backspace || key.delete) setInput((prev) => prev.slice(0, -1));
    else if (inputChar && !key.ctrl && !key.meta) setInput((prev) => prev + inputChar);
    if (key.ctrl && inputChar === "c") exit();
  });

  // ── 每次渲染后将终端光标移到底部，帮助 IME 定位 ──
  useEffect(() => {
    if (inputFocused.current) {
      stdout.write("\x1b[999B\x1b[999D");
    }
  });

  const truncate = (s: string, max: number) =>
    s.length > max ? s.substring(0, max) + "..." : s;

  const cursor = showCursor ? "|" : " ";

  return (
    <Box flexDirection="column">
      <Box borderStyle="single" borderColor="blue" paddingX={1}>
        <Text bold color="white">NovelAgent v0.3.0</Text>
        <Text dimColor> - AI Writing Assistant</Text>
      </Box>
      <Box>
        <Text backgroundColor="blue" color="white"> {truncate(statusLine, 60)} </Text>
        {projectInfo ? <Text dimColor> {truncate(projectInfo, 40)}</Text> : null}
      </Box>
      <Box flexDirection="column" marginTop={1}>
        {messages.slice(-15).map((msg, i) => (
          <Box key={i} flexDirection="column">
            {msg.role === "user" && (
              <Text><Text bold color="blue">{"> "}</Text><Text>{truncate(msg.content, 200)}</Text></Text>
            )}
            {msg.role === "assistant" && <Text color="green">{truncate(msg.content, 500)}</Text>}
            {msg.role === "tool" && <Text dimColor>{truncate(msg.content, 120)}</Text>}
            {msg.role === "error" && <Text color="red">{truncate(msg.content, 150)}</Text>}
          </Box>
        ))}
      </Box>
      <Box borderStyle="single" borderColor="blue" paddingX={1}>
        {loading ? <Text color="yellow"><Spinner type="dots" /> </Text>
         : <Text bold color="blue">{"> "}</Text>}
        <Text>{input}</Text>
        <Text color="blue">{cursor}</Text>
      </Box>
    </Box>
  );
};
