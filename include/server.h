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

struct Connection {
    int fd;
    std::string buffer;
    bool keep_alive;
    std::chrono::steady_clock::time_point last_activity;
    
    Connection(int socket_fd) : fd(socket_fd), keep_alive(false), 
                               last_activity(std::chrono::steady_clock::now()) {}
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
    void close_connection(int client_fd);
    void cleanup_inactive_connections();
    HttpResponse handle_api_request(const HttpRequest& request);
    
    int server_fd_;
    int port_;
    std::string host_;
    std::atomic<bool> running_;
    
    std::unique_ptr<EpollWrapper> epoll_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::unique_ptr<std::thread> event_thread_;
    std::unique_ptr<FileHandler> file_handler_;
    
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    std::mutex connections_mutex_;
    
    static constexpr int BUFFER_SIZE = 4096;
    static constexpr int BACKLOG = 128;
    static constexpr int CONNECTION_TIMEOUT_SECONDS = 30;
};