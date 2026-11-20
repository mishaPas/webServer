#include "http.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace {

std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

void parse_query(std::string_view qs, std::unordered_map<std::string, std::string>& out) {
    while (!qs.empty()) {
        std::size_t amp = qs.find('&');
        std::string_view pair = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);
        if (!pair.empty()) {
            std::size_t eq = pair.find('=');
            if (eq == std::string_view::npos) {
                out[std::string(pair)] = "";
            } else {
                out[std::string(pair.substr(0, eq))] = std::string(pair.substr(eq + 1));
            }
        }
        if (amp == std::string_view::npos) break;
        qs.remove_prefix(amp + 1);
    }
}

std::string status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        default:  return "OK";
    }
}

} // namespace

ParseResult try_parse_http_request(std::string_view buffer) {
    ParseResult result;
    std::size_t header_end = buffer.find("\r\n\r\n");
    if (header_end == std::string_view::npos) {
        result.status = ParseStatus::Incomplete;
        return result;
    }

    std::string_view head = buffer.substr(0, header_end);
    std::size_t line_end = head.find("\r\n");
    std::string_view req_line = (line_end == std::string_view::npos) ? head : head.substr(0, line_end);

    std::size_t sp1 = req_line.find(' ');
    std::size_t sp2 = (sp1 == std::string_view::npos) ? std::string_view::npos : req_line.find(' ', sp1 + 1);
    if (sp1 == std::string_view::npos || sp2 == std::string_view::npos) {
        result.status = ParseStatus::Error;
        return result;
    }

    HttpRequest req;
    req.method = std::string(req_line.substr(0, sp1));
    std::string_view target = req_line.substr(sp1 + 1, sp2 - (sp1 + 1));
    std::string_view version = req_line.substr(sp2 + 1);

    if (req.method.empty() || target.empty() || (version != "HTTP/1.1" && version != "HTTP/1.0")) {
        result.status = ParseStatus::Error;
        return result;
    }

    req.keep_alive = (version == "HTTP/1.1");
    std::size_t qpos = target.find('?');
    if (qpos == std::string_view::npos) {
        req.path = std::string(target);
    } else {
        req.path = std::string(target.substr(0, qpos));
        parse_query(target.substr(qpos + 1), req.query_params);
    }

    if (line_end != std::string_view::npos) {
        std::string_view rest = head.substr(line_end + 2);
        while (!rest.empty()) {
            std::size_t next = rest.find("\r\n");
            std::string_view hline = (next == std::string_view::npos) ? rest : rest.substr(0, next);
            if (!hline.empty()) {
                std::size_t colon = hline.find(':');
                if (colon == std::string_view::npos) {
                    result.status = ParseStatus::Error;
                    return result;
                }
                req.headers[to_lower(trim(hline.substr(0, colon)))] = std::string(trim(hline.substr(colon + 1)));
            }
            if (next == std::string_view::npos) break;
            rest.remove_prefix(next + 2);
        }
    }

    if (auto it = req.headers.find("connection"); it != req.headers.end()) {
        std::string conn = to_lower(it->second);
        if (conn.find("close") != std::string::npos) req.keep_alive = false;
        else if (conn.find("keep-alive") != std::string::npos) req.keep_alive = true;
    }

    std::size_t content_length = 0;
    if (auto it = req.headers.find("content-length"); it != req.headers.end()) {
        const std::string& s = it->second;
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), content_length);
        if (ec != std::errc() || ptr != s.data() + s.size()) {
            result.status = ParseStatus::Error;
            return result;
        }
    }

    std::size_t total = header_end + 4 + content_length;
    if (buffer.size() < total) {
        result.status = ParseStatus::Incomplete;
        return result;
    }

    req.body = std::string(buffer.substr(header_end + 4, content_length));
    result.status = ParseStatus::Complete;
    result.request = std::move(req);
    result.bytes_consumed = total;
    return result;
}

std::string serialize_http_response(const HttpResponse& resp) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << resp.status_code << " " << status_text(resp.status_code) << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << resp.body.size() << "\r\n"
        << "Connection: " << (resp.keep_alive ? "keep-alive" : "close") << "\r\n"
        << "\r\n"
        << resp.body;
    return oss.str();
}