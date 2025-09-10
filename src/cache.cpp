#include "cache.h"
#include <algorithm>
#include <iostream>

LRUCache::LRUCache(size_t max_size_mb, int ttl_seconds)
    : max_size_bytes_(max_size_mb * 1024 * 1024)
    , ttl_seconds_(ttl_seconds)
    , current_size_(0)
    , cache_hits_(0)
    , cache_misses_(0) {
    
    std::cout << "LRU Cache initialized: " << max_size_mb << "MB max, " 
              << ttl_seconds << "s TTL" << std::endl;
}

std::optional<CacheEntry> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        cache_misses_++;
        return std::nullopt;
    }
    
    auto& [entry, list_it] = it->second;
    
    //check if entry is expired
    if (is_expired(entry)) {
        lru_list_.erase(list_it);
        current_size_ -= entry.data.size();
        cache_.erase(it);
        cache_misses_++;
        return std::nullopt;
    }
    
    //move to front - most recently used 
    lru_list_.erase(list_it);
    lru_list_.push_front(key);
    it->second.second = lru_list_.begin();
    
    //update access statistics
    entry.last_accessed = std::chrono::steady_clock::now();
    entry.access_count++;
    
    cache_hits_++;
    return entry;
}

void LRUCache::put(const std::string& key, const std::vector<char>& data, const std::string& content_type) {
    //inpput validation
    if (key.empty() || data.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        auto& [entry, list_it] = it->second;
        current_size_ -= entry.data.size();
        current_size_ += data.size();
        
        entry.data = data;
        entry.content_type = content_type;
        entry.created = std::chrono::steady_clock::now();
        entry.last_accessed = entry.created;
        entry.access_count = 1;
        
        lru_list_.erase(list_it);
        lru_list_.push_front(key);
        it->second.second = lru_list_.begin();
        return;
    }
    
    size_t entry_size = data.size();
    
    //evict entries if necessary
    while (current_size_ + entry_size > max_size_bytes_ && !cache_.empty()) {
        evict_lru();
    }
    
    // skip caching if single entry is too large
    if (entry_size > max_size_bytes_) {
        std::cerr << "Warning: File too large to cache: " << entry_size 
                  << " bytes > " << max_size_bytes_ << " bytes" << std::endl;
        return;
    }
    
    //add new entry
    lru_list_.push_front(key);
    CacheEntry entry(data, content_type);
    cache_[key] = std::make_pair(entry, lru_list_.begin());
    current_size_ += entry_size;
}

void LRUCache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        auto& [entry, list_it] = it->second;
        current_size_ -= entry.data.size();
        lru_list_.erase(list_it);
        cache_.erase(it);
    }
}

void LRUCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    cache_.clear();
    lru_list_.clear();
    current_size_ = 0;
    cache_hits_ = 0;
    cache_misses_ = 0;
}

void LRUCache::evict_lru() {
    // called from put() which already holds the mutex
    if (lru_list_.empty()) {
        return;
    }
    
    // remove least recently used (back of list)
    const std::string lru_key = lru_list_.back();
    lru_list_.pop_back();
    
    auto it = cache_.find(lru_key);
    if (it != cache_.end()) {
        current_size_ -= it->second.first.data.size();
        cache_.erase(it);
    }
}

void LRUCache::evict_expired() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> expired_keys;
    
    // first pass where we identify expired entries
    for (const auto& [key, value] : cache_) {
        if (is_expired(value.first)) {
            expired_keys.push_back(key);
        }
    }
    
    //second pass where we safely remove expired entries
    for (const std::string& key : expired_keys) {
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            current_size_ -= it->second.first.data.size();
            lru_list_.erase(it->second.second);
            cache_.erase(it);
        }
    }
}

bool LRUCache::is_expired(const CacheEntry& entry) const {
    if (ttl_seconds_ <= 0) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.created);
    return elapsed.count() >= ttl_seconds_;
}

size_t LRUCache::get_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_size_;
}

size_t LRUCache::get_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

double LRUCache::get_hit_ratio() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total_requests = cache_hits_ + cache_misses_;
    return total_requests > 0 ? static_cast<double>(cache_hits_) / total_requests : 0.0;
}

void LRUCache::get_stats(size_t& hits, size_t& misses, size_t& entries, size_t& memory_usage) const {
    std::lock_guard<std::mutex> lock(mutex_);
    hits = cache_hits_;
    misses = cache_misses_;
    entries = cache_.size();
    memory_usage = current_size_;
}