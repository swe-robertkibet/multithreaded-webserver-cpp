#include "server.h"
#include <iostream>
#include <csignal>
#include <memory>

std::unique_ptr<Server> server_instance;

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived shutdown signal, stopping server..." << std::endl;
        if (server_instance) {
            server_instance->stop();
        }
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
            if (port <= 0 || port > 65535) {
                std::cerr << "Invalid port number. Using default port 8080." << std::endl;
                port = 8080;
            }
        } catch (const std::exception&) {
            std::cerr << "Invalid port argument. Using default port 8080." << std::endl;
        }
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    server_instance = std::make_unique<Server>(port);
    
    std::cout << "Starting HTTP server on port " << port << "..." << std::endl;
    
    if (!server_instance->start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Server started successfully. Press Ctrl+C to stop." << std::endl;
    
    while (server_instance->is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Server stopped." << std::endl;
    return 0;
}