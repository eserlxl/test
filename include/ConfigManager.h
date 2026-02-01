#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>
#include <expected>
#include <filesystem>
#include <optional>
#include "LogAnalysisConfig.h" // For LogAnalysisConfig, ConfigFormat
#include "CommandLineOptions.h" // For CommandLineOptions

namespace LogAnalysis {

class ConfigManager {
public:
    ConfigManager() = default;

    /**
     * @brief Loads a configuration from a file, auto-detecting the format from the file extension.
     * @param filePath The path to the configuration file.
     * @return std::expected<LogAnalysisConfig, std::string>
     */
    std::expected<LogAnalysisConfig, std::string> loadConfigFile(const std::filesystem::path& filePath);

    /**
     * @brief Loads a configuration from a file with an explicitly specified format.
     * @param filePath The path to the configuration file.
     * @param format The format of the configuration file.
     * @return std::expected<LogAnalysisConfig, std::string>
     */
    std::expected<LogAnalysisConfig, std::string> loadConfigFile(const std::filesystem::path& filePath, ConfigFormat format);

    /**
     * @brief Saves a configuration to a file. Format is deduced from the extension if not provided.
     * @param config The configuration object to save.
     * @param filePath The destination path.
     * @param format Optional: Explicit format.
     * @return std::expected<void, std::string>
     */
    std::expected<void, std::string> saveConfigFile(const LogAnalysisConfig& config, 
                                                   const std::filesystem::path& filePath, 
                                                   std::optional<ConfigFormat> format = std::nullopt);

    /**
     * @brief Searches for and loads a configuration file from a set of default locations.
     *        Order of precedence: current directory, user home directory, system-wide config.
     * @return std::expected<std::optional<LogAnalysisConfig>, std::string> Returns nullopt if no config is found.
     */
    std::expected<std::optional<LogAnalysisConfig>, std::string> loadDefaultConfig();

    /**
     * @brief Merges command-line options into an existing LogAnalysisConfig.
     *        Only CLI options that were explicitly provided by the user will override the config.
     * @param config The LogAnalysisConfig to merge into.
     * @param cliOpts The CommandLineOptions parsed from the command line.
     */
    void mergeCliOptions(LogAnalysisConfig& config, const CommandLineOptions& cliOpts);

    // --- Static wrappers for convenience ---

    /**
     * @brief Static wrapper to load a configuration with format auto-detection.
     */
    static std::expected<LogAnalysisConfig, std::string> load(const std::filesystem::path& filePath);

    /**
     * @brief Static wrapper for merging CLI options.
     */
    static void merge(LogAnalysisConfig& config, const CommandLineOptions& cliOpts);

    /**
     * @brief Helper to convert string to ConfigFormat
     */
    static std::optional<ConfigFormat> stringToConfigFormat(const std::string& formatStr);

    /**
     * @brief Helper to detect ConfigFormat from file extension.
     */
    static std::optional<ConfigFormat> detectFormatFromExtension(const std::filesystem::path& filePath);

protected:
    // Helpers to parse content - now instance methods to allow for overrides
    virtual std::expected<LogAnalysisConfig, std::string> parseYaml(const std::string& content);
    virtual std::expected<LogAnalysisConfig, std::string> parseJson(const std::string& content);

    // Helpers to serialize content
    virtual std::expected<std::string, std::string> serializeYaml(const LogAnalysisConfig& config);
    virtual std::expected<std::string, std::string> serializeJson(const LogAnalysisConfig& config);
};

} // namespace LogAnalysis

#endif // CONFIG_MANAGER_H
