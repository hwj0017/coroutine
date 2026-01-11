#include "http/httpserver.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "http/httpcontext.h"
auto utils::main_coro() -> MainCoroutine
{
    HttpServer server("127.0.0.1", 8888);
    server.GET("/", [](HttpContext* ctx) -> Coroutine<> {
        ctx->response().body = "Hello World";
        co_return;
    });
    server.start();
    co_await server.join();
    co_return 0;
}