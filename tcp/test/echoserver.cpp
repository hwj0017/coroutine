#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "tcp/inetaddress.h"
#include "tcp/socket.h"
#include "tcp/tcpserver.h"
#include <array>
#include <cstddef>
#include <iostream>
#include <span>
auto echo(utils::Socket conn) -> utils::Coroutine<>
{
    constexpr size_t buffer_size = 1024;
    std::array<char, buffer_size> buffer;
    while (true)
    {
        if (auto n = co_await conn.recv(buffer); n > 0)
        {
            std::cout << "received " << n << " bytes" << std::endl;
            co_await conn.send({buffer.data(), static_cast<size_t>(n)});
        }
        else
        {
            co_return;
        }
    }
}
auto utils::main_coro() -> MainCoroutine
{
    InetAddress addr(8888, "127.0.0.1");
    utils::TcpServer server(addr);
    server.set_connection_handler(echo);
    co_await server.start();
    co_return 0;
}