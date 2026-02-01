#include <model/LogEntryContext.h>
#include <model/LogEntry.h>

// Definition for the thread-local context stack
thread_local std::vector<ContextFrame> current_context_stack;

namespace LogContext {
    Scope::Scope(std::map<std::string, LogValue> attributes,
                 std::unordered_set<std::string> tags) {
        current_context_stack.push_back({std::move(attributes), std::move(tags)});
    }

    Scope::~Scope() {
        if (!current_context_stack.empty()) {
            current_context_stack.pop_back();
        }
    }

    void apply(LogEntry& entry) {
        if (current_context_stack.empty()) {
            return;
        }

        // Apply attributes: inner scopes override outer ones
        for (const auto& frame : current_context_stack) {
            for (const auto& [key, value] : frame.attributes) {
                entry.withAttribute(key, value); // withAttribute will overwrite existing
            }
        }

        // Apply tags: cumulative
        for (const auto& frame : current_context_stack) {
            for (const auto& tag : frame.tags) {
                entry.withTag(tag); // withTag will add if not exists
            }
        }
    }
} // namespace LogContext
