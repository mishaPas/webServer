#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool keep_alive = true;
};

struct HttpResponse {
    int status_code = 200;
    std::string body;
    bool keep_alive = true;
};

enum class ParseStatus {
    Incomplete,
    Complete,
    Error
};

struct ParseResult {
    ParseStatus status = ParseStatus::Incomplete;
    HttpRequest request;
    std::size_t bytes_consumed = 0;
};

ParseResult try_parse_http_request(std::string_view buffer);
std::string serialize_http_response(const HttpResponse& resp);