#include <analyzer/LogParser.h>
#include <util/LogTimeUtil.h>

namespace LogAnalysis {

LogParser::LogParser(const ParsingConfig& config) : config_(config) {
    if (!config_.custom_regex_pattern.empty()) {
        try {
            line_regex_ = std::regex(config_.custom_regex_pattern);
        } catch (const std::regex_error& e) {
            // Handle regex compilation error, maybe log it or throw an exception
            // For now, we'll just leave line_regex_ as std::nullopt
        }
    }
}

std::optional<LogEntry> LogParser::parse(const std::string& line, size_t line_number) const {
    if (!line_regex_) {
        // No regex pattern, so we can't parse structured data.
        // Return a basic entry.
        LogEntry entry;
        entry.raw_line = line;
        entry.message = line;
        entry.level = LogLevel::INFO;
        return entry;
    }

    std::smatch matches;
    if (std::regex_match(line, matches, *line_regex_)) {
        LogEntry entry;
        entry.raw_line = line;
        entry.source_line = line_number;
        
        if (config_.timestamp_index > 0 && config_.timestamp_index < matches.size()) {
            entry.timestamp = matches[config_.timestamp_index].str();
            LogTimeUtil::ParseOptions parse_opts;
            auto parsed_time = LogTimeUtil::parseTimestamp(entry.timestamp, config_.time_format, parse_opts);
            if (parsed_time.has_value()) {
                entry.time_point = parsed_time.value();
            }
        }

        if (config_.level_index > 0 && config_.level_index < matches.size()) {
            entry.level = LogEntry::parseLevel(matches[config_.level_index].str());
        }

        if (config_.message_index > 0 && config_.message_index < matches.size()) {
            entry.message = matches[config_.message_index].str();
        }

        if (config_.thread_id_index > 0 && config_.thread_id_index < matches.size()) {
            entry.thread_id = matches[config_.thread_id_index].str();
        }

        if (config_.file_index > 0 && config_.file_index < matches.size()) {
            entry.source_file = matches[config_.file_index].str();
        }

        // line_index in ParsingConfig might be used to parse source line if present in log
        // The line_number argument is the physical line number from the file reader.
        if (config_.line_index > 0 && config_.line_index < matches.size()) {
             try {
                 entry.source_line = std::stoi(matches[config_.line_index].str());
             } catch(...) { /* ignore conversion errors */ }
        }

        return entry;
    }

    if (config_.strict_mode) {
        return std::nullopt;
    }
    
    // Non-strict mode, return a basic entry
    LogEntry entry;
    entry.raw_line = line;
    entry.message = line;
    entry.level = LogLevel::UNKNOWN;
    entry.source_line = line_number;
    return entry;
}

} // namespace LogAnalysis
