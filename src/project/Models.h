#pragma once

// Central data model header. In Phase 1 this will grow to include:
//   Chapter, Character, Setting, Outline, Conversation
//   with full nlohmann serialization macros.
// Phase 0 keeps just the Project stub so ProjectManager compiles.

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct Project {
    std::string title;
    std::string description;
    std::string genre;
    std::string path; // absolute path to the project directory on disk
};
