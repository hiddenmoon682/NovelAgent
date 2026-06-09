/// ParameterValidator 实现。

#include "agent/ParameterValidator.h"

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

    checkRequired(schema, arguments, result.errors);
    checkTypes(schema, arguments, result.errors);
    checkEnums(schema, arguments, result.errors);
    checkAdditionalProperties(schema, arguments, result.errors);

    result.valid = result.errors.empty();
    return result;
}

void ParameterValidator::checkRequired(
    const json& schema, const json& arguments,
    std::vector<ValidationError>& errors)
{
    if (!schema.contains("required") || !schema["required"].is_array()) return;

    for (const auto& req : schema["required"]) {
        std::string field = req.get<std::string>();
        if (!arguments.contains(field)) {
            errors.push_back({field, "缺少必填字段"});
        }
    }
}

void ParameterValidator::checkTypes(
    const json& schema, const json& arguments,
    std::vector<ValidationError>& errors)
{
    if (!schema.contains("properties") || !schema["properties"].is_object()) return;

    for (auto it = schema["properties"].begin();
         it != schema["properties"].end(); ++it) {
        const std::string& name = it.key();
        const auto& prop = it.value();

        if (!arguments.contains(name)) continue;
        if (!prop.contains("type")) continue;

        std::string expected = prop["type"].get<std::string>();
        const auto& value = arguments[name];
        bool type_ok = false;

        if (expected == "string")  type_ok = value.is_string();
        else if (expected == "integer") type_ok = value.is_number_integer();
        else if (expected == "number")  type_ok = value.is_number();
        else if (expected == "boolean") type_ok = value.is_boolean();
        else if (expected == "array")   type_ok = value.is_array();
        else if (expected == "object")  type_ok = value.is_object();

        if (!type_ok) {
            errors.push_back({name, "类型不匹配（期望 " + typeName(expected)
                              + "，实际 " + std::string(value.type_name()) + "）"});
        }
    }
}

void ParameterValidator::checkEnums(
    const json& schema, const json& arguments,
    std::vector<ValidationError>& errors)
{
    if (!schema.contains("properties") || !schema["properties"].is_object()) return;

    for (auto it = schema["properties"].begin();
         it != schema["properties"].end(); ++it) {
        const std::string& name = it.key();
        const auto& prop = it.value();
        if (!prop.contains("enum") || !prop["enum"].is_array()) continue;
        if (!arguments.contains(name)) continue;

        const auto& value = arguments[name];
        bool found = false;
        for (const auto& e : prop["enum"]) {
            if (e == value) { found = true; break; }
        }
        if (!found) {
            errors.push_back({name, "值不在允许的枚举范围内"});
        }
    }
}

void ParameterValidator::checkAdditionalProperties(
    const json& schema, const json& arguments,
    std::vector<ValidationError>& errors)
{
    bool allow_extra = true;
    if (schema.contains("additionalProperties")) {
        if (schema["additionalProperties"].is_boolean()) {
            allow_extra = schema["additionalProperties"].get<bool>();
        }
    }

    if (allow_extra) return;
    if (!schema.contains("properties")) return;

    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        if (!schema["properties"].contains(it.key())) {
            spdlog::warn("[ParameterValidator] 额外字段 '{}' 传入但被忽略", it.key());
            // 不阻断执行，仅记录 warning
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
