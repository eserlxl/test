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
#include <stdexcept>

namespace LogAnalysis {

    enum class FilterLogic { AND, OR };

    struct FieldFilter {
        std::string fieldName;
        std::string op;
        std::string value;
    };

    struct AggregateFunction {
        std::string fieldName;
        std::string function;
    };

    enum class OutputFormat {
        TEXT, JSON, CSV, MARKDOWN
    };

    enum class LogStorageStrategy {
        DEFAULT_COPY_STRINGS
    };

    struct LogSource {
        enum class SourceType { FILE, DIRECTORY, URL, STD_IN, S3 };
        LogSource() : type_(SourceType::FILE) {} // Default constructor added
        LogSource(std::string_view path_or_url, SourceType type = SourceType::FILE, std::string_view url = "")
            : path_(path_or_url), type_(type), url_(url) {}
        
        SourceType getType() const { return type_; }
        std::string getPath() const { return path_; }
        std::string getUrl() const { return url_; }
        
        std::vector<std::string> getFilePaths() const {
             if (type_ == SourceType::FILE) return {path_};
             if (type_ == SourceType::DIRECTORY) return {path_};
             return {};
        }
        void resolveFilePaths() const {}; 

        bool operator<(const LogSource& other) const {
            if (type_ != other.type_) return type_ < other.type_;
            if (path_ != other.path_) return path_ < other.path_;
            return url_ < other.url_;
        }
        bool operator==(const LogSource& other) const {
            return type_ == other.type_ && path_ == other.path_ && url_ == other.url_;
        }

    private:
        std::string path_;
        SourceType type_;
        std::string url_;
        std::vector<std::string> resolved_file_paths_; 
    };

    using GroupKey = std::variant<LogValue, std::vector<LogValue>>;

    struct GroupStatistics {
        size_t total_entries = 0;
        std::map<LogLevel, size_t> level_counts;
        std::optional<std::string> first_timestamp;
        std::optional<std::string> last_timestamp;
        std::chrono::seconds duration{0};
        double entries_per_second = 0.0;
    };

    using GroupedAnalysisResults = std::map<GroupKey, GroupStatistics>;

    class LogAnalyzer;
    struct LogStatistics;
    class ILogInputStream; // Forward declaration
    class ILogDataStore; // Forward declaration

    struct AnomalyDetectionConfig {
        std::chrono::system_clock::duration bucket_size = std::chrono::minutes(5);
        double sensitivity = 2.0;
        bool detect_spikes = true;
        bool detect_drops = true;
        std::set<LogLevel> levels_to_monitor = {LogLevel::ERROR, LogLevel::CRITICAL};
        std::string attribute_for_grouping;
    };

    struct AnomalyReportEntry {
        std::chrono::system_clock::time_point timestamp;
        size_t observed_count;
        double expected_count;
        double deviation;
        std::string description;
        std::optional<LogValue> group_key;
    };

    struct CorrelationConfig {
        std::vector<std::string> attributes_to_correlate;
        std::chrono::system_clock::duration time_window = std::chrono::seconds(1);
        size_t min_occurrences = 10;
    };

    struct CorrelationResult {
        std::map<std::string, LogValue> pattern_A;
        std::map<std::string, LogValue> pattern_B;
        double correlation_score;
        size_t co_occurrence_count;
        size_t occurrences_A;
        size_t occurrences_B;
        std::string description;
    };

    struct SessionConfig {
        std::string session_id_attribute;
        std::optional<std::chrono::system_clock::duration> session_timeout;
        std::vector<std::string> session_start_patterns;
        std::vector<std::string> session_end_patterns;
    };

    class ITemplatingEngine {
    public:
        virtual ~ITemplatingEngine() = default;
        virtual std::string render(std::string_view template_str, const LogEntry& entry) const = 0;
        virtual std::string renderStats(std::string_view template_str, const LogStatistics& stats) const = 0;
    };

    struct LogStatistics {
        size_t total_entries = 0;
        std::map<LogLevel, size_t> level_counts;
        std::optional<std::string> first_timestamp;
        std::optional<std::string> last_timestamp;
        std::chrono::seconds duration{0};
        double entries_per_second = 0.0;
        std::vector<std::pair<std::string, size_t>> top_errors;
        std::map<std::chrono::system_clock::time_point, size_t> timeline_distribution;
        std::map<std::string, size_t> thread_distribution;
        std::map<std::string, size_t> message_template_counts;
        std::map<std::string, std::map<LogValue, size_t>> attribute_value_distributions;
        std::map<std::string, std::vector<std::pair<std::string, size_t>>> top_string_attribute_occurrences;
    };

    struct LogSession {
        std::string session_id_value;
        std::chrono::system_clock::time_point start_time;
        std::chrono::system_clock::time_point end_time;
        std::vector<LogEntry> entries;
        LogStatistics session_stats;
    };

    struct CustomAggregation {};
    struct IndexingConfig {};

    struct AnalysisConfig {
        std::set<std::string> attributes_for_distribution;
        std::map<std::string, size_t> top_N_string_attributes;
        bool enable_message_template_counts = false;
        size_t top_n_results = 0;
        std::vector<std::string> group_by_fields;
        std::string sort_by_field;
        bool sort_descending = true;
        std::optional<AnomalyDetectionConfig> anomaly_detection_config;
        std::optional<CorrelationConfig> correlation_config;
        std::optional<SessionConfig> session_config;
        std::vector<CustomAggregation> custom_aggregations; 
        std::optional<IndexingConfig> indexing_config;
        std::vector<AggregateFunction> aggregate_functions;
        std::string time_window_duration;
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
        std::string line_pattern; 
        int timestamp_index = 0;
        int level_index = 0;
        int message_index = 0;
        int thread_id_index = 0;
        int file_index = 0;
        int line_index = 0;
        std::string time_format = "%Y-%m-%d %H:%M:%S"; 
        std::function<void(const ParseError&)> error_callback = nullptr;
        size_t max_errors = 0;
        std::optional<std::string> entry_start_pattern;
        size_t max_continuation_lines = 100;
        std::map<std::string, std::string> field_mapping;
        std::map<std::string, std::function<LogValue(std::string_view)>> custom_parsers;
        std::string custom_regex_pattern;
        std::string custom_timestamp_format;
        LogStorageStrategy storage_strategy = LogStorageStrategy::DEFAULT_COPY_STRINGS;

        bool validate() const;
        static ParsingConfig fromRegex(std::string pattern);
        static ParsingConfig fromRegexWithNamedGroups(std::string pattern);
        static ParsingConfig jsonLogFormat();
        std::string toJson() const;
        static std::expected<ParsingConfig, std::string> fromJson(std::string_view json_str);
    };

    enum class StringComparisonOp { EQUALS, CONTAINS, STARTS_WITH, ENDS_WITH, REGEX };
    enum class NumericComparisonOp { EQUALS, NOT_EQUALS, GT, LT, GTE, LTE };

    struct AttributeFilterCondition {
        LogValue value;
        std::optional<StringComparisonOp> string_op;
        std::optional<NumericComparisonOp> numeric_op;
        explicit AttributeFilterCondition(LogValue val, StringComparisonOp op) : value(std::move(val)), string_op(op) {}
        explicit AttributeFilterCondition(LogValue val, NumericComparisonOp op) : value(std::move(val)), numeric_op(op) {}
    };

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
        std::unique_ptr<LogPredicate> MinLevel(LogLevel l);
        std::unique_ptr<LogPredicate> MultiLevel(std::vector<LogLevel> levels);
        std::unique_ptr<LogPredicate> TimeRange(std::optional<std::chrono::system_clock::time_point> start, std::optional<std::chrono::system_clock::time_point> end);
        std::unique_ptr<LogPredicate> Tag(std::string tag);
        std::unique_ptr<LogPredicate> ThreadId(std::string tid);
        std::unique_ptr<LogPredicate> SourceFile(std::string file);
        std::unique_ptr<LogPredicate> Attribute(std::string attribute_name, AttributeFilterCondition condition, bool case_sensitive);
        std::unique_ptr<LogPredicate> HasAttribute(std::string attribute_name);
        std::unique_ptr<LogPredicate> NotHasAttribute(std::string attribute_name);
        std::unique_ptr<LogPredicate> NotHasTag(std::string tag);
        std::unique_ptr<LogPredicate> And(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b);
        std::unique_ptr<LogPredicate> Or(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b);
        std::unique_ptr<LogPredicate> Not(std::unique_ptr<LogPredicate> p);
        template<typename... Predicates>
        std::unique_ptr<LogPredicate> Or(Predicates&&... preds); // Defined below or in cpp
        std::expected<std::unique_ptr<LogPredicate>, std::string> fromQuery(std::string_view query_string);
    }

    struct RetrievalOptions {
        size_t limit = 0;
        size_t tail_count = 0;
        std::string sort_by_field;
        bool sort_descending = false;
        bool follow = true;
        std::vector<std::string> fields_to_export;
        bool follow_by_name = false;
        std::string highlight_regex;
        std::string tail_grep_regex;
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
        std::map<std::string, std::vector<AttributeFilterCondition>> attribute_filter_conditions;
        std::set<std::string> excluded_tags;
        std::vector<std::map<std::string, LogValue>> attribute_or_matches;
        std::optional<std::chrono::system_clock::duration> since;
        std::vector<std::string> any_keywords;
        std::vector<std::string> include_keywords;
        std::vector<std::string> exclude_keywords;
        std::vector<std::string> include_regexes;
        std::vector<std::string> exclude_regexes;
        std::optional<std::pair<LogLevel, LogLevel>> level_range;
        std::vector<FieldFilter> field_filters;
        FilterLogic combined_filter_logic = FilterLogic::AND;
        std::string timezone_str;

        std::unique_ptr<LogPredicate> toPredicate() const;
        std::string toJson() const;
        static std::expected<FilterOptions, std::string> fromJson(std::string_view json_str);
    };

    struct LoadResult {
        size_t loaded_count;
        size_t error_count;
    };

    struct ParallelConfig {
        size_t thread_count = std::thread::hardware_concurrency();
        size_t chunk_size_mb = 64;
        ProgressCallback progress = nullptr;
    };

    using LogEnricher = std::function<void(LogEntry&)>;

    class LogExporter {
    public:
        virtual ~LogExporter() = default;
        virtual void exportStats(const LogStatistics& stats) = 0;
        virtual void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}, std::string_view highlight_regex = "") = 0;
    };

    class JsonExporter : public LogExporter {
        std::ostream& out_;
        bool pretty_;
    public:
        explicit JsonExporter(std::ostream& out, bool pretty = false) : out_(out), pretty_(pretty) {}
        void exportStats(const LogStatistics& stats) override;
        void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}, std::string_view highlight_regex = "") override;
    };

    class CsvExporter : public LogExporter {
        std::ostream& out_;
    public:
        explicit CsvExporter(std::ostream& out) : out_(out) {}
        void exportStats(const LogStatistics& stats) override;
        void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}, std::string_view highlight_regex = "") override;
    };

    class MarkdownExporter : public LogExporter {
        std::ostream& out_;
    public:
        explicit MarkdownExporter(std::ostream& out) : out_(out) {}
        void exportStats(const LogStatistics& stats) override;
        void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}, std::string_view highlight_regex = "") override;
    };

    class ConsoleExporter : public LogExporter {
        std::ostream& out_;
        bool use_color_ = true;
    public:
        explicit ConsoleExporter(std::ostream& out, bool use_color = true) 
            : out_(out), use_color_(use_color) {}
        void exportStats(const LogStatistics& stats) override;
        void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}, std::string_view highlight_regex = "") override;
    };

    class TemplatedExporter : public LogExporter {
        std::ostream& out_;
        std::unique_ptr<ITemplatingEngine> templating_engine_;
        std::string entry_template_;
        std::string stats_template_;
    public:
        explicit TemplatedExporter(std::ostream& out,
                                   std::unique_ptr<ITemplatingEngine> engine,
                                   std::string entry_template,
                                   std::string stats_template = "");
        void exportStats(const LogStatistics& stats) override;
        void exportEntries(std::span<const LogEntry> entries, const std::vector<std::string>& fields_to_export = {}, std::string_view highlight_regex = "") override;
    };

    class LogAnonymizer {
    public:
        virtual ~LogAnonymizer() = default;
        virtual bool anonymize(LogEntry& entry) const = 0;
    };

    class RegexAnonymizer : public LogAnonymizer {
    public:
        RegexAnonymizer(std::string_view pattern, std::string_view replacement, std::set<std::string> fields_to_anonymize = {});
        bool anonymize(LogEntry& entry) const override;
    private:
        std::regex pattern_;
        std::string replacement_;
        std::set<std::string> fields_to_anonymize_;
    };

    class LogAnalyzerBuilder;

    class LogAnalyzer {
    public:
        using EnricherFunction = LogEnricher; 
        LogAnalyzer();
        LogAnalyzer(LogAnalyzer&&) noexcept = default;
        LogAnalyzer& operator=(LogAnalyzer&&) noexcept = default;
        LogAnalyzer(const LogAnalyzer&) = delete;
        LogAnalyzer& operator=(const LogAnalyzer&) = delete;
        
        void setParsingConfig(ParsingConfig config, bool reparseExisting = false);
        void setAnalysisConfig(AnalysisConfig config, bool reanalyzeExisting = false);
        void clearAnalysisConfig();
        void setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex);
        void loadParsingProfile(const std::string& profileName);
        static void registerParsingProfile(const std::string& name, ParsingConfig config);

        std::expected<void, std::string> loadFile(const std::filesystem::path& filepath);
        std::expected<std::pair<LoadResult, std::vector<LogEntry>>, std::string> loadFileWithStats(const std::filesystem::path& filepath, ProgressCallback progress = nullptr);
        std::future<LoadResult> loadFileAsync(std::filesystem::path filepath, ProgressCallback progress = nullptr);
        std::future<LoadResult> loadParallel(std::filesystem::path path, ParallelConfig config = {});
        std::expected<LoadResult, std::string> loadLogSources(const std::vector<LogAnalysis::LogSource>& sources, bool recursive_global_flag = false, ProgressCallback progress = nullptr);

        bool loadLogFile(const std::string& filepath);
        bool loadLogFile(const std::filesystem::path& filepath);

        std::generator<LogEntry> streamEntries(std::filesystem::path filepath);
        std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, FilterOptions options);
        std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, const LogPredicate& predicate);

        std::future<void> tailFile(const std::filesystem::path& filepath, std::function<void(LogEntry)> entry_callback, std::function<void(const ParseError&)> error_callback = nullptr, std::function<bool() > stop_predicate = nullptr);
        void tailFileStream(const LogSource& source, const FilterOptions& filter, const RetrievalOptions& retrieval, LogExporter& exporter, std::function<void(const ParseError&)> error_callback = nullptr);
        std::future<void> startTailing(const std::filesystem::path& path, std::chrono::milliseconds interval = std::chrono::seconds(1));
        void stopTailing();

        std::expected<LogStatistics, std::string> analyzeStream(const std::filesystem::path& filepath);

        std::map<LogValue, size_t> getAttributeFrequency(std::string_view attr_key) const;
        std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> getTimeline(std::chrono::system_clock::duration bucket_size) const;
        std::vector<LogEntry> getTrace(std::string_view trace_id) const;
        
        LogStatistics getFilteredStatistics(const FilterOptions& options) const;
        LogStatistics getFilteredStatistics(const LogPredicate& predicate) const;

        std::vector<std::pair<std::string, size_t>> getTopNMessages(size_t n, bool useTemplates = true) const;
        std::vector<std::pair<LogValue, size_t>> getTopNAttributeValues(const std::string& attributeName, size_t n) const;
        std::vector<std::pair<std::string, size_t>> getTopNThreadIds(size_t n) const;
        std::vector<std::pair<std::string, size_t>> getTopNSourceFiles(size_t n) const;

        std::vector<AnomalyReportEntry> detectAnomalies(const std::optional<FilterOptions>& options = std::nullopt) const;
        std::vector<CorrelationResult> analyzeCorrelations(const std::optional<FilterOptions>& options = std::nullopt) const;
        std::vector<LogSession> getLogSessions(const std::optional<FilterOptions>& options = std::nullopt) const;
        std::map<LogValue, std::map<std::string, LogValue>> performCustomAggregations(const std::optional<FilterOptions>& options = std::nullopt) const;

        std::expected<LoadResult, std::string> loadFromStream(std::unique_ptr<ILogInputStream> input_stream, ProgressCallback progress = nullptr);
        void processStreamContinuously(std::unique_ptr<ILogInputStream> input_stream, std::function<void(LogEntry)> entry_callback, std::function<void(const ParseError&)> error_callback = nullptr, std::function<bool()> stop_predicate = nullptr);
        std::expected<LoadResult, std::string> loadFromDataStore(std::unique_ptr<ILogDataStore> data_store);
        void exportToDataStore(std::unique_ptr<ILogDataStore> data_store, bool export_entries = true, bool export_stats = true, const std::optional<FilterOptions>& options = std::nullopt) const;

        std::vector<LogEntry> getEntriesSpan() const;
        [[nodiscard]] std::vector<LogEntry> getEntries() const;

        std::expected<void, std::string> saveFilterProfile(std::string_view profile_name) const;
        std::expected<void, std::string> loadFilterProfile(std::string_view profile_name);
        std::vector<std::string> listFilterProfiles() const;

        void rebuildIndices();
        bool isIndexed(std::string_view attribute_name) const;

        void addEnricher(LogEnricher enricher);
        void addAnonymizer(std::unique_ptr<LogAnonymizer> anonymizer);
        void clearAnonymizers();
        void addEntry(LogEntry entry);

        void analyze();
        LogStatistics getStatistics() const;
        LogStatistics analyzeAndGetResults();

        void setFilterOptions(const FilterOptions& options);
        void clearFilterOptions();
        void setFilterQuery(std::string_view query_string);

        std::map<LogValue, size_t> getFrequencyMap(std::string_view attribute_key) const;
        void sort(std::function<bool(const LogEntry&, const LogEntry&)> cmp);
        void removeIf(const LogPredicate& predicate);
        void transform(std::function<void(LogEntry&)> transformer);

        void writeStatistics(std::ostream& out, bool as_json = false) const;
        void writeFilteredEntries(std::ostream& out, const FilterOptions& options, bool as_json = false) const;
        void exportTo(LogExporter& exporter) const;
        void exportFilteredTo(LogExporter& exporter, const LogPredicate& predicate) const;
        void exportStatistics(LogExporter& exporter) const;
        void printResults(std::ostream& os, LogAnalysis::OutputFormat format = LogAnalysis::OutputFormat::TEXT) const;
        
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
        std::vector<std::unique_ptr<LogAnonymizer>> anonymizers_; // Missing in previous read, inferred from errors
        std::optional<FilterOptions> currentFilterOptions_;
        std::unique_ptr<LogPredicate> currentFilterPredicate_;

        LogEntry parseLogLine(const std::string& line, size_t line_number = 0);
        std::string levelToString(LogLevel level) const;
        bool matchFilter(const LogEntry& entry, const FilterOptions& options) const;
        void applyEnrichers(LogEntry& entry);
        void applyAnonymizers(LogEntry& entry);
        std::string generateMessageTemplate(std::string_view message) const;
        std::vector<LogEntry> getFilteredEntriesInternal() const;
        LogStatistics calculateStatistics(const std::vector<LogEntry>& entries) const;

        std::map<std::string, int> named_group_indices_;
        mutable std::unique_ptr<std::shared_mutex> rw_mutex_ptr_; 
        mutable std::optional<LogStatistics> cached_stats_;
        std::optional<AnalysisConfig> currentAnalysisConfig_;
        std::map<std::string, std::string> stored_filter_profiles_;
        std::map<std::string, std::map<LogValue, std::vector<size_t>>> attribute_indices_;
        std::map<LogLevel, std::vector<size_t>> level_index_;
        bool indices_dirty_ = true;

        std::thread tailing_thread_;
        std::unique_ptr<std::atomic<bool>> stop_tailing_ptr_ = std::make_unique<std::atomic<bool>>(false);
        std::filesystem::path current_tail_path_;
        std::chrono::milliseconds tail_interval_ = std::chrono::seconds(1);
    };

    class LogAnalyzerBuilder {
    public:
        LogAnalyzerBuilder();
        LogAnalyzerBuilder& withParsingConfig(ParsingConfig config);
        LogAnalyzerBuilder& withParsingProfile(const std::string& profileName);
        LogAnalyzerBuilder& withAnalysisConfig(AnalysisConfig config);
        LogAnalyzerBuilder& withInitialFilterOptions(FilterOptions options);
        LogAnalyzerBuilder& withFilterQuery(const std::string& query);
        LogAnalyzerBuilder& addEnricher(LogEnricher enricher);
        LogAnalyzerBuilder& addAnonymizer(std::unique_ptr<LogAnonymizer> anonymizer);
        LogAnalyzerBuilder& withTemplatedExporter(std::unique_ptr<ITemplatingEngine> engine, std::string entry_template, std::string stats_template = "");
        std::unique_ptr<LogAnalyzer> build();

    private:
        ParsingConfig current_parsing_config_;
        AnalysisConfig current_analysis_config_;
        std::optional<FilterOptions> initial_filter_options_;
        std::optional<std::string> initial_filter_query_;
        std::vector<LogEnricher> enrichers_;
        std::vector<std::unique_ptr<LogAnonymizer>> anonymizers_;
        std::unique_ptr<LogExporter> custom_exporter_;
    };

} // namespace LogAnalysis

#endif // LOG_ANALYZER_H
