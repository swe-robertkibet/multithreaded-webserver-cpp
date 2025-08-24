#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>

class ThreadPool {
public:
    using Task = std::function<void()>;
    
    explicit ThreadPool(size_t thread_count = std::thread::hardware_concurrency());
    ~ThreadPool();
    
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
    
    void shutdown();
    size_t get_queue_size() const;
    size_t get_thread_count() const { return workers_.size(); }
    bool is_shutdown() const { return shutdown_.load(); }
    
private:
    void worker_thread();
    
    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> shutdown_;
};

template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    
    if (shutdown_.load()) {
        throw std::runtime_error("ThreadPool is shutdown");
    }
    
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    
    std::future<return_type> result = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        tasks_.emplace([task]() { (*task)(); });
    }
    
    condition_.notify_one();
    return result;
}