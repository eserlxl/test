#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <map> // For string to enum mapping

#include "CLI/CLI.hpp" // CLI11 header

#include <analyzer/LogAnalyzer.h>
#include <model/LogEntry.h>
#include <config/ConfigManager.h>
#include <config/LogAnalysisConfig.h>
#include <config/CommandLineOptions.h>

// CLI11 will handle argument parsing and help message generation.

int main(int argc, char* argv[]) {
    CommandLineOptions opts;
    CLI::App app{"gemini-cli: A command-line tool for analyzing log files"};

    // --- Global Options ---
    app.set_version_flag("--version", "Log Analyzer CLI Version 1.0");

    app.add_flag("-v,--verbose", opts.verbose, "Enable verbose output.");
    app.add_option("-o,--output", opts.outputPath, "Write output to a file.");
    
    // Output format parsing
    std::map<std::string, LogAnalysis::OutputFormat> formatMap{
        {"text", LogAnalysis::OutputFormat::TEXT},
        {"json", LogAnalysis::OutputFormat::JSON},
        {"csv", LogAnalysis::OutputFormat::CSV},
        {"markdown", LogAnalysis::OutputFormat::MARKDOWN}
    };
    app.add_option("--format", opts.outputFormat, "Output format (text, json, csv, markdown).")
        ->transform(CLI::CheckedTransformer(formatMap, CLI::ignore_case))
        ->default_val("text");
    app.add_flag("--pretty-print", opts.prettyPrint, "Enable pretty printing for JSON output.");
    app.add_flag("--no-color", opts.noColor, "Disable colored output for console.");
    app.add_option("--config-file", opts.configFilePath, "Path to a configuration file (YAML or JSON).");
    std::map<std::string, LogAnalysis::ConfigFormat> configFormatMap{
        {"yaml", LogAnalysis::ConfigFormat::YAML},
        {"json", LogAnalysis::ConfigFormat::JSON}
    };
    app.add_option("--config-format", opts.configFormatStr, "Specify the format of the config file (yaml, json).")
        ->transform(CLI::CheckedTransformer(configFormatMap, CLI::ignore_case));

    // NEW Iteration 28 Output Format & Display Options (Global)
    app.add_option("--text-fields", opts.tempTextFields, "For text and markdown formats, specify which fields to display and their order (e.g., 'timestamp,level,message').")->delimiter(',')->expected(-1);
    app.add_option("--text-template", opts.tempTextTemplate, "Provide a custom format string for each log entry (e.g., '{timestamp} [{level}] {message}').");
    std::map<std::string, std::string> tableStyleMap{
        {"plain", "plain"}, {"grid", "grid"}, {"pipe", "pipe"}
    };
    app.add_option("--table-style", opts.tempTableStyle, "For markdown and text table output, specify a table style (e.g., 'plain', 'grid', 'pipe').")
        ->transform(CLI::CheckedTransformer(tableStyleMap, CLI::ignore_case))
        ->default_val("pipe");

    // --- Input Source Options ---
    app.add_option("-f,--file", opts.tempFilePaths, "Specify a log file. Can be used multiple times.")->allow_extra_args(false);
    app.add_option("-d,--directory", opts.tempDirPaths, "Specify a directory of log files.")->allow_extra_args(false);
    app.add_flag("-r,--recursive", opts.recursive, "Scan directories recursively.");
    app.add_flag("--stdin", opts.tempStdin, "Read from standard input.");
    app.add_option("--url", opts.tempUrls, "Specify a remote log source URL (e.g., http://example.com/logs/app.log, s3://mybucket/logs/prod.log). Can be used multiple times.");

    // Positional arguments for backward compatibility: gemini-cli <logfile>
    // This needs to be handled carefully with subcommands.
    // CLI11 usually prefers explicit options or subcommands for this.
    // For now, let's assume if no subcommand is given and a positional arg exists, it's a file for 'analyze'.
    // This will be overridden if 'analyze', 'entries', or 'tail' is explicitly given.

    // --- Filtering Options ---
    app.add_option("-l,--level", opts.tempLogLevelStr, "Filter by minimum log level (debug, info, warn, error, critical).")
        ->transform(CLI::CheckedTransformer(std::vector<std::pair<std::string, std::string>>{
            {"debug", "debug"}, {"info", "info"}, {"warn", "warn"}, {"error", "error"}, {"critical", "critical"}
        }, CLI::ignore_case))
        ->option_text("LEVEL");
    app.add_option("-k,--keyword", opts.tempKeyword, "Filter entries containing a keyword (deprecated, use --include-keyword).");
    app.add_option("--regex-filter", opts.tempMessageRegexPattern, "Filter entries matching a regex (deprecated, use --include-regex).");
    app.add_option("--start-time", opts.tempStartTime, "Filter entries after this timestamp (YYYY-MM-DD HH:MM:SS).");
    app.add_option("--end-time", opts.tempEndTime, "Filter entries before this timestamp (YYYY-MM-DD HH:MM:SS).");

    // New Filtering Enhancements
    app.add_option("--include-keyword", opts.tempIncludeKeywords, "Include entries containing this keyword. Can be used multiple times.");
    app.add_option("--exclude-keyword", opts.tempExcludeKeywords, "Exclude entries containing this keyword. Can be used multiple times.");
    app.add_option("--include-regex", opts.tempIncludeRegexes, "Include entries matching this regex. Can be used multiple times.");
    app.add_option("--exclude-regex", opts.tempExcludeRegexes, "Exclude entries matching this regex. Can be used multiple times.");
    app.add_option("--level-range", opts.tempLevelRange, "Filter entries within a level range (e.g., 'info:error').");
    app.add_option("--field-filter", opts.tempFieldFilters, "Generic field-specific filter (e.g., 'component:=database'). Can be used multiple times.");
    std::map<std::string, LogAnalysis::FilterLogic> filterLogicMap{
        {"AND", LogAnalysis::FilterLogic::AND},
        {"OR", LogAnalysis::FilterLogic::OR}
    };
    app.add_option("--filter-logic", opts.tempFilterLogic, "How multiple include filters (keyword/regex/field) are combined (AND, OR). Defaults to AND.");
    app.add_option("--timezone", opts.tempTimezoneStr, "Specify timezone for timestamp parsing and filtering (e.g., 'UTC', 'America/New_York').");

    // --- Parsing Config Options ---
    app.add_option("--parser-regex", opts.tempParserRegex, "Custom regex pattern for parsing log lines.");
    app.add_option("--parser-timestamp-format", opts.tempParserTimestampFormat, "Custom timestamp format string (e.g., \"%Y-%m-%d %H:%M:%S\").");

    // --- Subcommands ---
    auto analyzeCmd = app.add_subcommand("analyze", "Analyzes logs and prints statistics (default command).");
    analyzeCmd->fallthrough(); // If no subcommand is given, this one is used
    analyzeCmd->callback([&](){ opts.command = "analyze"; });

    analyzeCmd->add_option("--top-n", opts.tempTopNResults, "Show top N results for grouped analysis.")->default_val(0);
    analyzeCmd->add_option("--group-by", opts.tempGroupByFields, "Group analysis by specified LogEntry field(s) (e.g., level, source, component).")->expected(-1);
    std::map<std::string, bool> orderMap{{"asc", false}, {"desc", true}}; // false for asc, true for desc
    analyzeCmd->add_option("--analysis-order", opts.tempAnalysisOrder, "Sort order for analysis results (asc, desc).")->transform(CLI::CheckedTransformer(orderMap, CLI::ignore_case))->default_val("desc");
    analyzeCmd->add_option("--sort-analysis-by", opts.tempSortAnalysisBy, "Field to sort analysis results by (e.g., count, level).");
    // New Analysis Capabilities
    analyzeCmd->add_option("--aggregate-function", opts.tempAggregateFunctions, "Apply an aggregation function on a specific field (e.g., 'duration:avg', 'bytes:sum'). Can be used multiple times.");
    analyzeCmd->add_option("--time-window", opts.tempTimeWindowDuration, "Group analysis results into time windows (e.g., '1h', '5m', '30s'). Requires --group-by timestamp implicitly.");

    auto entriesCmd = app.add_subcommand("entries", "Lists filtered log entries.");
    entriesCmd->callback([&](){ opts.command = "entries"; });
    entriesCmd->add_option("--limit", opts.tempLimit, "Output the first N matching entries.")->default_val(0);
    entriesCmd->add_option("--tail", opts.tempTailCount, "Output the last N matching entries.")->default_val(0);
    entriesCmd->add_option("--sort-by", opts.tempSortByField, "Sort log entries by a specific field (e.g., timestamp, level, message).");
    entriesCmd->add_option("--order", opts.tempOrder, "Sort order for log entries (asc, desc).")->transform(CLI::CheckedTransformer(orderMap, CLI::ignore_case))->default_val("asc");
    entriesCmd->add_option("--fields", opts.tempFieldsToExport, "Comma-separated list of LogEntry fields to display.")->delimiter(',')->expected(-1);

    auto tailCmd = app.add_subcommand("tail", "Monitors a log file in real-time.");
    tailCmd->callback([&](){ opts.command = "tail"; });
    tailCmd->add_option("--lines", opts.tempTailCount, "Output the last N lines and exit (non-follow).")->default_val(0);
    // Determine 'follow' behavior:
    // By default, follow is true unless --lines is used or --no-follow is specified.
    // If --lines N is given, it implies non-follow.
    // If --no-follow is explicitly given, it implies non-follow.
    // Otherwise, follow is true.
    // CLI11 doesn't have a direct way to set `tempFollow` based on presence of other flags
    // without `fallthrough` or `group`. Let's assume default `opts.tempFollow = true`
    // and let `--lines` or `--no-follow` override it during post-parsing logic.
    tailCmd->add_flag("--no-follow", opts.tempFollow, "Do not continuously output new lines (exits after --lines).")->negate_flag(); // negate_flag means if present, sets to false
    // New Tail Command Robustness
    tailCmd->add_flag("-F,--follow-name", opts.tempFollowByName, "Follow file by name (re-open if rotated/renamed).");
    tailCmd->add_option("--tail-highlight-regex", opts.tempHighlightRegex, "Highlight lines matching a specific regex during real-time tailing.");
    tailCmd->add_option("--tail-grep", opts.tempTailGrepRegex, "Filter lines matching a specific regex during real-time tailing.");

    // Backward compatibility for positional logfile
    app.add_option("files", opts.tempPositionalFiles, "Input log files for backward compatibility.")
      ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll)
      ->required(false);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    // --- Configuration Loading and Merging Logic ---
    LogAnalysis::ConfigManager configManager;
    LogAnalysis::LogAnalysisConfig finalConfig;

    // 1. Load a default config file if one exists
    auto defaultConfigResult = configManager.loadDefaultConfig();
    if (!defaultConfigResult) {
        std::cerr << "Error loading default config: " << defaultConfigResult.error() << std::endl;
        // This might not be a fatal error, so we just warn.
    } else if (*defaultConfigResult) {
        finalConfig = **defaultConfigResult;
        if (opts.verbose.value_or(false)) {
            std::cerr << "Loaded default configuration." << std::endl;
        }
    }

    // 2. Load from config file specified on the command line (overrides default)
    if (opts.configFilePath) {
        auto configLoadResult = configManager.loadConfigFile(*opts.configFilePath);
        if (!configLoadResult) {
            std::cerr << "Error loading config file '" << *opts.configFilePath << "': " << configLoadResult.error() << std::endl;
            return 1;
        }
        finalConfig = *configLoadResult;
        if (opts.verbose.value_or(false)) {
            std::cerr << "Loaded configuration from " << *opts.configFilePath << std::endl;
        }
    }

    // 3. Merge CLI options into the final configuration
    configManager.mergeCliOptions(finalConfig, opts);

    // Input sources must be available for all commands.
    if (finalConfig.sources.empty()) {
        std::cerr << "Error: No input source specified. Use --file, --directory, --stdin, --url, or provide a log file as a positional argument." << std::endl;
        // Re-display help for clarity
        std::cout << app.help() << std::endl;
        return 1;
    }

    LogAnalysis::LogAnalyzer analyzer;
    // The analyzer should take the whole config object, or we set parts of it.
    analyzer.setParsingConfig(finalConfig.parsingConfig);
    analyzer.setFilterOptions(finalConfig.filterOptions);
    analyzer.setAnalysisConfig(finalConfig.analysisConfig);
    
    auto loadResult = analyzer.loadLogSources(finalConfig.sources, finalConfig.recursive);

    if (!loadResult) {
        std::cerr << "Error loading log sources: " << loadResult.error() << std::endl;
        return 1;
    }

    std::ofstream outFileStream;
    if (!finalConfig.outputPath.empty()) {
        outFileStream.open(finalConfig.outputPath);
        if (!outFileStream) {
            std::cerr << "Error: Could not open output file: " << finalConfig.outputPath << std::endl;
            return 1;
        }
    }
    std::ostream& out = finalConfig.outputPath.empty() ? std::cout : outFileStream;

    std::unique_ptr<LogAnalysis::LogExporter> exporter;
    switch (finalConfig.outputFormat) {
        case LogAnalysis::OutputFormat::JSON:
            exporter = std::make_unique<LogAnalysis::JsonExporter>(out, finalConfig.prettyPrint);
            break;
        case LogAnalysis::OutputFormat::CSV:
            exporter = std::make_unique<LogAnalysis::CsvExporter>(out);
            break;
        case LogAnalysis::OutputFormat::MARKDOWN:
            exporter = std::make_unique<LogAnalysis::MarkdownExporter>(out);
            break;
        case LogAnalysis::OutputFormat::TEXT:
        default:
             exporter = std::make_unique<LogAnalysis::ConsoleExporter>(out, !finalConfig.noColor);
            break;
    }

    if (finalConfig.command == "analyze") {
        auto stats = analyzer.analyzeAndGetResults();
        exporter->exportStats(stats);

    } else if (finalConfig.command == "entries") {
        auto entries = analyzer.getFilteredEntries(finalConfig.filterOptions, finalConfig.retrievalOptions);
        exporter->exportEntries(entries, finalConfig.retrievalOptions.fields_to_export, finalConfig.retrievalOptions.highlight_regex);

    } else if (finalConfig.command == "tail") {
        if (finalConfig.sources.size() != 1 || finalConfig.sources[0].getType() != LogAnalysis::LogSource::SourceType::FILE) {
            std::cerr << "Error: 'tail' command requires a single file source." << std::endl;
            return 1;
        }
        
        // This logic is now part of mergeCliOptions.
        // if (finalConfig.retrievalOptions.tail_count > 0) {
        //     finalConfig.retrievalOptions.follow = false;
        // }

        try {
            analyzer.tailFileStream(finalConfig.sources[0], finalConfig.filterOptions, finalConfig.retrievalOptions, *exporter);
        } catch (const std::exception& e) {
            std::cerr << "Error during tail: " << e.what() << std::endl;
            return 1;
        }
    }

    return 0;
}