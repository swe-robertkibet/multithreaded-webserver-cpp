#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <thread>

class Server {
public:
    explicit Server(int port = 8080, const std::string& host = "0.0.0.0");
    ~Server();
    
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
private:
    void accept_loop();
    void handle_client(int client_fd);
    std::string generate_response(const std::string& request);
    
    int server_fd_;
    int port_;
    std::string host_;
    std::atomic<bool> running_;
    std::unique_ptr<std::thread> accept_thread_;
    
    static constexpr int BUFFER_SIZE = 4096;
    static constexpr int BACKLOG = 128;
};