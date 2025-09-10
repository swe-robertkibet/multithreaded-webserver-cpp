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
#include <thread>
#include <errno.h>
#include <algorithm>
#include <vector>
#include <fstream>
#include <regex>

// Simple function to load max_connections from config.json
size_t load_max_connections_from_config() {
    std::ifstream config_file("config.json");
    if (!config_file.is_open()) {
        std::cerr << "Warning: Could not open config.json, using default max_connections of 2000" << std::endl;
        return 2000;
    }
    
    std::string line;
    std::regex max_conn_regex(R"("max_connections"\s*:\s*(\d+))");
    std::smatch match;
    
    while (std::getline(config_file, line)) {
        if (std::regex_search(line, match, max_conn_regex)) {
            try {
                size_t max_connections = std::stoul(match[1].str());
                if (max_connections > 0 && max_connections <= 100000) {
                    std::cout << "Loaded max_connections from config.json: " << max_connections << std::endl;
                    return max_connections;
                } else {
                    std::cerr << "Warning: Invalid max_connections value in config.json, using default of 2000" << std::endl;
                    return 2000;
                }
            } catch (const std::exception&) {
                std::cerr << "Warning: Could not parse max_connections from config.json, using default of 2000" << std::endl;
                return 2000;
            }
        }
    }
    
    std::cerr << "Warning: max_connections not found in config.json, using default of 2000" << std::endl;
    return 2000;
}

Server::Server(int port, const std::string& host, size_t thread_count)
    : server_fd_(-1), port_(port), host_(host), running_(false), 
      max_connections_(load_max_connections_from_config()) {
    
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
    
    // Increase socket send/receive buffers for high throughput
    int buffer_size = 256 * 1024; // 256KB
    if (setsockopt(server_fd_, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) == -1) {
        std::cerr << "Warning: Could not set SO_SNDBUF: " << strerror(errno) << std::endl;
    }
    if (setsockopt(server_fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) == -1) {
        std::cerr << "Warning: Could not set SO_RCVBUF: " << strerror(errno) << std::endl;
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
                epoll_->remove_fd(fd);
                close(fd);
            }
            connections_.clear();
        }
        
        if (server_fd_ != -1) {
            epoll_->remove_fd(server_fd_);
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
                if (event.events & EPOLLOUT) {
                    handle_client_write(event.fd);
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
        
        // Check connection limit to prevent resource exhaustion
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            if (connections_.size() >= max_connections_) {
                std::cerr << "[Accept] ERROR: Connection limit reached (" << connections_.size() << "/" << max_connections_ << "), rejecting fd=" << client_fd << std::endl;
                close(client_fd);
                continue;
            }
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
            std::cerr << "Failed to add client_fd " << client_fd << " to epoll, closing connection" << std::endl;
            close(client_fd);
            continue;
        }
        
        auto connection = std::make_shared<Connection>(client_fd);
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_[client_fd] = connection;
            // std::cerr << "[Accept] SUCCESS: fd=" << client_fd << " (total connections: " << connections_.size() << "/" << max_connections_ << ")" << std::endl;
        }
    }
}

void Server::handle_client_data(int client_fd) {
    std::shared_ptr<Connection> conn;
    bool connection_exists = false;
    
    // Get connection with proper locking
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(client_fd);
        if (it != connections_.end()) {
            conn = it->second;
            connection_exists = true;
        }
    }
    
    // Check if connection still valid before proceeding
    if (!connection_exists || !conn) {
        return;
    }
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    
    if (bytes_received <= 0) {
        // Connection closed or error
        close_connection(client_fd);
        return;
    }
    
    // Validate received bytes and append to connection buffer (thread-safe)
    if (bytes_received > 0 && bytes_received <= BUFFER_SIZE - 1) {
        bool should_process = false;
        {
            std::lock_guard<std::mutex> conn_lock(conn->mutex_);
            // Double-check connection is still valid after acquiring buffer lock
            {
                std::lock_guard<std::mutex> map_lock(connections_mutex_);
                if (connections_.find(client_fd) == connections_.end()) {
                    return; // Connection was closed while waiting for lock
                }
            }
            
            // Check buffer size limit to prevent memory exhaustion
            if (conn->buffer.size() + bytes_received > MAX_REQUEST_SIZE) {
                std::cerr << "Request too large, closing connection fd=" << client_fd << std::endl;
                {
                    std::lock_guard<std::mutex> map_lock2(connections_mutex_);
                    if (connections_.find(client_fd) != connections_.end()) {
                        connections_.erase(client_fd);
                    }
                }
                epoll_->remove_fd(client_fd);
                close(client_fd);
                return;
            }
            
            conn->buffer.append(buffer, bytes_received);  // Append exact bytes received
            conn->last_activity = std::chrono::steady_clock::now();
            
            // Check if we have a complete HTTP request and not already processing one
            should_process = !conn->processing_request && is_http_request_complete(conn->buffer);
            if (should_process) {
                conn->processing_request = true;
            }
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
        // Log invalid requests only in debug mode to reduce spam under load
        #ifdef DEBUG_INVALID_REQUESTS
        std::cerr << "[Request] fd=" << conn->fd << " ERROR: Invalid HTTP request" << std::endl;
        #endif
        response = HttpResponse::create_error_response(HttpStatus::BAD_REQUEST, "Invalid HTTP request");
    } else {
        // std::cerr << "[Request] fd=" << conn->fd << " " << HttpRequest::method_to_string(request.get_method()) << " " << request.get_path() << std::endl;
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
    
    // Log response status
    // std::cerr << "[Response] fd=" << conn->fd << " Status=" << static_cast<int>(response.get_status()) << " Size=" << response.get_body().size() << "B" << std::endl;
    
    // Check if connection is still valid before sending
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        if (connections_.find(conn->fd) == connections_.end()) {
            std::cerr << "[Response] fd=" << conn->fd << " ERROR: Connection already closed" << std::endl;
            // Reset processing flag since connection is invalid
            {
                std::lock_guard<std::mutex> conn_lock(conn->mutex_);
                conn->processing_request = false;
            }
            return;
        }
    }
    
    // Store response for async sending
    {
        std::lock_guard<std::mutex> conn_lock(conn->mutex_);
        conn->pending_response = response.to_string();
        conn->response_offset = 0;
        conn->has_pending_write = true;
    }
    
    // Try to send immediately (non-blocking)
    send_response_async(conn);
    
    // Clear input buffer and update activity (since we've processed the request)
    {
        std::lock_guard<std::mutex> conn_lock(conn->mutex_);
        conn->buffer.clear();
        conn->processing_request = false;
        conn->last_activity = std::chrono::steady_clock::now();
    }
}

void Server::send_response_async(std::shared_ptr<Connection> conn) {
    if (!conn) return;
    
    std::unique_lock<std::mutex> conn_lock(conn->mutex_);
    
    if (!conn->has_pending_write) {
        return; // Nothing to send
    }
    
    const std::string& response = conn->pending_response;
    size_t remaining = response.length() - conn->response_offset;
    
    if (remaining == 0) {
        // All data sent, clean up
        conn->has_pending_write = false;
        conn->pending_response.clear();
        conn->response_offset = 0;
        
        // Remove EPOLLOUT from events since we're done writing
        epoll_->modify_fd(conn->fd, EPOLLIN | EPOLLHUP | EPOLLERR);
        
        if (!conn->keep_alive) {
            conn_lock.unlock();
            close_connection(conn->fd);
        }
        return;
    }
    
    // Try to send as much as possible
    ssize_t sent = send(conn->fd, response.c_str() + conn->response_offset, remaining, MSG_NOSIGNAL);
    
    if (sent == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Socket buffer full, add EPOLLOUT and wait for writability
            epoll_->modify_fd(conn->fd, EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLERR);
            return;
        } else if (errno == EPIPE || errno == ECONNRESET) {
            // Connection closed by peer
            std::cerr << "[Send] fd=" << conn->fd << " ERROR: Connection closed by peer (" << strerror(errno) << ")" << std::endl;
            conn->keep_alive = false;
            conn->has_pending_write = false;
            conn_lock.unlock();
            close_connection(conn->fd);
            return;
        } else {
            // Other error
            std::cerr << "[Send] fd=" << conn->fd << " ERROR: Failed to send response: " << strerror(errno) << std::endl;
            conn->keep_alive = false;
            conn->has_pending_write = false;
            conn_lock.unlock();
            close_connection(conn->fd);
            return;
        }
    } else if (sent == 0) {
        // Connection closed
        conn->keep_alive = false;
        conn->has_pending_write = false;
        conn_lock.unlock();
        close_connection(conn->fd);
        return;
    } else {
        // Successfully sent some data
        conn->response_offset += sent;
        
        // Check if we've sent everything
        if (conn->response_offset >= response.length()) {
            // All data sent, clean up
            conn->has_pending_write = false;
            conn->pending_response.clear();
            conn->response_offset = 0;
            
            // Remove EPOLLOUT from events
            epoll_->modify_fd(conn->fd, EPOLLIN | EPOLLHUP | EPOLLERR);
            
            if (!conn->keep_alive) {
                conn_lock.unlock();
                close_connection(conn->fd);
            }
        } else {
            // Still have data to send, ensure EPOLLOUT is monitored
            epoll_->modify_fd(conn->fd, EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLERR);
        }
    }
}

void Server::handle_client_write(int client_fd) {
    std::shared_ptr<Connection> conn;
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        auto it = connections_.find(client_fd);
        if (it == connections_.end()) {
            return; // Connection no longer exists
        }
        conn = it->second;
    }
    
    send_response_async(conn);
}

void Server::close_connection(int client_fd) {
    std::unique_lock<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(client_fd);
    if (it == connections_.end()) {
        return; // Already closed
    }
    
    // Get the connection and remove from map immediately to prevent other threads from accessing it
    auto conn = it->second;
    
    // Check if we're closing a connection with pending writes (potential bug!)
    bool has_pending_write = false;
    {
        std::lock_guard<std::mutex> conn_lock(conn->mutex_);
        has_pending_write = conn->has_pending_write;
    }
    
    if (has_pending_write) {
        std::cerr << "[CloseConn] fd=" << client_fd << " WARNING: Closing connection with pending write!" << std::endl;
    }
    
    connections_.erase(it);
    lock.unlock();
    
    epoll_->remove_fd(client_fd);
    
    // Close the socket
    if (close(client_fd) == -1 && errno != EBADF) {
        std::cerr << "Warning: Error closing fd " << client_fd << ": " << strerror(errno) << std::endl;
    }
}

void Server::cleanup_inactive_connections() {
    auto now = std::chrono::steady_clock::now();
    std::vector<int> inactive_fds;
    
    {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (const auto& [fd, conn] : connections_) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - conn->last_activity);
            
            // Only timeout connections that don't have pending writes
            bool has_pending_write = false;
            {
                std::lock_guard<std::mutex> conn_lock(conn->mutex_);
                has_pending_write = conn->has_pending_write;
            }
            
            if (elapsed.count() > CONNECTION_TIMEOUT_SECONDS && !has_pending_write) {
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

bool Server::is_http_request_complete(const std::string& buffer) {
    // Find the end of headers
    size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        return false;  // Headers not complete
    }
    
    // Parse headers to extract Content-Length
    std::string headers = buffer.substr(0, header_end);
    std::istringstream header_stream(headers);
    std::string line;
    size_t content_length = 0;
    bool has_content_length = false;
    
    // Skip first line (request line)
    std::getline(header_stream, line);
    
    // Parse headers
    while (std::getline(header_stream, line) && !line.empty()) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string name = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            name.erase(name.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Convert to lowercase for comparison
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            
            if (name == "content-length") {
                try {
                    content_length = std::stoull(value);
                    has_content_length = true;
                } catch (const std::exception&) {
                    // Invalid content-length, treat as no body
                    content_length = 0;
                }
                break;
            }
        }
    }
    
    // Calculate expected total size
    size_t expected_size = header_end + 4; // +4 for "\r\n\r\n"
    if (has_content_length) {
        expected_size += content_length;
    }
    
    // Check if we have all data
    return buffer.size() >= expected_size;
}

bool Server::is_likely_http_request(const std::string& buffer) {
    if (buffer.empty()) return false;
    
    // Check for minimum HTTP request line
    size_t first_line_end = buffer.find('\n');
    if (first_line_end == std::string::npos && buffer.size() < 16) {
        return false; // Too short to be a valid request line
    }
    
    std::string first_line = (first_line_end != std::string::npos) 
        ? buffer.substr(0, first_line_end) 
        : buffer;
    
    // Remove carriage return if present
    if (!first_line.empty() && first_line.back() == '\r') {
        first_line.pop_back();
    }
    
    // Quick check for HTTP method at the start
    static const std::vector<std::string> methods = {"GET", "POST", "PUT", "DELETE", "HEAD", "OPTIONS"};
    for (const auto& method : methods) {
        if (first_line.compare(0, method.length(), method) == 0 && 
            first_line.size() > method.length() && 
            first_line[method.length()] == ' ') {
            return true;
        }
    }
    
    return false;
}