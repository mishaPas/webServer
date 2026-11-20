#pragma once

#include "http.hpp"
#include "storage.hpp"

class Router {
public:
    explicit Router(Storage& storage) : storage_(storage) {}
    HttpResponse handle(const HttpRequest& req) const;

private:
    Storage& storage_;
};