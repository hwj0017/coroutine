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

        // 设定一个最小读取底线，防止为了收 20 字节包头单独发起一次系统调用
        constexpr size_t MinReadSize = 1024;

        Buffer buffer;
        RpcParser parser;
        RpcMessage msg;

        while (true)
        {
            // 1. 核心优化：动态计算还需要读多少数据
            size_t expected_bytes = parser.get_expected_bytes();
            size_t readable_bytes = buffer.readable_span().size();

            // 缺少的字节数 = 期望字节数 - 当前已有字节数
            size_t need_bytes = 0;
            if (expected_bytes > readable_bytes)
            {
                need_bytes = expected_bytes - readable_bytes;
            }

            // 最终读取量：在 "缺少的字节数" 和 "最小基础量" 之间取最大值
            // - 如果是普通小包，按 1KB 读
            // - 如果解析出 Header 发现是个 10MB 的大包，这里直接变成读 10MB
            size_t bytes_to_read = std::max(MinReadSize, need_bytes);

            // 2. 告诉 Buffer：“给我确切准备好 bytes_to_read 大小的连续内存写空间”
            // 这里会触发底层 vector 或内存池的动态扩容
            auto n = co_await session->recv(buffer.writable_span(bytes_to_read));
            if (n <= 0)
            {
                break; // 对端关闭或出错
            }
            buffer.commit_write(n);

            // 3. 循环解析
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
                    // 数据不够。退出内层循环，回到上面重新动态扩容并等待网络数据
                    break;
                }
                else if (result == RpcParseResult::Success)
                {
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
