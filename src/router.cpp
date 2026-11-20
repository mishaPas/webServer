#include "router.hpp"

#include <cctype>
#include <charconv>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string item_to_json(const Item& item) {
    std::ostringstream oss;
    oss << "{\"id\":" << item.id
        << ",\"name\":\"" << json_escape(item.name) << "\""
        << ",\"value\":" << item.value
        << ",\"version\":" << item.version << "}";
    return oss.str();
}

std::string items_to_json(const std::vector<Item>& items) {
    std::ostringstream oss;
    oss << "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) oss << ",";
        oss << item_to_json(items[i]);
    }
    oss << "]";
    return oss.str();
}

using JsonVal = std::variant<std::nullptr_t, bool, int64_t, std::string>;
using JsonObject = std::unordered_map<std::string, JsonVal>;

class MiniJsonParser {
public:
    explicit MiniJsonParser(std::string_view src) : s_(src) {}

    std::optional<JsonObject> parse_object() {
        skip_ws();
        if (!consume('{')) return std::nullopt;
        JsonObject obj;
        skip_ws();
        if (consume('}')) {
            skip_ws();
            return (pos_ == s_.size()) ? std::optional<JsonObject>(std::move(obj)) : std::nullopt;
        }
        while (true) {
            skip_ws();
            auto key = parse_string();
            if (!key) return std::nullopt;
            skip_ws();
            if (!consume(':')) return std::nullopt;
            skip_ws();
            auto val = parse_value();
            if (!val) return std::nullopt;
            obj[std::move(*key)] = std::move(*val);
            skip_ws();
            if (consume('}')) break;
            if (!consume(',')) return std::nullopt;
        }
        skip_ws();
        if (pos_ != s_.size()) return std::nullopt;
        return obj;
    }

private:
    void skip_ws() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::optional<std::string> parse_string() {
        if (!consume('"')) return std::nullopt;
        std::string out;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos_ >= s_.size()) return std::nullopt;
                char esc = s_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    default:   return std::nullopt;
                }
            } else {
                out.push_back(c);
            }
        }
        return std::nullopt;
    }

    std::optional<JsonVal> parse_value() {
        if (pos_ >= s_.size()) return std::nullopt;
        char c = s_[pos_];
        if (c == '"') {
            auto str = parse_string();
            if (!str) return std::nullopt;
            return JsonVal(std::move(*str));
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t start = pos_;
            if (s_[pos_] == '-') ++pos_;
            if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                return std::nullopt;
            }
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
                ++pos_;
            }
            int64_t num = 0;
            auto [ptr, ec] = std::from_chars(s_.data() + start, s_.data() + pos_, num);
            if (ec != std::errc() || ptr != s_.data() + pos_) return std::nullopt;
            return JsonVal(num);
        }
        if (s_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return JsonVal(true);
        }
        if (s_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return JsonVal(false);
        }
        if (s_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonVal(nullptr);
        }
        return std::nullopt;
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

template <typename T>
std::optional<T> get_as(const JsonObject& obj, const std::string& key) {
    auto it = obj.find(key);
    if (it == obj.end()) return std::nullopt;
    if (const auto* val = std::get_if<T>(&it->second)) {
        return *val;
    }
    return std::nullopt;
}

bool parse_uint64(std::string_view sv, uint64_t& out) {
    if (sv.empty()) return false;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
    return ec == std::errc() && ptr == sv.data() + sv.size();
}

} // namespace

HttpResponse Router::handle(const HttpRequest& req) const {
    const bool ka = req.keep_alive;

    // 1. GET /health
    if (req.path == "/health") {
        if (req.method != "GET") {
            return HttpResponse{405, "{\"error\":\"method not allowed\"}", ka};
        }
        return HttpResponse{200, "{\"status\":\"ok\"}", ka};
    }

    // 2 и 4. /items (GET list, POST create)
    if (req.path == "/items") {
        if (req.method == "GET") {
            std::size_t offset = 0;
            std::size_t limit = 100;
            if (auto it = req.query_params.find("offset"); it != req.query_params.end()) {
                uint64_t v = 0;
                if (!parse_uint64(it->second, v)) {
                    return HttpResponse{400, "{\"error\":\"invalid offset\"}", ka};
                }
                offset = static_cast<std::size_t>(v);
            }
            if (auto it = req.query_params.find("limit"); it != req.query_params.end()) {
                uint64_t v = 0;
                if (!parse_uint64(it->second, v)) {
                    return HttpResponse{400, "{\"error\":\"invalid limit\"}", ka};
                }
                limit = static_cast<std::size_t>(v);
            }
            auto items = storage_.list(offset, limit);
            return HttpResponse{200, items_to_json(items), ka};
        }

        if (req.method == "POST") {
            auto parsed = MiniJsonParser(req.body).parse_object();
            if (!parsed) {
                return HttpResponse{400, "{\"error\":\"invalid json\"}", ka};
            }
            auto name = get_as<std::string>(*parsed, "name");
            auto value = get_as<int64_t>(*parsed, "value");
            if (!name || !value) {
                return HttpResponse{400, "{\"error\":\"name and value required\"}", ka};
            }
            Item created = storage_.create(std::move(*name), *value);
            return HttpResponse{201, item_to_json(created), ka};
        }

        return HttpResponse{405, "{\"error\":\"method not allowed\"}", ka};
    }

    // Ручки вида /items/{id} и /items/{id}/increment
    const std::string prefix = "/items/";
    if (req.path.rfind(prefix, 0) == 0) {
        std::string_view rest = std::string_view(req.path).substr(prefix.size());
        std::size_t slash = rest.find('/');

        // 8. POST /items/{id}/increment
        if (slash != std::string_view::npos) {
            std::string_view id_sv = rest.substr(0, slash);
            std::string_view sub = rest.substr(slash + 1);
            uint64_t id = 0;
            if (sub != "increment" || !parse_uint64(id_sv, id)) {
                return HttpResponse{404, "{\"error\":\"not found\"}", ka};
            }
            if (req.method != "POST") {
                return HttpResponse{405, "{\"error\":\"method not allowed\"}", ka};
            }
            auto parsed = MiniJsonParser(req.body).parse_object();
            if (!parsed) {
                return HttpResponse{400, "{\"error\":\"invalid json\"}", ka};
            }
            auto delta = get_as<int64_t>(*parsed, "delta");
            if (!delta) {
                return HttpResponse{400, "{\"error\":\"delta required\"}", ka};
            }
            auto updated = storage_.increment(id, *delta);
            if (!updated) {
                return HttpResponse{404, "{\"error\":\"not found\"}", ka};
            }
            return HttpResponse{200, item_to_json(*updated), ka};
        }

        // /items/{id}
        uint64_t id = 0;
        if (!parse_uint64(rest, id)) {
            return HttpResponse{404, "{\"error\":\"not found\"}", ka};
        }

        // 3. GET /items/{id}
        if (req.method == "GET") {
            auto item = storage_.get(id);
            if (!item) {
                return HttpResponse{404, "{\"error\":\"not found\"}", ka};
            }
            return HttpResponse{200, item_to_json(*item), ka};
        }

        // 5. PUT /items/{id}
        if (req.method == "PUT") {
            auto parsed = MiniJsonParser(req.body).parse_object();
            if (!parsed) {
                return HttpResponse{400, "{\"error\":\"invalid json\"}", ka};
            }
            auto name = get_as<std::string>(*parsed, "name");
            auto value = get_as<int64_t>(*parsed, "value");
            if (!name || !value) {
                return HttpResponse{400, "{\"error\":\"name and value required\"}", ka};
            }
            std::optional<uint64_t> exp_ver;
            if (parsed->count("version")) {
                auto v = get_as<int64_t>(*parsed, "version");
                if (!v || *v < 0) {
                    return HttpResponse{400, "{\"error\":\"invalid version\"}", ka};
                }
                exp_ver = static_cast<uint64_t>(*v);
            }
            auto res = storage_.put(id, std::move(*name), *value, exp_ver);
            if (res.status == UpdateStatus::NotFound) {
                return HttpResponse{404, "{\"error\":\"not found\"}", ka};
            }
            if (res.status == UpdateStatus::Conflict) {
                return HttpResponse{409, "{\"error\":\"version conflict\"}", ka};
            }
            return HttpResponse{200, item_to_json(*res.item), ka};
        }

        // 6. PATCH /items/{id}
        if (req.method == "PATCH") {
            auto parsed = MiniJsonParser(req.body).parse_object();
            if (!parsed) {
                return HttpResponse{400, "{\"error\":\"invalid json\"}", ka};
            }
            std::optional<std::string> name;
            if (parsed->count("name")) {
                name = get_as<std::string>(*parsed, "name");
                if (!name) return HttpResponse{400, "{\"error\":\"invalid name\"}", ka};
            }
            std::optional<int64_t> value;
            if (parsed->count("value")) {
                value = get_as<int64_t>(*parsed, "value");
                if (!value) return HttpResponse{400, "{\"error\":\"invalid value\"}", ka};
            }
            std::optional<uint64_t> exp_ver;
            if (parsed->count("version")) {
                auto v = get_as<int64_t>(*parsed, "version");
                if (!v || *v < 0) return HttpResponse{400, "{\"error\":\"invalid version\"}", ka};
                exp_ver = static_cast<uint64_t>(*v);
            }
            auto res = storage_.patch(id, std::move(name), value, exp_ver);
            if (res.status == UpdateStatus::NotFound) {
                return HttpResponse{404, "{\"error\":\"not found\"}", ka};
            }
            if (res.status == UpdateStatus::Conflict) {
                return HttpResponse{409, "{\"error\":\"version conflict\"}", ka};
            }
            return HttpResponse{200, item_to_json(*res.item), ka};
        }

        // 7. DELETE /items/{id}
        if (req.method == "DELETE") {
            if (!storage_.remove(id)) {
                return HttpResponse{404, "{\"error\":\"not found\"}", ka};
            }
            return HttpResponse{204, "", ka};
        }

        return HttpResponse{405, "{\"error\":\"method not allowed\"}", ka};
    }

    return HttpResponse{404, "{\"error\":\"not found\"}", ka};
}