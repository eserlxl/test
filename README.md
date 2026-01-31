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
*   **Dynamic Context Capture**: Methods to automatically capture system-level information like process ID, host name, system load averages, and memory usage.
*   **JSON Serialization**: Convert `LogEntry` objects to JSON with extensive customization options:
    *   Pretty printing
    *   Inclusion/exclusion of source, thread, tracing info, and empty fields
    *   Configurable timestamp formats (Default, ISO8601, UnixMillis)
    *   Adjustable timestamp precision (Seconds, Millis, Micros, Nanos)
    *   Timezone selection (Local, UTC)
    *   Binary data encoding (Hex, Base64)
    *   Custom `strftime` format for timestamps
*   **Comparison and Utilities**: Supports comparison operations, level parsing, and conversion to `std::map<std::string, LogValue>`.

### `LogAnalyzer` Features

The `LogAnalyzer` class provides a high-level interface for processing, analyzing, and exporting log data:

*   **Versatile Log Loading**: Process logs from various sources, including single files, directories (recursively), and standard input. Supports high-performance parallel and asynchronous file loading.
*   **Real-time Monitoring**: Live-monitor log files with `tailFile`, providing a `tail -f`-like capability to process new log entries as they are written.
*   **Flexible Parsing**: A powerful and configurable regex-based engine (`ParsingConfig`) allows for parsing a wide variety of log formats, with support for multi-line log entries, named capture groups, and custom field parsers.
*   **Powerful Filtering Engine**:
    *   **User-Friendly Query Language**: Filter logs using simple, intuitive query strings (e.g., `level:ERROR AND http.status >= 500`).
    *   **Composable Predicates**: Programmatically build complex filter logic using a tree of predicates (`And`, `Or`, `Not`, `Keyword`, `Regex`, `Attribute`, `TimeRange`, etc.).
*   **Advanced Analysis**: Go beyond simple counts with advanced statistical analysis, configurable via `AnalysisConfig`:
    *   **Message Templating**: Group similar log messages into templates to identify common event types.
    *   **Attribute Analysis**: Compute value distributions and top-N occurrences for specified attributes to understand trends.
*   **Log Enrichment and Anonymization**:
    *   **Enrichment**: Add contextual information to log entries on-the-fly using custom `LogEnricher` functions.
    *   **Anonymization**: Automatically find and redact sensitive data (like PII, passwords, or tokens) using the `LogAnonymizer` framework and `RegexAnonymizer`.
*   **Configuration Persistence**: Serialize parsing (`ParsingConfig`) and filtering (`FilterOptions`) configurations to and from JSON, making it easy to save, share, and reuse settings.
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

## Run Instructions

After a successful build, the executable will be located in the `build` directory.

```bash
./logAnalyzer
```

## Running Tests

To execute the unit tests, run the following commands from within the `build` directory:

```bash
make test
# or
ctest
