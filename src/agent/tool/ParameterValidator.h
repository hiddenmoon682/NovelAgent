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
// 以上四类检查**递归**进入 array.items 与嵌套 object.properties：
// 嵌套不校验时，schema 里的嵌套约束形同装饰，模型传错嵌套类型不会被拦下。

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
    // 递归校验单个节点：required / 类型 / 枚举 / additionalProperties 四类检查
    // 都在这里完成，并对 array.items 与嵌套 object.properties 继续下探。
    //
    // path 是出错时写入 ValidationError::field 的字段路径前缀：顶层为空（错误字段
    // 就是裸字段名，如 chapter_id），嵌套时逐级拼成 scenes[0].title 形式，让模型
    // 能直接定位到是第几个元素的哪个字段不合规。
    static void checkNode(
        const nlohmann::json& schema,
        const nlohmann::json& value,
        const std::string& path,
        std::vector<ValidationError>& errors);

    // Schema type → 可读名称。
    static std::string typeName(const std::string& schema_type);
};

} // namespace agent
