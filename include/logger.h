#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <sstream>
#include <chrono>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

class Logger {
public:
    static Logger& get_instance();
    
    void init(const std::string& access_log_path = "./logs/access.log",
              const std::string& error_log_path = "./logs/error.log",
              LogLevel level = LogLevel::INFO);
    
    void log_access(const std::string& client_ip, 
                   const std::string& method,
                   const std::string& path,
                   int status_code,
                   size_t response_size,
                   const std::string& user_agent = "",
                   const std::string& referer = "");
    
    void log_error(const std::string& message, LogLevel level = LogLevel::ERROR);
    void log_info(const std::string& message);
    void log_warn(const std::string& message);
    void log_debug(const std::string& message);
    
    void set_log_level(LogLevel level) { log_level_ = level; }
    LogLevel get_log_level() const { return log_level_; }
    
    void enable_console_output(bool enable) { console_output_ = enable; }
    void flush_logs();
    
private:
    Logger() = default;
    ~Logger();
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    void write_log(std::ofstream& file, const std::string& message);
    std::string get_timestamp() const;
    std::string get_log_level_string(LogLevel level) const;
    void ensure_log_directories();
    
    std::mutex access_mutex_;
    std::mutex error_mutex_;
    std::unique_ptr<std::ofstream> access_log_;
    std::unique_ptr<std::ofstream> error_log_;
    
    LogLevel log_level_;
    bool console_output_;
    bool initialized_;
};