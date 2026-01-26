#ifndef LOG_ENTRY_H
#define LOG_ENTRY_H

#include <string>
#include <chrono>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL,
    UNKNOWN
};

struct LogEntry {
    std::string timestamp;
    LogLevel level;
    std::string message;
    std::string raw_line;
    
    LogEntry() : level(LogLevel::UNKNOWN) {}
};

LogLevel parseLogLevel(const std::string& level_str);

#endif // LOG_ENTRY_H
