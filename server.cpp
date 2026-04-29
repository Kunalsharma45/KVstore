#include "server.h"
#include <iostream>
#include <sstream>
#include <vector>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

TCPServer::TCPServer(KVStore& store, int port) : store_(store), port_(port), server_socket_(INVALID_SOCKET), running_(false) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        exit(1);
    }
}

TCPServer::~TCPServer() {
    stop();
    WSACleanup();
}

void TCPServer::start() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket_ == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        return;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);

    if (bind(server_socket_, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed." << std::endl;
        closesocket(server_socket_);
        return;
    }

    if (listen(server_socket_, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed." << std::endl;
        closesocket(server_socket_);
        return;
    }

    running_ = true;
    std::cout << "Server listening on port " << port_ << "..." << std::endl;
    accept_thread_ = std::thread(&TCPServer::accept_loop, this);
}

void TCPServer::stop() {
    running_ = false;
    if (server_socket_ != INVALID_SOCKET) {
        closesocket(server_socket_);
        server_socket_ = INVALID_SOCKET;
    }
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
}

void TCPServer::accept_loop() {
    while (running_) {
        sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket_, (sockaddr*)&client_addr, &client_len);
        if (client_socket == INVALID_SOCKET) {
            if (running_) {
                std::cerr << "Accept failed." << std::endl;
            }
            continue;
        }

        std::thread(&TCPServer::handle_client, this, client_socket).detach();
    }
}

std::string TCPServer::process_command(const std::string& command_line) {
    if (command_line.empty()) return "";

    std::vector<std::string> args;
    std::string current_arg;
    bool in_quotes = false;
    
    // Simple tokenizer supporting quotes for json values like '{"name":"Alice"}'
    for (size_t i = 0; i < command_line.length(); ++i) {
        char c = command_line[i];
        if (c == '\'' || c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!current_arg.empty()) {
                args.push_back(current_arg);
                current_arg.clear();
            }
        } else if (c != '\r' && c != '\n') {
            current_arg += c;
        }
    }
    if (!current_arg.empty()) {
        args.push_back(current_arg);
    }

    if (args.empty()) return "";

    std::string cmd = args[0];
    // To uppercase
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
        return store_.set(key, val, ex);
    } else if (cmd == "GET") {
        if (args.size() != 2) return "ERR wrong number of arguments for 'GET' command";
        auto val = store_.get(args[1]);
        if (val) return *val;
        return "(nil)";
    } else if (cmd == "DEL") {
        if (args.size() != 2) return "ERR wrong number of arguments for 'DEL' command";
        store_.del(args[1]);
        return "OK";
    } else if (cmd == "KEYS") {
        if (args.size() != 2) return "ERR wrong number of arguments for 'KEYS' command";
        auto keys = store_.keys(args[1]);
        if (keys.empty()) return "(empty)";
        std::string res;
        for (const auto& k : keys) {
            res += k + "\n";
        }
        // Remove last newline
        if (!res.empty()) res.pop_back();
        return res;
    } else if (cmd == "TTL") {
        if (args.size() != 2) return "ERR wrong number of arguments for 'TTL' command";
        return std::to_string(store_.ttl(args[1]));
    } else if (cmd == "SAVE") {
        if (args.size() != 1 && args.size() != 2) return "ERR wrong number of arguments for 'SAVE' command";
        std::string file = (args.size() == 2) ? args[1] : "dump.json";
        return store_.save(file);
    } else if (cmd == "LOAD") {
        if (args.size() != 1 && args.size() != 2) return "ERR wrong number of arguments for 'LOAD' command";
        std::string file = (args.size() == 2) ? args[1] : "dump.json";
        return store_.load(file);
    } else if (cmd == "STATS") {
        return store_.stats();
    } else if (cmd == "INCR") {
        if (args.size() != 2) return "ERR wrong number of arguments for 'INCR' command";
        auto val = store_.incr(args[1]);
        if (val) return std::to_string(*val);
        return "ERR value is not an integer or out of range";
    } else if (cmd == "DECR") {
        if (args.size() != 2) return "ERR wrong number of arguments for 'DECR' command";
        auto val = store_.decr(args[1]);
        if (val) return std::to_string(*val);
        return "ERR value is not an integer or out of range";
    } else if (cmd == "PING") {
        return "PONG";
    } else {
        return "ERR unknown command '" + cmd + "'";
    }
}

void TCPServer::handle_client(SOCKET client_socket) {
    char buffer[4096];
    std::string current_line;

    while (running_) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
        if (bytes_received <= 0) {
            // Connection closed or error
            break;
        }

        // Process data line by line
        for (int i = 0; i < bytes_received; ++i) {
            char c = buffer[i];
            if (c == '\n') {
                std::string response = process_command(current_line);
                if (!response.empty()) {
                    response += "\n";
                    send(client_socket, response.c_str(), response.length(), 0);
                }
                current_line.clear();
            } else {
                current_line += c;
            }
        }
    }

    closesocket(client_socket);
}
