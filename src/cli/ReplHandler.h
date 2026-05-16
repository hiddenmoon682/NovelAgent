#pragma once

// REPL (Read-Eval-Print Loop) handler.
// Phase 0: stub.
// Phase 3: full interactive loop with history, slash commands, streaming output.
//   - User input is read via std::getline (simple, portable, no extra deps).
//   - Slash commands (/help, /save, /model, etc.) are intercepted and handled locally.
//   - Everything else goes to the Agent for LLM processing.

#include <string>

class ReplHandler {
public:
    explicit ReplHandler() = default;
    void run();
};
