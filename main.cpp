#include "store.h"
#include "server.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    int port = 6379; // Default Redis port
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid port number. Using 6379." << std::endl;
        }
    }

    KVStore store;
    TCPServer server(store, port);

    std::cout << "Starting Key-Value Store on port " << port << "..." << std::endl;
    server.start();

    // Keep the main thread alive. Wait for an exit command from stdin.
    std::cout << "Type 'exit' to stop the server." << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "exit" || line == "quit") {
            break;
        }
    }

    std::cout << "Stopping server..." << std::endl;
    server.stop();
    return 0;
}
