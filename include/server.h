#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include "epoll_wrapper.h"
#include "thread_pool.h"
#include "http_request.h"
#include "http_response.h"
#include "file_handler.h"
#include "rate_limiter.h"
#include "logger.h"

struct Connection {
    int fd;
    std::string buffer;
    bool keep_alive;
    std::chrono::steady_clock::time_point last_activity;
    mutable std::mutex mutex_;
    
    // Fields for handling partial writes
    std::string pending_response;
    size_t response_offset;
    bool has_pending_write;
    
    Connection(int socket_fd) : fd(socket_fd), keep_alive(false), 
                               last_activity(std::chrono::steady_clock::now()),
                               response_offset(0), has_pending_write(false) {}
};

class Server {
public:
    explicit Server(int port = 8080, const std::string& host = "0.0.0.0", size_t thread_count = 0);
    ~Server();
    
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
private:
    void event_loop();
    void handle_accept();
    void handle_client_data(int client_fd);
    void handle_client_request(std::shared_ptr<Connection> conn);
    void handle_client_write(int client_fd);
    void send_response_async(std::shared_ptr<Connection> conn);
    void close_connection(int client_fd);
    void cleanup_inactive_connections();
    HttpResponse handle_api_request(const HttpRequest& request);
    std::string get_client_ip(int client_fd);
    
    int server_fd_;
    int port_;
    std::string host_;
    std::atomic<bool> running_;
    
    std::unique_ptr<EpollWrapper> epoll_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<std::thread> event_thread_;
    std::unique_ptr<FileHandler> file_handler_;
    std::unique_ptr<RateLimiter> rate_limiter_;
    
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    std::mutex connections_mutex_;
    
    static constexpr int BUFFER_SIZE = 4096;
    static constexpr int BACKLOG = 1024;
    static constexpr int CONNECTION_TIMEOUT_SECONDS = 30;
    static constexpr size_t MAX_REQUEST_SIZE = 64 * 1024;
    static constexpr size_t MAX_CONNECTIONS = 2000;
};