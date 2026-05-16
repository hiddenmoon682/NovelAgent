// NovelAgent entry point.
// Two modes:
//   1. Direct command:  novelagent -p my-novel -e "write chapter 3"
//   2. Interactive REPL: novelagent -p my-novel
//
// API keys flow: environment variable → config file → built-in default.
// Environment variables are checked first so CI/dotenv workflows work naturally.

#include "config/AppConfig.h"
#include "cli/ReplHandler.h"
#include "project/ProjectManager.h"

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv) {
    CLI::App app{"NovelAgent - AI-Powered Novel Writing Assistant"};

    std::string projectPath;
    std::string execCommand;
    std::string providerName = "deepseek";
    bool verbose = false;

    app.add_option("-p,--project", projectPath, "Project directory path");
    app.add_option("-e,--exec", execCommand, "Execute a single command and exit");
    app.add_option("--provider", providerName, "LLM provider (deepseek, kimi, claude)");
    app.add_flag("-v,--verbose", verbose, "Verbose logging");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    if (verbose) {
        spdlog::set_level(spdlog::level::debug);
    }

    // Load config from ~/.novelagent/config.json (or create default)
    AppConfig config = AppConfig::load();

    // Environment variables override config file values.
    // This lets the user keep API keys out of config files (safer for git).
    for (const auto& envProvider : {"DEEPSEEK", "KIMI", "CLAUDE"}) {
        std::string envName = std::string(envProvider) + "_API_KEY";
        const char* envKey = std::getenv(envName.c_str());
        if (envKey) {
            std::string lower = envProvider;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            config.setApiKey(lower, envKey);
        }
    }

    std::cout << "NovelAgent v0.1.0\n";
    std::cout << "Type /help for available commands.\n\n";

    if (config.getDefaultProvider()) {
        const auto* p = config.getDefaultProvider();
        std::cout << "Provider: " << config.default_provider << "\n";
        std::cout << "Model: " << p->model << "\n";
        std::cout << "Base URL: " << p->base_url << "\n";
    } else {
        std::cout << "No provider configured.\n";
        std::cout << "Set API keys via environment variables:\n";
        std::cout << "  DEEPSEEK_API_KEY, KIMI_API_KEY, CLAUDE_API_KEY\n";
        std::cout << "Or create ~/.novelagent/config.json with provider settings.\n";
    }

    if (!projectPath.empty()) {
        ProjectManager pm;
        auto project = pm.openOrCreate(projectPath);
        std::cout << "Project: " << projectPath << "\n";
    }

    if (!execCommand.empty()) {
        std::cout << "Exec mode: " << execCommand << " (coming in Phase 3)\n";
        return 0;
    }

    std::cout << "\nReady.\n";
    return 0;
}
