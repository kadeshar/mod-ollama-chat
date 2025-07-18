#ifndef MOD_OLLAMA_CHAT_UTILS_H
#define MOD_OLLAMA_CHAT_UTILS_H

#include <string>
#include <fmt/core.h>
#include "Log.h"

// Safe formatting utility for the Ollama Chat module.
// This will catch all fmt::format errors and log them.
template<typename... Args>
inline std::string SafeFormat(const std::string& templ, Args&&... args) {
    try {
        return fmt::format(templ, std::forward<Args>(args)...);
    } catch (const fmt::format_error& e) {
        LOG_ERROR("server.loading", "[Ollama Chat] Format error: {} | Template: {}", e.what(), templ);
        return "[Format Error]";
    }
}

#endif // MOD_OLLAMA_CHAT_UTILS_H
