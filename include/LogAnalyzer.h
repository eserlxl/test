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
#include <stdexcept> // For std::runtime_error

namespace LogAnalysis { // Consider a namespace for better organization

// Represents a collection of log files/sources
class LogSource {
public:
    enum class SourceType { FILE, DIRECTORY, STD_IN };

    // Constructor for file paths
    explicit LogSource(const std::string& path, SourceType type = SourceType::FILE, bool recursive = false);

    // Get all individual log file paths to process
    std::vector<std::string> getFilePaths() const;

    SourceType getType() const { return type_; }
    const std::string& getPath() const { return path_; }
    bool isRecursive() const { return recursive_; }

private:
    std::string path_;
    SourceType type_;
    bool recursive_;
    // Internal list of resolved file paths
    mutable std::vector<std::string> resolved_file_paths_; 
    void resolveFilePaths() const; // Helper to populate resolved_file_paths_
};

enum class OutputFormat {
    TEXT, // Human-readable, similar to current printStatistics
    JSON,
    CSV,
    MARKDOWN
    // Add more formats as needed (e.g., XML, YAML)
};

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

    // --- NEW Iteration 8 Additions ---

    // Stores templates of messages and their counts.
    // A message template could be derived by generalizing common messages
    // (e.g., "User {user_id} logged in from {ip_address}" -> "User {} logged in from {}").
    // Implementation detail: LogAnalyzer would need a way to generate these templates.
    std::map<std::string, size_t> message_template_counts;

    // Distribution of values for specified arbitrary attributes.
    // Key: attribute name (e.g., "request_id", "user_agent", "component")
    // Value: map of attribute_value -> count for that attribute.
    std::map<std::string, std::map<LogValue, size_t>> attribute_value_distributions;

    // Top N most frequent string values for specified attributes.
    // Key: attribute name
    // Value: vector of (value, count) pairs, sorted by count descending.
    std::map<std::string, std::vector<std::pair<std::string, size_t>>> top_string_attribute_occurrences;
};

// --- NEW Iteration 8: Analysis Configuration ---
struct AnalysisConfig {
    // Attributes for which to compute the full value distribution.
    std::set<std::string> attributes_for_distribution;

    // Attributes for which to compute the top N most frequent string values.
    // Key: attribute name, Value: N (e.g., 10 for top 10).
    std::map<std::string, size_t> top_N_string_attributes;

    // Flag to enable/disable message template generation and counting.
    bool enable_message_template_counts = false;

    // Future: Could include custom aggregators, anomaly detection parameters, etc.
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

    // NEW Iteration 8: Configuration Persistence
    // Serializes the ParsingConfig to a JSON string.
    std::string toJson() const;

    // Deserializes a ParsingConfig from a JSON string.
    // Returns std::expected<ParsingConfig, std::string> for parsing errors.
    static std::expected<ParsingConfig, std::string> fromJson(std::string_view json_str);
};

// Iteration 1: Composable Filtering
class LogPredicate {
public:
    virtual ~LogPredicate() = default;
    virtual bool test(const LogEntry& entry) const = 0;
    virtual std::unique_ptr<LogPredicate> clone() const = 0;
};

namespace Filters {
    std::unique_ptr<LogPredicate> Level(LogLevel l);
    std::unique_ptr<LogPredicate> Keyword(std::string k, bool case_sensitive = true);
    std::unique_ptr<LogPredicate> Regex(std::string pattern); 
    std::unique_ptr<LogPredicate> Attribute(std::string key, LogValue val);
    std::unique_ptr<LogPredicate> AttributeRange(std::string key, LogValue min, LogValue max);
    std::unique_ptr<LogPredicate> Since(std::chrono::system_clock::duration d);
    std::unique_ptr<LogPredicate> AnyKeyword(std::vector<std::string> keywords, bool case_sensitive = true);
    
    // Iteration 1 Additions
    std::unique_ptr<LogPredicate> MinLevel(LogLevel l);
    std::unique_ptr<LogPredicate> MultiLevel(std::vector<LogLevel> levels);
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

    // NEW Iteration 8: Query Language Parsing
    // Parses a query string into a LogPredicate tree.
    // Returns an std::expected containing the predicate on success, or an error string on failure.
    std::expected<std::unique_ptr<LogPredicate>, std::string> fromQuery(std::string_view query_string);
}

struct FilterOptions {
    std::optional<LogLevel> level;
    std::optional<std::string> keyword;
    std::optional<std::string> start_time;
    std::optional<std::string> end_time;

    bool case_sensitive = true;
    bool invert_match = false;
    std::vector<LogLevel> levels;

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

    // NEW Iteration 8: Configuration Persistence
    // Serializes the FilterOptions to a JSON string.
    std::string toJson() const;

    // Deserializes FilterOptions from a JSON string.
    // Returns std::expected<FilterOptions, std::string> for parsing errors.
    static std::expected<FilterOptions, std::string> fromJson(std::string_view json_str);
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

// --- NEW Iteration 8: Log Anonymization / Redaction ---
// Base interface for anonymization strategies.
class LogAnonymizer {
public:
    virtual ~LogAnonymizer() = default;
    // Applies anonymization rules to a LogEntry.
    // Returns true if the entry was modified by this anonymizer.
    virtual bool anonymize(LogEntry& entry) const = 0;
};

// Example concrete implementation: Regex-based anonymization.
// Can target specific fields or all string fields.
class RegexAnonymizer : public LogAnonymizer {
public:
    // Constructs a RegexAnonymizer.
    // `pattern`: The regular expression to match.
    // `replacement`: The string to replace matches with.
    // `fields_to_anonymize`: A set of LogEntry field names (e.g., "message", "thread_id", "custom_attr")
    //                        to apply the regex to. If empty, it applies to all string-based fields (message, file, and all string custom attributes).
    RegexAnonymizer(std::string_view pattern, std::string_view replacement, std::set<std::string> fields_to_anonymize = {});
    
    bool anonymize(LogEntry& entry) const override;
private:
    std::regex pattern_;
    std::string replacement_;
    std::set<std::string> fields_to_anonymize_;
};


class LogAnalyzer {
public:
    LogAnalyzer();
    
    // Configuration
    void setParsingConfig(const ParsingConfig& config);
    // NEW Iteration 8: Analysis Configuration
    void setAnalysisConfig(const AnalysisConfig& config);
    void clearAnalysisConfig();
    void setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex);

    // Modern Loading
    std::expected<void, std::string> loadFile(const std::filesystem::path& filepath);
    std::expected<std::pair<LoadResult, std::vector<LogEntry>>, std::string> loadFileWithStats(
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

    // New: Load from multiple LogSource objects
    std::expected<LoadResult, std::string> loadLogSources(const std::vector<LogAnalysis::LogSource>& sources, ProgressCallback progress = nullptr);

    // Legacy Loading
    bool loadLogFile(const std::string& filepath);
    bool loadLogFile(const std::filesystem::path& filepath);

    // Streaming
    std::generator<LogEntry> streamEntries(std::filesystem::path filepath);
    std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, FilterOptions options);
    std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, const LogPredicate& predicate);

    // NEW Iteration 8: Real-time / Tail-like Monitoring
    std::future<void> tailFile(
        const std::filesystem::path& filepath,
        std::function<void(LogEntry)> entry_callback,
        std::function<void(const ParseError&)> error_callback = nullptr,
        std::function<bool() > stop_predicate = nullptr
    );

    std::generator<LogEntry> tailFileStream(
        const std::filesystem::path& filepath,
        std::function<void(const ParseError&)> error_callback = nullptr
    );

    // Analysis
    std::expected<LogStatistics, std::string> analyzeStream(const std::filesystem::path& filepath);

    std::map<LogValue, size_t> getAttributeFrequency(std::string_view attr_key) const;
    std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> getTimeline(std::chrono::system_clock::duration bucket_size) const;
    std::vector<LogEntry> getTrace(std::string_view trace_id) const;
    
    // Accessors
    std::vector<LogEntry> getEntriesSpan() const;
    [[nodiscard]] std::vector<LogEntry> getEntries() const;

    // Enrichment
    void addEnricher(LogEnricher enricher);

    // NEW Iteration 8: Log Anonymization
    void addAnonymizer(std::unique_ptr<LogAnonymizer> anonymizer);
    void clearAnonymizers();

    // Testing Support
    void addEntry(LogEntry entry);

    // Analysis and Filtering
    void analyze(); // This will now apply current filters
    LogStatistics getStatistics() const; // This will now apply current filters
    LogStatistics analyzeAndGetResults(); // New: Performs analysis and returns structured results (applies current filters)

    // Set filter options for subsequent analysis/retrieval operations
    void setFilterOptions(const FilterOptions& options);
    void clearFilterOptions();
    // NEW Iteration 8: User-Friendly Query Language
    void setFilterQuery(std::string_view query_string);

    // Iteration 1: New aggregation and transformation APIs
    std::map<LogValue, size_t> getFrequencyMap(std::string_view attribute_key) const;
    void sort(std::function<bool(const LogEntry&, const LogEntry&)> cmp);
    void removeIf(const LogPredicate& predicate);
    void transform(std::function<void(LogEntry&)> transformer);

    // Output
    // Legacy Output (kept for backward compatibility, will wrap new methods)
    void writeStatistics(std::ostream& out, bool as_json = false) const;
    void writeFilteredEntries(std::ostream& out, const FilterOptions& options, bool as_json = false) const;
    
    // Iteration 1: Unified Exporter API
    void exportTo(LogExporter& exporter) const;
    void exportFilteredTo(LogExporter& exporter, const LogPredicate& predicate) const;
    void exportStatistics(LogExporter& exporter) const;

    // New: Prints the analysis results to an ostream in a specified format
    void printResults(std::ostream& os, LogAnalysis::OutputFormat format = LogAnalysis::OutputFormat::TEXT) const;
    
    // Legacy Output (will be deprecated or changed to call printResults)
    [[deprecated("Use printResults() instead for structured output control.")]]
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
    std::optional<FilterOptions> currentFilterOptions_; // New: Stores current filter for analyze/getStatistics

    LogEntry parseLogLine(const std::string& line, size_t line_number = 0);
    std::string levelToString(LogLevel level) const;
    bool matchFilter(const LogEntry& entry, const FilterOptions& options) const; // Keep for internal use if needed
    void applyEnrichers(LogEntry& entry);
    std::vector<LogEntry> getFilteredEntriesInternal() const; // Helper to apply currentFilterOptions_

    std::map<std::string, int> named_group_indices_;
    mutable std::shared_mutex rw_mutex_; // Changed to shared_mutex
    mutable std::optional<LogStatistics> cached_stats_; // For caching statistics
    // NEW Iteration 8: Analysis Configuration
    std::optional<AnalysisConfig> currentAnalysisConfig_; // Stores current analysis configuration
    // NEW Iteration 8: Log Anonymization
    std::vector<std::unique_ptr<LogAnonymizer>> anonymizers_; // List of active anonymizers
};

} // namespace LogAnalysis

#endif // LOG_ANALYZER_H