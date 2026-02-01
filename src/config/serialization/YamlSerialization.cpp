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
#include <utility> // For std::pair
#include <optional> // For std::optional
#include <set> // For std::set
#include <map> // For std::map
#include <vector> // For std::vector
#include <memory> // For std::shared_ptr
#include <chrono> // For std::chrono::nanoseconds, etc.
#include <variant> // For std::visit, std::monostate
#include <algorithm> // For std::find
#include <iostream> // For std::istringstream in potential C++20 chrono parsing

// Ensure yaml-cpp headers are included correctly if not implicitly done
// Typically, <yaml-cpp/yaml.h> is needed, but assuming it's handled by the .h file.

namespace LogAnalysis {

std::expected<LogAnalysisConfig, YamlParseError> parseYamlConfig(const std::string& content) {
    try {
        YAML::Node node = YAML::Load(content);
        if (!node.IsDefined()) {
            return std::unexpected<YamlParseError>({-1, -1, "Failed to parse YAML content: Document is empty or malformed."});
        }
        return node.as<LogAnalysisConfig>();
    } catch (const YAML::Exception& e) {
        // YAML::Exception covers BadFile, ParserException, InvalidNode etc.
        return std::unexpected<YamlParseError>({e.line(), e.column(), e.what()});
    } catch (const std::exception& e) {
        // For any other unexpected standard exceptions
        return std::unexpected<YamlParseError>({-1, -1, "An unexpected error occurred during YAML parsing: " + std::string(e.what())});
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

// New struct for validation errors
struct ValidationError {
    std::string message;
    // Potentially add more details like field name, invalid value, etc. if needed.
};

// New function for configuration validation
std::expected<LogAnalysisConfig, ValidationError> validateConfig(const LogAnalysisConfig& config) {
    // Perform validation checks for semantic correctness.

    // Example: Validate FilterOptions level range
    if (config.filterOptions.level_range) {
        if (config.filterOptions.level_range->first > config.filterOptions.level_range->second) {
            return std::unexpected<ValidationError>({
                "Invalid level range in FilterOptions: minimum level (" + std::to_string(static_cast<int>(config.filterOptions.level_range->first)) +
                ") is greater than maximum level (" + std::to_string(static_cast<int>(config.filterOptions.level_range->second)) + ")."
            });
        }
    }
    
    // Example: Validate that if outputPath is set, outputFormat is not STDOUT, or handle appropriately.
    // Depending on exact requirements, this might be a warning or error.
    // For now, assuming it's a potential warning and not an error that stops processing.
    // if (!config.outputPath.empty() && config.outputFormat == OutputFormat::STDOUT) {
    //     // Potentially log a warning here.
    // }

    // Add more validation rules here...
    // E.g., check for conflicting retrieval options, logical inconsistencies in analysis config, etc.

    // If all checks pass, return the validated configuration.
    return config;
}

// Modular parsing and serialization functions

// FilterOptions
std::expected<LogAnalysis::FilterOptions, std::string> parseYamlFilterOptions(const std::string& content) {
    try {
        YAML::Node node = YAML::Load(content);
        if (!node.IsDefined()) {
            return std::unexpected("Failed to parse YAML content: Document is empty or malformed.");
        }
        return node.as<LogAnalysis::FilterOptions>();
    } catch (const YAML::Exception& e) {
        return std::unexpected("YAML parsing error for FilterOptions: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during FilterOptions YAML parsing: " + std::string(e.what()));
    }
}

std::expected<std::string, std::string> serializeYamlFilterOptions(const LogAnalysis::FilterOptions& options) {
    try {
        YAML::Node node = options; // Convert options to YAML::Node via specialization
        YAML::Emitter emitter;
        emitter << node;
        if (emitter.good()) {
            return emitter.c_str();
        } else {
            return std::unexpected("Failed to emit YAML for FilterOptions: " + std::string(emitter.GetLastError()));
        }
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during FilterOptions YAML serialization: " + std::string(e.what()));
    }
}

// AnalysisConfig
std::expected<LogAnalysis::AnalysisConfig, std::string> parseYamlAnalysisConfig(const std::string& content) {
    try {
        YAML::Node node = YAML::Load(content);
        if (!node.IsDefined()) {
            return std::unexpected("Failed to parse YAML content: Document is empty or malformed.");
        }
        return node.as<LogAnalysis::AnalysisConfig>();
    } catch (const YAML::Exception& e) {
        return std::unexpected("YAML parsing error for AnalysisConfig: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during AnalysisConfig YAML parsing: " + std::string(e.what()));
    }
}

std::expected<std::string, std::string> serializeYamlAnalysisConfig(const LogAnalysis::AnalysisConfig& config) {
    try {
        YAML::Node node = config; // Convert config to YAML::Node via specialization
        YAML::Emitter emitter;
        emitter << node;
        if (emitter.good()) {
            return emitter.c_str();
        } else {
            return std::unexpected("Failed to emit YAML for AnalysisConfig: " + std::string(emitter.GetLastError()));
        }
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during AnalysisConfig YAML serialization: " + std::string(e.what()));
    }
}

// RetrievalOptions
std::expected<LogAnalysis::RetrievalOptions, std::string> parseYamlRetrievalOptions(const std::string& content) {
    try {
        YAML::Node node = YAML::Load(content);
        if (!node.IsDefined()) {
            return std::unexpected("Failed to parse YAML content: Document is empty or malformed.");
        }
        return node.as<LogAnalysis::RetrievalOptions>();
    } catch (const YAML::Exception& e) {
        return std::unexpected("YAML parsing error for RetrievalOptions: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during RetrievalOptions YAML parsing: " + std::string(e.what()));
    }
}

std::expected<std::string, std::string> serializeYamlRetrievalOptions(const LogAnalysis::RetrievalOptions& options) {
    try {
        YAML::Node node = options; // Convert options to YAML::Node via specialization
        YAML::Emitter emitter;
        emitter << node;
        if (emitter.good()) {
            return emitter.c_str();
        } else {
            return std::unexpected("Failed to emit YAML for RetrievalOptions: " + std::string(emitter.GetLastError()));
        }
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during RetrievalOptions YAML serialization: " + std::string(e.what()));
    }
}

// TextOutputConfig
std::expected<LogAnalysis::TextOutputConfig, std::string> parseYamlTextOutputConfig(const std::string& content) {
    try {
        YAML::Node node = YAML::Load(content);
        if (!node.IsDefined()) {
            return std::unexpected("Failed to parse YAML content: Document is empty or malformed.");
        }
        return node.as<LogAnalysis::TextOutputConfig>();
    } catch (const YAML::Exception& e) {
        return std::unexpected("YAML parsing error for TextOutputConfig: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during TextOutputConfig YAML parsing: " + std::string(e.what()));
    }
}

std::expected<std::string, std::string> serializeYamlTextOutputConfig(const LogAnalysis::TextOutputConfig& config) {
    try {
        YAML::Node node = config; // Convert config to YAML::Node via specialization
        YAML::Emitter emitter;
        emitter << node;
        if (emitter.good()) {
            return emitter.c_str();
        } else {
            return std::unexpected("Failed to emit YAML for TextOutputConfig: " + std::string(emitter.GetLastError()));
        }
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during TextOutputConfig YAML serialization: " + std::string(e.what()));
    }
}

// ParsingConfig
std::expected<LogAnalysis::ParsingConfig, std::string> parseYamlParsingConfig(const std::string& content) {
    try {
        YAML::Node node = YAML::Load(content);
        if (!node.IsDefined()) {
            return std::unexpected("Failed to parse YAML content: Document is empty or malformed.");
        }
        return node.as<LogAnalysis::ParsingConfig>();
    } catch (const YAML::Exception& e) {
        return std::unexpected("YAML parsing error for ParsingConfig: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during ParsingConfig YAML parsing: " + std::string(e.what()));
    }
}

std::expected<std::string, std::string> serializeYamlParsingConfig(const LogAnalysis::ParsingConfig& config) {
    try {
        YAML::Node node = config; // Convert config to YAML::Node via specialization
        YAML::Emitter emitter;
        emitter << node;
        if (emitter.good()) {
            return emitter.c_str();
        } else {
            return std::unexpected("Failed to emit YAML for ParsingConfig: " + std::string(emitter.GetLastError()));
        }
    } catch (const std::exception& e) {
        return std::unexpected("An unexpected error occurred during ParsingConfig YAML serialization: " + std::string(e.what()));
    }
}

// Merge configurations
LogAnalysis::LogAnalysisConfig mergeConfigs(const LogAnalysisConfig& base, const LogAnalysisConfig& overlay) {
    LogAnalysisConfig merged = base; // Start with base configuration

    // Simple field overrides (overlay takes precedence)
    if (!overlay.command.empty()) merged.command = overlay.command;
    if (!overlay.outputPath.empty()) merged.outputPath = overlay.outputPath;
    // Note: outputFormat is an enum, simple assignment is usually appropriate
    // unless there are specific merge rules for it. Assuming overlay overrides.
    if (overlay.outputFormat != static_cast<LogAnalysis::OutputFormat>(-1)) { // Check if overlay has a valid value
        merged.outputFormat = overlay.outputFormat;
    }
    merged.prettyPrint = overlay.prettyPrint; // Overwrite
    merged.noColor = overlay.noColor;       // Overwrite
    merged.recursive = overlay.recursive;     // Overwrite

    // Merging vectors (e.g., sources, levels, keywords)
    // For sources, a common strategy is unique merge based on path or URL.
    // Overlay sources might override existing ones with the same path or add new ones.
    // For simplicity here, we'll add unique sources from overlay to base.
    if (!overlay.sources.empty()) {
        std::set<std::string> base_source_paths;
        for(const auto& src : merged.sources) base_source_paths.insert(src.getPath());
        for(const auto& src : overlay.sources) {
            if(base_source_paths.find(src.getPath()) == base_source_paths.end()) {
                merged.sources.push_back(src);
                base_source_paths.insert(src.getPath());
            }
            // Else: Source with this path already exists in base, decide whether to update or ignore overlay.
            // For now, ignoring overlay if path matches. A more complex merge could update.
        }
    }

    // For complex objects like FilterOptions, AnalysisConfig, RetrievalOptions, TextOutputConfig,
    // a full recursive merge could be implemented, or a simple overlay override.
    // For now, let's assume overlay overrides the entire object if it's considered "set".
    // The definition of "set" for these complex types needs consideration (e.g., checking if they are default constructed).
    // A simple approach is to just assign if the overlay has specific settings.
    // Assuming default-constructed FilterOptions means "not set" for merging purposes.
    // A more robust check would be needed to determine if overlay fields are meaningful.
    if (!overlay.filterOptions.isEmpty()) { // Assuming isEmpty() or similar check to see if FilterOptions was modified.
        merged.filterOptions = overlay.filterOptions;
    }
    if (!overlay.analysisConfig.isEmpty()) { // Assuming isEmpty() check
        merged.analysisConfig = overlay.analysisConfig;
    }
    if (!overlay.retrievalOptions.isEmpty()) { // Assuming isEmpty() check
        merged.retrievalOptions = overlay.retrievalOptions;
    }
    if (!overlay.textOutputConfig.isEmpty()) { // Assuming isEmpty() check
        merged.textOutputConfig = overlay.textOutputConfig;
    }
    if (overlay.parsingConfig.custom_regex_pattern.has_value() || overlay.parsingConfig.custom_timestamp_format.has_value()) {
         merged.parsingConfig = overlay.parsingConfig;
    }

    // Note: This merge logic is a basic example and may need refinement based on specific requirements
    // for each field, especially for nested structures and lists/maps.

    return merged;
}


} // namespace LogAnalysis

namespace { // Anonymous namespace for internal helpers
    std::string to_iso_string(std::chrono::system_clock::time_point tp) {
        auto tt = std::chrono::system_clock::to_time_t(tp);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
        return ss.str();
    }

    // Modified from_iso_string to accept and attempt to use timezone string.
    // Note: Full C++20 zoned_time parsing would be more robust and is noted as a potential enhancement.
    std::optional<std::chrono::system_clock::time_point> from_iso_string(const std::string& s, const std::string& tz_str = "") {
        // If timezone string is provided, attempt timezone-aware parsing.
        // This part is complex and might require C++20 std::chrono::zoned_time or external libraries.
        // For this implementation, we'll outline the intent but keep the core parsing simple
        // as a full timezone implementation is complex.
        if (!tz_str.empty()) {
            // Placeholder for timezone-aware parsing logic.
            // A real implementation would parse 's' according to 'tz_str'.
            // For now, we fall back to the default parsing, but the tz_str is available.
            // std::chrono::zoned_time might be used here if C++20 is fully supported and configured.
            // Example conceptual logic for C++20:
            // try {
            //     std::chrono::system_clock::time_point parsed_tp;
            //     std::istringstream iss(s);
            //     // Use std::chrono::parse or similar for ISO 8601 with timezone
            //     // For now, just use the basic parser and acknowledge the timezone.
            //     // A better solution would involve a full ISO 8601 parser that handles offsets/Z.
            // } catch (const std::exception& e) {
            //     // Log error or handle appropriately
            // }
        }

        // Existing non-timezone aware parsing logic:
        std::tm tm = {};
        std::stringstream ss(s);
        // This specific format string might be too restrictive for general ISO 8601.
        // A more flexible parser might be needed.
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        if (ss.fail()) {
            return std::nullopt;
        }
        std::time_t tt = std::mktime(&tm); // mktime interprets based on local timezone by default
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

    // Base64 utility functions
    static const std::string base64_chars =
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                 "abcdefghijklmnopqrstuvwxyz"
                 "0123456789+/";

    static std::string base64_encode(const std::vector<uint8_t>& data) {
        std::string ret;
        int i = 0;
        int j = 0;
        std::vector<uint8_t> char_array_3(3);
        std::vector<uint8_t> char_array_4(4);
        size_t in_len = data.size();
        const uint8_t* bytes_to_encode = data.data();

        while (in_len--) {
            char_array_3[i++] = *(bytes_to_encode++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (i = 0; (i <4) ; i++)
                    ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i) {
            for(j = i; j < 3; j++)
                char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (j = 0; (j < i + 1) ; j++)
                ret += base64_chars[char_array_4[j]];
        }

        while((ret.length() % 4) != 0)
            ret += '=';
        return ret;
    }

    static std::vector<uint8_t> base64_decode(const std::string& encoded_string) {
        size_t in_len = encoded_string.size();
        if (in_len == 0) return {};
        size_t i = 0;
        size_t j = 0;
        int in_ = 0;
        std::vector<uint8_t> char_array_4(4);
        std::vector<uint8_t> char_array_3(3);
        std::vector<uint8_t> ret;

        // Remove padding
        size_t padding = 0;
        if (in_len > 0 && encoded_string[in_len - 1] == '=') padding++;
        if (in_len > 1 && encoded_string[in_len - 2] == '=') padding++;
        in_len -= padding;

        while (i < in_len) {
            char_array_4[in_] = encoded_string[i++];
            in_++;
            if (in_ ==4) {
                for (i = 0; i < 4; i++) {
                    size_t found_pos = base64_chars.find(char_array_4[i]);
                    if (found_pos == std::string::npos) return {}; // Invalid character
                    char_array_4[i] = static_cast<uint8_t>(found_pos);
                }

                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                for (i = 0; (i < 3); i++)
                    ret.push_back(char_array_3[i]);
                in_ = 0;
            }
        }

        if (in_ > 0) {
            for (i = 0; i < in_; i++) {
                size_t found_pos = base64_chars.find(char_array_4[i]);
                 if (found_pos == std::string::npos) return {}; // Invalid character
                char_array_4[i] = static_cast<uint8_t>(found_pos);
            }

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);

            for (i = 0; (i < in_ - 1); i++)
                ret.push_back(char_array_3[i]);
        }

        return ret;
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
                    // Use YAML::Binary for explicit binary data representation
                    // This might implicitly use Base64 or another binary format depending on yaml-cpp's capabilities.
                    node = YAML::Binary(arg.begin(), arg.end());
                } else if constexpr (std::is_same_v<T, std::chrono::nanoseconds>) {
                    node = to_duration_string(arg);
                } else if constexpr (std::is_same_v<T, std::shared_ptr<LogList>>) {
                    if (arg) { // Check if shared_ptr is not null
                        // Rely on YAML::convert<LogList> specialization if it exists,
                        // otherwise on yaml-cpp's conversion for std::vector<LogValue> if LogList is a typedef.
                        node = *arg;
                    }
                } else if constexpr (std::is_same_v<T, std::shared_ptr<LogObject>>) {
                    if (arg) { // Check if shared_ptr is not null
                        // Rely on YAML::convert<LogObject> specialization if it exists,
                        // otherwise on yaml-cpp's conversion for std::map<std::string, LogValue> if LogObject is a typedef.
                        node = *arg;
                    }
                } else {
                    throw YAML::Exception(
                        YAML::Mark(), // Default mark (no specific location available)
                        "Unhandled type in LogValue variant during YAML encoding."
                    );
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
                else if (auto dur = from_duration_string(s)) { lv = *dur; } // Check duration first
                else {
                    // Attempt to decode as base64 for vector<uint8_t>
                    // A basic heuristic for base64: length multiple of 4, and contains base64 chars.
                    bool likely_base64 = (s.length() % 4 == 0);
                    if (likely_base64) {
                        for (char c : s) {
                            if (!isalnum(c) && c != '+' && c != '/' && c != '=') {
                                likely_base64 = false;
                                break;
                            }
                        }
                    }

                    if (likely_base64) {
                        if (auto decoded_bytes = base64_decode(s)) {
                            // The `LogValue` variant must contain `std::vector<uint8_t>`
                            // Assuming it does, we can assign directly.
                            lv = std::move(decoded_bytes);
                            return true; // Successfully decoded as bytes
                        }
                    }

                    // Fallback to other types
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
                std::string time_str = node["start_tp"].as<std::string>();
                // Pass the timezone string to the parsing function
                if (auto tp = from_iso_string(time_str, rhs.timezone_str)) { // Pass timezone string
                    rhs.start_tp = *tp;
                }
            }
            if(node["end_tp"]) {
                std::string time_str = node["end_tp"].as<std::string>();
                // Pass the timezone string to the parsing function
                if (auto tp = from_iso_string(time_str, rhs.timezone_str)) { // Pass timezone string
                    rhs.end_tp = *tp;
                }
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
