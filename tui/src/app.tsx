/// NovelAgent Ink TUI 主应用（审查修复版）。

import React, { useState, useEffect, useRef, useCallback } from "react";
import { Box, Text, useInput, useApp } from "ink";
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
  const [sessionId, setSessionId] = useState("");
  const [messages, setMessages] = useState<Message[]>([]);
  const [input, setInput] = useState("");
  const [loading, setLoading] = useState(false);
  const [statusLine, setStatusLine] = useState("连接中...");
  const [projectInfo, setProjectInfo] = useState("");

  // Fix #3: 使用 ref 跟踪当前消息索引，避免闭包竞态
  const msgCountRef = useRef(0);

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

  const sendMessage = useCallback(() => {
    if (!input.trim() || !sessionId) return;
    const msg = input.trim();
    setInput("");
    setMessages((prev) => [...prev, { role: "user", content: msg }]);
    setLoading(true);
    setStatusLine("思考中...");

    // Fix #3: 使用函数式 setState + ref 保证索引正确
    setMessages((prev) => {
      msgCountRef.current = prev.length + 1;
      return [...prev, { role: "assistant", content: "" }];
    });

    api.chat(
      sessionId, msg,
      (event: ServerEvent) => {
        switch (event.type) {
          case "content":
            // Fix #3: 使用函数式 setState，prev 始终是最新值
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
          case "tool_call_start":
            setStatusLine("执行工具...");
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

  useInput((inputChar, key) => {
    if (key.return) sendMessage();
    else if (key.backspace || key.delete) setInput((prev) => prev.slice(0, -1));
    else if (inputChar && !key.ctrl && !key.meta) setInput((prev) => prev + inputChar);
    if (key.ctrl && inputChar === "c") exit();
  });

  return (
    <Box flexDirection="column" height="100%">
      <Box borderStyle="round" borderColor="blue" paddingX={1}>
        <Text bold color="white">NovelAgent v0.3.0</Text>
        <Text dimColor> — AI 写小说助手</Text>
      </Box>
      <Box>
        <Text backgroundColor="blue" color="white"> {statusLine} </Text>
        {projectInfo ? <Text dimColor> {projectInfo}</Text> : null}
      </Box>
      <Box flexDirection="column" flexGrow={1} marginTop={1}>
        {messages.slice(-20).map((msg, i) => (
          <Box key={i} flexDirection="column">
            {msg.role === "user" && (
              <Text><Text bold color="blue">{"> "}</Text><Text>{msg.content}</Text></Text>
            )}
            {msg.role === "assistant" && <Text color="green">{msg.content}</Text>}
            {msg.role === "tool" && <Text dimColor>{msg.content}</Text>}
            {msg.role === "error" && <Text color="red">{msg.content}</Text>}
          </Box>
        ))}
      </Box>
      <Box borderStyle="single" borderColor="blue" paddingX={1}>
        {loading ? <Text color="yellow"><Spinner type="dots" /> </Text>
         : <Text bold color="blue">{"> "}</Text>}
        <Text>{input}</Text>
      </Box>
      <Box><Text dimColor>Ctrl+C 退出 | 输入消息开始写作</Text></Box>
    </Box>
  );
};
