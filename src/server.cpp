#include "server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <chrono>

Server::Server(int port, const std::string& host)
    : server_fd_(-1), port_(port), host_(host), running_(false) {}

Server::~Server() {
    stop();
}

bool Server::start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    int opt = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        std::cerr << "Failed to set socket options: " << strerror(errno) << std::endl;
        close(server_fd_);
        return false;
    }
    
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(host_.c_str());
    if (address.sin_addr.s_addr == INADDR_NONE) {
        address.sin_addr.s_addr = INADDR_ANY;
    }
    address.sin_port = htons(port_);
    
    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
        close(server_fd_);
        return false;
    }
    
    if (listen(server_fd_, BACKLOG) == -1) {
        std::cerr << "Failed to listen on socket: " << strerror(errno) << std::endl;
        close(server_fd_);
        return false;
    }
    
    running_.store(true);
    accept_thread_ = std::make_unique<std::thread>(&Server::accept_loop, this);
    
    return true;
}

void Server::stop() {
    if (running_.load()) {
        running_.store(false);
        
        if (accept_thread_ && accept_thread_->joinable()) {
            accept_thread_->join();
        }
        
        if (server_fd_ != -1) {
            close(server_fd_);
            server_fd_ = -1;
        }
    }
}

void Server::accept_loop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == -1) {
            if (running_.load()) {
                std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        std::thread client_thread(&Server::handle_client, this, client_fd);
        client_thread.detach();
    }
}

void Server::handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        std::string request(buffer);
        std::string response = generate_response(request);
        
        send(client_fd, response.c_str(), response.length(), 0);
    }
    
    close(client_fd);
}

std::string Server::generate_response(const std::string& request) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/html\r\n";
    response << "Connection: close\r\n";
    response << "Server: MultithreadedWebServer/1.0\r\n";
    response << "\r\n";
    response << "<!DOCTYPE html>\n";
    response << "<html><head><title>Hello World Server</title></head>\n";
    response << "<body>\n";
    response << "<h1>Hello World!</h1>\n";
    response << "<p>Multithreaded C++ Web Server</p>\n";
    response << "<p>Request time: " << std::ctime(&time_t) << "</p>\n";
    response << "<pre>Request received:\n" << request << "</pre>\n";
    response << "</body></html>\n";
    
    return response.str();
}