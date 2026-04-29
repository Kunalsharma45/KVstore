#include "store.h"
#include "json.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

using json = nlohmann::json;

KVStore::KVStore() {
    cleanup_thread_ = std::thread(&KVStore::cleanup_loop, this);
}

KVStore::~KVStore() {
    stop_cleanup_ = true;
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

void KVStore::cleanup_loop() {
    while (!stop_cleanup_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Take an exclusive lock to perform cleanup
        std::unique_lock lock(mutex_);
        auto now = std::chrono::system_clock::now();
        for (auto it = expiry_.begin(); it != expiry_.end(); ) {
            if (now > it->second) {
                data_.erase(it->first);
                it = expiry_.erase(it);
                expired_keys_cleaned_++;
            } else {
                ++it;
            }
        }
    }
}

bool KVStore::is_expired_unsafe(const std::string& key) {
    auto it = expiry_.find(key);
    if (it != expiry_.end()) {
        if (std::chrono::system_clock::now() > it->second) {
            data_.erase(key);
            expiry_.erase(it);
            expired_keys_cleaned_++;
            return true;
        }
    }
    return false;
}

std::string KVStore::set(const std::string& key, const std::string& value, int ex_seconds) {
    std::unique_lock lock(mutex_);
    data_[key] = value;
    if (ex_seconds > 0) {
        expiry_[key] = std::chrono::system_clock::now() + std::chrono::seconds(ex_seconds);
    } else {
        expiry_.erase(key);
    }
    return "OK";
}

std::optional<std::string> KVStore::get(const std::string& key) {
    // We might need to write (lazy expiration), so we can't use shared_lock if we erase
    // For simplicity, take unique lock for now since read might mutate state.
    {
        std::shared_lock lock(mutex_);
        auto it = expiry_.find(key);
        if (it != expiry_.end() && std::chrono::system_clock::now() > it->second) {
            // Needs deletion, fall through to unique_lock block
        } else {
            auto d_it = data_.find(key);
            if (d_it != data_.end()) return d_it->second;
            return std::nullopt;
        }
    }
    
    // Lazy deletion
    std::unique_lock lock(mutex_);
    if (is_expired_unsafe(key)) {
        return std::nullopt;
    }
    auto d_it = data_.find(key);
    if (d_it != data_.end()) return d_it->second;
    return std::nullopt;
}

int KVStore::del(const std::string& key) {
    std::unique_lock lock(mutex_);
    int removed = data_.erase(key) > 0 ? 1 : 0;
    expiry_.erase(key);
    return removed;
}

static bool match_glob(const char* pattern, const char* str) {
    if (*pattern == '\0') return *str == '\0';
    if (*pattern == '*') {
        return match_glob(pattern + 1, str) || (*str != '\0' && match_glob(pattern, str + 1));
    }
    if (*str == '\0') return false;
    if (*pattern == '?' || *pattern == *str) {
        return match_glob(pattern + 1, str + 1);
    }
    return false;
}

std::vector<std::string> KVStore::keys(const std::string& pattern) {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    auto now = std::chrono::system_clock::now();
    for (const auto& [k, v] : data_) {
        // Skip expired keys (don't lazy delete here, just ignore)
        auto ex_it = expiry_.find(k);
        if (ex_it != expiry_.end() && now > ex_it->second) {
            continue;
        }
        if (match_glob(pattern.c_str(), k.c_str())) {
            result.push_back(k);
        }
    }
    return result;
}

long long KVStore::ttl(const std::string& key) {
    std::shared_lock lock(mutex_);
    auto d_it = data_.find(key);
    if (d_it == data_.end()) return -2;

    auto it = expiry_.find(key);
    if (it == expiry_.end()) return -1;

    auto now = std::chrono::system_clock::now();
    if (now > it->second) {
        // Expired but not yet lazily deleted
        return -2;
    }

    auto diff = std::chrono::duration_cast<std::chrono::seconds>(it->second - now).count();
    return diff;
}

std::optional<long long> KVStore::incr(const std::string& key) {
    std::unique_lock lock(mutex_);
    if (is_expired_unsafe(key)) {
        data_[key] = "1";
        return 1;
    }
    auto it = data_.find(key);
    if (it == data_.end()) {
        data_[key] = "1";
        return 1;
    } else {
        try {
            long long val = std::stoll(it->second);
            val++;
            it->second = std::to_string(val);
            return val;
        } catch (...) {
            return std::nullopt; // Not an integer
        }
    }
}

std::optional<long long> KVStore::decr(const std::string& key) {
    std::unique_lock lock(mutex_);
    if (is_expired_unsafe(key)) {
        data_[key] = "-1";
        return -1;
    }
    auto it = data_.find(key);
    if (it == data_.end()) {
        data_[key] = "-1";
        return -1;
    } else {
        try {
            long long val = std::stoll(it->second);
            val--;
            it->second = std::to_string(val);
            return val;
        } catch (...) {
            return std::nullopt; // Not an integer
        }
    }
}

std::string KVStore::save(const std::string& filename) {
    std::shared_lock lock(mutex_);
    json j;
    auto now = std::chrono::system_clock::now();
    
    j["data"] = json::object();
    j["expiry"] = json::object();
    
    for (const auto& [k, v] : data_) {
        // Skip expired keys
        auto ex_it = expiry_.find(k);
        if (ex_it != expiry_.end() && now > ex_it->second) {
            continue;
        }
        
        j["data"][k] = v;
        if (ex_it != expiry_.end()) {
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(ex_it->second.time_since_epoch()).count();
            j["expiry"][k] = timestamp;
        }
    }
    
    std::ofstream file(filename);
    if (file.is_open()) {
        file << j.dump(4);
        return "OK";
    }
    return "ERR Failed to open file for writing";
}

std::string KVStore::load(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        return "ERR Failed to open file for reading";
    }
    
    json j;
    try {
        file >> j;
    } catch (...) {
        return "ERR Failed to parse JSON";
    }
    
    std::unique_lock lock(mutex_);
    data_.clear();
    expiry_.clear();
    
    if (j.contains("data") && j["data"].is_object()) {
        for (const auto& item : j["data"].items()) {
            data_[item.key()] = item.value().get<std::string>();
        }
    }
    
    if (j.contains("expiry") && j["expiry"].is_object()) {
        for (const auto& item : j["expiry"].items()) {
            long long timestamp = item.value().get<long long>();
            std::chrono::milliseconds dur(timestamp);
            std::chrono::time_point<std::chrono::system_clock> tp(dur);
            expiry_[item.key()] = tp;
        }
    }
    
    return "OK";
}

std::string KVStore::stats() {
    std::shared_lock lock(mutex_);
    std::stringstream ss;
    ss << "Total keys: " << data_.size() << "\n";
    ss << "Keys with expiry: " << expiry_.size() << "\n";
    ss << "Expired keys cleaned: " << expired_keys_cleaned_.load() << "\n";
    
    // Rough memory estimate
    size_t mem = 0;
    for (const auto& [k, v] : data_) {
        mem += k.capacity() + v.capacity() + sizeof(k) + sizeof(v);
    }
    for (const auto& [k, v] : expiry_) {
        mem += k.capacity() + sizeof(v) + sizeof(k);
    }
    // Add overhead of unordered_map nodes (approximate 32 bytes per node)
    mem += (data_.size() + expiry_.size()) * 32;
    
    ss << "Memory usage estimate: " << mem << " bytes\n";
    return ss.str();
}
