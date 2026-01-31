# logAnalyzer

A C++ project designed for parsing, analyzing, and processing log files. It provides core components for handling individual log entries and a main analyzer class to process log data from various sources.

## Project Structure

*   `CMakeLists.txt`: The CMake build configuration file.
*   `data/`: Contains sample log files (e.g., `sample.log`) for testing or demonstration.
*   `include/`: Header files for the core classes (`LogAnalyzer.h`, `LogEntry.h`).
*   `src/`: Source files implementing the project's logic (`LogAnalyzer.cpp`, `LogEntry.cpp`, `main.cpp`).
*   `test/`: Unit tests for the project's components (`LogAnalyzerTest.cpp`, `LogEntryTest.cpp`).

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