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
    explicit LogSource(const std::string& path, SourceType type = SourceType::FILE);

    // Get all individual log file paths to process
    std::vector<std::string> getFilePaths() const;

    SourceType getType() const { return type_; }
    const std::string& getPath() const { return path_; }

private:
    std::string path_;
    SourceType type_;
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

    // Iteration 14 Additions
    size_t top_n_results = 0; // 0 for no limit
    std::vector<std::string> group_by_fields;
    std::string sort_by_field;
    bool sort_descending = true; // true for descending, false for ascending
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

    // NEW Iteration 14: Custom parsing rules
    std::string custom_regex_pattern;
    std::string custom_timestamp_format;

    // Returns true if the configuration is valid (regex compiles, indices are within range)
    bool validate() const;
    
    // Helper to auto-map named groups from a regex to LogEntry fields
    static ParsingConfig fromRegex(std::string pattern);
    // Improved static helper
    static ParsingConfig fromRegexWithNamedGroups(std::string pattern);

    // New: Factory method for JSON log format
    static ParsingConfig jsonLogFormat();

    // NEW Iteration 8: Configuration Persistence
    // Serializes the ParsingConfig to a JSON string.
    std::string toJson() const;

    // Deserializes a ParsingConfig from a JSON string.
    // Returns std::expected<ParsingConfig, std::string> for parsing errors.
    static std::expected<ParsingConfig, std::string> fromJson(std::string_view json_str);
};

// New: String comparison options for message_regex_pattern, keyword, and new string attributes
enum class StringComparisonOp {
    EQUALS,         // Exact match (case-sensitive or insensitive based on global case_sensitive flag)
    CONTAINS,       // Substring check
    STARTS_WITH,    // Prefix check
    ENDS_WITH,      // Suffix check
    REGEX           // Regular expression match
};

// New: Numeric comparison options for numeric attributes
enum class NumericComparisonOp {
    EQUALS,
    NOT_EQUALS,
    GT,             // Greater than
    LT,             // Less than
    GTE,            // Greater than or equal
    LTE             // Less than or equal
};

// New: Structure to define a single attribute filter condition
struct AttributeFilterCondition {
    LogValue value;                         // The value to compare against
    std::optional<StringComparisonOp> string_op; // Operator for string comparisons
    std::optional<NumericComparisonOp> numeric_op; // Operator for numeric comparisons

    // Constructor for string comparisons
    // Added explicit to prevent unintended implicit conversions
    explicit AttributeFilterCondition(LogValue val, StringComparisonOp op) : value(std::move(val)), string_op(op) {}
    // Constructor for numeric comparisons
    // Added explicit to prevent unintended implicit conversions
    explicit AttributeFilterCondition(LogValue val, NumericComparisonOp op) : value(std::move(val)), numeric_op(op) {}
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

    // New: Predicate for advanced attribute filtering based on AttributeFilterCondition
    std::unique_ptr<LogPredicate> Attribute(std::string attribute_name, AttributeFilterCondition condition, bool case_sensitive); // Added case_sensitive

    // New: Predicate to check if an attribute exists
    std::unique_ptr<LogPredicate> HasAttribute(std::string attribute_name);

    // New: Predicate to check if an attribute does NOT exist
    std::unique_ptr<LogPredicate> NotHasAttribute(std::string attribute_name);

    // New: Predicate to check for absence of a tag
    std::unique_ptr<LogPredicate> NotHasTag(std::string tag);

    std::unique_ptr<LogPredicate> And(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b);
    std::unique_ptr<LogPredicate> Or(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b);
    std::unique_ptr<LogPredicate> Not(std::unique_ptr<LogPredicate> p);

    // Extension: Or predicate to allow more than two arguments (variadic template)
    template<typename... Predicates>
    std::unique_ptr<LogPredicate> Or(Predicates&&... preds);

    // NEW Iteration 8: Query Language Parsing
    // Parses a query string into a LogPredicate tree.
    // Returns an std::expected containing the predicate on success, or an error string on failure.
    std::expected<std::unique_ptr<LogPredicate>, std::string> fromQuery(std::string_view query_string);
}

// NEW Iteration 14: Retrieval Options for 'entries' and 'tail' commands
struct RetrievalOptions {
    size_t limit = 0;
    size_t tail_count = 0;
    std::string sort_by_field;
    bool sort_descending = false; // default to ascending for entries
    bool follow = true; // For tail command, true means continuous output
    std::vector<std::string> fields_to_export; // New: fields to display
};

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

    // New: For complex attribute comparisons (e.g., numeric range, string contains/starts_with/regex)
    // Key: attribute name, Value: vector of conditions (implicitly ORed for the same attribute)
    std::map<std::string, std::vector<AttributeFilterCondition>> attribute_filter_conditions;

    // New: Filtering based on the absence of tags
    std::set<std::string> excluded_tags;

    // New: Support for OR logic between sets of attribute matches
    // Each element in the vector represents an OR group. Inside each map, conditions are ANDed.
    std::vector<std::map<std::string, LogValue>> attribute_or_matches;

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
    virtual void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}) = 0;
};

class JsonExporter : public LogExporter {
    std::ostream& out_;
    bool pretty_;
public:
    explicit JsonExporter(std::ostream& out, bool pretty = false) : out_(out), pretty_(pretty) {}
    void exportStats(const LogStatistics& stats) override;
    void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}) override;
};

class CsvExporter : public LogExporter {
    std::ostream& out_;
public:
    explicit CsvExporter(std::ostream& out) : out_(out) {}
    void exportStats(const LogStatistics& stats) override;
    void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}) override;
};

class MarkdownExporter : public LogExporter {
    std::ostream& out_;
public:
    explicit MarkdownExporter(std::ostream& out) : out_(out) {}
    void exportStats(const LogStatistics& stats) override;
    void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}) override;
};

class ConsoleExporter : public LogExporter {
    std::ostream& out_;
    bool use_color_ = true;
public:
    explicit ConsoleExporter(std::ostream& out, bool use_color = true) 
        : out_(out), use_color_(use_color) {}
    void exportStats(const LogStatistics& stats) override;
    void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}) override;
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


// Forward declaration for LogAnalyzer (needed by LogAnalyzerBuilder)
class LogAnalyzer;

/**
 * @brief A fluent builder class to simplify complex LogAnalyzer setup.
 *        Allows configuring various aspects of LogAnalyzer before construction.
 */
class LogAnalyzerBuilder {
public:
    LogAnalyzerBuilder();

    // Configuration methods
    LogAnalyzerBuilder& withParsingConfig(ParsingConfig config);
    LogAnalyzerBuilder& withParsingProfile(const std::string& profileName);
    LogAnalyzerBuilder& withAnalysisConfig(AnalysisConfig config);
    LogAnalyzerBuilder& withInitialFilterOptions(FilterOptions options);
    LogAnalyzerBuilder& withFilterQuery(const std::string& query);

    // Extensibility
    LogAnalyzerBuilder& addEnricher(LogEnricher enricher); // Use LogEnricher directly
    LogAnalyzerBuilder& addAnonymizer(std::unique_ptr<LogAnonymizer> anonymizer); // Use LogAnonymizer

    // Build method
    std::unique_ptr<LogAnalyzer> build(); // Return unique_ptr to avoid move/copy issues

private:
    // Internal state for building LogAnalyzer
    ParsingConfig current_parsing_config_;
    AnalysisConfig current_analysis_config_;
    std::optional<FilterOptions> initial_filter_options_;
    std::optional<std::string> initial_filter_query_;
    std::vector<LogEnricher> enrichers_; // Use LogEnricher directly
    std::vector<std::unique_ptr<LogAnonymizer>> anonymizers_; // Use LogAnonymizer
    // ... other potential configurations
};


class LogAnalyzer {
public:
    // Type alias for clarity, accessible within LogAnalyzer context.
    // LogAnalyzerBuilder uses LogEnricher directly.
    using EnricherFunction = LogEnricher; 
    LogAnalyzer();
    // Defaulted move constructor and assignment operator to enable builder returning by value
    LogAnalyzer(LogAnalyzer&&) noexcept = default;
    LogAnalyzer& operator=(LogAnalyzer&&) noexcept = default;

    // Delete copy constructor and assignment operator
    LogAnalyzer(const LogAnalyzer&) = delete;
    LogAnalyzer& operator=(const LogAnalyzer&) = delete;
    
    // Configuration
    /**
     * @brief Sets a new parsing configuration for the analyzer.
     * @param config The new ParsingConfig to use.
     * @param reparseExisting If true, all currently loaded log entries will be re-parsed with the new config.
     *                        Note: This can be a performance-intensive operation for large datasets.
     */
    void setParsingConfig(ParsingConfig config, bool reparseExisting = false);
    // NEW Iteration 8: Analysis Configuration
    /**
     * @brief Sets a new analysis configuration for the analyzer.
     * @param config The new AnalysisConfig to use.
     * @param reanalyzeExisting If true, all statistics will be recalculated based on the new config.
     */
    void setAnalysisConfig(AnalysisConfig config, bool reanalyzeExisting = false);
    void clearAnalysisConfig();
    void setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex);

    /**
     * @brief Loads a pre-defined parsing configuration profile.
     * @param profileName The name of the parsing profile (e.g., "apache_common", "syslog_rfc5424", "json").
     * @throws std::runtime_error if the profileName is not recognized.
     */
    void loadParsingProfile(const std::string& profileName);

    /**
     * @brief Registers a custom parsing configuration profile by name.
     *        This allows users to define and reuse their own complex parsing setups.
     * @param name The name to assign to the parsing profile.
     * @param config The ParsingConfig to associate with the name.
     */
    static void registerParsingProfile(const std::string& name, ParsingConfig config);

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
    std::expected<LoadResult, std::string> loadLogSources(const std::vector<LogAnalysis::LogSource>& sources, bool recursive_global_flag = false, ProgressCallback progress = nullptr);

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

    void tailFileStream(
        const LogSource& source, 
        const FilterOptions& filter, 
        const RetrievalOptions& retrieval, 
        LogExporter& exporter, 
        std::function<void(const ParseError&)> error_callback = nullptr
    );

    /**
     * @brief Starts tailing a log file, periodically checking for new entries and processing them.
     *        Processed entries are added to the analyzer's internal store.
     *        This operation can be stopped via `stopTailing()`.
     * @param path The path to the log file to tail.
     * @param interval The interval at which to check for new file content.
     * @return A future that completes when the tailing process is stopped.
     * @note This method typically runs in a separate thread.
     */
    std::future<void> startTailing(const std::filesystem::path& path, std::chrono::milliseconds interval = std::chrono::seconds(1));

    /**
     * @brief Stops any active tailing operations started by `startTailing()`.
     */
    void stopTailing();

    // Analysis
    std::expected<LogStatistics, std::string> analyzeStream(const std::filesystem::path& filepath);

    std::map<LogValue, size_t> getAttributeFrequency(std::string_view attr_key) const;
    std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> getTimeline(std::chrono::system_clock::duration bucket_size) const;
    std::vector<LogEntry> getTrace(std::string_view trace_id) const;
    
    /**
     * @brief Calculates comprehensive statistics for log entries matching the given filter options.
     * @param options The FilterOptions to apply before calculating statistics.
     * @return A LogStats object containing statistics for the filtered entries.
     */
    LogStatistics getFilteredStatistics(const FilterOptions& options) const;

    /**
     * @brief Calculates comprehensive statistics for log entries matching the given predicate.
     * @param predicate The IPredicate to apply before calculating statistics.
     * @return A LogStats object containing statistics for the filtered entries.
     */
    LogStatistics getFilteredStatistics(const LogPredicate& predicate) const;

    /**
     * @brief Retrieves the top N most frequent log messages (or message templates).
     * @param n The number of top messages to retrieve.
     * @param useTemplates If true, uses message templates; otherwise, uses raw messages.
     * @return A vector of message/template to its frequency count, sorted by frequency (descending).
     */
    std::vector<std::pair<std::string, size_t>> getTopNMessages(size_t n, bool useTemplates = true) const;

    /**
     * @brief Retrieves the top N most frequent values for a given attribute.
     * @param attributeName The name of the attribute to analyze.
     * @param n The number of top attribute values to retrieve.
     * @return A vector of LogValue to its frequency count, sorted by frequency (descending).
     */
    std::vector<std::pair<LogValue, size_t>> getTopNAttributeValues(const std::string& attributeName, size_t n) const;

    /**
     * @brief Retrieves the top N most frequent thread IDs.
     * @param n The number of top thread IDs to retrieve.
     * @return A vector of thread ID to its frequency count, sorted by frequency (descending).
     */
    std::vector<std::pair<std::string, size_t>> getTopNThreadIds(size_t n) const;

    /**
     * @brief Retrieves the top N most frequent source files.
     * @param n The number of top source files to retrieve.
     * @return A vector of source file path to its frequency count, sorted by frequency (descending).
     */
    std::vector<std::pair<std::string, size_t>> getTopNSourceFiles(size_t n) const;
    
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
    [[nodiscard]] std::vector<LogEntry> getFilteredEntries(const FilterOptions& filter, const RetrievalOptions& retrieval) const;
    [[nodiscard]] std::vector<LogEntry> getFilteredEntries() const;

private:
    std::vector<LogEntry> entries_;
    ParsingConfig config_;
    
    std::regex legacy_timestamp_regex_;
    std::regex legacy_level_regex_;
    std::regex strict_regex_;
    std::optional<std::regex> entry_start_regex_;

    std::vector<LogEnricher> enrichers_;
    std::optional<FilterOptions> currentFilterOptions_; // New: Stores current filter for analyze/getStatistics
    std::unique_ptr<LogPredicate> currentFilterPredicate_; // NEW Iteration 8: Stores predicate from query string

    LogEntry parseLogLine(const std::string& line, size_t line_number = 0);
    std::string levelToString(LogLevel level) const;
    bool matchFilter(const LogEntry& entry, const FilterOptions& options) const; // Keep for internal use if needed
    void applyEnrichers(LogEntry& entry);
    void applyAnonymizers(LogEntry& entry);
    std::string generateMessageTemplate(std::string_view message) const;
    std::vector<LogEntry> getFilteredEntriesInternal() const; // Helper to apply currentFilterOptions_
    LogStatistics calculateStatistics(const std::vector<LogEntry>& entries) const; // NEW: Declare helper

    std::map<std::string, int> named_group_indices_;
    // Wrap shared_mutex and atomic in unique_ptr to make LogAnalyzer movable
    mutable std::unique_ptr<std::shared_mutex> rw_mutex_ptr_; 
    mutable std::optional<LogStatistics> cached_stats_; // For caching statistics
    // NEW Iteration 8: Analysis Configuration
    std::optional<AnalysisConfig> currentAnalysisConfig_; // Stores current analysis configuration
    // NEW Iteration 8: Log Anonymization
    std::vector<std::unique_ptr<LogAnonymizer>> anonymizers_; // List of active anonymizers

    // Tailing-related members
    std::thread tailing_thread_;
    std::unique_ptr<std::atomic<bool>> stop_tailing_ptr_ = std::make_unique<std::atomic<bool>>(false);
    std::filesystem::path current_tail_path_;
    std::chrono::milliseconds tail_interval_ = std::chrono::seconds(1);
};

} // namespace LogAnalysis

#endif // LOG_ANALYZER_H