# p-search
p-search: High-Performance Parallel Text Search Tool

**p-search** is a lightweight, efficient command-line utility designed to search for strings within massive files). 

Unlike standard single-threaded search tools, p-search utilises Linux System Calls to distribute the workload across multiple CPU cores, maximizing I/O throughput via Memory Mapped I/O (`mmap`) and Parallel Processing.

-----

## Key Features 

* **Multi-Core Architecture:** Automatically detects available CPU cores and spawns worker processes using `fork()`.
* **Zero-Copy I/O:** Uses `mmap()` to map file contents directly into virtual memory, avoiding expensive kernel-to-user buffer copying for file-based searches.
* **Stream Processing:** Supports **Standard Input (STDIN)** via dynamic buffering, allowing seamless integration into Unix pipelines (e.g., `cat log.txt | ./p_search ...`).
* **Inter-Process Communication (IPC):** Workers stream results atomically to a shared `pipe()`, preventing output corruption without complex locking mechanisms.

------

## Installation

### Prerequisites
* GCC Compiler
* Linux/Unix Environment (WSL is also supported)

### Build
Run `make` to compile with optimisation flags:
```bash
make
```
To clean artifacts:
```bash
make clean
```

### Usage

The syntax follows standard Unix conventions:
```bash
./p_search [flags] <keyword> [filename] [num_workers]
```
Supported Flags:

* **-i, Case Insensitive:** Ignore case distinctions in patterns.
* **-n, Line Number:** Print the line number for each match.
* **-w, Whole Word:** Match only whole words (e.g., "log" wont match "login").
* **-F, Fixed String**: Interpret pattern as a fixed string (default behavior).

### Performance & Limitations

* **Regex:** This tool focuses on raw I/O performance for string matching. It deliberately does not implement a full Regex engine to avoid the CPU overhead associated with state machines.
* **Line Numbers:** Enabling line numbers requires calculating new lines from the start of the file for every match. Even though accurate, this might impact performance on extremely large files compared to raw searching.

*Note: p-search is not meant to replace grep.*
