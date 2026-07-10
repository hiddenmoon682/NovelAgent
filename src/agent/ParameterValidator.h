#pragma once

// 工具参数 Schema 校验器 — Phase 5.5。
//
// 在 ToolPipeline 执行工具前，根据 ToolDefinition::parameters 的 JSON Schema
// 校验 LLM 传入的 arguments。校验失败返回结构化错误而非让工具崩溃。
//
// 校验内容:
//   - required 字段是否存在
//   - 字段类型是否匹配（string/integer/boolean/array）
//   - string 枚举值是否在允许范围内
//   - additionalProperties 检测

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace agent {

// 校验错误详情。
struct ValidationError {
    std::string field;    // 字段名
    std::string reason;   // 失败原因
};

// 校验结果。
struct ValidationResult {
    bool valid = true;
    std::vector<ValidationError> errors;

    // 生成结构化错误 JSON。
    nlohmann::json toJson() const;
};

// 工具参数校验器。
class ParameterValidator {
public:
    // 根据 JSON Schema 校验参数。
    //
    // schema      工具定义的 parameters JSON Schema
    // arguments   LLM 传入的 arguments JSON
    // 校验结果（valid=true 表示通过）
    static ValidationResult validate(
        const nlohmann::json& schema,
        const nlohmann::json& arguments);

private:
    // 校验必填字段。
    static void checkRequired(
        const nlohmann::json& schema,
        const nlohmann::json& arguments,
        std::vector<ValidationError>& errors);

    // 校验字段类型。
    static void checkTypes(
        const nlohmann::json& schema,
        const nlohmann::json& arguments,
        std::vector<ValidationError>& errors);

    // 校验枚举值。
    static void checkEnums(
        const nlohmann::json& schema,
        const nlohmann::json& arguments,
        std::vector<ValidationError>& errors);

    // 检测额外字段（additionalProperties: false 时记录 warning）。
    static void checkAdditionalProperties(
        const nlohmann::json& schema,
        const nlohmann::json& arguments,
        std::vector<ValidationError>& errors);

    // Schema type → 可读名称。
    static std::string typeName(const std::string& schema_type);
};

} // namespace agent
