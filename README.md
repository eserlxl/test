# logAnalyzer

A C++ project designed for parsing and analyzing log files, breaking them down into structured log entries.

## Getting Started

These instructions will get you a copy of the project up and running on your local machine.

### Prerequisites

*   A C++ compiler (e.g., g++, clang)
*   CMake (version 3.10 or higher recommended)

### Building

1.  Create a build directory and navigate into it:
    ```bash
    mkdir build
    cd build
    ```
2.  Configure and build the project:
    ```bash
    cmake ..
    make
    ```

### Running

After building, the executable `logAnalyzer` will be located in your `build` directory.

```bash
./logAnalyzer
```

*Example usage with a sample log file:*
```bash
./logAnalyzer ../data/sample.log
```

### Running Tests

From the `build` directory, you can execute the unit tests:

```bash
make test
# OR
ctest