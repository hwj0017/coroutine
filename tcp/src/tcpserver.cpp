#include "tcp/tcpserver.h"
#include "coroutine/coroutine.h"
#include "tcpacceptor.h"
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <span>
#include <string>
#include <string_view>
namespace utils
{
TcpServer::TcpServer(std::string_view host, uint16_t port, Handler handler)
    : acceptor_(std::make_unique<TcpAcceptor>(host, port)), handler_(std::move(handler))
{
}
TcpServer::~TcpServer() = default;

void TcpServer::start() { co_spawn(start_impl()); }

auto TcpServer::start_impl() -> Coroutine<>
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