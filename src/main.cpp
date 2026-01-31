#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <map> // For string to enum mapping

#include "CLI/CLI.hpp" // CLI11 header

#include "LogAnalyzer.h"
#include "LogEntry.h"

// Struct to hold all parsed command-line options
struct CommandLineOptions {
    std::string command = "analyze";
    std::vector<LogAnalysis::LogSource> sources;
    LogAnalysis::ParsingConfig parsingConfig;
    LogAnalysis::FilterOptions filterOptions;
    LogAnalysis::AnalysisConfig analysisConfig;
    LogAnalysis::RetrievalOptions retrievalOptions; // New member for entries/tail
    LogAnalysis::OutputFormat outputFormat = LogAnalysis::OutputFormat::TEXT;
    std::string outputPath;
    bool prettyPrint = false;
    bool verbose = false;
    bool recursive = false;
    bool noColor = false; // For ConsoleExporter
    std::string configFilePath; // For config file support

    // Temporary storage for CLI11 binding
    std::vector<std::string> tempFilePaths;
    std::vector<std::string> tempDirPaths;
    bool tempStdin = false;
    std::string tempLogLevelStr;
    std::vector<std::string> tempPositionalFiles; // For backward compatibility
};

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
    app.add_option("--config", opts.configFilePath, "Path to a configuration file.");

    // --- Input Source Options ---
    app.add_option("-f,--file", opts.tempFilePaths, "Specify a log file. Can be used multiple times.")->allow_extra_args(false);
    app.add_option("-d,--directory", opts.tempDirPaths, "Specify a directory of log files.")->allow_extra_args(false);
    app.add_flag("-r,--recursive", opts.recursive, "Scan directories recursively.");
    app.add_flag("--stdin", opts.tempStdin, "Read from standard input.");

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
    app.add_option("-k,--keyword", opts.filterOptions.keyword, "Filter entries containing a keyword.");
    app.add_option("--regex-filter", opts.filterOptions.message_regex_pattern, "Filter entries matching a regex.");
    app.add_option("--start-time", opts.filterOptions.start_time, "Filter entries after this timestamp (YYYY-MM-DD HH:MM:SS).");
    app.add_option("--end-time", opts.filterOptions.end_time, "Filter entries before this timestamp (YYYY-MM-DD HH:MM:SS).");

    // --- Parsing Config Options ---
    app.add_option("--parser-regex", opts.parsingConfig.custom_regex_pattern, "Custom regex pattern for parsing log lines.");
    app.add_option("--parser-timestamp-format", opts.parsingConfig.custom_timestamp_format, "Custom timestamp format string (e.g., \"%Y-%m-%d %H:%M:%S\").");

    // --- Subcommands ---
    auto analyzeCmd = app.add_subcommand("analyze", "Analyzes logs and prints statistics (default command).");
    analyzeCmd->fallthrough(); // If no subcommand is given, this one is used
    analyzeCmd->callback([&](){ opts.command = "analyze"; });

    analyzeCmd->add_option("--top-n", opts.analysisConfig.top_n_results, "Show top N results for grouped analysis.")->default_val(0);
    analyzeCmd->add_option("--group-by", opts.analysisConfig.group_by_fields, "Group analysis by specified LogEntry field(s) (e.g., level, source, component).")->expected(-1);
    std::map<std::string, bool> orderMap{{"asc", false}, {"desc", true}}; // false for asc, true for desc
    analyzeCmd->add_option("--analysis-order", opts.analysisConfig.sort_descending, "Sort order for analysis results (asc, desc).")->transform(CLI::CheckedTransformer(orderMap, CLI::ignore_case))->default_val("desc");
    analyzeCmd->add_option("--sort-analysis-by", opts.analysisConfig.sort_by_field, "Field to sort analysis results by (e.g., count, level).");

    auto entriesCmd = app.add_subcommand("entries", "Lists filtered log entries.");
    entriesCmd->callback([&](){ opts.command = "entries"; });
    entriesCmd->add_option("--limit", opts.retrievalOptions.limit, "Output the first N matching entries.")->default_val(0);
    entriesCmd->add_option("--tail", opts.retrievalOptions.tail_count, "Output the last N matching entries.")->default_val(0);
    entriesCmd->add_option("--sort-by", opts.retrievalOptions.sort_by_field, "Sort log entries by a specific field (e.g., timestamp, level, message).");
    entriesCmd->add_option("--order", opts.retrievalOptions.sort_descending, "Sort order for log entries (asc, desc).")->transform(CLI::CheckedTransformer(orderMap, CLI::ignore_case))->default_val("asc");
    entriesCmd->add_option("--fields", opts.retrievalOptions.fields_to_export, "Comma-separated list of LogEntry fields to display.")->delimiter(',')->expected(-1);

    auto tailCmd = app.add_subcommand("tail", "Monitors a log file in real-time.");
    tailCmd->callback([&](){ opts.command = "tail"; });
    tailCmd->add_option("--lines", opts.retrievalOptions.tail_count, "Output the last N lines and exit (non-follow).")->default_val(0);
    // --follow is default, so no explicit flag needed for now. If a --no-follow is desired, it can be added.
    auto noFollowFlag = tailCmd->add_flag("--no-follow", "Do not continuously output new lines (exits after --lines)."); // This flag implies non-follow

    // Backward compatibility for positional logfile
    app.add_option("files", opts.tempPositionalFiles, "Input log files for backward compatibility.")
      ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll)
      ->required(false);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &e) {
        return app.exit(e);
    }

    // --- Post-parsing logic to populate opts.sources and other fields ---
    for (const auto& path : opts.tempFilePaths) {
        opts.sources.emplace_back(path, LogAnalysis::LogSource::SourceType::FILE);
    }
    for (const auto& path : opts.tempDirPaths) {
        opts.sources.emplace_back(path, LogAnalysis::LogSource::SourceType::DIRECTORY);
    }
    if (opts.tempStdin) {
        opts.sources.emplace_back("", LogAnalysis::LogSource::SourceType::STD_IN);
    }
    // Handle positional files (backward compatibility) only if no explicit sources were provided via -f, -d, --stdin
    if (opts.sources.empty()) {
        for (const auto& path : opts.tempPositionalFiles) {
            opts.sources.emplace_back(path, LogAnalysis::LogSource::SourceType::FILE);
        }
    }

    if (!opts.tempLogLevelStr.empty()) {
        opts.filterOptions.level = LogEntry::parseLevel(opts.tempLogLevelStr);
    }

    if (opts.sources.empty()) {
        std::cerr << "Error: No input source specified. Use --file, --directory, --stdin, or provide a log file as a positional argument." << std::endl;
        // Re-display help for clarity
        std::cout << app.help() << std::endl;
        return 1;
    }

    LogAnalysis::LogAnalyzer analyzer;
    analyzer.setParsingConfig(opts.parsingConfig);
    analyzer.setFilterOptions(opts.filterOptions);
    analyzer.setAnalysisConfig(opts.analysisConfig);
    
    // The loadLogSources method needs to be updated to accept the recursive flag
    // This will be addressed in the next step when updating LogAnalyzer API
    auto loadResult = analyzer.loadLogSources(opts.sources, opts.recursive);

    if (!loadResult) {
        std::cerr << "Error loading log sources: " << loadResult.error() << std::endl;
        return 1;
    }

    std::ofstream outFileStream;
    if (!opts.outputPath.empty()) {
        outFileStream.open(opts.outputPath);
        if (!outFileStream) {
            std::cerr << "Error: Could not open output file: " << opts.outputPath << std::endl;
            return 1;
        }
    }
    std::ostream& out = opts.outputPath.empty() ? std::cout : outFileStream;

    std::unique_ptr<LogAnalysis::LogExporter> exporter;
    switch (opts.outputFormat) {
        case LogAnalysis::OutputFormat::JSON:
            exporter = std::make_unique<LogAnalysis::JsonExporter>(out, opts.prettyPrint);
            break;
        case LogAnalysis::OutputFormat::CSV:
            exporter = std::make_unique<LogAnalysis::CsvExporter>(out);
            break;
        case LogAnalysis::OutputFormat::MARKDOWN:
            exporter = std::make_unique<LogAnalysis::MarkdownExporter>(out);
            break;
        case LogAnalysis::OutputFormat::TEXT:
        default:
             exporter = std::make_unique<LogAnalysis::ConsoleExporter>(out, !opts.noColor); // Use opts.noColor
            break;
    }

    if (opts.command == "analyze") {
        auto stats = analyzer.analyzeAndGetResults(); // This needs to use the updated config
        exporter->exportStats(stats);

    } else if (opts.command == "entries") {
        auto entries = analyzer.getFilteredEntries(opts.filterOptions, opts.retrievalOptions);
        exporter->exportEntries(entries, opts.retrievalOptions.fields_to_export);

    } else if (opts.command == "tail") {
        if (opts.sources.size() != 1 || opts.sources[0].getType() != LogAnalysis::LogSource::SourceType::FILE) {
            std::cerr << "Error: 'tail' command requires a single file source." << std::endl;
            return 1;
        }

        // Determine 'follow' behavior:
        // If --lines is specified, follow is implicitly false (read N lines and exit).
        // If --no-follow flag is present, follow is false.
        // Otherwise, follow is true (default continuous tailing).
        if (opts.retrievalOptions.tail_count > 0) { // --lines N was used
            opts.retrievalOptions.follow = false;
        } else if (noFollowFlag->count() > 0) { // --no-follow was used
            opts.retrievalOptions.follow = false;
        } else { // Neither --lines nor --no-follow, so default to continuous follow
            opts.retrievalOptions.follow = true;
        }

        try {
            analyzer.tailFileStream(opts.sources[0], opts.filterOptions, opts.retrievalOptions, *exporter);
        } catch (const std::exception& e) {
            std::cerr << "Error during tail: " << e.what() << std::endl;
            return 1;
        }
    }

    return 0;
}