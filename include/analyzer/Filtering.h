#ifndef FILTERING_H
#define FILTERING_H

#include <model/LogEntry.h>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory>
#include <set>
#include <expected>

namespace LogAnalysis {

    enum class FilterLogic { AND, OR };

    struct FieldFilter {
        std::string fieldName;
        std::string op;
        std::string value;
    };

    enum class StringComparisonOp { EQUALS, CONTAINS, STARTS_WITH, ENDS_WITH, REGEX };
    enum class NumericComparisonOp { EQUALS, NOT_EQUALS, GT, LT, GTE, LTE };

    struct AttributeFilterCondition {
        LogValue value;
        std::optional<StringComparisonOp> string_op;
        std::optional<NumericComparisonOp> numeric_op;
        AttributeFilterCondition() = default; 
        explicit AttributeFilterCondition(LogValue val, StringComparisonOp op) : value(std::move(val)), string_op(op) {}
        explicit AttributeFilterCondition(LogValue val, NumericComparisonOp op) : value(std::move(val)), numeric_op(op) {}
    };

    class LogPredicate {
    public:
        virtual ~LogPredicate() = default;
        virtual bool test(const LogEntry& entry) const = 0;
        virtual std::unique_ptr<LogPredicate> clone() const = 0;
        virtual std::string toString() const { return "LogPredicate"; }
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

} // namespace LogAnalysis

#endif // FILTERING_H
