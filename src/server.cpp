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
#include <errno.h>

Server::Server(int port, const std::string& host, size_t thread_count)
    : server_fd_(-1), port_(port), host_(host), running_(false) {
    
    epoll_ = std::make_unique<EpollWrapper>();
    thread_pool_ = std::make_unique<ThreadPool>(thread_count);
}

Server::~Server() {
    stop();
}

bool Server::start() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (!EpollWrapper::set_non_blocking(server_fd_)) {
        close(server_fd_);
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
    
    if (!epoll_->init()) {
        close(server_fd_);
        return false;
    }
    
    if (!epoll_->add_fd(server_fd_, EPOLLIN)) {
        close(server_fd_);
        return false;
    }
    
    running_.store(true);
    event_thread_ = std::make_unique<std::thread>(&Server::event_loop, this);
    
    return true;
}

void Server::stop() {
    if (running_.load()) {
        running_.store(false);
        
        if (event_thread_ && event_thread_->joinable()) {
            event_thread_->join();
        }
        
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            for (auto& [fd, conn] : connections_) {
                close(fd);
            }
            connections_.clear();
        }
        
        if (thread_pool_) {
            thread_pool_->shutdown();
        }
        
        if (server_fd_ != -1) {
            close(server_fd_);
            server_fd_ = -1;
        }
    }
}

void Server::event_loop() {
    std::vector<EpollWrapper::Event> events;
    
    while (running_.load()) {
        int num_events = epoll_->wait_for_events(events, 1000);
        
        if (num_events == -1) {
            if (errno != EINTR) {
                std::cerr << "epoll_wait error: " << strerror(errno) << std::endl;
            }
            continue;
        }
        
        for (int i = 0; i < num_events; ++i) {
            const auto& event = events[i];
            
            if (event.fd == server_fd_) {
                if (event.events & EPOLLIN) {
                    handle_accept();
                }
            } else {
                if (event.events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                    handle_client_data(event.fd);
                }
            }
        }
        
        cleanup_inactive_connections();
    }
}

void Server::handle_accept() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::cerr << "Failed to accept connection: " << strerror(errno) << std::endl;
            continue;
        }
        
        if (!EpollWrapper::set_non_blocking(client_fd)) {
            close(client_fd);
            continue;
        }
        
        if (!epoll_->add_fd(client_fd, EPOLLIN | EPOLLHUP | EPOLLERR)) {
            close(client_fd);
            continue;
        }
        
        auto connection = std::make_shared<Connection>(client_fd);
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[client_fd] = connection;
        }
    }
}

void Server::handle_client_data(int client_fd) {
    std::shared_ptr<Connection> conn;
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(client_fd);
        if (it == connections_.end()) {
            return;
        }
        conn = it->second;
    }
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
    
    if (bytes_received <= 0) {
        close_connection(client_fd);
        return;
    }
    
    conn->buffer.append(buffer, bytes_received);
    conn->last_activity = std::chrono::steady_clock::now();
    
    if (conn->buffer.find("\r\n\r\n") != std::string::npos) {
        thread_pool_->enqueue(&Server::handle_client_request, this, conn);
    }
}

void Server::handle_client_request(std::shared_ptr<Connection> conn) {
    if (!conn) return;
    
    std::string response = generate_response(conn->buffer);
    
    ssize_t sent = send(conn->fd, response.c_str(), response.length(), 0);
    if (sent == -1) {
        std::cerr << "Failed to send response: " << strerror(errno) << std::endl;
    }
    
    if (!conn->keep_alive) {
        close_connection(conn->fd);
    } else {
        conn->buffer.clear();
        conn->last_activity = std::chrono::steady_clock::now();
    }
}

void Server::close_connection(int client_fd) {
    epoll_->remove_fd(client_fd);
    close(client_fd);
    
    std::lock_guard<std::mutex> lock(connections_mutex_);
    connections_.erase(client_fd);
}

void Server::cleanup_inactive_connections() {
    auto now = std::chrono::steady_clock::now();
    std::vector<int> inactive_fds;
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (const auto& [fd, conn] : connections_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - conn->last_activity);
            if (elapsed.count() > CONNECTION_TIMEOUT_SECONDS) {
                inactive_fds.push_back(fd);
            }
        }
    }
    
    for (int fd : inactive_fds) {
        close_connection(fd);
    }
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
    response << "<h1>Hello World with Epoll!</h1>\n";
    response << "<p>High-Performance Multithreaded C++ Web Server</p>\n";
    response << "<p>Request time: " << std::ctime(&time_t) << "</p>\n";
    response << "<p>Thread Pool Queue Size: " << thread_pool_->get_queue_size() << "</p>\n";
    response << "<p>Active Connections: " << connections_.size() << "</p>\n";
    response << "<pre>Request received:\n" << request.substr(0, 500) << "</pre>\n";
    response << "</body></html>\n";
    
    return response.str();
}