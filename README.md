# C++ In-Memory Key-Value Store

A simple, thread-safe, Redis-like in-memory key-value store. Built as part of the uTrade Solutions Campus Hiring assignment.

## Design Decisions

1. **Architecture Overview**: The system is split into a core storage engine (`KVStore`) and a networking layer (`TCPServer`). This decoupling allows the core engine to be easily tested or used with different interfaces in the future.
2. **Thread Safety**: High concurrency is supported via a Multiple Readers / Single Writer pattern. We use `std::shared_mutex`, which allows concurrent `GET`, `TTL`, and `KEYS` commands (using `std::shared_lock`) while exclusively locking state-mutating commands like `SET` and `DEL` (using `std::unique_lock`).
3. **Expiration Mechanism**: A hybrid approach to expiration is implemented:
    - **Lazy Expiration**: Checked on every read (`GET`, `TTL`, `INCR`). If an accessed key is expired, it is deleted and reported as non-existent.
    - **Active Background Cleanup**: A dedicated thread sweeps the memory periodically (every 1 second) to prune orphaned keys that haven't been accessed recently, preventing out-of-memory issues.
4. **Networking Model**: The TCP Server accepts connections and spawns an isolated, detached `std::thread` per client. This ensures that one slow or unresponsive client does not bottleneck the entire server.

## Data Structures and Algorithms

- **Primary Data Store**: `std::unordered_map<std::string, std::string>`
  - *Why?* O(1) average time complexity for insertion, deletion, and lookup.
- **TTL Tracking**: `std::unordered_map<std::string, std::chrono::time_point<std::chrono::system_clock>>`
  - *Why?* Allows O(1) checks during `GET`/`SET`/`DEL`.
- **KEYS Pattern Matching**: A standard recursive string matching algorithm.
  - *Why?* Efficient enough for basic glob matching without the heavy compilation overhead of `<regex>`.
- **JSON Serialization**: Using `nlohmann/json` (a popular single-header C++ library).
  - *Why?* Standard, robust, and clean approach to fulfilling the snapshot requirements.

## How to Build and Run

### Prerequisites
- C++17 compliant compiler (`g++` or MSVC)
- Windows OS (Network layer relies on Winsock2)

### Building
Using GCC (MinGW/MSYS2):
```bash
g++ -std=c++17 -O2 main.cpp store.cpp server.cpp -o kvstore.exe -lws2_32
```

Using CMake (if available):
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Running
```bash
# Start the server (defaults to port 6379)
./kvstore.exe

# Start on a custom port
./kvstore.exe 8080
```

### Connecting as a Client
You can interact with the server using a simple TCP client like `telnet`.
```bash
telnet 127.0.0.1 6379
```

**Commands Supported:**
- `SET key value [EX seconds]`
- `GET key`
- `DEL key`
- `KEYS pattern` (supports `*` and `?`)
- `TTL key`
- `SAVE [filename]`
- `LOAD [filename]`
- `STATS`
- `INCR key` (Bonus)
- `DECR key` (Bonus)

## Trade-offs and Known Limitations

1. **Thread per Client Model**: The server currently creates a new OS thread for every active connection. This is simple and effective for low-to-medium concurrency but doesn't scale to C10k. For ultra-high concurrency, an asynchronous event loop (e.g., using `epoll` or IOCP) would be superior.
2. **Global Lock Contention**: A single `std::shared_mutex` manages the entire `KVStore`. During high write-throughput, lock contention could bottleneck performance. Sharding the `unordered_map` into multiple buckets with distinct locks would mitigate this.
3. **Memory Overhead**: C++ `std::unordered_map` has noticeable per-node memory overhead. If millions of tiny keys are expected, a flat array-based hash map might save memory.
4. **Command Tokenizer**: The naive text tokenizer respects single and double quotes to group terms but strips them from the final string. It doesn't feature full escape sequence parsing (like `\"`) for complex nested JSON.
