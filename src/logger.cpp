#include "logger.h"
#include <iostream>
#include <iomanip>
#include <filesystem>

Logger& Logger::get_instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    flush_logs();
}

void Logger::init(const std::string& access_log_path, const std::string& error_log_path, LogLevel level) {
    std::lock_guard<std::mutex> access_lock(access_mutex_);
    std::lock_guard<std::mutex> error_lock(error_mutex_);
    
    log_level_ = level;
    console_output_ = false;
    initialized_ = true;
    
    try {
        ensure_log_directories();
        
        access_log_ = std::make_unique<std::ofstream>(access_log_path, std::ios::app);
        error_log_ = std::make_unique<std::ofstream>(error_log_path, std::ios::app);
        
        if (!access_log_->is_open()) {
            std::cerr << "Warning: Could not open access log file: " << access_log_path << std::endl;
            console_output_ = true;
        }
        
        if (!error_log_->is_open()) {
            std::cerr << "Warning: Could not open error log file: " << error_log_path << std::endl;
            console_output_ = true;
        }
        
        log_info("Logger initialized - Access: " + access_log_path + ", Error: " + error_log_path);
        
    } catch (const std::exception& e) {
        std::cerr << "Logger initialization error: " << e.what() << std::endl;
        console_output_ = true;
    }
}

void Logger::log_access(const std::string& client_ip, 
                       const std::string& method,
                       const std::string& path,
                       int status_code,
                       size_t response_size,
                       const std::string& user_agent,
                       const std::string& referer) {
    
    if (!initialized_) return;
    
    std::ostringstream log_entry;
    
    log_entry << client_ip << " - - [" << get_timestamp() << "] "
              << "\"" << method << " " << path << " HTTP/1.1\" "
              << status_code << " " << response_size;
    
    if (!referer.empty()) {
        log_entry << " \"" << referer << "\"";
    } else {
        log_entry << " \"-\"";
    }
    
    if (!user_agent.empty()) {
        log_entry << " \"" << user_agent << "\"";
    } else {
        log_entry << " \"-\"";
    }
    
    std::lock_guard<std::mutex> lock(access_mutex_);
    if (access_log_ && access_log_->is_open()) {
        *access_log_ << log_entry.str() << std::endl;
        access_log_->flush();
    }
    
    if (console_output_) {
        std::cout << "[ACCESS] " << log_entry.str() << std::endl;
    }
}

void Logger::log_error(const std::string& message, LogLevel level) {
    if (!initialized_ || level < log_level_) return;
    
    std::ostringstream log_entry;
    log_entry << "[" << get_timestamp() << "] "
              << "[" << get_log_level_string(level) << "] "
              << message;
    
    std::lock_guard<std::mutex> lock(error_mutex_);
    if (error_log_ && error_log_->is_open()) {
        *error_log_ << log_entry.str() << std::endl;
        error_log_->flush();
    }
    
    if (console_output_ || level >= LogLevel::ERROR) {
        std::cerr << log_entry.str() << std::endl;
    }
}

void Logger::log_info(const std::string& message) {
    log_error(message, LogLevel::INFO);
}

void Logger::log_warn(const std::string& message) {
    log_error(message, LogLevel::WARN);
}

void Logger::log_debug(const std::string& message) {
    log_error(message, LogLevel::DEBUG);
}

void Logger::flush_logs() {
    std::lock_guard<std::mutex> access_lock(access_mutex_);
    std::lock_guard<std::mutex> error_lock(error_mutex_);
    
    if (access_log_) {
        access_log_->flush();
    }
    if (error_log_) {
        error_log_->flush();
    }
}

void Logger::write_log(std::ofstream& file, const std::string& message) {
    if (file.is_open()) {
        file << message << std::endl;
        file.flush();
    }
}

std::string Logger::get_timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%d/%b/%Y:%H:%M:%S %z");
    return oss.str();
}

std::string Logger::get_log_level_string(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

void Logger::ensure_log_directories() {
    try {
        std::filesystem::create_directories("logs");
    } catch (const std::exception& e) {
        std::cerr << "Could not create logs directory: " << e.what() << std::endl;
    }
}