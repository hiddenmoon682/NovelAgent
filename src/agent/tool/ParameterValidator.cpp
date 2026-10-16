// ParameterValidator 实现。

#include "agent/tool/ParameterValidator.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace agent {

json ValidationResult::toJson() const {
    if (valid) return {{"valid", true}};

    json err = json::object();
    err["error"] = "参数校验失败";
    json details = json::array();
    for (const auto& e : errors) {
        details.push_back({{"field", e.field}, {"reason", e.reason}});
    }
    err["details"] = details;
    return err;
}

ValidationResult ParameterValidator::validate(
    const json& schema, const json& arguments)
{
    ValidationResult result;

    if (!schema.is_object() || !arguments.is_object()) {
        result.valid = true; // 无法校验时放行
        return result;
    }

    checkNode(schema, arguments, "", result.errors);

    result.valid = result.errors.empty();
    return result;
}

void ParameterValidator::checkNode(
    const json& schema, const json& value,
    const std::string& path,
    std::vector<ValidationError>& errors)
{
    if (!schema.is_object()) return;

    // 枚举：enum 是"类型 + 取值列表"的合并约束，嵌套节点同样适用
    if (schema.contains("enum") && schema["enum"].is_array()) {
        bool found = false;
        for (const auto& e : schema["enum"]) {
            if (e == value) { found = true; break; }
        }
        if (!found) {
            errors.push_back({path, "值不在允许的枚举范围内"});
        }
    }

    // 类型（只认 string 形态的 type；多类型数组如 ["string","null"] 不在此列，直接放行）
    std::string expected;
    if (schema.contains("type") && schema["type"].is_string()) {
        expected = schema["type"].get<std::string>();
    }
    if (!expected.empty()) {
        bool type_ok = false;
        if (expected == "string")       type_ok = value.is_string();
        else if (expected == "integer") type_ok = value.is_number_integer();
        else if (expected == "number")  type_ok = value.is_number();
        else if (expected == "boolean") type_ok = value.is_boolean();
        else if (expected == "array")   type_ok = value.is_array();
        else if (expected == "object")  type_ok = value.is_object();
        else                            type_ok = true;  // 未知类型名不拦，避免误伤

        if (!type_ok) {
            errors.push_back({path, "类型不匹配（期望 " + typeName(expected)
                              + "，实际 " + std::string(value.type_name()) + "）"});
            // 类型已不符即返回：对着错误类型继续取子节点只会产生一连串误报
            return;
        }
    }

    // 数组 → 逐元素按 items 递归（此前不下探，是嵌套约束形同装饰的主因之一）
    if (value.is_array() && schema.contains("items")) {
        for (size_t i = 0; i < value.size(); ++i) {
            checkNode(schema["items"], value[i],
                      path + "[" + std::to_string(i) + "]", errors);
        }
        return;
    }

    if (!value.is_object()) return;

    // 必填字段（嵌套对象同样生效）
    if (schema.contains("required") && schema["required"].is_array()) {
        for (const auto& req : schema["required"]) {
            if (!req.is_string()) continue;
            const std::string name = req.get<std::string>();
            if (!value.contains(name)) {
                errors.push_back({path.empty() ? name : path + "." + name,
                                  "缺少必填字段"});
            }
        }
    }

    // 逐属性递归；additionalProperties:false 时未知字段阻断（而非仅 warn）。
    // 此前只 spdlog::warn 且刻意忽略 errors 参数（(void)errors），LLM 拼错字段名
    // （如 charcter_id）被静默吞、浪费一轮工具调用。改为 push 错误使
    // validation.valid=false，ToolPipeline 会返回带 suggestion 的错误让 LLM 自纠。
    const json* props = (schema.contains("properties") && schema["properties"].is_object())
                        ? &schema["properties"] : nullptr;
    const bool allow_extra = !schema.contains("additionalProperties")
                          || !schema["additionalProperties"].is_boolean()
                          || schema["additionalProperties"].get<bool>();

    for (auto it = value.begin(); it != value.end(); ++it) {
        const std::string child = path.empty() ? it.key() : path + "." + it.key();
        if (props && props->contains(it.key())) {
            checkNode((*props)[it.key()], it.value(), child, errors);
        } else if (!allow_extra) {
            spdlog::warn("[ParameterValidator] 额外字段 '{}' 传入被拒绝", child);
            errors.push_back({child, "未知字段，请检查字段名拼写（该 schema 不允许额外属性）"});
        }
    }
}

std::string ParameterValidator::typeName(const std::string& schema_type) {
    if (schema_type == "string")  return "字符串";
    if (schema_type == "integer") return "整数";
    if (schema_type == "number")  return "数字";
    if (schema_type == "boolean") return "布尔";
    if (schema_type == "array")   return "数组";
    if (schema_type == "object")  return "对象";
    return schema_type;
}

} // namespace agent
