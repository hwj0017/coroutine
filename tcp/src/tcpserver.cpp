#include "tcp/tcpserver.h"
#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "tcpacceptor.h"
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
namespace utils
{
struct TcpServer::Impl
{
    Impl(std::string_view host, uint16_t port, Handler&& handler) : acceptor_(host, port), handler_(std::move(handler))
    {
    }
    void start()
    {
        co_spawn(accept_connection());
        // co_spawn(clear_connection());
    }
    void stop();
    auto join();
    auto accept_connection() -> Coroutine<>
    {
        while (true)
        {
            if (auto Connection = co_await acceptor_.accept(); Connection)
            {
                // connections_.emplace(Connection->get_id(), Connection);
                co_spawn(handler_(std::move(Connection)));
            }
        }
    }

    TcpAcceptor acceptor_;
    Handler handler_;
    // std::unordered_map<size_t, std::weak_ptr<TcpConnection>> connections_;
};
TcpServer::TcpServer(std::string_view host, uint16_t port, Handler handler)
    : impl_(std::make_unique<Impl>(host, port, std::move(handler)))
{
}
TcpServer::~TcpServer() = default;

void TcpServer::start() { impl_->start(); }

auto TcpServer::join() -> std::suspend_always { return {}; }
} // namespace utils