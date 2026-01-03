#include "net/tcp/tcpserver.h"
#include "coroutine/coroutine.h"
#include "tcpacceptor.h"
#include <cassert>
#include <coroutine>
#include <memory>
#include <netinet/in.h>
namespace utils
{
TcpServer::TcpServer(int port, ConnectionHandler handler)
    : acceptor_(std::make_unique<TcpAcceptor>(port)), handler_(std::move(handler))
{
}
TcpServer::~TcpServer() = default;

auto TcpServer::start() -> Coroutine<>
{
    assert(acceptor_->start());

    while (true)
    {
        if (auto Connection = co_await acceptor_->accept(); Connection)
        {
            co_spawn(handler_(std::move(Connection)));
        }
    }
}
auto TcpServer::join() -> std::suspend_always { return {}; }
} // namespace utils