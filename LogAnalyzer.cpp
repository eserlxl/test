#include "LogAnalyzer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <map>

LogAnalyzer::LogAnalyzer() {
    // Default legacy patterns
    legacy_timestamp_regex_ = std::regex(R"(\[?(\d{4}-\d{2}-\d{2}[\sT]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?)\]?)");
    legacy_level_regex_ = std::regex(R"(\[?(DEBUG|INFO|WARNING|WARN|ERROR|ERR|CRITICAL|CRIT|FATAL)\]?)");
}

void LogAnalyzer::setParsingConfig(const ParsingConfig& config) {
    config_ = config;
    if (!config_.line_pattern.empty()) {
        try {
            strict_regex_ = std::regex(config_.line_pattern);
        } catch (const std::regex_error& e) {
            std::cerr << "Invalid regex pattern: " << e.what() << std::endl;
        }
    }
}

void LogAnalyzer::setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex) {
    legacy_timestamp_regex_ = std::regex(std::string(timestamp_regex));
    legacy_level_regex_ = std::regex(std::string(level_regex));
    
    // Clear strict config to force legacy mode
    config_.line_pattern.clear();
}

std::expected<LoadResult, std::string> LogAnalyzer::loadFileWithStats(const std::filesystem::path& filepath) {
    entries_.clear();
    LoadResult result = {0, 0};
    
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            return std::unexpected("Could not open file: " + filepath.string());
        }
        
        std::string line;
        size_t line_number = 0;
        
        while (std::getline(file, line)) {
            line_number++;
            if (line.empty()) continue;
            
            LogEntry entry = parseLogLine(line, line_number);
            
            if (config_.strict_mode && !config_.line_pattern.empty()) {
                 if (entry.timestamp.empty() && entry.message.empty()) {
                     result.error_count++;
                     if (config_.max_errors > 0 && result.error_count >= config_.max_errors) {
                         break;
                     }
                     continue;
                 }
            }
            
            entries_.push_back(std::move(entry));
            result.loaded_count++;
        }
        
    } catch (const std::exception& e) {
        return std::unexpected(e.what());
    }
    
    return result;
}

std::expected<void, std::string> LogAnalyzer::loadFile(const std::filesystem::path& filepath) {
    auto result = loadFileWithStats(filepath);
    if (!result) return std::unexpected(result.error());
    return {};
}

bool LogAnalyzer::loadLogFile(const std::string& filepath) {
    auto result = loadFile(std::filesystem::path(filepath));
    if (!result) {
        std::cerr << "Error: " << result.error() << std::endl;
        return false;
    }
    return true;
}

bool LogAnalyzer::loadLogFile(const std::filesystem::path& filepath) {
    auto result = loadFile(filepath);
    if (!result) {
        std::cerr << "Error: " << result.error() << std::endl;
        return false;
    }
    return true;
}

std::generator<LogEntry> LogAnalyzer::streamEntries(std::filesystem::path filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filepath.string());
    }
    
    std::string line;
    size_t line_number = 0;
    size_t error_count = 0;

    while (std::getline(file, line)) {
        line_number++;
        if (line.empty()) continue;
        
        LogEntry entry = parseLogLine(line, line_number);
        
        if (config_.strict_mode && !config_.line_pattern.empty()) {
             if (entry.timestamp.empty() && entry.message.empty()) {
                 error_count++;
                 if (config_.max_errors > 0 && error_count >= config_.max_errors) {
                     break;
                 }
                 continue;
             }
        }
        
        co_yield entry;
    }
}

std::generator<LogEntry> LogAnalyzer::streamFilteredEntries(std::filesystem::path filepath, FilterOptions options) {
    for (const auto& entry : streamEntries(filepath)) {
        if (matchFilter(entry, options)) {
            co_yield entry;
        }
    }
}

std::expected<LogStatistics, std::string> LogAnalyzer::analyzeStream(const std::filesystem::path& filepath) {
    LogStatistics stats;
    std::map<std::string, size_t> error_counts;
    
    try {
        for (const auto& entry : streamEntries(filepath)) {
            stats.total_entries++;
            stats.level_counts[entry.level]++;
            
            if (!stats.first_timestamp.has_value()) {
                stats.first_timestamp = entry.timestamp;
            }
            stats.last_timestamp = entry.timestamp;
            
            if (entry.level == LogLevel::ERROR) {
                error_counts[entry.message]++;
            }
        }
    } catch (const std::exception& e) {
        return std::unexpected(std::string(e.what()));
    }
    
    for (const auto& [msg, count] : error_counts) {
        stats.top_errors.push_back({msg, count});
    }
    std::sort(stats.top_errors.begin(), stats.top_errors.end(), 
        [](const auto& a, const auto& b) { return a.second > b.second; });
    if (stats.top_errors.size() > 5) stats.top_errors.resize(5);
    
    return stats;
}

std::span<const LogEntry> LogAnalyzer::getEntriesSpan() const {
    return entries_;
}

const std::vector<LogEntry>& LogAnalyzer::getEntries() const {
    return entries_;
}

LogEntry LogAnalyzer::parseLogLine(const std::string& line, size_t line_number) {
    LogEntry entry;
    entry.raw_line = line;
    
    if (!config_.line_pattern.empty()) {
        // Strict/Regex based parsing
        std::smatch match;
        if (std::regex_match(line, match, strict_regex_)) {
            // Extract fields based on indices
            if (config_.timestamp_index > 0 && static_cast<size_t>(config_.timestamp_index) < match.size()) {
                entry.timestamp = match[config_.timestamp_index].str();
                // Parse time
                std::istringstream ss(entry.timestamp);
                std::tm tm = {};
                ss >> std::get_time(&tm, config_.time_format.c_str());
                if (!ss.fail()) {
                    tm.tm_isdst = -1;
                    std::time_t tt = std::mktime(&tm);
                    if (tt != -1) {
                        entry.time_point = std::chrono::system_clock::from_time_t(tt);
                    }
                }
            }
            
            if (config_.level_index > 0 && static_cast<size_t>(config_.level_index) < match.size()) {
                entry.level = LogEntry::parseLevel(match[config_.level_index].str());
            } else {
                 entry.level = LogLevel::UNKNOWN;
            }
            
            if (config_.message_index > 0 && static_cast<size_t>(config_.message_index) < match.size()) {
                entry.message = match[config_.message_index].str();
            }
            
            if (config_.thread_id_index > 0 && static_cast<size_t>(config_.thread_id_index) < match.size()) {
                entry.thread_id = match[config_.thread_id_index].str();
            }
            
            if (config_.file_index > 0 && static_cast<size_t>(config_.file_index) < match.size()) {
                entry.source_file = match[config_.file_index].str();
            }
            
            if (config_.line_index > 0 && static_cast<size_t>(config_.line_index) < match.size()) {
                try {
                    entry.source_line = std::stoi(match[config_.line_index].str());
                } catch (...) {
                    entry.source_line = 0;
                }
            }
            
            return entry;
        } else {
            // Match failed
            if (config_.strict_mode) {
                if (config_.error_callback) {
                    config_.error_callback({line_number, line, "Regex match failed"});
                }
                return LogEntry(); 
            }
        }
    }
    
    // Legacy Heuristic Parsing
    std::smatch match;
    
    // Extract timestamp
    if (std::regex_search(line, match, legacy_timestamp_regex_)) {
        entry.timestamp = match[1].str();
        entry.parseTime(); // Use default parsing
    }
    
    // Extract log level
    if (std::regex_search(line, match, legacy_level_regex_)) {
        entry.level = LogEntry::parseLevel(match[1].str());
    } else {
        entry.level = LogLevel::UNKNOWN;
    }
    
    // Extract message
    size_t message_start = 0;
    if (entry.level != LogLevel::UNKNOWN && !match.empty()) {
        message_start = match.position() + match.length();
        // Skip separators
        while (message_start < line.length() && 
               (line[message_start] == ' ' || line[message_start] == ':' || line[message_start] == ']')) {
            message_start++;
        }
        entry.message = line.substr(message_start);
    } else {
         entry.message = line;
    }
    
    return entry;
}

bool LogAnalyzer::matchFilter(const LogEntry& entry, const FilterOptions& options) const {
    // Level filter
    if (options.level.has_value()) {
        if (entry.level < options.level.value()) return false;
    }
    
    if (!options.levels.empty()) {
        bool found = false;
        for (auto l : options.levels) {
            if (entry.level == l) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }

    // Source file filter
    if (options.source_file.has_value()) {
        if (entry.source_file != options.source_file.value()) return false;
    }
    
    // Thread ID filter
    if (options.thread_id.has_value()) {
        if (entry.thread_id != options.thread_id.value()) return false;
    }

    // Keyword filter
    if (options.keyword.has_value()) {
        bool match = false;
        if (options.case_sensitive) {
            match = (entry.message.find(options.keyword.value()) != std::string::npos);
        } else {
            // Case insensitive
            auto it = std::search(
                entry.message.begin(), entry.message.end(),
                options.keyword.value().begin(), options.keyword.value().end(),
                [](char ch1, char ch2) { 
                    return std::toupper(static_cast<unsigned char>(ch1)) == 
                           std::toupper(static_cast<unsigned char>(ch2)); 
                }
            );
            match = (it != entry.message.end());
        }
        
        if (options.invert_match ? match : !match) return false;
    }

    // Regex Message filter
    if (options.message_regex_pattern.has_value()) {
        try {
            std::regex msg_regex(options.message_regex_pattern.value());
            bool match = std::regex_search(entry.message, msg_regex);
            if (options.invert_match ? match : !match) return false;
        } catch (...) {
            // Invalid regex, treat as no match (or maybe should throw/log?)
            return false;
        }
    }

    // Start time filter
    if (options.start_time.has_value()) {
         if (entry.timestamp < options.start_time.value()) return false;
    }
    if (options.start_tp.has_value()) {
        if (entry.time_point < options.start_tp.value()) return false;
    }

    // End time filter
    if (options.end_time.has_value()) {
        if (entry.timestamp > options.end_time.value()) return false;
    }
    if (options.end_tp.has_value()) {
        if (entry.time_point > options.end_tp.value()) return false;
    }

    return true;
}

void LogAnalyzer::analyze() {
    // Legacy support: no-op as stats are computed on demand
}

LogStatistics LogAnalyzer::getStatistics() const {
    LogStatistics stats;
    stats.total_entries = entries_.size();
    if (entries_.empty()) return stats;
    
    std::map<std::string, size_t> error_counts;
    
    auto min_max_time = std::minmax_element(entries_.begin(), entries_.end(), 
        [](const LogEntry& a, const LogEntry& b) {
            return a.time_point < b.time_point;
        });
        
    if (min_max_time.first != entries_.end() && min_max_time.first->time_point.time_since_epoch().count() > 0) {
        stats.first_timestamp = min_max_time.first->timestamp; 
        stats.last_timestamp = min_max_time.second->timestamp;
        
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            min_max_time.second->time_point - min_max_time.first->time_point).count();
            
        stats.duration = std::chrono::duration_cast<std::chrono::seconds>(
            min_max_time.second->time_point - min_max_time.first->time_point);
            
        if (duration_ms > 0) {
            stats.entries_per_second = (static_cast<double>(stats.total_entries) / duration_ms) * 1000.0;
        }
    } else {
         stats.first_timestamp = entries_.front().timestamp;
         stats.last_timestamp = entries_.back().timestamp;
    }

    for (const auto& entry : entries_) {
        stats.level_counts[entry.level]++;
        
        if (entry.level == LogLevel::ERROR) {
            error_counts[entry.message]++;
        }
    }
    
    for (const auto& [msg, count] : error_counts) {
        stats.top_errors.push_back({msg, count});
    }
    
    std::sort(stats.top_errors.begin(), stats.top_errors.end(), 
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        
    if (stats.top_errors.size() > 5) {
        stats.top_errors.resize(5);
    }
    
    return stats;
}

void LogAnalyzer::writeStatistics(std::ostream& out, bool as_json) const {
    auto stats = getStatistics();
    
    if (as_json) {
        out << "{\n";
        out << "  \"total_entries\": " << stats.total_entries << ",\n";
        out << "  \"duration_seconds\": " << stats.duration.count() << ",\n";
        out << "  \"level_counts\": {\n";
        bool first = true;
        for (const auto& [level, count] : stats.level_counts) {
            if (!first) out << ",\n";
            out << "    \"" << levelToString(level) << "\": " << count;
            first = false;
        }
        out << "\n  },\n";
        out << "  \"top_errors\": [\n";
        first = true;
        for (const auto& [msg, count] : stats.top_errors) {
            if (!first) out << ",\n";
            out << "    {\"message\": \"" << msg << "\", \"count\": " << count << "}";
            first = false;
        }
        out << "\n  ]\n";
        out << "}\n";
    } else {
        out << "\n=== Log Analysis Statistics ===\n";
        out << "Total log entries: " << stats.total_entries << "\n";
        if (stats.first_timestamp && stats.last_timestamp) {
            out << "Time range: " << *stats.first_timestamp << " to " << *stats.last_timestamp << "\n";
            out << "Duration: " << stats.duration.count() << " seconds\n";
        }
        
        out << "\nEntries by log level:\n";
        out << std::left << std::setw(12) << "Level" << "Count" << "\n";
        out << std::string(20, '-') << "\n";
        for (const auto& [level, count] : stats.level_counts) {
            if (count > 0)
                out << std::left << std::setw(12) << levelToString(level) << count << "\n";
        }
        
        out << "\nTop Errors:\n";
        for (const auto& [msg, count] : stats.top_errors) {
            out << "  (" << count << ") " << msg << "\n";
        }
    }
}

void LogAnalyzer::printStatistics() const {
    writeStatistics(std::cout, false);
}

void LogAnalyzer::writeFilteredEntries(std::ostream& out, const FilterOptions& options, bool as_json) const {
    auto filtered = getFilteredEntries(options);
    
    if (as_json) {
        out << "[\n";
        bool first = true;
        for (const auto& entry : filtered) {
            if (!first) out << ",\n";
            out << entry.toJson();
            first = false;
        }
        out << "\n]\n";
    } else {
        for (const auto& entry : filtered) {
             out << "[" << entry.timestamp << "] [" << levelToString(entry.level) << "] " << entry.message << "\n";
        }
    }
}

std::vector<LogEntry> LogAnalyzer::getFilteredEntries(const FilterOptions& options) const {
    std::vector<LogEntry> result;
    result.reserve(entries_.size());

    for (const auto& entry : entries_) {
        if (matchFilter(entry, options)) {
            result.push_back(entry);
        }
    }
    return result;
}

std::string LogAnalyzer::levelToString(LogLevel level) const {
    return std::string(LogEntry::levelToString(level));
}
