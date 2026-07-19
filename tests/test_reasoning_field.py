"""
DeepSeek V4 工具调用 reasoning_content 字段必要性测试

测试目的：验证带 tool_calls 的 assistant 消息中，删除 reasoning_content 字段
是否会导致 400 报错（据某些社区说法存在这个限制）。

测试场景：
  测试1（正常组）：完整保留 reasoning_content → 预期正常完成
  测试2（实验组）：删除带 tool_calls 消息的 reasoning_content → 预期不触发 400

使用 config.json 中的 API 配置（default_provider 字段指定供应商）。
"""
import json, os, sys
from openai import OpenAI
from openai import APIStatusError

# ============================================================
# 配置读取
# ============================================================
CONFIG_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "config.json")
with open(CONFIG_PATH, "r", encoding="utf-8") as f:
    config = json.load(f)

provider_name = config.get("default_provider", "deepseek")
provider_cfg = config["providers"][provider_name]
API_KEY = provider_cfg["api_key"]
BASE_URL = provider_cfg["base_url"]
MODEL = provider_cfg["model"]

print(f"提供方: {provider_name}")
print(f"模型:   {MODEL}")
print(f"URL:    {BASE_URL}")
print()

client = OpenAI(api_key=API_KEY, base_url=BASE_URL)

# ============================================================
# 工具定义
# ============================================================
TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "add_numbers",
            "description": "计算两个数字的和",
            "parameters": {
                "type": "object",
                "properties": {
                    "a": {"type": "number", "description": "第一个数字"},
                    "b": {"type": "number", "description": "第二个数字"}
                },
                "required": ["a", "b"]
            }
        }
    }
]

SYSTEM_PROMPT = {
    "role": "system",
    "content": "你是 DeepSeek V4 模型，必须使用工具完成所有计算，不要直接给出答案。"
}

USER_PROMPT = "请先计算 12 + 34，再用得到的结果加上 56，最终答案是多少？必须使用工具完成计算。"


def execute_tool(tool_call):
    """执行工具调用，返回 tool result 格式的字典。"""
    if tool_call.function.name == "add_numbers":
        args = json.loads(tool_call.function.arguments)
        result = args["a"] + args["b"]
        print(f"    -> add_numbers({args['a']}, {args['b']}) = {result}")
        return {
            "role": "tool",
            "tool_call_id": tool_call.id,
            "content": str(result)
        }
    return {"role": "tool", "tool_call_id": tool_call.id, "content": "unknown tool"}


def run_tool_loop(initial_messages, modify_fn=None, max_rounds=5):
    """
    执行工具调用循环，返回 (最后一条消息, 完整消息列表, 是否成功)。

    modify_fn: 可选回调，在每轮 assistant 消息加入上下文之前调用，
               接收 (msg对象, msg_dict) 用于修改消息（如删除字段）。
    """
    messages = initial_messages[:]

    for round_idx in range(max_rounds):
        resp = client.chat.completions.create(
            model=MODEL,
            messages=messages,
            tools=TOOLS,
            tool_choice="auto" if round_idx == 0 else None
        )
        msg = resp.choices[0].message
        finish_reason = resp.choices[0].finish_reason

        msg_dict = msg.model_dump(mode="json")

        # 留给测试的修改钩子（如删除 reasoning_content）
        if modify_fn:
            modify_fn(msg, msg_dict)

        # 加入 assistant 消息
        messages.append(msg_dict)

        if finish_reason == "stop":
            return msg_dict, messages, True

        if finish_reason == "tool_calls" and msg.tool_calls:
            for tc in msg.tool_calls:
                messages.append(execute_tool(tc))
            continue

        # 意外终止
        print(f"   [警告] 意外终止: finish_reason={finish_reason}")
        return msg, messages, False

    print(f"   [警告] 达到最大轮数 {max_rounds}")
    return None, messages, False


# ============================================================
# 测试1：正常流程 — 保留 reasoning_content
# ============================================================
print("=" * 60)
print("【测试1】正常流程：保留 reasoning_content")
print("=" * 60)

messages_normal = [SYSTEM_PROMPT, {"role": "user", "content": USER_PROMPT}]

try:
    final_msg, _, ok = run_tool_loop(messages_normal, max_rounds=5)

    if ok and final_msg and final_msg.get("content"):
        print(f"\n最终答案: {final_msg['content']}")
        print(">>> 测试1 通过：完整保留 reasoning_content 正常运行\n")
    elif ok:
        print(">>> 测试1 通过（无文本回复）\n")
    else:
        print(">>> 测试1 失败：工具调用循环未正常结束\n")

except APIStatusError as e:
    print(f">>> 测试1 报错: {e.status_code} - {e.response.text}\n")
except Exception as e:
    print(f">>> 测试1 异常: {type(e).__name__}: {e}\n")


# ============================================================
# 测试2：异常流程 — 删除带 tool_calls 消息的 reasoning_content
# ============================================================
print("=" * 60)
print("【测试2】实验组：删除 tool_calls 消息中的 reasoning_content")
print("=" * 60)

messages_exp = [SYSTEM_PROMPT, {"role": "user", "content": USER_PROMPT}]
reasoning_deleted = False
tool_call_count = 0


def strip_reasoning(msg_obj, msg_dict):
    """删除 assistant 消息中的 reasoning_content 字段。"""
    global reasoning_deleted, tool_call_count
    if msg_dict.get("tool_calls") and "reasoning_content" in msg_dict:
        del msg_dict["reasoning_content"]
        reasoning_deleted = True
        tool_call_count += 1
        print(f"   [操作] 第 {tool_call_count} 轮：删除 reasoning_content")


try:
    final_msg, _, ok = run_tool_loop(messages_exp, modify_fn=strip_reasoning, max_rounds=5)

    if ok and final_msg and final_msg.get("content"):
        print(f"\n最终答案: {final_msg['content']}")

    if reasoning_deleted:
        print(f">>> 测试2 完成：共删除 {tool_call_count} 条 reasoning_content，未触发 400\n")
    else:
        print(">>> 测试2 完成（模型未产生 reasoning_content，无需删除）\n")

except APIStatusError as e:
    if e.status_code == 400:
        print(f"\n>>> 触发 400 报错！状态码: {e.status_code}")
        print(f"错误信息: {e.response.text}")
        if "reasoning_content" in (e.body.get("message", "") if isinstance(e.body, dict) else str(e.body)):
            print(">>> 确认：reasoning_content 缺失导致 400！")
        else:
            print(">>> 报错内容与 reasoning_content 无关，请检查其他参数")
    else:
        print(f">>> 测试2 报错: {e.status_code} - {e.response.text}")
except Exception as e:
    print(f">>> 测试2 异常: {type(e).__name__}: {e}")

# ============================================================
# 安全提醒
# ============================================================
print("\n" + "-" * 60)
print("注意：API Key 已在上述请求中使用。")
print("如果提交此脚本到仓库，请确保 config.json 在 .gitignore 中。")
print("-" * 60)
