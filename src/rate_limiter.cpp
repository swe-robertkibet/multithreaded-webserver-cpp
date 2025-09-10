#include "rate_limiter.h"
#include <algorithm>
#include <iostream>

RateLimiter::RateLimiter(double requests_per_second, double burst_capacity, bool enabled)
    : enabled_(enabled)
    , requests_per_second_(requests_per_second)
    , burst_capacity_(burst_capacity)
    , total_requests_(0)
    , blocked_requests_(0)
    , last_cleanup_(std::chrono::steady_clock::now()) {
    
    if (enabled_) {
        std::cout << "Rate limiter enabled: " << requests_per_second 
                  << " req/s, burst: " << burst_capacity << std::endl;
    }
}

bool RateLimiter::is_allowed(const std::string& client_ip) {
    if (!enabled_) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    total_requests_++;
    
    //cleanup of expired buckets
    auto now = std::chrono::steady_clock::now();
    auto cleanup_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_cleanup_);
    if (cleanup_elapsed.count() >= CLEANUP_INTERVAL_SECONDS) {
        cleanup_expired_buckets();
        last_cleanup_ = now;
    }
    
    std::string ip = extract_ip_from_address(client_ip);
    
    //find/create bucket for this IP
    auto it = buckets_.find(ip);
    if (it == buckets_.end()) {
        it = buckets_.emplace(ip, TokenBucket(burst_capacity_)).first;
    }
    
    TokenBucket& bucket = it->second;
    refill_bucket(bucket);
    
    if (bucket.tokens >= 1.0) {
        bucket.tokens -= 1.0;
        return true;
    } else {
        blocked_requests_++;
        return false;
    }
}

void RateLimiter::refill_bucket(TokenBucket& bucket) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - bucket.last_refill);
    double elapsed_seconds = elapsed.count() / 1000.0;
    double tokens_to_add = elapsed_seconds * requests_per_second_;
    bucket.tokens = std::min(burst_capacity_, bucket.tokens + tokens_to_add);
    bucket.last_refill = now;
}

std::string RateLimiter::extract_ip_from_address(const std::string& address) {
    // Handle both "IP:port" and just "IP" formats
    size_t colon_pos = address.find_last_of(':');
    if (colon_pos != std::string::npos) {
        return address.substr(0, colon_pos);
    }
    return address;
}

void RateLimiter::get_stats(size_t& total_requests, size_t& blocked_requests, size_t& active_clients) const {
    std::lock_guard<std::mutex> lock(mutex_);
    total_requests = total_requests_;
    blocked_requests = blocked_requests_;
    active_clients = buckets_.size();
}

void RateLimiter::reset_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    total_requests_ = 0;
    blocked_requests_ = 0;
}

void RateLimiter::cleanup_expired_buckets() {
    auto now = std::chrono::steady_clock::now();
    
    auto it = buckets_.begin();
    while (it != buckets_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_refill);
        if (elapsed.count() >= BUCKET_EXPIRY_SECONDS) {
            it = buckets_.erase(it);
        } else {
            ++it;
        }
    }
}