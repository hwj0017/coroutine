#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "tcp/tcpconnection.h"
#include "tcp/tcpserver.h"
#include <array>
#include <cstddef>
auto echo(std::shared_ptr<utils::TcpConnection> conn) -> utils::Coroutine<>
{
    constexpr size_t buffer_size = 1024;
    std::array<char, buffer_size> buffer;
    while (true)
    {

        if (auto n = co_await conn->read(buffer); n > 0)
        {
            co_await conn->write({buffer.data(), static_cast<size_t>(n)});
        }
        else
        {
            co_return;
        }
    }
}
auto utils::main_coro() -> MainCoroutine
{
    utils::TcpServer server("127.0.0.1", 8080, echo);
    server.start();
    co_await server.join();
    co_return 0;
}