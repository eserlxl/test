#ifndef LOG_ANALYZER_H
#define LOG_ANALYZER_H

#include "LogEntry.h"
#include <vector>
#include <string>
#include <map>
#include <filesystem>
#include <optional>
#include <string_view>
#include <regex>
#include <expected>
#include <span>
#include <chrono>
#include <iostream>
#include <functional>
#include <generator>
#include <future>
#include <set>

struct LogStatistics {
    size_t total_entries = 0;
    std::map<LogLevel, size_t> level_counts;
    
    // Time analysis
    std::optional<std::string> first_timestamp;
    std::optional<std::string> last_timestamp;
    std::chrono::seconds duration{0};
    
    // Frequency analysis
    double entries_per_second = 0.0;
    
    // Top errors (message, count)
    std::vector<std::pair<std::string, size_t>> top_errors;

    // Distribution: count per time bucket (e.g., per minute/hour)
    std::map<std::chrono::system_clock::time_point, size_t> timeline_distribution;
    
    // Count per thread
    std::map<std::string, size_t> thread_distribution;
};

struct ParseError {
    size_t line_number;
    std::string raw_line;
    std::string message;
};

struct ProgressInfo {
    size_t bytes_processed = 0;
    size_t total_bytes = 0;
    size_t lines_processed = 0;
};

using ProgressCallback = std::function<void(const ProgressInfo&)>;

struct ParsingConfig {
    // ... existing ...
    bool strict_mode = false; 
    
    // A regex with capture groups, e.g., "^(\S+) \[(.*?)\] (.*)$"
    std::string line_pattern; 
    
    // Group indices (1-based). 0 = unused.
    int timestamp_index = 0;
    int level_index = 0;
    int message_index = 0;
    int thread_id_index = 0;
    int file_index = 0;
    int line_index = 0;
    
    // Date format for strptime/std::get_time (e.g., "%Y-%m-%d %H:%M:%S")
    std::string time_format = "%Y-%m-%d %H:%M:%S"; 

    // New: Callback for parsing errors. If set, called on failure.
    std::function<void(const ParseError&)> error_callback = nullptr;
    
    // New: If true, parsing stops after 'max_errors' are encountered.
    size_t max_errors = 0; // 0 = unlimited
};

struct FilterOptions {
    // Existing
    std::optional<LogLevel> level;
    std::optional<std::string> keyword;
    std::optional<std::string> start_time;
    std::optional<std::string> end_time;

    // New
    bool case_sensitive = true;     // For keyword search
    bool invert_match = false;      // Invert the filter logic
    std::vector<LogLevel> levels;   // Allow selecting multiple specific levels

    // New: Regex-based message filtering
    std::optional<std::string> message_regex_pattern;
    
    // New: Precise time filtering (preferred over string)
    std::optional<std::chrono::system_clock::time_point> start_tp;
    std::optional<std::chrono::system_clock::time_point> end_tp;
    
    // New: Source file filter
    std::optional<std::string> source_file;
    
    // New: Thread ID filter
    std::optional<std::string> thread_id;

    // Attribute filtering: match if attribute exists and equals value
    std::map<std::string, LogValue> attribute_matches;
    
    // Tag filtering: match if entry has all these tags
    std::set<std::string> required_tags;
};

struct LoadResult {
    size_t loaded_count;
    size_t error_count;
};


class LogAnalyzer {
public:
    LogAnalyzer();
    
    // Configuration
    void setParsingConfig(const ParsingConfig& config);
    
    // Legacy support (updates ParsingConfig internally or sets legacy regexes)
    void setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex);

    // Modern Loading
    std::expected<void, std::string> loadFile(const std::filesystem::path& filepath);
    
    // New: Load file with stats
    std::expected<LoadResult, std::string> loadFileWithStats(
        const std::filesystem::path& filepath,
        ProgressCallback progress = nullptr
    );

    // New: Async load with optional progress callback
    std::future<LoadResult> loadFileAsync(
        std::filesystem::path filepath, 
        ProgressCallback progress = nullptr
    );

    // Legacy Loading (Keep for compatibility, implemented via loadFile)
    bool loadLogFile(const std::string& filepath);
    bool loadLogFile(const std::filesystem::path& filepath);

    // New: Stream entries from file.
    // Throws std::runtime_error if file cannot be opened.
    // Returns a generator that yields parsed LogEntry objects one by one.
    std::generator<LogEntry> streamEntries(std::filesystem::path filepath);

    // New: Stream analysis.
    // Calculates statistics by streaming the file (O(1) memory for entries).
    std::expected<LogStatistics, std::string> analyzeStream(const std::filesystem::path& filepath);
    
    // New: Filtered stream.
    // Yields only entries matching the filter.
    std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, FilterOptions options);

    // Accessors
    std::span<const LogEntry> getEntriesSpan() const;
    [[nodiscard]] const std::vector<LogEntry>& getEntries() const; // Legacy

    // Testing Support
    void addEntry(LogEntry entry);

    // Analysis
    void analyze(); // Legacy, triggers stats calculation if needed
    LogStatistics getStatistics() const;

    // Output
    void writeStatistics(std::ostream& out, bool as_json = false) const;
    void writeFilteredEntries(std::ostream& out, const FilterOptions& options, bool as_json = false) const;
    
    // Legacy Output
    void printStatistics() const;
    [[nodiscard]] std::vector<LogEntry> getFilteredEntries(const FilterOptions& options) const;

private:
    std::vector<LogEntry> entries_;
    ParsingConfig config_;
    
    // Legacy Regex caching (used if strict_mode is false and line_pattern is empty)
    std::regex legacy_timestamp_regex_;
    std::regex legacy_level_regex_;
    
    // Strict Regex
    std::regex strict_regex_;

    LogEntry parseLogLine(const std::string& line, size_t line_number = 0);
    std::string levelToString(LogLevel level) const;
    bool matchFilter(const LogEntry& entry, const FilterOptions& options) const;
};

#endif // LOG_ANALYZER_H