#pragma once

// ProjectManager handles project lifecycle: create, open, validate.
// Phase 0: minimal stub (just creates directory if missing).
// Phase 1: full novel.json loading, project metadata, listing projects.

#include "Models.h"
#include <string>

class ProjectManager {
public:
    // If the directory exists, open it. Otherwise create it empty.
    // Full novel.json initialization comes in Phase 1.
    Project openOrCreate(const std::string& path);

    bool isValid(const std::string& path) const;
};
