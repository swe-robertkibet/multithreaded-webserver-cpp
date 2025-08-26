#pragma once

#include <string>
#include <unordered_map>
#include <vector>

enum class HttpStatus {
    OK = 200,
    CREATED = 201,
    NO_CONTENT = 204,
    MOVED_PERMANENTLY = 301,
    FOUND = 302,
    NOT_MODIFIED = 304,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503
};

class HttpResponse {
public:
    HttpResponse(HttpStatus status = HttpStatus::OK);
    
    void set_status(HttpStatus status);
    void set_header(const std::string& name, const std::string& value);
    void set_body(const std::string& body);
    void set_body(const std::vector<char>& body);
    void append_body(const std::string& data);
    
    void set_content_type(const std::string& content_type);
    void set_content_length(size_t length);
    void set_keep_alive(bool keep_alive);
    void set_server_header(const std::string& server_name = "MultithreadedWebServer/1.0");
    
    std::string to_string() const;
    std::vector<char> to_bytes() const;
    
    HttpStatus get_status() const { return status_; }
    const std::string& get_body() const { return body_; }
    size_t get_body_size() const { return body_.size(); }
    
    static std::string get_mime_type(const std::string& file_extension);
    static std::string get_status_text(HttpStatus status);
    static HttpResponse create_error_response(HttpStatus status, const std::string& message = "");
    static HttpResponse create_file_response(const std::string& file_path, const std::vector<char>& file_content);
    
private:
    void set_default_headers();
    std::string format_date() const;
    
    HttpStatus status_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    std::string version_ = "HTTP/1.1";
};