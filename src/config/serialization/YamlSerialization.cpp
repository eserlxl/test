#include <config/serialization/YamlSerialization.h>
#include <config/ConfigManager.h> // For ConfigFormat and helper functions if still needed
#include <analyzer/LogAnalyzer.h> // For LogLevel
#include <model/LogEntry.h> // For LogEntry::parseLevel
#include <model/LogValue.h>

#include <sstream>
#include <iomanip> // For std::put_time
#include <ctime>   // For std::gmtime, std::mktime
#include <cctype>  // For isdigit
#include <stdexcept> // For std::runtime_error

namespace LogAnalysis {

std::expected<LogAnalysisConfig, std::string> parseYamlConfig(const std::string& content) {
    try {
        YAML::Node node = YAML::Load(content);
        if (!node.IsDefined()) {
            return std::unexpected("Failed to parse YAML content: Document is empty or malformed.");
        }
        return node.as<LogAnalysisConfig>();
    } catch (const YAML::BadFile& e) {
        return std::unexpected("Bad YAML file: " + std::string(e.what()));
    } catch (const YAML::ParserException& e) {
        return std::unexpected("YAML parsing error: " + std::string(e.what()));
    } catch (const YAML::InvalidNode& e) {
        return std::unexpected("YAML invalid node error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during YAML parsing: " + std::string(e.what()));
    }
}

std::expected<std::string, std::string> serializeYamlConfig(const LogAnalysisConfig& config) {
    try {
        YAML::Node node = config; // Convert config to YAML::Node via specialization
        YAML::Emitter emitter;
        emitter << node;
        if (emitter.good()) {
            return emitter.c_str();
        } else {
            return std::unexpected("Failed to emit YAML: " + std::string(emitter.GetLastError()));
        }
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during YAML serialization: " + std::string(e.what()));
    }
}

} // namespace LogAnalysis

namespace { // Anonymous namespace for internal helpers
    std::string to_iso_string(std::chrono::system_clock::time_point tp) {
        auto tt = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }

    std::optional<std::chrono::system_clock::time_point> from_iso_string(const std::string& s) {
        std::tm tm = {};
        std::stringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        if (ss.fail()) {
            return std::nullopt;
        }
        std::time_t tt = std::mktime(&tm);
        return std::chrono::system_clock::from_time_t(tt);
    }

    std::string to_duration_string(std::chrono::nanoseconds ns) {
        if (ns == std::chrono::nanoseconds::zero()) return "0ns";
        if (ns % std::chrono::seconds(1) == std::chrono::nanoseconds::zero()) return std::to_string(ns / std::chrono::seconds(1)) + "s";
        if (ns % std::chrono::milliseconds(1) == std::chrono::nanoseconds::zero()) return std::to_string(ns / std::chrono::milliseconds(1)) + "ms";
        if (ns % std::chrono::microseconds(1) == std::chrono::nanoseconds::zero()) return std::to_string(ns / std::chrono::microseconds(1)) + "us";
        return std::to_string(ns.count()) + "ns";
    }

    std::optional<std::chrono::nanoseconds> from_duration_string(const std::string& s) {
        if (s.empty()) return std::nullopt;

        size_t first_non_digit = 0;
        while (first_non_digit < s.length() && (isdigit(s[first_non_digit]) || s[first_non_digit] == '-')) {
            first_non_digit++;
        }
        std::string num_str = s.substr(0, first_non_digit);
        if (num_str.empty()) return std::nullopt;

        long long count;
        try {
            count = std::stoll(num_str);
        } catch (...) {
            return std::nullopt;
        }

        std::string unit_str = s.substr(first_non_digit);
        if (unit_str == "ns") return std::chrono::nanoseconds(count);
        if (unit_str == "us") return std::chrono::microseconds(count);
        if (unit_str == "ms") return std::chrono::milliseconds(count);
        if (unit_str == "s") return std::chrono::seconds(count);
        if (unit_str == "m") return std::chrono::minutes(count);
        if (unit_str == "h") return std::chrono::hours(count);
        if (unit_str == "d") return std::chrono::days(count);

        return std::nullopt; // Unknown unit
    }
} // Anonymous namespace

namespace YAML {
    template<>
    struct convert<LogAnalysis::LogSource> {
        static Node encode(const LogAnalysis::LogSource& rhs) {
            Node node;
            node["type"] = static_cast<int>(rhs.getType());
            node["path"] = rhs.getPath();
            node["url"] = rhs.getUrl();
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::LogSource& rhs) {
            if(!node.IsMap()) return false;
            int type = node["type"] ? node["type"].as<int>() : 0;
            std::string path = node["path"] ? node["path"].as<std::string>() : "";
            std::string url = node["url"] ? node["url"].as<std::string>() : "";
            rhs = LogAnalysis::LogSource(path, static_cast<LogAnalysis::LogSource::SourceType>(type), url);
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::ParsingConfig> {
        static Node encode(const LogAnalysis::ParsingConfig& rhs) {
            Node node;
            node["custom_regex_pattern"] = rhs.custom_regex_pattern;
            node["custom_timestamp_format"] = rhs.custom_timestamp_format;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::ParsingConfig& rhs) {
            if(!node.IsMap()) return false;
            if(node["custom_regex_pattern"]) rhs.custom_regex_pattern = node["custom_regex_pattern"].as<std::string>();
            if(node["custom_timestamp_format"]) rhs.custom_timestamp_format = node["custom_timestamp_format"].as<std::string>();
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::FieldFilter> {
        static Node encode(const LogAnalysis::FieldFilter& rhs) {
            Node node;
            node["fieldName"] = rhs.fieldName;
            node["op"] = rhs.op;
            node["value"] = rhs.value;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::FieldFilter& rhs) {
            if(!node.IsMap()) return false;
            rhs.fieldName = node["fieldName"] ? node["fieldName"].as<std::string>() : "";
            rhs.op = node["op"] ? node["op"].as<std::string>() : "";
            rhs.value = node["value"] ? node["value"].as<std::string>() : "";
            return true;
        }
    };

    template<>
    struct convert<LogValue> {
        static Node encode(const LogValue& lv) {
            Node node;
            std::visit([&](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    node = YAML::Node(); // Represent null
                } else if constexpr (std::is_same_v<T, bool>) {
                    node = arg;
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    node = arg;
                } else if constexpr (std::is_same_v<T, uint64_t>) {
                    node = arg;
                } else if constexpr (std::is_same_v<T, double>) {
                    node = arg;
                } else if constexpr (std::is_same_v<T, std::string>) {
                    node = arg;
                } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                    std::stringstream ss;
                    for (const auto& byte : arg) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
                    }
                    node = ss.str();
                } else if constexpr (std::is_same_v<T, std::chrono::nanoseconds>) {
                    node = to_duration_string(arg);
                } else if constexpr (std::is_same_v<T, std::shared_ptr<LogList>>) {
                    node = YAML::Node(*arg); // Explicitly convert std::vector<LogValue> to YAML::Node
                } else if constexpr (std::is_same_v<T, std::shared_ptr<LogObject>>) {
                    node = YAML::Node(*arg); // Explicitly convert std::map<std::string, LogValue> to YAML::Node
                } else {
                    node = lv.toString(); // Fallback
                }
            }, static_cast<const LogValueBase&>(lv));
            return node;
        }

        static bool decode(const Node& node, LogValue& lv) {
            if (node.IsNull()) {
                lv = std::monostate{};
            } else if (node.IsScalar()) {
                std::string s = node.as<std::string>();
                if (s == "true") { lv = true; }
                else if (s == "false") { lv = false; }
                else if (auto dur = from_duration_string(s)) { lv = *dur; }
                else {
                    try { lv = node.as<int64_t>(); return true; } catch (...) {}
                    try { lv = node.as<uint64_t>(); return true; } catch (...) {}
                    try { lv = node.as<double>(); return true; } catch (...) {}
                    lv = s; // Default to string if nothing else matches
                }
            } else if (node.IsSequence()) {
                auto list_ptr = std::make_shared<LogList>();
                *list_ptr = node.as<LogList>(); // Explicitly convert YAML::Node to LogList
                lv = list_ptr;
            } else if (node.IsMap()) {
                auto obj_ptr = std::make_shared<LogObject>();
                *obj_ptr = node.as<LogObject>(); // Explicitly convert YAML::Node to LogObject
                lv = obj_ptr;
            }
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::StringComparisonOp> {
        static Node encode(const LogAnalysis::StringComparisonOp& rhs) {
            switch (rhs) {
                case LogAnalysis::StringComparisonOp::EQUALS: return YAML::Node("EQUALS");
                case LogAnalysis::StringComparisonOp::CONTAINS: return YAML::Node("CONTAINS");
                case LogAnalysis::StringComparisonOp::STARTS_WITH: return YAML::Node("STARTS_WITH");
                case LogAnalysis::StringComparisonOp::ENDS_WITH: return YAML::Node("ENDS_WITH");
                case LogAnalysis::StringComparisonOp::REGEX: return YAML::Node("REGEX");
            }
            return YAML::Node(); // Should not reach here
        }
        static bool decode(const Node& node, LogAnalysis::StringComparisonOp& rhs) {
            if (!node.IsScalar()) return false;
            std::string s = node.as<std::string>();
            if (s == "EQUALS") rhs = LogAnalysis::StringComparisonOp::EQUALS;
            else if (s == "CONTAINS") rhs = LogAnalysis::StringComparisonOp::CONTAINS;
            else if (s == "STARTS_WITH") rhs = LogAnalysis::StringComparisonOp::STARTS_WITH;
            else if (s == "ENDS_WITH") rhs = LogAnalysis::StringComparisonOp::ENDS_WITH;
            else if (s == "REGEX") rhs = LogAnalysis::StringComparisonOp::REGEX;
            else return false;
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::NumericComparisonOp> {
        static Node encode(const LogAnalysis::NumericComparisonOp& rhs) {
            switch (rhs) {
                case LogAnalysis::NumericComparisonOp::EQUALS: return YAML::Node("EQUALS");
                case LogAnalysis::NumericComparisonOp::NOT_EQUALS: return YAML::Node("NOT_EQUALS");
                case LogAnalysis::NumericComparisonOp::GT: return YAML::Node("GT");
                case LogAnalysis::NumericComparisonOp::LT: return YAML::Node("LT");
                case LogAnalysis::NumericComparisonOp::GTE: return YAML::Node("GTE");
                case LogAnalysis::NumericComparisonOp::LTE: return YAML::Node("LTE");
            }
            return YAML::Node(); // Should not reach here
        }
        static bool decode(const Node& node, LogAnalysis::NumericComparisonOp& rhs) {
            if (!node.IsScalar()) return false;
            std::string s = node.as<std::string>();
            if (s == "EQUALS") rhs = LogAnalysis::NumericComparisonOp::EQUALS;
            else if (s == "NOT_EQUALS") rhs = LogAnalysis::NumericComparisonOp::NOT_EQUALS;
            else if (s == "GT") rhs = LogAnalysis::NumericComparisonOp::GT;
            else if (s == "LT") rhs = LogAnalysis::NumericComparisonOp::LT;
            else if (s == "GTE") rhs = LogAnalysis::NumericComparisonOp::GTE;
            else if (s == "LTE") rhs = LogAnalysis::NumericComparisonOp::LTE;
            else return false;
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::AttributeFilterCondition> {
        static Node encode(const LogAnalysis::AttributeFilterCondition& rhs) {
            Node node;
            node["value"] = rhs.value;
            if (rhs.string_op) node["string_op"] = *rhs.string_op;
            if (rhs.numeric_op) node["numeric_op"] = *rhs.numeric_op;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::AttributeFilterCondition& rhs) {
            if (!node.IsMap()) return false;
            if (node["value"]) rhs.value = node["value"].as<LogValue>();
            if (node["string_op"]) rhs.string_op = node["string_op"].as<LogAnalysis::StringComparisonOp>();
            if (node["numeric_op"]) rhs.numeric_op = node["numeric_op"].as<LogAnalysis::NumericComparisonOp>();
            return true;
        }
    };

    template<>
    struct convert<LogLevel> {
        static Node encode(const LogLevel& rhs) {
            return YAML::Node(static_cast<int>(rhs));
        }
        static bool decode(const Node& node, LogLevel& rhs) {
            if (!node.IsScalar()) return false;
            rhs = static_cast<LogLevel>(node.as<int>());
            return true;
        }
    };

    template<typename T>
    struct convert<std::set<T>> {
        static Node encode(const std::set<T>& rhs) {
            Node node(NodeType::Sequence);
            for (const auto& element : rhs) {
                node.push_back(element);
            }
            return node;
        }
        static bool decode(const Node& node, std::set<T>& rhs) {
            if (!node.IsSequence()) return false;
            rhs.clear();
            for (const auto& element : node) {
                rhs.insert(element.as<T>());
            }
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::FilterOptions> {
        static Node encode(const LogAnalysis::FilterOptions& rhs) {
            Node node;
            if (rhs.level) node["level"] = static_cast<int>(*rhs.level);
            if (rhs.keyword) node["keyword"] = *rhs.keyword;
            if (rhs.start_time) node["start_time"] = *rhs.start_time;
            if (rhs.end_time) node["end_time"] = *rhs.end_time;
            if (rhs.message_regex_pattern) node["message_regex_pattern"] = *rhs.message_regex_pattern;
            if (rhs.start_tp) node["start_tp"] = to_iso_string(*rhs.start_tp);
            if (rhs.end_tp) node["end_tp"] = to_iso_string(*rhs.end_tp);
            if (rhs.source_file) node["source_file"] = *rhs.source_file;
            if (rhs.thread_id) node["thread_id"] = *rhs.thread_id;
            if (rhs.since) node["since"] = to_duration_string(*rhs.since);
            if (rhs.level_range) {
                node["level_range_min"] = static_cast<int>(rhs.level_range->first);
                node["level_range_max"] = static_cast<int>(rhs.level_range->second);
            }
            node["case_sensitive"] = rhs.case_sensitive;
            node["invert_match"] = rhs.invert_match;
            node["levels"] = rhs.levels;
            node["attribute_matches"] = rhs.attribute_matches;
            node["required_tags"] = rhs.required_tags;
            node["attribute_filter_conditions"] = rhs.attribute_filter_conditions;
            node["excluded_tags"] = rhs.excluded_tags;
            node["attribute_or_matches"] = rhs.attribute_or_matches;
            node["any_keywords"] = rhs.any_keywords;
            node["include_keywords"] = rhs.include_keywords;
            node["exclude_keywords"] = rhs.exclude_keywords;
            node["include_regexes"] = rhs.include_regexes;
            node["exclude_regexes"] = rhs.exclude_regexes;
            node["field_filters"] = rhs.field_filters;
            node["combined_filter_logic"] = static_cast<int>(rhs.combined_filter_logic);
            node["timezone_str"] = rhs.timezone_str;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::FilterOptions& rhs) {
            if(!node.IsMap()) return false;
            if(node["level"]) rhs.level = static_cast<LogLevel>(node["level"].as<int>());
            if(node["keyword"]) rhs.keyword = node["keyword"].as<std::string>();
            if(node["start_time"]) rhs.start_time = node["start_time"].as<std::string>();
            if(node["end_time"]) rhs.end_time = node["end_time"].as<std::string>();
            if(node["message_regex_pattern"]) rhs.message_regex_pattern = node["message_regex_pattern"].as<std::string>();
            if(node["start_tp"]) {
                if (auto tp = from_iso_string(node["start_tp"].as<std::string>())) rhs.start_tp = *tp;
            }
            if(node["end_tp"]) {
                if (auto tp = from_iso_string(node["end_tp"].as<std::string>())) rhs.end_tp = *tp;
            }
            if(node["source_file"]) rhs.source_file = node["source_file"].as<std::string>();
            if(node["thread_id"]) rhs.thread_id = node["thread_id"].as<std::string>();
            if(node["since"]) {
                if (auto dur = from_duration_string(node["since"].as<std::string>())) rhs.since = *dur;
            }
            if(node["level_range_min"] && node["level_range_max"]) {
                rhs.level_range = std::make_pair(static_cast<LogLevel>(node["level_range_min"].as<int>()),
                                               static_cast<LogLevel>(node["level_range_max"].as<int>()));
            }
            if(node["case_sensitive"]) rhs.case_sensitive = node["case_sensitive"].as<bool>();
            if(node["invert_match"]) rhs.invert_match = node["invert_match"].as<bool>();
            if(node["levels"]) rhs.levels = node["levels"].as<std::vector<LogLevel>>();
            if(node["attribute_matches"]) rhs.attribute_matches = node["attribute_matches"].as<std::map<std::string, LogValue>>();
            if(node["required_tags"]) rhs.required_tags = node["required_tags"].as<std::set<std::string>>();
            if(node["attribute_filter_conditions"]) rhs.attribute_filter_conditions = node["attribute_filter_conditions"].as<std::map<std::string, std::vector<LogAnalysis::AttributeFilterCondition>>>();
            if(node["excluded_tags"]) rhs.excluded_tags = node["excluded_tags"].as<std::set<std::string>>();
            if(node["attribute_or_matches"]) rhs.attribute_or_matches = node["attribute_or_matches"].as<std::vector<std::map<std::string, LogValue>>>();
            if(node["any_keywords"]) rhs.any_keywords = node["any_keywords"].as<std::vector<std::string>>();
            if(node["include_keywords"]) rhs.include_keywords = node["include_keywords"].as<std::vector<std::string>>();
            if(node["exclude_keywords"]) rhs.exclude_keywords = node["exclude_keywords"].as<std::vector<std::string>>();
            if(node["include_regexes"]) rhs.include_regexes = node["include_regexes"].as<std::vector<std::string>>();
            if(node["exclude_regexes"]) rhs.exclude_regexes = node["exclude_regexes"].as<std::vector<std::string>>();
            if(node["field_filters"]) rhs.field_filters = node["field_filters"].as<std::vector<LogAnalysis::FieldFilter>>();
            if(node["combined_filter_logic"]) rhs.combined_filter_logic = static_cast<LogAnalysis::FilterLogic>(node["combined_filter_logic"].as<int>());
            if(node["timezone_str"]) rhs.timezone_str = node["timezone_str"].as<std::string>();
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::AggregateFunction> {
        static Node encode(const LogAnalysis::AggregateFunction& rhs) {
            Node node;
            node["fieldName"] = rhs.fieldName;
            node["function"] = rhs.function;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::AggregateFunction& rhs) {
            if(!node.IsMap()) return false;
            if(node["fieldName"]) rhs.fieldName = node["fieldName"].as<std::string>();
            if(node["function"]) rhs.function = node["function"].as<std::string>();
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::AnalysisConfig> {
        static Node encode(const LogAnalysis::AnalysisConfig& rhs) {
            Node node;
            node["top_n_results"] = rhs.top_n_results;
            node["group_by_fields"] = rhs.group_by_fields;
            node["sort_by_field"] = rhs.sort_by_field;
            node["sort_descending"] = rhs.sort_descending;
            node["aggregate_functions"] = rhs.aggregate_functions;
            node["time_window_duration"] = rhs.time_window_duration;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::AnalysisConfig& rhs) {
            if(!node.IsMap()) return false;
            if(node["top_n_results"]) rhs.top_n_results = node["top_n_results"].as<size_t>();
            if(node["group_by_fields"]) rhs.group_by_fields = node["group_by_fields"].as<std::vector<std::string>>();
            if(node["sort_by_field"]) rhs.sort_by_field = node["sort_by_field"].as<std::string>();
            if(node["sort_descending"]) rhs.sort_descending = node["sort_descending"].as<bool>();
            if(node["aggregate_functions"]) rhs.aggregate_functions = node["aggregate_functions"].as<std::vector<LogAnalysis::AggregateFunction>>();
            if(node["time_window_duration"]) rhs.time_window_duration = node["time_window_duration"].as<std::string>();
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::RetrievalOptions> {
        static Node encode(const LogAnalysis::RetrievalOptions& rhs) {
            Node node;
            node["limit"] = rhs.limit;
            node["tail_count"] = rhs.tail_count;
            node["sort_by_field"] = rhs.sort_by_field;
            node["sort_descending"] = rhs.sort_descending;
            node["follow"] = rhs.follow;
            node["fields_to_export"] = rhs.fields_to_export;
            node["follow_by_name"] = rhs.follow_by_name;
            node["highlight_regex"] = rhs.highlight_regex;
            node["tail_grep_regex"] = rhs.tail_grep_regex;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::RetrievalOptions& rhs) {
            if(!node.IsMap()) return false;
            if(node["limit"]) rhs.limit = node["limit"].as<size_t>();
            if(node["tail_count"]) rhs.tail_count = node["tail_count"].as<size_t>();
            if(node["sort_by_field"]) rhs.sort_by_field = node["sort_by_field"].as<std::string>();
            if(node["sort_descending"]) rhs.sort_descending = node["sort_descending"].as<bool>();
            if(node["follow"]) rhs.follow = node["follow"].as<bool>();
            if(node["fields_to_export"]) rhs.fields_to_export = node["fields_to_export"].as<std::vector<std::string>>();
            if(node["follow_by_name"]) rhs.follow_by_name = node["follow_by_name"].as<bool>();
            if(node["highlight_regex"]) rhs.highlight_regex = node["highlight_regex"].as<std::string>();
            if(node["tail_grep_regex"]) rhs.tail_grep_regex = node["tail_grep_regex"].as<std::string>();
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::TextOutputConfig> {
        static Node encode(const LogAnalysis::TextOutputConfig& rhs) {
            Node node;
            node["fields_to_display"] = rhs.fields_to_display;
            node["custom_template"] = rhs.custom_template;
            node["table_style"] = rhs.table_style;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::TextOutputConfig& rhs) {
            if(!node.IsMap()) return false;
            if(node["fields_to_display"]) rhs.fields_to_display = node["fields_to_display"].as<std::vector<std::string>>();
            if(node["custom_template"]) rhs.custom_template = node["custom_template"].as<std::string>();
            if(node["table_style"]) rhs.table_style = node["table_style"].as<std::string>();
            return true;
        }
    };

    template<>
    struct convert<LogAnalysis::LogAnalysisConfig> {
        static Node encode(const LogAnalysis::LogAnalysisConfig& rhs) {
            Node node;
            node["verbose"] = rhs.verbose;
            node["outputPath"] = rhs.outputPath;
            node["outputFormat"] = static_cast<int>(rhs.outputFormat);
            node["prettyPrint"] = rhs.prettyPrint;
            node["noColor"] = rhs.noColor;
            node["sources"] = rhs.sources;
            node["recursive"] = rhs.recursive;
            node["parsingConfig"] = rhs.parsingConfig;
            node["filterOptions"] = rhs.filterOptions;
            node["analysisConfig"] = rhs.analysisConfig;
            node["retrievalOptions"] = rhs.retrievalOptions;
            node["textOutputConfig"] = rhs.textOutputConfig;
            node["command"] = rhs.command;
            return node;
        }
        static bool decode(const Node& node, LogAnalysis::LogAnalysisConfig& rhs) {
            if(!node.IsMap()) return false;
            if(node["verbose"]) rhs.verbose = node["verbose"].as<bool>();
            if(node["outputPath"]) rhs.outputPath = node["outputPath"].as<std::string>();
            if(node["outputFormat"]) rhs.outputFormat = static_cast<LogAnalysis::OutputFormat>(node["outputFormat"].as<int>());
            if(node["prettyPrint"]) rhs.prettyPrint = node["prettyPrint"].as<bool>();
            if(node["noColor"]) rhs.noColor = node["noColor"].as<bool>();
            if(node["sources"]) rhs.sources = node["sources"].as<std::vector<LogAnalysis::LogSource>>();
            if(node["recursive"]) rhs.recursive = node["recursive"].as<bool>();
            if(node["parsingConfig"]) rhs.parsingConfig = node["parsingConfig"].as<LogAnalysis::ParsingConfig>();
            if(node["filterOptions"]) rhs.filterOptions = node["filterOptions"].as<LogAnalysis::FilterOptions>();
            if(node["analysisConfig"]) rhs.analysisConfig = node["analysisConfig"].as<LogAnalysis::AnalysisConfig>();
            if(node["retrievalOptions"]) rhs.retrievalOptions = node["retrievalOptions"].as<LogAnalysis::RetrievalOptions>();
            if(node["textOutputConfig"]) rhs.textOutputConfig = node["textOutputConfig"].as<LogAnalysis::TextOutputConfig>();
            if(node["command"]) rhs.command = node["command"].as<std::string>();
            return true;
        }
    };
} // namespace YAML
