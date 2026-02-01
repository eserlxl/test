#include <model/LogEntryContext.h>
#include <model/LogEntry.h>
#include <algorithm> // For std::find_if

// Definition for the thread-local context stack
thread_local std::vector<ContextFrame> current_context_stack;

namespace LogContext {

    std::optional<LogValue> getCurrentAttribute(const std::string& key) {
        // Iterate from the most recent context frame downwards
        for (auto it = current_context_stack.rbegin(); it != current_context_stack.rend(); ++it) {
            if (it->attributes.count(key)) {
                return it->attributes.at(key);
            }
        }
        return std::nullopt;
    }

    bool hasCurrentTag(const std::string& tag) {
        // Iterate through all context frames to find the tag
        for (const auto& frame : current_context_stack) {
            if (frame.tags.count(tag)) {
                return true;
            }
        }
        return false;
    }

    std::map<std::string, LogValue> getCurrentAttributes() {
        std::map<std::string, LogValue> combinedAttributes;
        // Iterate from the oldest context frame upwards to ensure later frames override earlier ones
        for (const auto& frame : current_context_stack) {
            for (const auto& [key, value] : frame.attributes) {
                combinedAttributes[key] = value;
            }
        }
        return combinedAttributes;
    }

    std::unordered_set<std::string> getCurrentTags() {
        std::unordered_set<std::string> combinedTags;
        // Iterate through all context frames to accumulate tags
        for (const auto& frame : current_context_stack) {
            combinedTags.insert(frame.tags.begin(), frame.tags.end());
        }
        return combinedTags;
    }

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
        // Use getCurrentAttributes to get the correctly resolved attributes
        for (const auto& [key, value] : getCurrentAttributes()) {
            entry.withAttribute(key, value);
        }

        // Apply tags: cumulative
        // Use getCurrentTags to get the correctly resolved tags
        for (const auto& tag : getCurrentTags()) {
            entry.withTag(tag);
        }
    }
} // namespace LogContext
