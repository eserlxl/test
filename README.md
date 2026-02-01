# logAnalyzer

A powerful C++ project designed for structured log parsing, analysis, and processing. It provides robust core components for handling individual log entries with rich metadata and a main analyzer class to process log data from various sources. This library is built for high-performance and flexibility, supporting advanced features like structured attributes, tracing context, and flexible JSON serialization.

## Project Structure

*   `CMakeLists.txt`: The CMake build configuration file.
*   `data/`: Contains sample log files (e.g., `sample.log`) for testing or demonstration.
*   `include/`: Header files for the core classes (`LogAnalyzer.h`, `LogEntry.h`).
*   `src/`: Source files implementing the project's logic (`LogAnalyzer.cpp`, `LogEntry.cpp`, `main.cpp`).
*   `test/`: Unit tests for the project's components (`LogAnalyzerTest.cpp`, `LogEntryTest.cpp`).

## Features

### `LogEntry` Features

The `LogEntry` class is at the heart of `logAnalyzer`, offering a comprehensive model for log data:

*   **Structured Logging**: Supports key-value pairs for `attributes` using a flexible `LogValue` variant type (supporting bool, int64, uint64, double, string, binary data, duration, lists, and objects).
    *   `std::chrono::duration` values are automatically formatted into human-readable strings (e.g., "1h 5m 10s") during serialization.
*   **Rich Metadata**: Each log entry can store:
    *   Timestamp (`std::chrono::system_clock::time_point`)
    *   Log Level (DEBUG, INFO, WARNING, ERROR, CRITICAL)
    *   Message
    *   Process ID, Host Name, Application Name
    *   Source Code Location (file, function, line)
    *   Thread ID
    *   Tracing Context (Trace ID, Span ID)
    *   Tags (a set of strings)
*   **Fluent API**: Easily construct `LogEntry` objects using a builder pattern (e.g., `LogEntry::create(...).withAttribute(...).withTag(...)`).
*   **Scoped and Thread-Local Context (`LogContext`)**: Automatically enrich log entries with contextual attributes and tags within a specific scope, ideal for environments like request handling or transaction processing. This thread-local, RAII-based context management simplifies adding consistent data to logs without passing it through function calls.
    *   **RAII-based Scopes**: Use `LogContext::Scope` to push a context (attributes and tags) onto a thread-local stack. The context is automatically removed when the scope is exited.
    *   **Hierarchical Attributes**: Inner scopes override attribute values from outer scopes.
    *   **Cumulative Tags**: Tags from all active scopes are combined.
    *   **Example**:
        ```cpp
        #include "model/LogEntry.h"
        #include "model/LogEntryContext.h"

        void handle_request(const std::string& request_id) {
            // Create a context that will apply to all logs in this scope.
            LogContext::Scope request_scope({{"request_id", request_id}}, {"api"});

            // This log entry will automatically have {"request_id": "...", "source": "api"}
            auto entry = LogEntry::create(LogLevel::INFO, "Processing request");
            LogContext::apply(entry); 

            // You can create nested scopes
            {
                LogContext::Scope db_scope({{"db.transaction", true}}, {"database"});
                // This log will have attributes from both scopes.
                auto db_entry = LogEntry::create(LogLevel::DEBUG, "Executing query");
                LogContext::apply(db_entry);
            }
        }
        ```
*   **Dynamic Context Capture**: Methods to automatically capture system-level information like process ID, host name, system load averages, and memory usage.
*   **JSON Serialization**: Convert `LogEntry` objects to JSON with extensive customization options:
    *   **Field-Level Control**:
        *   `include_fields` / `exclude_fields`: Whitelist or blacklist specific fields (e.g., `timestamp`, `level`, `attributes.hostname`) for fine-grained control over the output.
        *   `include_source`, `include_thread`, `include_tracing`: Toggle the inclusion of source code location, thread, and tracing context.
        *   `exclude_empty`: Exclude fields that have empty or null values.
        *   `flatten_attributes`: If true, attributes are merged directly into the root JSON object instead of being nested under an "attributes" key.
    *   **Formatting and Indentation**:
        *   `pretty`: Enable pretty printing for the main log entry structure.
        *   `pretty_structured_data`: Enable pretty printing for nested JSON objects within attributes.
        *   `indent_level`: Set the number of spaces for indentation (e.g., 2 or 4).
        *   `sanitize_strings`: Control automatic escaping of special characters in string values.
    *   **Timestamp Formatting**:
        *   `timestamp_format`: Choose between `Default` (string), `ISO8601`, and `UnixMillis`.
        *   `custom_timestamp_format`: Provide a custom `strftime`-compatible format string.
        *   `precision`: Adjust timestamp precision (`Seconds`, `Millis`, `Micros`, `Nanos`).
        *   `timezone`: Set the output timezone (`Local` or `UTC`).
    *   **Data Encoding**:
        *   `binary_encoding`: Choose how binary data is encoded (`Hex` or `Base64`).
*   **Comparison and Utilities**: Supports comparison operations, level parsing, conversion to `std::map<std::string, LogValue>`, and hashability for use in `std::unordered_set` and `std::unordered_map`.

### `LogAnalyzer` Features

The `LogAnalyzer` class provides a high-level interface for processing, analyzing, and exporting log data:

*   **Versatile Log Loading**: The `LogLoader` component provides robust mechanisms to process logs from various sources. It supports loading single files, directories (recursively), and standard input, with options for synchronous, asynchronous, and high-performance parallel file loading.
*   **Log Streaming with Generators**: Utilize `std::generator` provided by `LogLoader` to efficiently stream log entries, including filtered ones, allowing for processing large log files without loading them entirely into memory.
*   **Real-time Monitoring**: Through `LogLoader`'s `tailFile` functionality, monitor log files in real-time, providing a `tail -f`-like capability to process and analyze new log entries as they are written.
*   **Flexible Parsing**: A powerful and configurable regex-based engine (`ParsingConfig`) allows for parsing a wide variety of log formats. It supports multi-line log entries, named capture groups for regex, and custom field parsers. A key feature is the ability to parse timestamps from arbitrary formats using `strftime`-compatible format strings, in addition to built-in support for ISO 8601 and Unix timestamps.
*   **Powerful Filtering Engine**:
    *   **User-Friendly Query Language**: Filter logs using simple, intuitive query strings (e.g., `level:ERROR AND http.status >= 500`).
    *   **Composable Predicates**: Programmatically build complex filter logic using a tree of predicates (`And`, `Or`, `Not`, `Keyword`, `Regex`, `Attribute`, `TimeRange`, etc.).
*   **Advanced Analysis**: Go beyond simple counts with advanced statistical analysis, configurable via `AnalysisConfig`:
    *   **Message Templating**: Group similar log messages into templates to identify common event types.
    *   **Attribute Analysis**: Compute value distributions and top-N occurrences for specified attributes to understand trends.
*   **Log Enrichment and Anonymization**:
    *   **Enrichment**: Add contextual information to log entries on-the-fly using custom `LogEnricher` functions.
    *   **Anonymization**: Automatically find and redact sensitive data (like PII, passwords, or tokens) using the `LogAnonymizer` framework and `RegexAnonymizer`.
*   **Configuration Persistence**: Serialize parsing (`ParsingConfig`) and filtering (`FilterOptions`) configurations to and from JSON and YAML, making it easy to save, share, and reuse settings.
*   **Multiple Export Formats**: Export analysis results or filtered log entries to various formats, including human-readable text, JSON, CSV, and Markdown, using a flexible `LogExporter` interface.

## Build Instructions

This project uses CMake for its build system.

1.  **Create a build directory and navigate into it:**
    ```bash
    mkdir build
    cd build
    ```

2.  **Configure the project with CMake:**
    ```bash
    cmake ..
    ```

3.  **Build the project:**
    ```bash
    make
    ```

## Command-Line Interface (CLI) Usage

The `logAnalyzer` executable provides a powerful command-line interface to access the library's features for analysis, filtering, and viewing logs directly from your terminal.

The basic syntax is: `logAnalyzer [GLOBAL_OPTIONS] <COMMAND> [COMMAND_OPTIONS] [INPUT_SOURCES...]`

### Commands

*   `analyze` (default): Analyzes logs and prints statistics.
*   `entries`: Lists filtered log entries.
*   `tail`: Monitors a log file in real-time, similar to `tail -f`.

### Global Options

*   `-h, --help`: Show the help message.
*   `--version`: Show version information.
*   `-v, --verbose`: Enable verbose output.
*   `-o, --output <path>`: Write output to a file instead of the console.
*   `--format <fmt>`: Specify the output format. Options: `text`, `json`, `csv`, `markdown`.
*   `--pretty-print`: Enable pretty printing for JSON output.

### Input Source Options

*   `-f, --file <path>`: Specify a log file. Can be used multiple times.
*   `-d, --directory <path>`: Specify a directory of log files.
*   `-r, --recursive`: Scan directories recursively. Must be specified before `-d`.
*   `--stdin`: Read log data from standard input.
*   You can also specify input files as positional arguments.

### Filtering Options

*   `-l, --level <level>`: Filter by minimum log level (e.g., `INFO`, `ERROR`).
*   `-k, --keyword <word>`: Filter entries containing a specific keyword.
*   `--regex-filter <pat>`: Filter entries where the message matches a regex pattern.
*   `--start-time <ts>`: Filter entries occurring after a given timestamp (ISO 8601 format).
*   `--end-time <ts>`: Filter entries occurring before a given timestamp (ISO 8601 format).

### Command-Specific Options

#### `analyze`
*   `--top-errors <N>`: Show the top N most frequent error messages.

#### `entries`
*   `--limit <N>`: Output only the first N matching entries.
*   `--tail <N>`: Output only the last N matching entries.

### Examples

*   **Analyze a log file for errors and warnings:**
    ```bash
    ./build/logAnalyzer analyze -f data/sample.log --level WARNING
    ```

*   **List the last 20 critical entries from a directory in JSON format:**
    ```bash
    ./build/logAnalyzer entries -d /var/log/my_app --recursive --level CRITICAL --tail 20 --format json --pretty-print
    ```

*   **Tail a log file in real-time:**
    ```bash
    ./build/logAnalyzer tail -f data/sample.log
    ```

*   **Filter logs from stdin between two times and save to a file:**
    ```bash
    cat data/sample.log | ./build/logAnalyzer entries --stdin --start-time "2023-10-26T10:00:00Z" --end-time "2023-10-26T12:00:00Z" -o filtered.log
    ```

## Run Instructions

After a successful build, the main executable `logAnalyzer` will be located in the `build` directory. You can run it directly from there, providing the desired commands and options as described above.

```bash
# Show the help message to see all available options
./build/logAnalyzer --help
```

## Running Tests

To execute the unit tests, run the following commands from within the `build` directory:

```bash
make test
# or
ctest
