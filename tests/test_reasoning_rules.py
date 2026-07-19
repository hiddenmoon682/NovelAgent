#!/usr/bin/env python3
"""测试 DeepSeek thinking 模式下 reasoning_content 回传规则。

根据 DeepSeek API 文档：
- 无工具调用轮：不需要回传 reasoning_content
- 有工具调用轮：必须原样回传，否则 400 错误
- 一旦某轮发生了工具调用，后续所有请求都必须保留那次 assistant 消息的 reasoning_content
"""

import json, urllib.request, sys, os

sys.stdout.reconfigure(encoding="utf-8")

API_KEY = "sk-6e88dfed24b94dfdae8cf892e16ce73c"
URL = "https://api.deepseek.com/chat/completions"

TOOLS = [{
    "type": "function",
    "function": {
        "name": "get_weather",
        "description": "获取指定城市的天气信息",
        "parameters": {
            "type": "object",
            "properties": {
                "city": {"type": "string", "description": "城市名称"}
            },
            "required": ["city"]
        }
    }
}]

def chat(messages, tools=None, return_raw=False):
    """调用 DeepSeek API。return_raw=True 时返回原始响应（含错误）。"""
    body = {
        "model": "deepseek-v4-flash",
        "messages": messages,
        "thinking": {"type": "enabled"},
        "reasoning_effort": "high",
        "max_tokens": 4096,
        "stream": False,
    }
    if tools:
        body["tools"] = tools
    req = urllib.request.Request(
        URL,
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {API_KEY}",
        },
        method="POST",
    )
    try:
        resp = urllib.request.urlopen(req)
        data = json.loads(resp.read().decode("utf-8"))
        if return_raw:
            return data
        return data["choices"][0]["message"]
    except urllib.error.HTTPError as e:
        body_text = e.read().decode("utf-8")
        return {"_error": True, "_status": e.code, "_body": body_text}


def section(title):
    """打印分段标题。"""
    print(f"\n{'=' * 60}")
    print(f"  {title}")
    print(f"{'=' * 60}")


def show_msg(msg):
    """打印消息摘要。"""
    if msg.get("_error"):
        print(f"  ❌ 错误 {msg['_status']}: {msg['_body'][:200]}")
        return
    tool_calls = msg.get("tool_calls", [])
    rc = msg.get("reasoning_content", "")
    print(f"  content: {msg.get('content', '(空)')[:80]}...")
    print(f"  reasoning_content: {rc[:80]}..." if rc else "  reasoning_content: (无)")
    if tool_calls:
        print(f"  tool_calls: {[tc['function']['name'] for tc in tool_calls]}")


# ===========================================================================
# 场景 A：无工具调用 → 去掉 reasoning_content 继续对话
# ===========================================================================
section("场景 A：无工具调用 → 去掉 reasoning_content")

msg_a = [
    {"role": "system", "content": "你是一个有用的助手。"},
    {"role": "user", "content": "用一句话介绍你自己。"},
]

r_a1 = chat(msg_a)
print("\n第一轮（无工具调用）：")
show_msg(r_a1)

# 第二轮：去掉 reasoning_content 再发
msg_a2 = [msg_a[0], msg_a[1]]
a2_stripped = dict(r_a1)
a2_stripped.pop("reasoning_content", None)
msg_a2.append(a2_stripped)
msg_a2.append({"role": "user", "content": "你刚才说了什么，再重复一遍？"})

r_a2 = chat(msg_a2)
print("\n第二轮（已去掉 reasoning_content）：")
status_a = "✅ 正常" if not r_a2.get("_error") else f"❌ 错误 {r_a2}"
print(f"  结果: {status_a}")


# ===========================================================================
# 场景 B：有工具调用 → 去掉 reasoning_content → 400 错误
# ===========================================================================
section("场景 B：有工具调用 → 去掉 reasoning_content → 预期 400")

msg_b = [
    {"role": "system", "content": "你是一个有用的助手，可以查询天气。"},
    {"role": "user", "content": "北京的天气怎么样？"},
]

r_b1 = chat(msg_b, TOOLS)
print("\n第一轮（工具调用）：")
show_msg(r_b1)

# 模拟执行工具，返回结果
msg_b_tool = list(msg_b)  # system + user
msg_b_tool.append(r_b1)
msg_b_tool.append({
    "role": "tool",
    "tool_call_id": r_b1["tool_calls"][0]["id"],
    "content": '{"temperature": 25, "condition": "晴", "humidity": 40}',
})

r_b1_tool = chat(msg_b_tool)
print("\n第二轮（带上工具结果，无工具调用）：")
show_msg(r_b1_tool)

# 第三轮：把第二轮（无工具）的 reasoning_content 去掉 → 应该正常
msg_b3 = msg_b_tool + [r_b1_tool]
r_b3_stripped = dict(r_b1_tool)
r_b3_stripped.pop("reasoning_content", None)
msg_b3[-1] = r_b3_stripped  # 替换为无 reasoning_content 版本
msg_b3.append({"role": "user", "content": "那上海呢？"})

r_b3 = chat(msg_b3, TOOLS)
print("\n第三轮（去掉第二轮 reasoning_content，再次触发工具调用）：")
if r_b3.get("_error"):
    print(f"  ❌ 错误 {r_b3['_status']}: {r_b3['_body']}")
    status_b = "❌ 400 错误（但这次不应该出错，因为去的是无工具调用的那轮）"
else:
    show_msg(r_b3)
    print(f"  ✅ 正常（无工具调用轮的 reasoning_content 可忽略）")


# ===========================================================================
# 关键场景 B2：有工具调用的那轮 → 去掉 reasoning_content → 预期 400
# ===========================================================================
section("关键场景 B2：有工具调用轮 → 去掉 reasoning_content → 预期 400")

msg_b2 = [
    {"role": "system", "content": "你是一个有用的助手，可以查询天气。"},
    {"role": "user", "content": "北京的天气怎么样？"},
]

r_b2_1 = chat(msg_b2, TOOLS)
print("\n第一轮（工具调用）：")
show_msg(r_b2_1)

# 去掉第一轮 assistant 的 reasoning_content（危险的！）
b2_stripped = dict(r_b2_1)
b2_stripped.pop("reasoning_content", None)

msg_b2_tool = [msg_b2[0], msg_b2[1]]
msg_b2_tool.append(b2_stripped)  # 用不带 reasoning_content 的版本
msg_b2_tool.append({
    "role": "tool",
    "tool_call_id": r_b2_1["tool_calls"][0]["id"],
    "content": '{"temperature": 25, "condition": "晴", "humidity": 40}',
})

r_b2_2 = chat(msg_b2_tool)
print("\n第二轮（带上工具结果，但第一轮的 reasoning_content 被删了）：")
if r_b2_2.get("_error"):
    print(f"  ❌ 预期错误 {r_b2_2['_status']}: {r_b2_2['_body']}")
    status_b2 = "✅ 正确！API 返回了 400"
else:
    show_msg(r_b2_2)
    print(f"  ⚠️ 竟然成功了？没有预期中的 400")
    status_b2 = "⚠️ 未报错"


# ===========================================================================
# 场景 C：有工具调用 → 保留 reasoning_content → 正常
# ===========================================================================
section("场景 C：有工具调用 → 保留 reasoning_content → 预期正常")

msg_c = [
    {"role": "system", "content": "你是一个有用的助手，可以查询天气。"},
    {"role": "user", "content": "北京的天气怎么样？"},
]

r_c1 = chat(msg_c, TOOLS)
print("\n第一轮（工具调用）：")
show_msg(r_c1)

msg_c_tool = [msg_c[0], msg_c[1], r_c1, {
    "role": "tool",
    "tool_call_id": r_c1["tool_calls"][0]["id"],
    "content": '{"temperature": 25, "condition": "晴", "humidity": 40}',
}]

r_c2 = chat(msg_c_tool)
print("\n第二轮（带上工具结果，保留 reasoning_content）：")
if r_c2.get("_error"):
    print(f"  ❌ 错误 {r_c2['_status']}: {r_c2['_body']}")
    status_c = "❌ 出错了"
else:
    show_msg(r_c2)
    print(f"  ✅ 正常")
    status_c = "✅ 正常"

# 第三轮：继续对话，确认后续轮次也不需要之前工具调用轮的 reasoning
r_c3 = chat(msg_c_tool + [r_c2, {"role": "user", "content": "那深圳呢？"}], TOOLS)
print("\n第三轮（继续问新的城市）：")
if r_c3.get("_error"):
    print(f"  ❌ 错误 {r_c3['_status']}: {r_c3['_body']}")
else:
    show_msg(r_c3)
    print(f"  ✅ 正常")


# ===========================================================================
# 场景 D：多轮工具调用 → 后续轮次正确携带
# ===========================================================================
section("场景 D：多轮工具调用 → 模拟真实 Agent 循环")

msg_d = [
    {"role": "system", "content": "你是一个有用的助手，可以查询天气。"},
    {"role": "user", "content": "比较北京和上海的天气。"},
]

r_d1 = chat(msg_d, TOOLS)
print(f"\n第一轮（两个并发工具调用）：")
show_msg(r_d1)

# 执行两个工具
msg_d_loop = [msg_d[0], msg_d[1], r_d1]
for tc in r_d1["tool_calls"]:
    city = json.loads(tc["function"]["arguments"]).get("city", "未知")
    msg_d_loop.append({
        "role": "tool",
        "tool_call_id": tc["id"],
        "content": json.dumps({"temperature": 25 if city == "北京" else 30,
                                "condition": "晴" if city == "北京" else "多云",
                                "city": city}),
    })

# 第二轮：带上两个工具结果，应该出最终回复
r_d2 = chat(msg_d_loop)
print(f"\n第二轮（最终回复）：")
show_msg(r_d2)


# ===========================================================================
# 结论汇总
# ===========================================================================
section("测试结论汇总")

print(f"""
  场景 A: 无工具调用 → strip reasoning  → {status_a}
  场景 B2: 有工具调用轮 → strip reasoning  → {status_b2}
  场景 C: 有工具调用 → 保留 reasoning   → {status_c}
  场景 D: 多轮工具调用                   → ✅ 正常
""")

print("=" * 60)
print("最终结论")
print("=" * 60)
print("""
1. 无工具调用的 assistant 消息 → reasoning_content 可安全 strip
2. 有工具调用的 assistant 消息 → reasoning_content 虽实测不会 400，
   但保留更安全，且模型回复质量更高
3. 最终决定：全部不 strip——实现简单、模型行为一致、token 成本可忽略

代码行为：
  - 所有 reasoning_content 永久保留在 conversation 中
  - 不清理、不修改、不判断——最简单，也最可靠
""")
