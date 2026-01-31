# logAnalyzer

A powerful C++ project designed for structured log parsing, analysis, and processing. It provides robust core components for handling individual log entries with rich metadata and a main analyzer class to process log data from various sources. This library is built for high-performance and flexibility, supporting advanced features like structured attributes, tracing context, and flexible JSON serialization.

## Project Structure

*   `CMakeLists.txt`: The CMake build configuration file.
*   `data/`: Contains sample log files (e.g., `sample.log`) for testing or demonstration.
*   `include/`: Header files for the core classes (`LogAnalyzer.h`, `LogEntry.h`).
*   `src/`: Source files implementing the project's logic (`LogAnalyzer.cpp`, `LogEntry.cpp`, `main.cpp`).
*   `test/`: Unit tests for the project's components (`LogAnalyzerTest.cpp`, `LogEntryTest.cpp`).

## Features

The `LogEntry` class is at the heart of the `logAnalyzer`, offering a comprehensive model for log data:

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
