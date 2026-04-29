#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <deque>
#include <variant>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include "json.hpp"

using json = nlohmann::json;

using ValueType = std::variant<std::string, std::deque<std::string>>;

class BlazeKV {
public:
    BlazeKV(size_t max_keys = 1000) : max_keys_(max_keys) {
        cleanup_thread_ = std::thread(&BlazeKV::cleanup_loop, this);
    }

    ~BlazeKV() {
        stop_cleanup_ = true;
        if (cleanup_thread_.joinable()) {
            cleanup_thread_.join();
        }
    }

    std::string process_command(const std::string& command_line) {
        if (command_line.empty()) return "";

        std::vector<std::string> args = tokenize(command_line);
        if (args.empty()) return "";

        std::string cmd = args[0];
        for (auto& c : cmd) c = (char)std::toupper((unsigned char)c);

        if (cmd == "SET") {
            if (args.size() < 3) return "ERR wrong number of arguments for 'SET' command";
            std::string key = args[1];
            std::string val = args[2];
            int ex = -1;
            if (args.size() >= 5 && (args[3] == "EX" || args[3] == "ex")) {
                try {
                    ex = std::stoi(args[4]);
                } catch (...) {
                    return "ERR invalid expire time";
                }
            }
            return set(key, val, ex);
        } else if (cmd == "GET") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'GET' command";
            return get(args[1]);
        } else if (cmd == "DEL") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'DEL' command";
            return del(args[1]);
        } else if (cmd == "KEYS") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'KEYS' command";
            return keys(args[1]);
        } else if (cmd == "TTL") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'TTL' command";
            return std::to_string(ttl(args[1]));
        } else if (cmd == "INCR") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'INCR' command";
            return incr(args[1]);
        } else if (cmd == "DECR") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'DECR' command";
            return decr(args[1]);
        } else if (cmd == "LPUSH") {
            if (args.size() < 3) return "ERR wrong number of arguments for 'LPUSH' command";
            std::vector<std::string> values(args.begin() + 2, args.end());
            return lpush(args[1], values);
        } else if (cmd == "RPUSH") {
            if (args.size() < 3) return "ERR wrong number of arguments for 'RPUSH' command";
            std::vector<std::string> values(args.begin() + 2, args.end());
            return rpush(args[1], values);
        } else if (cmd == "LPOP") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'LPOP' command";
            return lpop(args[1]);
        } else if (cmd == "RPOP") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'RPOP' command";
            return rpop(args[1]);
        } else if (cmd == "SUBSCRIBE") {
            if (args.size() != 2) return "ERR wrong number of arguments for 'SUBSCRIBE' command";
            return subscribe(args[1]);
        } else if (cmd == "PUBLISH") {
            if (args.size() != 3) return "ERR wrong number of arguments for 'PUBLISH' command";
            return publish(args[1], args[2]);
        } else if (cmd == "SAVE") {
            if (args.size() != 1) return "ERR wrong number of arguments for 'SAVE' command";
            return save("snapshot.json");
        } else if (cmd == "LOAD") {
            if (args.size() != 1) return "ERR wrong number of arguments for 'LOAD' command";
            return load("snapshot.json");
        } else if (cmd == "STATS") {
            return stats();
        } else if (cmd == "EXIT" || cmd == "QUIT") {
            // Handled in main loop
            return "";
        } else {
            return "ERR unknown command '" + cmd + "'";
        }
    }

private:
    std::string set(const std::string& key, const std::string& value, int ex_seconds) {
        std::unique_lock lock(mutex_);
        data_[key] = value;
        
        if (ex_seconds > 0) {
            expiry_[key] = std::chrono::system_clock::now() + std::chrono::seconds(ex_seconds);
        } else {
            expiry_.erase(key);
        }
        
        touch_lru(key);
        evict_if_needed();
        return "OK";
    }

    std::string get(const std::string& key) {
        std::unique_lock lock(mutex_);
        if (is_expired_unsafe(key)) {
            return "(nil)";
        }
        auto it = data_.find(key);
        if (it != data_.end()) {
            if (auto* str_val = std::get_if<std::string>(&it->second)) {
                touch_lru(key);
                return *str_val;
            } else {
                return "ERR WRONGTYPE Operation against a key holding the wrong kind of value";
            }
        }
        return "(nil)";
    }

    std::string del(const std::string& key) {
        std::unique_lock lock(mutex_);
        int removed = erase_unsafe(key) ? 1 : 0;
        return "OK"; // Or std::to_string(removed) for number of deleted items
    }

    std::string keys(const std::string& pattern) {
        std::shared_lock lock(mutex_);
        std::string res;
        auto now = std::chrono::system_clock::now();
        for (const auto& [k, v] : data_) {
            auto ex_it = expiry_.find(k);
            if (ex_it != expiry_.end() && now > ex_it->second) {
                continue;
            }
            if (match_glob(pattern.c_str(), k.c_str())) {
                res += k + "\n";
            }
        }
        if (!res.empty()) res.pop_back();
        else res = "(empty)";
        return res;
    }

    long long ttl(const std::string& key) {
        std::shared_lock lock(mutex_);
        auto d_it = data_.find(key);
        if (d_it == data_.end()) return -2;

        auto it = expiry_.find(key);
        if (it == expiry_.end()) return -1;

        auto now = std::chrono::system_clock::now();
        if (now > it->second) {
            return -2;
        }

        return std::chrono::duration_cast<std::chrono::seconds>(it->second - now).count();
    }

    std::string incr(const std::string& key) {
        std::unique_lock lock(mutex_);
        if (is_expired_unsafe(key)) {
            // Delete implicitly happened
        }
        auto it = data_.find(key);
        if (it == data_.end()) {
            data_[key] = "1";
            touch_lru(key);
            evict_if_needed();
            return "1";
        } else {
            if (auto* str_val = std::get_if<std::string>(&it->second)) {
                try {
                    long long val = std::stoll(*str_val);
                    val++;
                    *str_val = std::to_string(val);
                    touch_lru(key);
                    return *str_val;
                } catch (...) {
                    return "ERR value is not an integer or out of range";
                }
            } else {
                return "ERR WRONGTYPE Operation against a key holding the wrong kind of value";
            }
        }
    }

    std::string decr(const std::string& key) {
        std::unique_lock lock(mutex_);
        if (is_expired_unsafe(key)) {
        }
        auto it = data_.find(key);
        if (it == data_.end()) {
            data_[key] = "-1";
            touch_lru(key);
            evict_if_needed();
            return "-1";
        } else {
            if (auto* str_val = std::get_if<std::string>(&it->second)) {
                try {
                    long long val = std::stoll(*str_val);
                    val--;
                    *str_val = std::to_string(val);
                    touch_lru(key);
                    return *str_val;
                } catch (...) {
                    return "ERR value is not an integer or out of range";
                }
            } else {
                return "ERR WRONGTYPE Operation against a key holding the wrong kind of value";
            }
        }
    }

    std::string lpush(const std::string& key, const std::vector<std::string>& values) {
        std::unique_lock lock(mutex_);
        is_expired_unsafe(key); // Cleanup if expired
        
        auto it = data_.find(key);
        if (it == data_.end()) {
            data_[key] = std::deque<std::string>();
            it = data_.find(key);
        }
        
        if (auto* dq = std::get_if<std::deque<std::string>>(&it->second)) {
            for (const auto& val : values) {
                dq->push_front(val);
            }
            touch_lru(key);
            evict_if_needed();
            return std::to_string(dq->size());
        }
        return "ERR WRONGTYPE Operation against a key holding the wrong kind of value";
    }

    std::string rpush(const std::string& key, const std::vector<std::string>& values) {
        std::unique_lock lock(mutex_);
        is_expired_unsafe(key);
        
        auto it = data_.find(key);
        if (it == data_.end()) {
            data_[key] = std::deque<std::string>();
            it = data_.find(key);
        }
        
        if (auto* dq = std::get_if<std::deque<std::string>>(&it->second)) {
            for (const auto& val : values) {
                dq->push_back(val);
            }
            touch_lru(key);
            evict_if_needed();
            return std::to_string(dq->size());
        }
        return "ERR WRONGTYPE Operation against a key holding the wrong kind of value";
    }

    std::string lpop(const std::string& key) {
        std::unique_lock lock(mutex_);
        is_expired_unsafe(key);
        
        auto it = data_.find(key);
        if (it != data_.end()) {
            if (auto* dq = std::get_if<std::deque<std::string>>(&it->second)) {
                if (dq->empty()) return "(nil)";
                std::string val = dq->front();
                dq->pop_front();
                touch_lru(key);
                return val;
            }
            return "ERR WRONGTYPE Operation against a key holding the wrong kind of value";
        }
        return "(nil)";
    }

    std::string rpop(const std::string& key) {
        std::unique_lock lock(mutex_);
        is_expired_unsafe(key);
        
        auto it = data_.find(key);
        if (it != data_.end()) {
            if (auto* dq = std::get_if<std::deque<std::string>>(&it->second)) {
                if (dq->empty()) return "(nil)";
                std::string val = dq->back();
                dq->pop_back();
                touch_lru(key);
                return val;
            }
            return "ERR WRONGTYPE Operation against a key holding the wrong kind of value";
        }
        return "(nil)";
    }

    std::string subscribe(const std::string& channel) {
        // Mock pub/sub for single stdin session
        subscribed_channels_.insert(channel);
        return "Subscribed to channel: " + channel;
    }

    std::string publish(const std::string& channel, const std::string& message) {
        if (subscribed_channels_.count(channel)) {
            // Instantly print for mocked stdin pub/sub
            std::cout << "[MESSAGE - " << channel << "] " << message << std::endl;
            return "1"; // 1 subscriber received it
        }
        return "0";
    }

    std::string save(const std::string& filename) {
        std::shared_lock lock(mutex_);
        json j;
        auto now = std::chrono::system_clock::now();
        
        j["data"] = json::object();
        j["expiry"] = json::object();
        
        for (const auto& [k, v] : data_) {
            auto ex_it = expiry_.find(k);
            if (ex_it != expiry_.end() && now > ex_it->second) continue;
            
            if (auto* str_val = std::get_if<std::string>(&v)) {
                j["data"][k] = {{"type", "string"}, {"val", *str_val}};
            } else if (auto* dq_val = std::get_if<std::deque<std::string>>(&v)) {
                j["data"][k] = {{"type", "list"}, {"val", *dq_val}};
            }
            
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

    std::string load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return "ERR Failed to open file for reading";
        
        json j;
        try {
            file >> j;
        } catch (...) {
            return "ERR Failed to parse JSON";
        }
        
        std::unique_lock lock(mutex_);
        data_.clear();
        expiry_.clear();
        lru_list_.clear();
        lru_map_.clear();
        
        if (j.contains("data") && j["data"].is_object()) {
            for (const auto& item : j["data"].items()) {
                auto type = item.value()["type"].get<std::string>();
                if (type == "string") {
                    data_[item.key()] = item.value()["val"].get<std::string>();
                } else if (type == "list") {
                    data_[item.key()] = item.value()["val"].get<std::deque<std::string>>();
                }
                touch_lru(item.key());
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

    std::string stats() {
        std::shared_lock lock(mutex_);
        std::stringstream ss;
        ss << "Total keys: " << data_.size() << "\n";
        ss << "Keys with expiry: " << expiry_.size() << "\n";
        ss << "Expired keys cleaned: " << expired_keys_cleaned_.load() << "\n";
        
        size_t mem = 0;
        for (const auto& [k, v] : data_) {
            mem += sizeof(k) + k.capacity();
            if (auto* s = std::get_if<std::string>(&v)) {
                mem += sizeof(*s) + s->capacity();
            } else if (auto* dq = std::get_if<std::deque<std::string>>(&v)) {
                mem += sizeof(*dq);
                for (const auto& item : *dq) mem += sizeof(item) + item.capacity();
            }
        }
        mem += (data_.size() * 32); // overhead
        
        ss << "Memory usage estimate: " << mem << " bytes";
        return ss.str();
    }

    // INTERNAL UTILS
    void cleanup_loop() {
        while (!stop_cleanup_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::unique_lock lock(mutex_);
            auto now = std::chrono::system_clock::now();
            for (auto it = expiry_.begin(); it != expiry_.end(); ) {
                if (now > it->second) {
                    erase_unsafe(it->first);
                    it = expiry_.erase(it);
                    expired_keys_cleaned_++;
                } else {
                    ++it;
                }
            }
        }
    }

    bool erase_unsafe(const std::string& key) {
        if (data_.erase(key)) {
            expiry_.erase(key);
            auto it = lru_map_.find(key);
            if (it != lru_map_.end()) {
                lru_list_.erase(it->second);
                lru_map_.erase(it);
            }
            return true;
        }
        return false;
    }

    bool is_expired_unsafe(const std::string& key) {
        auto it = expiry_.find(key);
        if (it != expiry_.end() && std::chrono::system_clock::now() > it->second) {
            erase_unsafe(key);
            expiry_.erase(it);
            expired_keys_cleaned_++;
            return true;
        }
        return false;
    }

    void touch_lru(const std::string& key) {
        auto it = lru_map_.find(key);
        if (it != lru_map_.end()) {
            lru_list_.erase(it->second);
        }
        lru_list_.push_front(key);
        lru_map_[key] = lru_list_.begin();
    }

    void evict_if_needed() {
        while (data_.size() > max_keys_ && !lru_list_.empty()) {
            std::string lru_key = lru_list_.back();
            erase_unsafe(lru_key);
            // erase_unsafe handles lru_list and lru_map
        }
    }

    std::vector<std::string> tokenize(const std::string& cmd) {
        std::vector<std::string> args;
        std::string current;
        bool in_quotes = false;
        for (char c : cmd) {
            if (c == '\'' || c == '"') {
                in_quotes = !in_quotes;
            } else if (c == ' ' && !in_quotes) {
                if (!current.empty()) {
                    args.push_back(current);
                    current.clear();
                }
            } else if (c != '\r' && c != '\n') {
                current += c;
            }
        }
        if (!current.empty()) args.push_back(current);
        return args;
    }

    static bool match_glob(const char* pattern, const char* str) {
        if (*pattern == '\0') return *str == '\0';
        if (*pattern == '*') return match_glob(pattern + 1, str) || (*str != '\0' && match_glob(pattern, str + 1));
        if (*str == '\0') return false;
        if (*pattern == '?' || *pattern == *str) return match_glob(pattern + 1, str + 1);
        return false;
    }

    // STATE
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ValueType> data_;
    std::unordered_map<std::string, std::chrono::time_point<std::chrono::system_clock>> expiry_;
    
    // LRU state
    size_t max_keys_;
    std::list<std::string> lru_list_;
    std::unordered_map<std::string, std::list<std::string>::iterator> lru_map_;

    // PubSub State
    std::unordered_set<std::string> subscribed_channels_;

    // Thread State
    std::atomic<bool> stop_cleanup_{false};
    std::thread cleanup_thread_;
    std::atomic<size_t> expired_keys_cleaned_{0};
};

int main() {
    BlazeKV store(1000); // Max 1000 keys for LRU

    std::cout << "========================================\n";
    std::cout << " BlazeKV - In-Memory Key-Value Store\n";
    std::cout << " Type commands (e.g. SET foo bar EX 10)\n";
    std::cout << " Type EXIT or QUIT to terminate.\n";
    std::cout << "========================================\n\n";

    std::string line;
    while (true) {
        std::cout << "BlazeKV> ";
        if (!std::getline(std::cin, line)) break;
        
        std::string cmd;
        for (char c : line) {
            if (c == ' ') break;
            cmd += (char)std::toupper((unsigned char)c);
        }
        if (cmd == "EXIT" || cmd == "QUIT") break;

        std::string res = store.process_command(line);
        if (!res.empty()) {
            std::cout << res << std::endl;
        }
    }

    std::cout << "Shutting down BlazeKV..." << std::endl;
    return 0;
}
