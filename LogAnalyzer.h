#ifndef LOG_ANALYZER_H
#define LOG_ANALYZER_H

#include "LogEntry.h"
#include <vector>
#include <string>
#include <map>

class LogAnalyzer {
public:
    LogAnalyzer();
    
    bool loadLogFile(const std::string& filepath);
    void analyze();
    void printStatistics() const;
    
private:
    std::vector<LogEntry> entries_;
    std::map<LogLevel, int> level_counts_;
    std::string first_timestamp_;
    std::string last_timestamp_;
    
    LogEntry parseLogLine(const std::string& line);
    std::string levelToString(LogLevel level) const;
};

#endif // LOG_ANALYZER_H
