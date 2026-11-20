#include "http.hpp"
#include "router.hpp"
#include "storage.hpp"

#include <cassert>
#include <iostream>

void test_parser() {
    std::string raw =
        "POST /items?offset=0&limit=50 HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Length: 24\r\n"
        "\r\n"
        "{\"name\":\"box\",\"value\":3}";

    auto res = try_parse_http_request(raw);
    assert(res.status == ParseStatus::Complete);
    assert(res.request.method == "POST");
    assert(res.request.path == "/items");
    assert(res.request.query_params.at("limit") == "50");
    assert(res.request.body == "{\"name\":\"box\",\"value\":3}");
}

void test_endpoints() {
    Storage storage;
    Router router(storage);

    HttpRequest h;
    h.method = "GET";
    h.path = "/health";
    assert(router.handle(h).status_code == 200);

    HttpRequest create;
    create.method = "POST";
    create.path = "/items";
    create.body = "{\"name\":\"first\",\"value\":10}";
    auto r_create = router.handle(create);
    assert(r_create.status_code == 201);

    HttpRequest get_one;
    get_one.method = "GET";
    get_one.path = "/items/1";
    assert(router.handle(get_one).status_code == 200);

    HttpRequest put_conflict;
    put_conflict.method = "PUT";
    put_conflict.path = "/items/1";
    put_conflict.body = "{\"name\":\"upd\",\"value\":20,\"version\":5}";
    assert(router.handle(put_conflict).status_code == 409);

    HttpRequest patch_ok;
    patch_ok.method = "PATCH";
    patch_ok.path = "/items/1";
    patch_ok.body = "{\"value\":15,\"version\":1}";
    assert(router.handle(patch_ok).status_code == 200);

    HttpRequest inc;
    inc.method = "POST";
    inc.path = "/items/1/increment";
    inc.body = "{\"delta\":5}";
    assert(router.handle(inc).status_code == 200);
    assert(storage.get(1)->value == 20);
    assert(storage.get(1)->version == 3);

    HttpRequest list_req;
    list_req.method = "GET";
    list_req.path = "/items";
    list_req.query_params = {{"offset", "0"}, {"limit", "10"}};
    assert(router.handle(list_req).status_code == 200);

    HttpRequest del;
    del.method = "DELETE";
    del.path = "/items/1";
    assert(router.handle(del).status_code == 204);
    assert(router.handle(get_one).status_code == 404);
}

int main() {
    test_parser();
    test_endpoints();
    std::cout << "v0.2 unit tests passed!\n";
    return 0;
}