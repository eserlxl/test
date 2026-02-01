#ifndef LOG_PARSER_H
#define LOG_PARSER_H

#include <analyzer/ParsingConfig.h>
#include <model/LogEntry.h>
#include <optional>
#include <regex> // Required for regex parsing

namespace LogAnalysis {

class LogParser {
public:
    LogParser(const ParsingConfig& config); // Constructor declaration
    std::optional<LogEntry> parse(const std::string& line, size_t line_number) const; // Parse method declaration
private:
    ParsingConfig config_;
    std::optional<std::regex> line_regex_; // To store the compiled regex pattern
};

} // namespace LogAnalysis

#endif // LOG_PARSER_H
