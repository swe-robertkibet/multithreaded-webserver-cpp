#include "epoll_wrapper.h"
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>

EpollWrapper::EpollWrapper() : epoll_fd_(-1) {
    events_buffer_.resize(MAX_EVENTS);
}

EpollWrapper::~EpollWrapper() {
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
    }
}

bool EpollWrapper::init() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        std::cerr << "Failed to create epoll instance: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

bool EpollWrapper::add_fd(int fd, uint32_t events, void* data) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        std::cerr << "Failed to add fd " << fd << " to epoll: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (data) {
        fd_data_[fd] = data;
    }
    
    return true;
}

bool EpollWrapper::modify_fd(int fd, uint32_t events, void* data) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == -1) {
        std::cerr << "Failed to modify fd " << fd << " in epoll: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (data) {
        fd_data_[fd] = data;
    }
    
    return true;
}

bool EpollWrapper::remove_fd(int fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        if (errno != EBADF && errno != ENOENT) {
            std::cerr << "Failed to remove fd " << fd << " from epoll: " << strerror(errno) << std::endl;
        }
        fd_data_.erase(fd);
        return false;
    }
    
    fd_data_.erase(fd);
    return true;
}

int EpollWrapper::wait_for_events(std::vector<Event>& events, int timeout_ms) {
    int num_events = epoll_wait(epoll_fd_, events_buffer_.data(), MAX_EVENTS, timeout_ms);
    
    if (num_events == -1) {
        if (errno != EINTR) {
            std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
        }
        return -1;
    }
    
    events.clear();
    events.reserve(num_events);
    
    for (int i = 0; i < num_events; ++i) {
        Event event{};
        event.fd = events_buffer_[i].data.fd;
        event.events = events_buffer_[i].events;
        event.data = fd_data_[event.fd];
        events.push_back(event);
    }
    
    return num_events;
}

bool EpollWrapper::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        std::cerr << "Failed to get file flags for fd " << fd << ": " << strerror(errno) << std::endl;
        return false;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "Failed to set non-blocking for fd " << fd << ": " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}