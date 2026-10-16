#pragma once

// JSON Schema 构建辅助 — 减少工具参数定义的样板代码。
//
// 每个工具注册时都需要编写 JSON Schema 描述参数，
// 手写嵌套 JSON 对象冗长且易出错。本模块提供类型安全的 builder 函数。
//
// 使用示例：
//   auto schema = schema::object({
//       {"chapter_id", schema::stringProp("章节 ID")},
//       {"content",   schema::stringProp("章节内容")},
//       {"word_count", schema::integerProp("字数统计")}
//   }, {"chapter_id", "content"});

#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace utils::schema {

// 构造 object 类型的 JSON Schema 根节点。
// properties  属性名 → 属性 schema 的键值对列表（保持插入顺序）
// required    必填字段名列表（可选）
inline nlohmann::json object(
    std::vector<std::pair<std::string, nlohmann::json>> properties,
    std::vector<std::string> required = {},
    bool allowExtra = false)
{
    nlohmann::json props = nlohmann::json::object();
    for (auto& [name, schema] : properties) {
        props[name] = std::move(schema);
    }

    nlohmann::json result;
    result["type"] = "object";
    result["properties"] = std::move(props);
    if (!required.empty()) {
        result["required"] = required;
    }
    // 默认禁用额外属性（安全），允许调用方覆盖（兼容某些 LLM）
    result["additionalProperties"] = allowExtra;
    return result;
}

// string 类型属性。
inline nlohmann::json stringProp(std::string description) {
    return {
        {"type", "string"},
        {"description", std::move(description)}
    };
}

// string 枚举属性（限定可选值列表）。
inline nlohmann::json stringEnum(std::string description,
                                  std::vector<std::string> values) {
    return {
        {"type", "string"},
        {"description", std::move(description)},
        {"enum", std::move(values)}
    };
}

// integer 类型属性。
inline nlohmann::json integerProp(std::string description) {
    return {
        {"type", "integer"},
        {"description", std::move(description)}
    };
}

// boolean 类型属性。
inline nlohmann::json booleanProp(std::string description) {
    return {
        {"type", "boolean"},
        {"description", std::move(description)}
    };
}

// number 类型属性（浮点数）。
inline nlohmann::json numberProp(std::string description) {
    return {
        {"type", "number"},
        {"description", std::move(description)}
    };
}

// array 类型属性（元素类型为 string）。
inline nlohmann::json stringArrayProp(std::string description) {
    return {
        {"type", "array"},
        {"items", {{"type", "string"}}},
        {"description", std::move(description)}
    };
}

// array 类型属性（元素类型为 object，元素结构由 itemSchema 给出）。
//
// 为什么需要它：本项目存在"对象数组"形态的参数（章节场景列表、角色关系列表），
// 数组元素是结构体而非字符串。此前按元素本意只能借用 stringArrayProp 声明成
// 字符串数组、再用中文描述补救，导致 schema 与实际接受格式不一致——模型照
// schema 提交字符串数组时会被实现逐项跳过（见各工具实现里的 is_object 判断）。
inline nlohmann::json objectArrayProp(std::string description, nlohmann::json itemSchema) {
    return {
        {"type", "array"},
        {"items", std::move(itemSchema)},
        {"description", std::move(description)}
    };
}

} // namespace utils::schema
