# BlazeKV

## Description
BlazeKV is a fully-featured, thread-safe, in-memory Redis-like key-value store built entirely in C++ for fast caching and session management. Engineered for the uTrade Solutions Campus Hiring assignment, it combines high concurrency, advanced data structures, and efficient memory management into a streamlined single-file application.

## Features
- **Concurrent Access:** Thread-safe operations using Multiple-Readers/Single-Writer locks.
- **Lazy & Active Expiration:** Keys automatically expire via a dual-layered mechanism ensuring exact TTL precision and no memory leaks.
- **Snapshot Persistence:** Save and Load the entire state to a JSON file.
- **Interactive REPL:** A clean, Stdin-based text protocol for instant interaction.

## Bonus Features Implemented
- ⭐ **Atomic Integers:** `INCR` and `DECR` for lightning-fast counters.
- ⭐ **List Operations:** `LPUSH`, `RPUSH`, `LPOP`, and `RPOP` for managing sequence data.
- ⭐ **Pub/Sub Mechanism:** `SUBSCRIBE` and `PUBLISH` mocking for real-time messaging simulation.
- ⭐ **LRU Eviction:** Configurable memory limits (default 1000 keys) with strict Least Recently Used (LRU) pruning across all access methods.

## Commands Reference

| Command | Syntax | Description |
|---|---|---|
| **SET** | `SET key value [EX seconds]` | Stores a string. Optionally expires after `EX`. |
| **GET** | `GET key` | Retrieves the value of a string key. |
| **DEL** | `DEL key` | Deletes a key and its value. |
| **KEYS** | `KEYS pattern` | Finds keys matching a glob pattern (`*` or `?`). |
| **TTL** | `TTL key` | Returns remaining seconds, `-1` if no expiry, `-2` if missing. |
| **INCR** | `INCR key` | Atomically increments an integer. Creates as `1` if missing. |
| **DECR** | `DECR key` | Atomically decrements an integer. Creates as `-1` if missing. |
| **LPUSH** | `LPUSH key val [val...]` | Prepends one or more values to a list. |
| **RPUSH** | `RPUSH key val [val...]` | Appends one or more values to a list. |
| **LPOP** | `LPOP key` | Removes and returns the first element of a list. |
| **RPOP** | `RPOP key` | Removes and returns the last element of a list. |
| **SUBSCRIBE** | `SUBSCRIBE channel` | Subscribes the current session to a channel. |
| **PUBLISH** | `PUBLISH channel msg` | Pushes a message to subscribers. |
| **SAVE** | `SAVE` | Snapshots the state to `snapshot.json`. |
| **LOAD** | `LOAD` | Restores the state from `snapshot.json`. |
| **STATS** | `STATS` | Prints memory usage, key counts, and cleanup metrics. |

## How to Compile
You need a C++17 compliant compiler (`g++`, `clang++`, or MSVC).

```bash
# Using g++
g++ -std=c++17 -O2 main.cpp -o blazekv.exe
```

## How to Run
```bash
./blazekv.exe
```

## Complete Test Suite
You can copy and paste the following commands directly into the `BlazeKV>` prompt to test all features:

```text
SET user:1 "John Doe" EX 300
GET user:1
TTL user:1
INCR counter
INCR counter
DECR counter
GET counter
LPUSH tasks "Task 1"
LPUSH tasks "Task 2"
RPUSH tasks "Task 3"
LPOP tasks
RPOP tasks
SET user:2 "Jane"
KEYS user:*
DEL user:2
SUBSCRIBE notifications
PUBLISH notifications "Hello World!"
SAVE
STATS
LOAD
```

**Expected Output:**
```text
OK
John Doe
300
1
2
1
1
1
2
3
Task 2
Task 3
OK
user:1
user:2
OK
Subscribed to channel: notifications
[MESSAGE - notifications] Hello World!
1
OK
Total keys: 3...
OK
```

## Architecture & Design Explanations

### Design Explanation
BlazeKV is structured around an interactive REPL pattern backed by the `BlazeKV` C++ class. By using `std::variant<std::string, std::deque<std::string>>`, the engine elegantly supports dynamic typing (Strings vs Lists) natively without convoluted pointer casting. Command parsing is implemented via a lightweight, quote-aware tokenizer.

### Thread-Safety Explanation
The core engine relies on `std::shared_mutex` to implement the Multiple-Readers/Single-Writer (MRSW) pattern. Operations that read state (like `KEYS` or `TTL`) lock the mutex in shared mode (`std::shared_lock`). Operations that mutate state (like `SET`, `INCR`, `LPUSH`) use an exclusive lock (`std::unique_lock`).

### TTL Expiration Explanation
1. **Lazy Evaluation:** Before `GET`, `LPOP`, or `INCR` interact with a key, a check against `std::chrono::system_clock::now()` ensures the client never receives stale data.
2. **Active Cleanup:** A detached `std::thread` wakes up every 1 second, exclusively locks the database, and prunes expired keys to ensure background garbage collection without client intervention.

### Persistence Explanation
The `SAVE` command iterates through all unexpired data and serializes the `std::variant` instances into JSON using `nlohmann/json`. Keys, variants, and TTL UNIX timestamps are dumped synchronously. `LOAD` acts inversely, restoring both lists and strings.

### LRU Eviction Explanation
A strictly synchronized `std::list<std::string>` tracks the access order, and an `std::unordered_map` points directly to the list's nodes for O(1) removals. Any successful read/write (`touch_lru()`) bumps the key to the front of the list. When `data_.size() > max_keys`, the tail of the list is popped and the corresponding key is deleted.
