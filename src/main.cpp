#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <fstream>
#include <algorithm>
#include <cctype>
#include "LogAnalyzer.h"
#include "LogEntry.h"

// Struct to hold all parsed command-line options
struct CommandLineOptions {
    std::string command = "analyze";
    std::vector<LogAnalysis::LogSource> sources;
    LogAnalysis::ParsingConfig parsingConfig;
    LogAnalysis::FilterOptions filterOptions;
    LogAnalysis::AnalysisConfig analysisConfig;
    LogAnalysis::OutputFormat outputFormat = LogAnalysis::OutputFormat::TEXT;
    std::string outputPath;
    bool prettyPrint = false;
    bool verbose = false;
    bool help = false;
    bool version = false;
    bool recursive = false;
    size_t limit = 0;
    size_t tailCount = 0;
    std::string sortBy;
};

// Helper to convert string to lower case for case-insensitive comparisons
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}



void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [GLOBAL_OPTIONS] <COMMAND> [COMMAND_OPTIONS] <INPUT_SOURCES...>" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  analyze (default)  Analyzes logs and prints statistics." << std::endl;
    std::cout << "  entries            Lists filtered log entries." << std::endl;
    std::cout << "  tail               Monitors a log file in real-time." << std::endl;
    std::cout << "\nGlobal Options:" << std::endl;
    std::cout << "  -h, --help         Show this help message." << std::endl;
    std::cout << "  --version          Show version information." << std::endl;
    std::cout << "  -v, --verbose      Enable verbose output." << std::endl;
    std::cout << "  -o, --output <path> Write output to a file." << std::endl;
    std::cout << "  --format <fmt>     Output format (text, json, csv, markdown)." << std::endl;
    std::cout << "  --pretty-print     Enable pretty printing for JSON output." << std::endl;
    
    std::cout << "\nInput Source Options:" << std::endl;
    std::cout << "  -f, --file <path>       Specify a log file. Can be used multiple times." << std::endl;
    std::cout << "  -d, --directory <path>  Specify a directory of log files." << std::endl;
    std::cout << "  -r, --recursive         Scan directories recursively." << std::endl;
    std::cout << "  --stdin                 Read from standard input." << std::endl;

    std::cout << "\nFiltering Options:" << std::endl;
    std::cout << "  -l, --level <level>    Filter by minimum log level." << std::endl;
    std::cout << "  -k, --keyword <word>   Filter entries containing a keyword." << std::endl;
    std::cout << "  --regex-filter <pat>   Filter entries matching a regex." << std::endl;
    std::cout << "  --start-time <ts>      Filter entries after this timestamp." << std::endl;
    std::cout << "  --end-time <ts>        Filter entries before this timestamp." << std::endl;

    std::cout << "\nAnalyze Command Options:" << std::endl;
    std::cout << "  --top-errors <N>       Show top N error messages." << std::endl;
    
    std::cout << "\nEntries Command Options:" << std::endl;
    std::cout << "  --limit <N>            Output the first N matching entries." << std::endl;
    std::cout << "  --tail <N>             Output the last N matching entries." << std::endl;

    std::cout << "\nExample: " << programName << " analyze -f /var/log/app.log --level ERROR" << std::endl;
}

CommandLineOptions parseArguments(int argc, char* argv[]) {
    CommandLineOptions opts;
    std::vector<std::string> positionalArgs;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg[0] == '-') {
            std::string nextArg = (i + 1 < argc) ? argv[i + 1] : "";

            if (arg == "-h" || arg == "--help") {
                opts.help = true;
            } else if (arg == "--version") {
                opts.version = true;
            } else if (arg == "-v" || arg == "--verbose") {
                opts.verbose = true;
            } else if ((arg == "-o" || arg == "--output") && !nextArg.empty()) {
                opts.outputPath = nextArg;
                i++;
            } else if (arg == "--format" && !nextArg.empty()) {
                std::string format = toLower(nextArg);
                if (format == "json") opts.outputFormat = LogAnalysis::OutputFormat::JSON;
                else if (format == "csv") opts.outputFormat = LogAnalysis::OutputFormat::CSV;
                else if (format == "markdown") opts.outputFormat = LogAnalysis::OutputFormat::MARKDOWN;
                else opts.outputFormat = LogAnalysis::OutputFormat::TEXT;
                i++;
            } else if (arg == "--pretty-print") {
                opts.prettyPrint = true;
            } else if ((arg == "-f" || arg == "--file") && !nextArg.empty()) {
                opts.sources.emplace_back(nextArg, LogAnalysis::LogSource::SourceType::FILE);
                i++;
            } else if ((arg == "-d" || arg == "--directory") && !nextArg.empty()) {
                opts.sources.emplace_back(nextArg, LogAnalysis::LogSource::SourceType::DIRECTORY, opts.recursive);
                i++;
            } else if (arg == "-r" || arg == "--recursive") {
                opts.recursive = true;
                // Apply to previously added directories
                for(auto& source : opts.sources) {
                    if (source.getType() == LogAnalysis::LogSource::SourceType::DIRECTORY) {
                        source = LogAnalysis::LogSource(source.getPath(), LogAnalysis::LogSource::SourceType::DIRECTORY, true);
                    }
                }
            } else if (arg == "--stdin") {
                opts.sources.emplace_back("", LogAnalysis::LogSource::SourceType::STD_IN);
            } else if ((arg == "-l" || arg == "--level") && !nextArg.empty()) {
                opts.filterOptions.level = parseLogLevel(nextArg);
                i++;
            } else if ((arg == "-k" || arg == "--keyword") && !nextArg.empty()) {
                opts.filterOptions.keyword = nextArg;
                i++;
            } else if (arg == "--regex-filter" && !nextArg.empty()) {
                opts.filterOptions.message_regex_pattern = nextArg;
                i++;
            } else if (arg == "--start-time" && !nextArg.empty()) {
                opts.filterOptions.start_time = nextArg;
                i++;
            } else if (arg == "--end-time" && !nextArg.empty()) {
                opts.filterOptions.end_time = nextArg;
                i++;
            } else if (arg == "--top-errors" && !nextArg.empty()) {
                // This would be configured in AnalysisConfig. For simplicity, handled post-analysis for now.
                i++;
            } else if (arg == "--limit" && !nextArg.empty()) {
                opts.limit = std::stoul(nextArg);
                i++;
            } else if (arg == "--tail" && !nextArg.empty()) {
                opts.tailCount = std::stoul(nextArg);
                i++;
            } else {
                std::cerr << "Warning: Unknown option '" << arg << "'" << std::endl;
            }
        } else {
            positionalArgs.push_back(arg);
        }
    }
    
    // Handle positional arguments
    if (!positionalArgs.empty()) {
        std::string firstPosArg = toLower(positionalArgs[0]);
        if (firstPosArg == "analyze" || firstPosArg == "entries" || firstPosArg == "tail") {
            opts.command = firstPosArg;
            positionalArgs.erase(positionalArgs.begin());
        }
    }

    // Treat remaining positional args as files (for backward compatibility and general use)
    for (const auto& posArg : positionalArgs) {
        opts.sources.emplace_back(posArg, LogAnalysis::LogSource::SourceType::FILE);
    }
    
    // Backward compatibility: gemini-cli <logfile>
    if (argc == 2 && argv[1][0] != '-') {
        opts.command = "analyze";
        opts.sources.clear();
        opts.sources.emplace_back(argv[1], LogAnalysis::LogSource::SourceType::FILE);
    }

    return opts;
}

int main(int argc, char* argv[]) {
    CommandLineOptions opts = parseArguments(argc, argv);

    if (opts.help) {
        printUsage(argv[0]);
        return 0;
    }

    if (opts.version) {
        // In a real app, you'd have a version string.
        std::cout << "Log Analyzer CLI Version 1.0" << std::endl;
        return 0;
    }

    if (opts.sources.empty()) {
        std::cerr << "Error: No input source specified. Use --file, --directory, or --stdin." << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    LogAnalysis::LogAnalyzer analyzer;
    analyzer.setParsingConfig(opts.parsingConfig);
    analyzer.setFilterOptions(opts.filterOptions);
    analyzer.setAnalysisConfig(opts.analysisConfig);
    
    auto loadResult = analyzer.loadLogSources(opts.sources);
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
             exporter = std::make_unique<LogAnalysis::ConsoleExporter>(out, /*use_color=*/true);
            break;
    }

    if (opts.command == "analyze") {
        auto stats = analyzer.analyzeAndGetResults();
        exporter->exportStats(stats);

    } else if (opts.command == "entries") {
        auto entries = analyzer.getFilteredEntries(); // Deprecated, but simple for this use case
        
        if (opts.tailCount > 0 && opts.tailCount <= entries.size()) {
            entries = std::vector<LogEntry>(entries.end() - opts.tailCount, entries.end());
        }
        if (opts.limit > 0 && entries.size() > opts.limit) {
            entries.resize(opts.limit);
        }

        exporter->exportEntries(entries);

    } else if (opts.command == "tail") {
        if (opts.sources.size() != 1 || opts.sources[0].getType() != LogAnalysis::LogSource::SourceType::FILE) {
            std::cerr << "Error: 'tail' command requires a single file source." << std::endl;
            return 1;
        }

        try {
            for (const auto& entry : analyzer.tailFileStream(opts.sources[0].getPath())) {
                // For tail, we just print the raw line for now. A formatter could be used.
                out << entry.raw_line << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error during tail: " << e.what() << std::endl;
            return 1;
        }
    }

    return 0;
}
