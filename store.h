#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <optional>

class KVStore {
public:
    KVStore();
    ~KVStore();

    // Core commands
    std::string set(const std::string& key, const std::string& value, int ex_seconds = -1);
    std::optional<std::string> get(const std::string& key);
    int del(const std::string& key);
    std::vector<std::string> keys(const std::string& pattern);
    long long ttl(const std::string& key);
    
    // Bonus: INCR/DECR
    std::optional<long long> incr(const std::string& key);
    std::optional<long long> decr(const std::string& key);

    // Persistence
    std::string save(const std::string& filename);
    std::string load(const std::string& filename);

    // Stats
    std::string stats();

private:
    void cleanup_loop();
    bool is_expired_unsafe(const std::string& key);

    // Data structures
    std::unordered_map<std::string, std::string> data_;
    std::unordered_map<std::string, std::chrono::time_point<std::chrono::system_clock>> expiry_;

    // Thread safety
    mutable std::shared_mutex mutex_;

    // Background cleanup
    std::atomic<bool> stop_cleanup_{false};
    std::thread cleanup_thread_;

    // Stats tracking
    std::atomic<size_t> expired_keys_cleaned_{0};
};
