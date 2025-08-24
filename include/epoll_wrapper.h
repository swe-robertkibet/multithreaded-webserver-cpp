#pragma once

#include <sys/epoll.h>
#include <vector>
#include <functional>
#include <unordered_map>

class EpollWrapper {
public:
    struct Event {
        int fd;
        uint32_t events;
        void* data;
    };
    
    using EventHandler = std::function<void(const Event&)>;
    
    EpollWrapper();
    ~EpollWrapper();
    
    bool init();
    bool add_fd(int fd, uint32_t events, void* data = nullptr);
    bool modify_fd(int fd, uint32_t events, void* data = nullptr);
    bool remove_fd(int fd);
    
    int wait_for_events(std::vector<Event>& events, int timeout_ms = -1);
    void set_event_handler(EventHandler handler) { event_handler_ = handler; }
    
    static bool set_non_blocking(int fd);
    
private:
    int epoll_fd_;
    std::vector<epoll_event> events_buffer_;
    EventHandler event_handler_;
    std::unordered_map<int, void*> fd_data_;
    
    static constexpr int MAX_EVENTS = 1024;
};