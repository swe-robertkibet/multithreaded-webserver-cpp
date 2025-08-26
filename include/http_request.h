#pragma once

#include <string>
#include <unordered_map>
#include <vector>

enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    UNKNOWN
};

class HttpRequest {
public:
    HttpRequest() = default;
    
    static HttpRequest parse(const std::string& raw_request);
    
    HttpMethod get_method() const { return method_; }
    const std::string& get_path() const { return path_; }
    const std::string& get_query_string() const { return query_string_; }
    const std::string& get_version() const { return version_; }
    const std::string& get_body() const { return body_; }
    
    std::string get_header(const std::string& name) const;
    bool has_header(const std::string& name) const;
    const std::unordered_map<std::string, std::string>& get_headers() const { return headers_; }
    
    std::string get_query_param(const std::string& name) const;
    const std::unordered_map<std::string, std::string>& get_query_params() const { return query_params_; }
    
    bool is_keep_alive() const;
    bool is_valid() const { return valid_; }
    
    static std::string method_to_string(HttpMethod method);
    static HttpMethod string_to_method(const std::string& method_str);
    
private:
    void parse_request_line(const std::string& line);
    void parse_header_line(const std::string& line);
    void parse_query_string();
    std::string url_decode(const std::string& str);
    
    HttpMethod method_ = HttpMethod::UNKNOWN;
    std::string path_;
    std::string query_string_;
    std::string version_;
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> query_params_;
    bool valid_ = false;
};