#include "model/LogEntrySerializer.h"
#include "model/LogEntry.h"       // Assuming LogEntry.h defines LogEntry and provides a way to access its fields.
#include "model/LogValue.h"       // Assuming LogValue.h defines LogValue and its types/accessors.
#include "model/LogFormattingOptions.h" // Assuming LogFormattingOptions is defined.
#include "model/LogSerializationFormat.h" // Assuming LogSerializationFormat enum is defined.

#include <sstream>          // For std::stringstream
#include <stdexcept>        // For std::runtime_error
#include <map>              // Assuming LogEntry uses std::map for fields.

// --- Implementations for LogEntrySerializer ---

LogEntrySerializer::LogEntrySerializer(LogSerializationFormat format, LogFormattingOptions options)
    : m_format(format), m_options(options) {}

void LogEntrySerializer::setFormat(LogSerializationFormat format) {
    m_format = format;
}

void LogEntrySerializer::setOptions(LogFormattingOptions options) {
    m_options = options;
}

std::string LogEntrySerializer::serialize(const LogEntry& entry) const {
    if (m_format == LogSerializationFormat::JSON) {
        std::stringstream ss;
        serializeJson(ss, entry);
        return ss.str();
    } else {
        // Binary serialization will be implemented in a later stage.
        throw std::runtime_error("Binary serialization not yet supported.");
    }
}

void LogEntrySerializer::serialize(std::ostream& os, const LogEntry& entry) const {
    if (m_format == LogSerializationFormat::JSON) {
        serializeJson(os, entry);
    } else {
        // Binary serialization will be implemented in a later stage.
        throw std::runtime_error("Binary serialization not yet supported.");
    }
}

void LogEntrySerializer::serializeJson(std::ostream& os, const LogEntry& entry) const {
    os << "{";
    bool first = true;

    // Helper to add comma before all but the first element
    auto add_comma = [&]() {
        if (!first) {
            os << ",";
        }
        first = false;
    };

    // Serialize standard fields if they are not empty or have meaningful values
    if (!entry.timestamp.empty()) {
        add_comma();
        os << "\"timestamp\":\"" << entry.timestamp << "\"";
    }
    if (entry.level != LogLevel::UNKNOWN) {
        add_comma();
        os << "\"level\":\"" << LogEntry::levelToString(entry.level) << "\"";
    }
    if (!entry.message.empty()) {
        add_comma();
        os << "\"message\":\"" << entry.message << "\"";
    }

    // Serialize all attributes from the attributes map
    for (const auto& [key, value] : entry.attributes) {
        add_comma();
        os << "\"" << key << "\":";

        value.visit([&os](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                os << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                os << (arg ? "true" : "false");
            } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t> || std::is_same_v<T, double>) {
                os << arg;
            } else if constexpr (std::is_same_v<T, std::string>) {
                os << "\"" << arg << "\""; // Simplified: No escaping for now
            } else {
                os << "\"<unsupported_type>\"";
            }
        });
    }
    os << "}";
}
