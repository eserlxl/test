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
#include <memory>
#include <thread>
#include <shared_mutex>

struct LogStatistics {
    size_t total_entries = 0;
    std::map<SeverityLevel, size_t> level_counts;
    
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

    // Callback for parsing errors. If set, called on failure.
    std::function<void(const ParseError&)> error_callback = nullptr;
    
    // If true, parsing stops after 'max_errors' are encountered.
    size_t max_errors = 0; // 0 = unlimited

    // Iteration 1 Extensions
    // Pattern that identifies the START of a new log entry.
    // If a line doesn't match this, it's considered a continuation of the previous entry.
    std::optional<std::string> entry_start_pattern;
    
    // Maximum lines to aggregate into a single message
    size_t max_continuation_lines = 100;

    // Mapping from Capture Group Name to LogEntry property
    // e.g., {{"ts", "timestamp"}, {"lvl", "level"}}
    std::map<std::string, std::string> field_mapping;

    // Iteration 1: Custom parsers
    // Map of group name to a function that converts the string match to a LogValue
    std::map<std::string, std::function<LogValue(std::string_view)>> custom_parsers;

    // Returns true if the configuration is valid (regex compiles, indices are within range)
    bool validate() const;
    
    // Helper to auto-map named groups from a regex to LogEntry fields
    static ParsingConfig fromRegex(std::string pattern);
    // Improved static helper
    static ParsingConfig fromRegexWithNamedGroups(std::string pattern);
};

// Iteration 1: Composable Filtering
class LogPredicate {
public:
    virtual ~LogPredicate() = default;
    virtual bool test(const LogEntry& entry) const = 0;
    virtual std::unique_ptr<LogPredicate> clone() const = 0;
};

// Iteration 2: Log Query Language
namespace LogQuery {
    // Compiles a string query into a predicate
    std::expected<std::unique_ptr<LogPredicate>, std::string> compile(std::string_view query);
}

// Iteration 2: Numeric Analytics
struct NumericStats {
    double min = 0.0;
    double max = 0.0;
    double avg = 0.0;
    double std_dev = 0.0;
    std::map<int, double> percentiles; // e.g., {50: 120.0, 99: 500.0}
};

namespace Filters {
    std::unique_ptr<LogPredicate> Level(SeverityLevel l);
    std::unique_ptr<LogPredicate> Keyword(std::string k, bool case_sensitive = true);
    std::unique_ptr<LogPredicate> Regex(std::string pattern); 
    std::unique_ptr<LogPredicate> Attribute(std::string key, LogValue val);
    std::unique_ptr<LogPredicate> AttributeRange(std::string key, LogValue min, LogValue max);
    std::unique_ptr<LogPredicate> Since(std::chrono::system_clock::duration d);
    std::unique_ptr<LogPredicate> AnyKeyword(std::vector<std::string> keywords, bool case_sensitive = true);
    
    // Iteration 1 Additions
    std::unique_ptr<LogPredicate> MinLevel(SeverityLevel l);
    std::unique_ptr<LogPredicate> MultiLevel(std::vector<SeverityLevel> levels);
    std::unique_ptr<LogPredicate> TimeRange(
        std::optional<std::chrono::system_clock::time_point> start,
        std::optional<std::chrono::system_clock::time_point> end
    );
    std::unique_ptr<LogPredicate> Tag(std::string tag);
    std::unique_ptr<LogPredicate> ThreadId(std::string tid);
    std::unique_ptr<LogPredicate> SourceFile(std::string file);

    std::unique_ptr<LogPredicate> And(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b);
    std::unique_ptr<LogPredicate> Or(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b);
    std::unique_ptr<LogPredicate> Not(std::unique_ptr<LogPredicate> p);
}

struct FilterOptions {
    std::optional<SeverityLevel> level;
    std::optional<std::string> keyword;
    std::optional<std::string> start_time;
    std::optional<std::string> end_time;

    bool case_sensitive = true;
    bool invert_match = false;
    std::vector<SeverityLevel> levels;

    std::optional<std::string> message_regex_pattern;
    
    std::optional<std::chrono::system_clock::time_point> start_tp;
    std::optional<std::chrono::system_clock::time_point> end_tp;
    
    std::optional<std::string> source_file;
    std::optional<std::string> thread_id;

    std::map<std::string, LogValue> attribute_matches;
    std::set<std::string> required_tags;

    std::optional<std::chrono::system_clock::duration> since;
    std::vector<std::string> any_keywords;

    // Convert legacy options to a predicate
    std::unique_ptr<LogPredicate> toPredicate() const;
};

struct LoadResult {
    size_t loaded_count;
    size_t error_count;
};

// Iteration 1: Parallel Loading
struct ParallelConfig {
    size_t thread_count = std::thread::hardware_concurrency();
    size_t chunk_size_mb = 64;
    ProgressCallback progress = nullptr; // Added progress support
};

// Iteration 1: Enrichment
using LogEnricher = std::function<void(LogEntry&)>;

// Iteration 1: Exporters
class LogExporter {
public:
    virtual ~LogExporter() = default;
    virtual void exportStats(const LogStatistics& stats) = 0;
    virtual void exportEntries(std::span<const LogEntry> entries) = 0;
};

class JsonExporter : public LogExporter {
    std::ostream& out_;
    bool pretty_;
public:
    explicit JsonExporter(std::ostream& out, bool pretty = false) : out_(out), pretty_(pretty) {}
    void exportStats(const LogStatistics& stats) override;
    void exportEntries(std::span<const LogEntry> entries) override;
};

class CsvExporter : public LogExporter {
    std::ostream& out_;
public:
    explicit CsvExporter(std::ostream& out) : out_(out) {}
    void exportStats(const LogStatistics& stats) override;
    void exportEntries(std::span<const LogEntry> entries) override;
};

class MarkdownExporter : public LogExporter {
    std::ostream& out_;
public:
    explicit MarkdownExporter(std::ostream& out) : out_(out) {}
    void exportStats(const LogStatistics& stats) override;
    void exportEntries(std::span<const LogEntry> entries) override;
};

class ConsoleExporter : public LogExporter {
    std::ostream& out_;
    bool use_color_ = true;
public:
    explicit ConsoleExporter(std::ostream& out, bool use_color = true) 
        : out_(out), use_color_(use_color) {}
    void exportStats(const LogStatistics& stats) override;
    void exportEntries(std::span<const LogEntry> entries) override;
};


class LogAnalyzer {
public:
    enum class IndexType {
        Timestamp,
        Level,
        Attribute
    };

    LogAnalyzer();
    
    // Configuration
    void setParsingConfig(const ParsingConfig& config);
    void setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex);

    // Modern Loading
    std::expected<void, std::string> loadFile(const std::filesystem::path& filepath);
    std::expected<LoadResult, std::string> loadFileWithStats(
        const std::filesystem::path& filepath,
        ProgressCallback progress = nullptr
    );

    std::future<LoadResult> loadFileAsync(
        std::filesystem::path filepath, 
        ProgressCallback progress = nullptr
    );

    // Iteration 1: Parallel load
    std::future<LoadResult> loadParallel(
        std::filesystem::path path, 
        ParallelConfig config = {}
    );

    // Iteration 2: Binary Serialization
    std::expected<void, std::string> saveState(const std::filesystem::path& path) const;
    std::expected<void, std::string> loadState(const std::filesystem::path& path);

    // Legacy Loading
    bool loadLogFile(const std::string& filepath);
    bool loadLogFile(const std::filesystem::path& filepath);

    // Streaming
    std::generator<LogEntry> streamEntries(std::filesystem::path filepath);
    std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, FilterOptions options);
    std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, const LogPredicate& predicate);

    // Iteration 2: Real-time Tailing
    // yield entries as they are written to the file
    // blocks waiting for new data until stop_token is cancelled (conceptually, generator handles cancellation via iterator destruction)
    std::generator<LogEntry> tailFile(
        std::filesystem::path filepath, 
        std::chrono::milliseconds polling_interval = std::chrono::milliseconds(100)
    );

    // Analysis
    std::expected<LogStatistics, std::string> analyzeStream(const std::filesystem::path& filepath);

    // Iteration 2: Advanced Analytics
    std::expected<NumericStats, std::string> analyzeMetric(
        std::string_view attribute_key, 
        const std::vector<int>& percentiles = {50, 90, 95, 99}
    ) const;

    std::map<LogValue, size_t> getAttributeFrequency(std::string_view attr_key) const;
    std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> getTimeline(std::chrono::system_clock::duration bucket_size) const;
    std::vector<LogEntry> getTrace(std::string_view trace_id) const;
    
    // Accessors
    std::vector<LogEntry> getEntriesSpan() const;
    [[nodiscard]] std::vector<LogEntry> getEntries() const;

    // Enrichment
    void addEnricher(LogEnricher enricher);

    // Testing Support
    void addEntry(LogEntry entry);

    // Analysis
    void analyze();
    LogStatistics getStatistics() const;

    // Iteration 1: New aggregation and transformation APIs
    std::map<LogValue, size_t> getFrequencyMap(std::string_view attribute_key) const;
    void sort(std::function<bool(const LogEntry&, const LogEntry&)> cmp);
    void removeIf(const LogPredicate& predicate);
    void transform(std::function<void(LogEntry&)> transformer);

    // Iteration 2: LQL and Indexing
    // Convenience wrapper using LQL
    std::vector<LogEntry> query(std::string_view query_str) const;
    
    // Builds an index for a specific field/attribute
    void createIndex(IndexType type, const std::string& attribute_name = "");

    // Output
    // Legacy Output (kept for backward compatibility, will wrap new methods)
    void writeStatistics(std::ostream& out, bool as_json = false) const;
    void writeFilteredEntries(std::ostream& out, const FilterOptions& options, bool as_json = false) const;
    
    // Iteration 1: Unified Exporter API
    void exportTo(LogExporter& exporter) const;
    void exportFilteredTo(LogExporter& exporter, const LogPredicate& predicate) const;
    void exportStatistics(LogExporter& exporter) const;
    
    // Legacy Output
    void printStatistics() const;
    [[nodiscard]] std::vector<LogEntry> getFilteredEntries(const FilterOptions& options) const;
    [[nodiscard]] std::vector<LogEntry> getFilteredEntries(const LogPredicate& predicate) const;

private:
    std::vector<LogEntry> entries_;
    ParsingConfig config_;
    
    std::regex legacy_timestamp_regex_;
    std::regex legacy_level_regex_;
    std::regex strict_regex_;
    std::optional<std::regex> entry_start_regex_;

    std::vector<LogEnricher> enrichers_;

    LogEntry parseLogLine(const std::string& line, size_t line_number = 0);
    std::string levelToString(SeverityLevel level) const;
    bool matchFilter(const LogEntry& entry, const FilterOptions& options) const;
    void applyEnrichers(LogEntry& entry);

    std::map<std::string, int> named_group_indices_;
    mutable std::shared_mutex rw_mutex_; // Changed to shared_mutex
    mutable std::optional<LogStatistics> cached_stats_; // For caching statistics

    // Iteration 2: Indices
    mutable std::vector<size_t> timestamp_index_; // Indices sorted by time
    mutable std::map<SeverityLevel, std::vector<size_t>> level_index_;
    mutable std::map<std::string, std::map<LogValue, std::vector<size_t>>> attribute_indices_;
    
    // Helper to rebuild indices (if they exist)
    void rebuildIndices();
};

#endif // LOG_ANALYZER_H