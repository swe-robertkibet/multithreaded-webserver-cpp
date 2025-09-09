#include "server.h"
#include "http_request.h"
#include "http_response.h"
#include "file_handler.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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
    file_handler_ = std::make_unique<FileHandler>("./public", "index.html", true, 100);
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
    
    // Enable SO_REUSEPORT for better load balancing
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        std::cerr << "Warning: Could not set SO_REUSEPORT: " << strerror(errno) << std::endl;
    }
    
    // Set TCP_NODELAY to reduce latency
    if (setsockopt(server_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
        std::cerr << "Warning: Could not set TCP_NODELAY: " << strerror(errno) << std::endl;
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
        
        // First shutdown thread pool to prevent new tasks
        if (thread_pool_) {
            thread_pool_->shutdown();
        }
        
        // Then join event thread
        if (event_thread_ && event_thread_->joinable()) {
            event_thread_->join();
        }
        
        // Finally clean up connections (all threads are stopped)
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            for (auto& [fd, conn] : connections_) {
                epoll_->remove_fd(fd);  // Remove from epoll first
                close(fd);
            }
            connections_.clear();
        }
        
        if (server_fd_ != -1) {
            epoll_->remove_fd(server_fd_);  // Remove server socket from epoll
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
        
        // Set client socket options for better performance
        int opt = 1;
        if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
            std::cerr << "Warning: Could not set TCP_NODELAY on client socket: " << strerror(errno) << std::endl;
        }
        
        // Set socket receive timeout to prevent hanging connections
        struct timeval timeout = {30, 0}; // 30 seconds
        if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
            std::cerr << "Warning: Could not set SO_RCVTIMEO: " << strerror(errno) << std::endl;
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
    ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);  // Leave space for null terminator
    
    if (bytes_received <= 0) {
        close_connection(client_fd);
        return;
    }
    
    // Append received bytes to connection buffer (thread-safe)
    if (bytes_received > 0 && bytes_received <= BUFFER_SIZE - 1) {
        bool should_process = false;
        {
            std::lock_guard<std::mutex> conn_lock(conn->mutex_);
            conn->buffer.append(buffer, bytes_received);  // Append exact bytes received
            conn->last_activity = std::chrono::steady_clock::now();
            
            // Check if we have a complete HTTP request
            should_process = (conn->buffer.find("\r\n\r\n") != std::string::npos);
        }
        
        if (should_process) {
            thread_pool_->enqueue(&Server::handle_client_request, this, conn);
        }
    } else {
        std::cerr << "Invalid bytes_received: " << bytes_received << std::endl;
        close_connection(client_fd);
        return;
    }
}

void Server::handle_client_request(std::shared_ptr<Connection> conn) {
    if (!conn) return;
    
    // Parse request with thread-safe buffer access
    HttpRequest request;
    {
        std::lock_guard<std::mutex> conn_lock(conn->mutex_);
        request = HttpRequest::parse(conn->buffer);
    }
    
    HttpResponse response;
    
    if (!request.is_valid()) {
        response = HttpResponse::create_error_response(HttpStatus::BAD_REQUEST, "Invalid HTTP request");
    } else {
        // Determine keep-alive based on request (thread-safe access)
        {
            std::lock_guard<std::mutex> conn_lock(conn->mutex_);
            conn->keep_alive = request.is_keep_alive();
        }
        
        if (request.get_method() == HttpMethod::GET || request.get_method() == HttpMethod::HEAD) {
            std::string path = request.get_path();
            
            if (path.find("/api/") == 0) {
                response = handle_api_request(request);
            } else {
                response = file_handler_->handle_file_request(path);
            }
            
            // Handle HEAD method by clearing body
            if (request.get_method() == HttpMethod::HEAD) {
                response.set_body("");
            }
        } else {
            response = HttpResponse::create_error_response(HttpStatus::METHOD_NOT_ALLOWED, "Method not supported");
        }
    }
    
    // Set connection header based on keep-alive decision
    response.set_keep_alive(conn->keep_alive);
    
    // Check if connection is still valid before sending
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        if (connections_.find(conn->fd) == connections_.end()) {
            // Connection already closed
            return;
        }
    }
    
    std::string response_str = response.to_string();
    
    // Properly handle partial sends and socket errors
    size_t total_sent = 0;
    size_t total_length = response_str.length();
    
    while (total_sent < total_length) {
        // Check if connection still exists before each send attempt
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            if (connections_.find(conn->fd) == connections_.end()) {
                // Connection was closed during sending
                return;
            }
        }
        
        ssize_t sent = send(conn->fd, response_str.c_str() + total_sent, 
                           total_length - total_sent, MSG_NOSIGNAL);
        
        if (sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket would block, try again later - for now, close connection
                std::lock_guard<std::mutex> conn_lock(conn->mutex_);
                conn->keep_alive = false;
                break;
            } else if (errno == EPIPE || errno == ECONNRESET) {
                // Connection closed by peer
                std::lock_guard<std::mutex> conn_lock(conn->mutex_);
                conn->keep_alive = false;
                break;
            } else {
                std::cerr << "Failed to send response: " << strerror(errno) << std::endl;
                std::lock_guard<std::mutex> conn_lock(conn->mutex_);
                conn->keep_alive = false;
                break;
            }
        } else if (sent == 0) {
            // Connection closed
            std::lock_guard<std::mutex> conn_lock(conn->mutex_);
            conn->keep_alive = false;
            break;
        } else {
            total_sent += sent;
        }
    }
    
    // Only consider successful if all data was sent
    if (total_sent != total_length) {
        std::lock_guard<std::mutex> conn_lock(conn->mutex_);
        conn->keep_alive = false;
    }
    
    bool keep_connection_alive;
    {
        std::lock_guard<std::mutex> conn_lock(conn->mutex_);
        keep_connection_alive = conn->keep_alive;
    }
    
    if (!keep_connection_alive) {
        close_connection(conn->fd);
    } else {
        // Clear buffer and update activity timestamp (thread-safe)
        std::lock_guard<std::mutex> conn_lock(conn->mutex_);
        conn->buffer.clear();
        conn->last_activity = std::chrono::steady_clock::now();
    }
}

void Server::close_connection(int client_fd) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    // Check if connection still exists (avoid double-close)
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        return; // Already closed
    }
    
    // Remove from epoll first, ignore errors (fd might already be closed)
    epoll_->remove_fd(client_fd);
    
    // Close the socket
    close(client_fd);
    
    // Remove from connections map
    connections_.erase(it);
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

HttpResponse Server::handle_api_request(const HttpRequest& request) {
    std::string path = request.get_path();
    
    if (path == "/api/info" || path == "/api/status") {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        // Get cache statistics
        size_t cache_hits = 0, cache_misses = 0, cache_entries = 0, cache_memory = 0;
        file_handler_->get_cache_stats(cache_hits, cache_misses, cache_entries, cache_memory);
        
        std::ostringstream body;
        body << "{\n";
        body << "  \"server\": \"MultithreadedWebServer/1.0\",\n";
        body << "  \"timestamp\": \"" << std::ctime(&time_t) << "\",\n";
        body << "  \"thread_pool_size\": " << thread_pool_->get_thread_count() << ",\n";
        body << "  \"queue_size\": " << thread_pool_->get_queue_size() << ",\n";
        body << "  \"active_connections\": " << connections_.size() << ",\n";
        body << "  \"document_root\": \"" << file_handler_->get_document_root() << "\",\n";
        body << "  \"architecture\": \"epoll + thread_pool + lru_cache\",\n";
        body << "  \"http_version\": \"HTTP/1.1\",\n";
        body << "  \"cache\": {\n";
        body << "    \"hits\": " << cache_hits << ",\n";
        body << "    \"misses\": " << cache_misses << ",\n";
        body << "    \"entries\": " << cache_entries << ",\n";
        body << "    \"memory_usage_bytes\": " << cache_memory;
        if (cache_hits + cache_misses > 0) {
            double hit_ratio = static_cast<double>(cache_hits) / (cache_hits + cache_misses) * 100.0;
            body << ",\n    \"hit_ratio_percent\": " << std::fixed << std::setprecision(1) << hit_ratio;
        }
        body << "\n  }\n";
        body << "}\n";
        
        HttpResponse response(HttpStatus::OK);
        response.set_body(body.str());
        response.set_content_type("application/json");
        return response;
    }
    
    return HttpResponse::create_error_response(HttpStatus::NOT_FOUND, "API endpoint not found");
}