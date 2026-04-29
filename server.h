#pragma once
#include "store.h"
#include <string>
#include <thread>
#include <atomic>

// Minimal inclusion for SOCKET to avoid bloating headers
#ifdef _WIN32
#include <winsock2.h>
#else
typedef int SOCKET;
#endif

class TCPServer {
public:
    TCPServer(KVStore& store, int port);
    ~TCPServer();
    void start();
    void stop();

private:
    void accept_loop();
    void handle_client(SOCKET client_socket);
    std::string process_command(const std::string& command_line);

    KVStore& store_;
    int port_;
    SOCKET server_socket_;
    std::atomic<bool> running_;
    std::thread accept_thread_;
};
