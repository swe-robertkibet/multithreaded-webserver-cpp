#include "http_request.h"
#include <sstream>
#include <algorithm>
#include <cctype>

HttpRequest HttpRequest::parse(const std::string& raw_request) {
    HttpRequest request;
    
    if (raw_request.empty()) {
        return request;
    }
    
    std::istringstream stream(raw_request);
    std::string line;
    bool first_line = true;
    bool headers_done = false;
    std::string body;
    
    while (std::getline(stream, line)) {
        // Remove carriage return if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        if (first_line) {
            request.parse_request_line(line);
            first_line = false;
        } else if (!headers_done) {
            if (line.empty()) {
                headers_done = true;
                continue;
            }
            request.parse_header_line(line);
        } else {
            if (!body.empty()) {
                body += "\n";
            }
            body += line;
        }
    }
    
    request.body_ = body;
    request.parse_query_string();
    request.valid_ = (request.method_ != HttpMethod::UNKNOWN && !request.path_.empty());
    
    return request;
}

void HttpRequest::parse_request_line(const std::string& line) {
    std::istringstream stream(line);
    std::string method_str, path_with_query, version;
    
    if (!(stream >> method_str >> path_with_query >> version)) {
        return;
    }
    
    method_ = string_to_method(method_str);
    version_ = version;
    
    // Split path and query string
    size_t query_pos = path_with_query.find('?');
    if (query_pos != std::string::npos) {
        path_ = path_with_query.substr(0, query_pos);
        query_string_ = path_with_query.substr(query_pos + 1);
    } else {
        path_ = path_with_query;
    }
    
    // URL decode the path
    path_ = url_decode(path_);
}

void HttpRequest::parse_header_line(const std::string& line) {
    size_t colon_pos = line.find(':');
    if (colon_pos == std::string::npos) {
        return;
    }
    
    std::string name = line.substr(0, colon_pos);
    std::string value = line.substr(colon_pos + 1);
    
    // Trim whitespace
    name.erase(name.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));
    value.erase(value.find_last_not_of(" \t") + 1);
    
    // Convert header name to lowercase for case-insensitive lookup
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    
    headers_[name] = value;
}

void HttpRequest::parse_query_string() {
    if (query_string_.empty()) {
        return;
    }
    
    std::istringstream stream(query_string_);
    std::string pair;
    
    while (std::getline(stream, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = url_decode(pair.substr(0, eq_pos));
            std::string value = url_decode(pair.substr(eq_pos + 1));
            query_params_[key] = value;
        } else {
            query_params_[url_decode(pair)] = "";
        }
    }
}

std::string HttpRequest::url_decode(const std::string& str) {
    std::string result;
    result.reserve(str.length());
    
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            unsigned int hex_value;
            if (std::sscanf(str.substr(i + 1, 2).c_str(), "%x", &hex_value) == 1) {
                result += static_cast<char>(hex_value);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }
    
    return result;
}

std::string HttpRequest::get_header(const std::string& name) const {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    auto it = headers_.find(lower_name);
    return (it != headers_.end()) ? it->second : "";
}

bool HttpRequest::has_header(const std::string& name) const {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    return headers_.find(lower_name) != headers_.end();
}

std::string HttpRequest::get_query_param(const std::string& name) const {
    auto it = query_params_.find(name);
    return (it != query_params_.end()) ? it->second : "";
}

bool HttpRequest::is_keep_alive() const {
    std::string connection = get_header("connection");
    std::transform(connection.begin(), connection.end(), connection.begin(), ::tolower);
    
    if (version_ == "HTTP/1.1") {
        return connection != "close";
    } else {
        return connection == "keep-alive";
    }
}

std::string HttpRequest::method_to_string(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET:     return "GET";
        case HttpMethod::POST:    return "POST";
        case HttpMethod::PUT:     return "PUT";
        case HttpMethod::DELETE:  return "DELETE";
        case HttpMethod::HEAD:    return "HEAD";
        case HttpMethod::OPTIONS: return "OPTIONS";
        default:                  return "UNKNOWN";
    }
}

HttpMethod HttpRequest::string_to_method(const std::string& method_str) {
    if (method_str == "GET")     return HttpMethod::GET;
    if (method_str == "POST")    return HttpMethod::POST;
    if (method_str == "PUT")     return HttpMethod::PUT;
    if (method_str == "DELETE")  return HttpMethod::DELETE;
    if (method_str == "HEAD")    return HttpMethod::HEAD;
    if (method_str == "OPTIONS") return HttpMethod::OPTIONS;
    
    return HttpMethod::UNKNOWN;
}