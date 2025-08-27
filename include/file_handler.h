#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <memory>
#include "http_response.h"
#include "cache.h"

class FileHandler {
public:
    explicit FileHandler(const std::string& document_root = "./public", 
                        const std::string& default_file = "index.html",
                        bool enable_cache = true,
                        size_t cache_size_mb = 100);
    
    HttpResponse handle_file_request(const std::string& request_path);
    bool file_exists(const std::string& path) const;
    std::optional<std::vector<char>> read_file(const std::string& path) const;
    
    void set_document_root(const std::string& root) { document_root_ = root; }
    void set_default_file(const std::string& default_file) { default_file_ = default_file; }
    void set_max_file_size(size_t max_size) { max_file_size_ = max_size; }
    void enable_cache(bool enabled) { cache_enabled_ = enabled; }
    
    const std::string& get_document_root() const { return document_root_; }
    
    // Cache management
    void clear_cache() { if (cache_) cache_->clear(); }
    void get_cache_stats(size_t& hits, size_t& misses, size_t& entries, size_t& memory_usage) const {
        if (cache_) cache_->get_stats(hits, misses, entries, memory_usage);
    }
    
private:
    std::string resolve_path(const std::string& request_path) const;
    bool is_safe_path(const std::string& resolved_path) const;
    HttpResponse create_directory_listing(const std::string& dir_path, const std::string& request_path);
    std::string get_file_size_string(uintmax_t size) const;
    std::string get_last_modified_string(const std::filesystem::file_time_type& time) const;
    
    std::string document_root_;
    std::string default_file_;
    size_t max_file_size_;
    bool cache_enabled_;
    std::unique_ptr<LRUCache> cache_;
    
    static constexpr size_t DEFAULT_MAX_FILE_SIZE = 50 * 1024 * 1024; // 50MB
};