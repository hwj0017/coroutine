#pragma once
#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "rpc/message.h"
#include "tcp/socket.h"
#include <cstddef>
#include <string>
#include <string_view>
namespace utils
{
class RpcClient
{
  public:
    RpcClient(std::string_view host, uint16_t port)
        : socket_(Socket::create_tcp()), server_addr_(port, host), pending_(1024)
    {
        co_spawn(write_worker());
        co_spawn(read_worker());
    }

    template <IsMessage R, IsMessage Arg> auto call(std::string method, const Arg& arg, R& reply) -> Coroutine<bool>;

  private:
    struct RpcRequest
    {
        uint64_t sequence_id;
        std::string method;
        std::string payload;
    };
    struct RpcResponse
    {
        std::string payload;
        bool is_error;
    };
    class ReadyAwaiter
    {
      public:
        ReadyAwaiter(RpcClient* client, uint64_t id) : client_(client), id_(id) {}
        auto await_ready() { return false; }
        template <typename Promise> auto await_suspend(std::coroutine_handle<Promise> handle)
        {
            handle_ = &handle.promise();
            return !client_->ready_impl(this);
        }
        auto await_resume() const noexcept -> RpcResponse { return std::move(response_); }
        auto set_value(RpcResponse response_)
        {
            this->response_ = std::move(response_);
            return handle_;
        }

      private:
        RpcClient* client_;
        Promise* handle_;
        uint64_t id_;
        RpcResponse response_;
        friend class RpcClient;
    };
    auto ready(uint64_t id) -> ReadyAwaiter { return ReadyAwaiter{this, id}; }
    bool ready_impl(ReadyAwaiter* awaiter)
    {
        auto guard = std::lock_guard<std::mutex>(mutex_);
        if (is_closed_)
        {
            awaiter->response_.is_error = true;
            return true;
        }
        auto it = responses_.find(awaiter->id_);
        if (it == responses_.end())
        {
            awaiters_.emplace(awaiter->id_, awaiter);
            return false;
        }

        awaiter->set_value(std::move(it->second));
        responses_.erase(it);
        return true;
    }
    auto write_worker() -> Coroutine<>;
    auto read_worker() -> Coroutine<>;
    Socket socket_;
    InetAddress server_addr_;
    std::mutex mutex_;
    bool is_closed_{false};
    std::atomic<bool> is_connected_ = false;
    Channel<RpcRequest> pending_;
    std::unordered_map<uint64_t, ReadyAwaiter*> awaiters_;
    std::unordered_map<uint64_t, RpcResponse> responses_;
    std::atomic<size_t> sequence_id_ = 0;
};

template <IsMessage R, IsMessage Arg>
auto RpcClient::call(std::string method, const Arg& arg, R& reply) -> Coroutine<bool>
{
    if (!is_connected_.exchange(true))
    {
        if (auto res = co_await socket_.connect(server_addr_); res < 0)
        {
            co_return false;
        }
    }
    auto sequence_id = ++sequence_id_;
    if (auto res = co_await pending_.send(
            {sequence_id, std::move(method), static_cast<const Message*>(&arg)->SerializeAsString()});
        res != State::OK)
    {
        co_return false;
    }
    auto response = co_await ReadyAwaiter{this, sequence_id};
    if (response.is_error)
    {
        co_return false;
    }
    auto err = static_cast<Message*>(&reply)->ParseFromString(response.payload);
    co_return err;
}
} // namespace utils