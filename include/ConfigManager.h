#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <expected>
#include "LogAnalysisConfig.h" // For LogAnalysisConfig, ConfigFormat

#include "CommandLineOptions.h" // For CommandLineOptions

namespace LogAnalysis {

class ConfigManager {
public:
    /**
     * @brief Loads a configuration from a file, parsing it based on the specified format.
     * @param filePath The path to the configuration file.
     * @param format The format of the configuration file (YAML or JSON).
     * @return An std::expected containing the LogAnalysisConfig on success, or an error string on failure.
     */
    static std::expected<LogAnalysisConfig, std::string> loadConfigFile(const std::string& filePath, ConfigFormat format);

    /**
     * @brief Merges command-line options into an existing LogAnalysisConfig.
     *        CLI options always take precedence over values loaded from the config file.
     * @param config The LogAnalysisConfig to merge into.
     * @param cliOpts The CommandLineOptions parsed from the command line.
     */
    static void mergeCliOptions(LogAnalysisConfig& config, const CommandLineOptions& cliOpts);

private:
    // Helper to parse YAML content
    static std::expected<LogAnalysisConfig, std::string> parseYaml(const std::string& content);
    // Helper to parse JSON content
    static std::expected<LogAnalysisConfig, std::string> parseJson(const std::string& content);

    // Helper to convert string to ConfigFormat
    static std::optional<ConfigFormat> stringToConfigFormat(const std::string& formatStr);
};

} // namespace LogAnalysis

#endif // CONFIG_MANAGER_H
