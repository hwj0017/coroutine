#include "coroutine/main.h"
#include "tcp/tcpconnector.h"
#include <array>
#include <cstddef>

auto utils::main_coro() -> MainCoroutine
{
    constexpr size_t buffer_size = 1024;
    std::array<char, buffer_size> buffer;
    TcpConnector connector("127.0.0.1", 8080);
    if (auto res = co_await connector.connect(); res < 0)
    {
        co_return -1;
    }
    if (auto res = co_await connector.write("hello world"); res < 0)
    {
        co_return -1;
    }
    if (auto res = co_await connector.read(buffer); res < 0)
    {
        co_return -1;
    }
    co_return 0;
}