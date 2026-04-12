#include "rpc/rpcserver.h"
#include "coroutine/coroutine.h"
#include "coroutine/mutex.h"
#include "coroutine/syscall.h"
#include "rpcparser.h"
#include "tcp/session.h"
#include "tcp/tcpserver.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <ostream>
#include <span>
#include <string_view>
#include <vector>
namespace utils
{
// 引入一个协程安全的互斥锁 (你的框架如果支持的话，如 CoMutex)
// 如果没有，且都在单线程调度器跑，普通逻辑队列也可以

// 1. 定义 Impl 结构体及其成员函数
struct RpcServer::Impl
{
    TcpServer tcp_server_;
    std::unordered_map<std::string, std::function<std::string(std::string)>> services_;

    Impl(std::string_view listen_ip, uint16_t port) : tcp_server_(InetAddress{port, listen_ip})
    {
        // 只需用一个极简的 lambda 转发给 Impl 的成员函数即可
        tcp_server_.set_connection_handler([this](Socket connection) -> Coroutine<> {
            return this->handle_connection(std::make_shared<RpcSession>(std::move(connection)));
        });
    }

    // 提取出来的连接处理函数
    Coroutine<> handle_connection(std::shared_ptr<RpcSession> session)
    {
        session->start();
        constexpr size_t BufferSize = 1024;
        Buffer buffer;
        RpcParser parser;
        RpcMessage msg;

        while (true)
        {
            auto n = co_await session->read(buffer.writable_span(BufferSize));
            if (n <= 0)
                break;
            buffer.commit_write(n);

            while (!buffer.empty())
            {
                auto result = parser.parse(buffer.readable_span(), msg);

                if (result == RpcParseResult::Error)
                {
                    std::println(std::cout, "parse error");
                    session->close();
                    co_return;
                }
                else if (result == RpcParseResult::Incomplete)
                {
                    break;
                }
                else if (result == RpcParseResult::Success)
                {
                    // 完美切包，将具体业务执行逻辑也提取为一个成员函数
                    co_spawn(process_message(session, std::move(msg)));
                    buffer.retrieve(parser.get_consumed_bytes());
                    parser.reset();
                }
            }
        }
    }

    // 提取出来的单个 RPC 消息处理函数
    Coroutine<> process_message(std::shared_ptr<RpcSession> session, RpcMessage msg)
    {

        RpcMessage response;

        if (auto it = services_.find(msg.method); it == services_.end())
        {
            response.header.set_status_code(1);
        }
        else
        {
            response.payload = it->second(std::move(msg.payload));
            response.header.set_status_code(0);
        }
        response.header.set_sequence_id(msg.header.get_sequence_id());
        response.method = std::move(msg.method);
        response.header.set_method_length(response.method.size());
        response.header.set_body_length(response.payload.size());
        // 发送响应回客户端
        if (auto count = co_await session->send(response.string()); count != State::OK)
        {
            session->close();
        }
        // std::print(std::cout, "id:{}\n", msg.header.get_sequence_id());
    }
};

// 2. RpcServer 构造函数：变得非常清爽
RpcServer::RpcServer(std::string_view listen_ip, uint16_t port) : impl_(std::make_unique<Impl>(listen_ip, port)) {}

// 3. 析构与移动语义实现
RpcServer::~RpcServer() = default;

// 4. 委托方法调用
auto RpcServer::start() -> Coroutine<> { return impl_->tcp_server_.start(); }

void RpcServer::register_service_impl(std::string method, std::function<std::string(std::string)> func)
{
    impl_->services_.emplace(std::move(method), std::move(func));
}

} // namespace utils
