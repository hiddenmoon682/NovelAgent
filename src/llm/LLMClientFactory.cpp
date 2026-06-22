#include "llm/LLMClientFactory.h"
#include "llm/LLMClient.h"

namespace llm {

LLMClientFactory::LLMClientFactory(ProviderConfig config)
    : config_(std::move(config))
{}

std::unique_ptr<ILLMClient> LLMClientFactory::create() const
{
    return std::make_unique<LLMClient>(config_);
}

} // namespace llm
