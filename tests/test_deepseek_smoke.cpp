#include "config/AppConfig.h"
#include "llm/LLMClient.h"
#include <iostream>

int main() {
    // 加载配置
    auto config = AppConfig::load();
    auto* provider = config.getProvider("deepseek");
    if (!provider) {
        std::cerr << "错误: 未找到 deepseek 配置\n";
        return 1;
    }

    std::cout << "Provider: " << provider->name << "\n";
    std::cout << "Model: " << provider->model << "\n";
    std::cout << "Base URL: " << provider->base_url << "\n\n";

    llm::LLMClient client(*provider);

    // 流式调用
    std::cout << "=== 流式 chat() 测试 ===\n";
    std::cout << "发送: '用一句话介绍你自己'\n\n回复: ";

    llm::StreamCallbacks cb;
    cb.on_content = [](const std::string& delta) {
        std::cout << delta << std::flush;
    };

    try {
        auto response = client.chat(
            {{llm::MessageRole::User, "用一句话介绍你自己"}},
            {},
            "你是一个有帮助的AI助手。",
            cb
        );
        std::cout << "\n\n--- 响应摘要 ---\n";
        std::cout << "Model: " << response.model << "\n";
        std::cout << "Prompt tokens: " << response.prompt_tokens << "\n";
        std::cout << "Completion tokens: " << response.completion_tokens << "\n";
        std::cout << "Total tokens: " << response.total_tokens << "\n";
        std::cout << "Finish reason: " << response.finish_reason << "\n";
    } catch (const std::exception& e) {
        std::cerr << "\n错误: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
