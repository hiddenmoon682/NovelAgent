/// NovelAgent GUI — 流式文本渲染（Markdown + 光标动画）

import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import { cn } from "@/lib/utils";

interface StreamingTextProps {
  content: string;
  isStreaming: boolean;
}

export function StreamingText({ content, isStreaming }: StreamingTextProps) {
  if (!content && !isStreaming) {
    return (
      <span style={{ color: "var(--ctp-overlay0)" }}>
        （无内容）
      </span>
    );
  }

  return (
    <div className={cn("prose prose-sm max-w-none", isStreaming && "streaming-cursor")}>
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        components={{
          // 自定义样式（匹配深色主题）
          h1: ({ children }) => (
            <h1 className="text-lg font-bold mt-3 mb-2" style={{ color: "var(--ctp-text)" }}>
              {children}
            </h1>
          ),
          h2: ({ children }) => (
            <h2 className="text-base font-bold mt-2 mb-1" style={{ color: "var(--ctp-text)" }}>
              {children}
            </h2>
          ),
          h3: ({ children }) => (
            <h3 className="text-sm font-bold mt-2 mb-1" style={{ color: "var(--ctp-text)" }}>
              {children}
            </h3>
          ),
          p: ({ children }) => (
            <p className="my-1 leading-relaxed" style={{ color: "var(--ctp-text)" }}>
              {children}
            </p>
          ),
          ul: ({ children }) => (
            <ul className="my-1 pl-4" style={{ color: "var(--ctp-text)" }}>
              {children}
            </ul>
          ),
          ol: ({ children }) => (
            <ol className="my-1 pl-4" style={{ color: "var(--ctp-text)" }}>
              {children}
            </ol>
          ),
          li: ({ children }) => (
            <li className="my-0.5">{children}</li>
          ),
          strong: ({ children }) => (
            <strong style={{ color: "var(--ctp-peach)" }}>{children}</strong>
          ),
          em: ({ children }) => (
            <em style={{ color: "var(--ctp-green)" }}>{children}</em>
          ),
          code: ({ children, className }) => {
            const isInline = !className;
            if (isInline) {
              return (
                <code
                  className="px-1 py-0.5 rounded text-xs"
                  style={{
                    background: "var(--ctp-surface0)",
                    color: "var(--ctp-pink)",
                  }}
                >
                  {children}
                </code>
              );
            }
            return (
              <pre className="my-2">
                <code className={className}>{children}</code>
              </pre>
            );
          },
          blockquote: ({ children }) => (
            <blockquote
              className="border-l-2 pl-3 my-1"
              style={{
                borderColor: "var(--ctp-blue)",
                color: "var(--ctp-subtext0)",
              }}
            >
              {children}
            </blockquote>
          ),
          hr: () => (
            <hr className="my-2" style={{ borderColor: "var(--ctp-surface0)" }} />
          ),
        }}
      >
        {content || (isStreaming ? "" : "")}
      </ReactMarkdown>
    </div>
  );
}
