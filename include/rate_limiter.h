#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

struct TokenBucket {
    double tokens;
    std::chrono::steady_clock::time_point last_refill;
    
    TokenBucket(double capacity) 
        : tokens(capacity), last_refill(std::chrono::steady_clock::now()) {}
};

class RateLimiter {
public:
    explicit RateLimiter(double requests_per_second = 100.0, 
                        double burst_capacity = 200.0,
                        bool enabled = false);
    
    bool is_allowed(const std::string& client_ip);
    void set_enabled(bool enabled) { enabled_ = enabled; }
    void set_rate(double requests_per_second) { requests_per_second_ = requests_per_second; }
    void set_burst_capacity(double burst_capacity) { burst_capacity_ = burst_capacity; }
    
    bool is_enabled() const { return enabled_; }
    double get_rate() const { return requests_per_second_; }
    double get_burst_capacity() const { return burst_capacity_; }
    
    // Statistics
    void get_stats(size_t& total_requests, size_t& blocked_requests, size_t& active_clients) const;
    void reset_stats();
    
    // Cleanup expired buckets
    void cleanup_expired_buckets();
    
private:
    void refill_bucket(TokenBucket& bucket);
    std::string extract_ip_from_address(const std::string& address);
    
    bool enabled_;
    double requests_per_second_;
    double burst_capacity_;
    
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TokenBucket> buckets_;
    
    // Statistics
    mutable size_t total_requests_;
    mutable size_t blocked_requests_;
    
    // Cleanup
    std::chrono::steady_clock::time_point last_cleanup_;
    static constexpr int CLEANUP_INTERVAL_SECONDS = 300; // 5 minutes
    static constexpr int BUCKET_EXPIRY_SECONDS = 3600;   // 1 hour
};