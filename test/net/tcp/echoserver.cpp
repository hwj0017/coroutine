#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "net/tcp/tcpconnection.h"
#include "net/tcp/tcpserver.h"
#include <array>
#include <cstddef>
auto echo(std::shared_ptr<utils::TcpConnection> conn) -> utils::Coroutine<>
{
    constexpr size_t buffer_size = 1024;
    std::array<char, buffer_size> buffer;
    while (true)
    {

        if (auto n = co_await conn->read(buffer.data(), buffer_size); n > 0)
        {
            co_await conn->write(buffer.data(), n);
        }
        else
        {
            co_return;
        }
    }
}
auto utils::main_coro() -> Coroutine<>
{
    utils::TcpServer server(8080, echo);
    co_spawn(server.start());
    co_await server.join();
}