#pragma once

#include "agent/IIndexService.h"

#include <memory>

struct Project;
namespace retrieval {
class IVectorStore;
class IEmbeddingGenerator;
}

namespace agent {

class ProjectIndexService : public IIndexService {
public:
    ProjectIndexService(std::shared_ptr<Project> project,
                        retrieval::IVectorStore& vs,
                        retrieval::IEmbeddingGenerator& eg);

    IndexResult indexAll(
        std::function<void(const std::string&)> progress = nullptr) override;

private:
    std::shared_ptr<Project> project_;
    retrieval::IVectorStore& vector_store_;
    retrieval::IEmbeddingGenerator& embedding_gen_;
};

} // namespace agent
