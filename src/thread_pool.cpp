#include "thread_pool.h"
#include <iostream>

ThreadPool::ThreadPool(size_t thread_count) : shutdown_(false) {
    if (thread_count == 0) {
        thread_count = std::thread::hardware_concurrency();
        if (thread_count == 0) {
            thread_count = 4; // fallback
        }
    }
    
    workers_.reserve(thread_count);
    
    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back(&ThreadPool::worker_thread, this);
    }
    
    std::cout << "ThreadPool initialized with " << thread_count << " threads" << std::endl;
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    if (!shutdown_.load()) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            shutdown_.store(true);
        }
        
        condition_.notify_all();
        
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        workers_.clear();
    }
}

size_t ThreadPool::get_queue_size() const {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    return tasks_.size();
}

void ThreadPool::worker_thread() {
    while (!shutdown_.load()) {
        Task task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return shutdown_.load() || !tasks_.empty(); });
            
            if (shutdown_.load() && tasks_.empty()) {
                break;
            }
            
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "ThreadPool worker caught exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "ThreadPool worker caught unknown exception" << std::endl;
            }
        }
    }
}