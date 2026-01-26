#include "LogAnalyzer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <regex>

LogAnalyzer::LogAnalyzer() {
    level_counts_[LogLevel::DEBUG] = 0;
    level_counts_[LogLevel::INFO] = 0;
    level_counts_[LogLevel::WARNING] = 0;
    level_counts_[LogLevel::ERROR] = 0;
    level_counts_[LogLevel::CRITICAL] = 0;
    level_counts_[LogLevel::UNKNOWN] = 0;
}

bool LogAnalyzer::loadLogFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file: " << filepath << std::endl;
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            LogEntry entry = parseLogLine(line);
            entries_.push_back(entry);
        }
    }
    
    file.close();
    return true;
}

LogEntry LogAnalyzer::parseLogLine(const std::string& line) {
    LogEntry entry;
    entry.raw_line = line;
    
    // Try to parse common log formats
    // Format 1: [TIMESTAMP] [LEVEL] MESSAGE
    // Format 2: TIMESTAMP LEVEL MESSAGE
    // Format 3: LEVEL: MESSAGE
    
    std::regex timestamp_pattern(R"(\[?(\d{4}-\d{2}-\d{2}[\sT]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?)\]?)");
    std::regex level_pattern(R"(\[?(DEBUG|INFO|WARNING|WARN|ERROR|ERR|CRITICAL|CRIT|FATAL)\]?)");
    
    std::smatch match;
    
    // Extract timestamp
    if (std::regex_search(line, match, timestamp_pattern)) {
        entry.timestamp = match[1].str();
    }
    
    // Extract log level
    if (std::regex_search(line, match, level_pattern)) {
        entry.level = parseLogLevel(match[1].str());
    } else {
        entry.level = LogLevel::UNKNOWN;
    }
    
    // Extract message (everything after timestamp and level)
    size_t message_start = 0;
    if (match.position() + match.length() < line.length()) {
        message_start = match.position() + match.length();
        // Skip common separators
        while (message_start < line.length() && 
               (line[message_start] == ' ' || line[message_start] == ':' || line[message_start] == ']')) {
            message_start++;
        }
    }
    entry.message = line.substr(message_start);
    
    return entry;
}

void LogAnalyzer::analyze() {
    if (entries_.empty()) {
        return;
    }
    
    // Count by level
    for (const auto& entry : entries_) {
        level_counts_[entry.level]++;
    }
    
    // Find first and last timestamps
    first_timestamp_ = entries_[0].timestamp;
    last_timestamp_ = entries_[0].timestamp;
    
    for (const auto& entry : entries_) {
        if (!entry.timestamp.empty()) {
            if (entry.timestamp < first_timestamp_ || first_timestamp_.empty()) {
                first_timestamp_ = entry.timestamp;
            }
            if (entry.timestamp > last_timestamp_ || last_timestamp_.empty()) {
                last_timestamp_ = entry.timestamp;
            }
        }
    }
}

void LogAnalyzer::printStatistics() const {
    std::cout << "\n=== Log Analysis Statistics ===" << std::endl;
    std::cout << "Total log entries: " << entries_.size() << std::endl;
    
    if (!first_timestamp_.empty() && !last_timestamp_.empty()) {
        std::cout << "Time range: " << first_timestamp_ 
                  << " to " << last_timestamp_ << std::endl;
    }
    
    std::cout << "\nEntries by log level:" << std::endl;
    std::cout << std::left << std::setw(12) << "Level" << "Count" << std::endl;
    std::cout << std::string(20, '-') << std::endl;
    
    for (const auto& pair : level_counts_) {
        if (pair.second > 0) {
            std::cout << std::left << std::setw(12) << levelToString(pair.first)
                      << pair.second << std::endl;
        }
    }
    
    // Calculate percentages
    if (!entries_.empty()) {
        std::cout << "\nPercentages:" << std::endl;
        for (const auto& pair : level_counts_) {
            if (pair.second > 0) {
                double percentage = (static_cast<double>(pair.second) / entries_.size()) * 100.0;
                std::cout << std::left << std::setw(12) << levelToString(pair.first)
                          << std::fixed << std::setprecision(2) << percentage << "%" << std::endl;
            }
        }
    }
    
    // Show sample error entries
    std::cout << "\nSample ERROR entries:" << std::endl;
    int error_count = 0;
    for (const auto& entry : entries_) {
        if (entry.level == LogLevel::ERROR && error_count < 5) {
            std::cout << "  - " << entry.message.substr(0, 100) 
                      << (entry.message.length() > 100 ? "..." : "") << std::endl;
            error_count++;
        }
    }
    
    if (error_count == 0) {
        std::cout << "  (No ERROR entries found)" << std::endl;
    }
}

std::string LogAnalyzer::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        case LogLevel::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}
