#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "tcp/inetaddress.h"
#include "tcp/socket.h"
#include <array>
#include <cstddef>

auto utils::main_coro() -> MainCoroutine
{
    constexpr size_t buffer_size = 1024;
    std::array<char, buffer_size> buffer;
    Socket connector = Socket::create_tcp();
    InetAddress server{8888, "127.0.0.1"};
    if (auto res = co_await connector.connect(server); res < 0)
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