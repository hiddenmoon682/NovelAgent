/// NovelAgent Ink TUI — readline 输入 + Ink 渲染。

import React, { useState, useEffect, useRef, useCallback } from "react";
import { Box, Text, useApp } from "ink";
import Spinner from "ink-spinner";
import { ApiClient } from "./client/api";
import { ServerEvent } from "./client/protocol";
import * as readline from "readline";

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

  const msgCountRef = useRef(0);
  const rlRef = useRef<readline.Interface | null>(null);
  const inputRef = useRef("");      // 累积输入（IME 组合后的字符）
  const sendRef = useRef<() => void>(() => {});

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
    const msg = inputRef.current.trim();
    if (!msg || !sessionId) return;
    inputRef.current = "";
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
  }, [sessionId, api]);

  // 保持 sendRef 最新
  sendRef.current = sendMessage;

  // ── readline 输入（替代 useInput，IME 兼容）──
  useEffect(() => {
    // 关闭 Ink 的 raw mode，让 readline 接管
    if (process.stdin.isTTY) {
      try { process.stdin.setRawMode(false); } catch {}
    }

    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
      prompt: "",
      terminal: true,
    });
    rlRef.current = rl;

    // 逐字符更新输入显示（readline 已处理 IME 组合）
    process.stdin.on("keypress", (_char: any, key: any) => {
      if (!key) return;
      if (key.name === "return") {
        inputRef.current = (rl as any).line || "";
        rl.close();
        sendRef.current();
        // 重新创建 readline
        setTimeout(() => restartReadline(), 50);
      } else if (key.name === "backspace") {
        inputRef.current = inputRef.current.slice(0, -1);
        setInput(inputRef.current);
      } else if (key.ctrl && key.name === "c") {
        exit();
      } else if (key.sequence && !key.ctrl && !key.meta && key.name !== "return") {
        inputRef.current = inputRef.current + key.sequence;
        setInput(inputRef.current);
      }
    });

    readline.emitKeypressEvents(process.stdin);
    if (process.stdin.isTTY) process.stdin.setRawMode(true);
    rl.prompt();

    return () => {
      try { process.stdin.setRawMode(false); } catch {}
      rl.close();
    };
  }, []);

  function restartReadline() {
    if (rlRef.current) {
      try { rlRef.current.close(); } catch {}
    }
    if (process.stdin.isTTY) {
      try { process.stdin.setRawMode(false); } catch {}
    }
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
      prompt: "",
      terminal: true,
    });
    rlRef.current = rl;
    readline.emitKeypressEvents(process.stdin);
    if (process.stdin.isTTY) process.stdin.setRawMode(true);
    // 光标移到底部，显示当前输入
    process.stdout.write("\x1b[999B\x1b[999D> " + inputRef.current);
    rl.prompt();
  }

  // ── 每次渲染后光标回到底部 ──
  useEffect(() => {
    if (process.platform === "win32") {
      process.stdout.write("\x1b[999B\x1b[999D");
    }
  });

  const truncate = (s: string, max: number) =>
    s.length > max ? s.substring(0, max) + "..." : s;

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
      </Box>
    </Box>
  );
};
