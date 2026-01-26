#include "LogEntry.h"
#include <algorithm>
#include <cctype>

LogLevel parseLogLevel(const std::string& level_str) {
    std::string upper_level = level_str;
    std::transform(upper_level.begin(), upper_level.end(), upper_level.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    
    if (upper_level == "DEBUG" || upper_level == "DBG") {
        return LogLevel::DEBUG;
    } else if (upper_level == "INFO" || upper_level == "INF") {
        return LogLevel::INFO;
    } else if (upper_level == "WARNING" || upper_level == "WARN") {
        return LogLevel::WARNING;
    } else if (upper_level == "ERROR" || upper_level == "ERR") {
        return LogLevel::ERROR;
    } else if (upper_level == "CRITICAL" || upper_level == "CRIT" || upper_level == "FATAL") {
        return LogLevel::CRITICAL;
    }
    
    return LogLevel::UNKNOWN;
}
