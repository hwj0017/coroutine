
#include "rpc/rpcclient.h"
#include "coroutine/coroutine.h"
#include "coroutine/waitgroup.h"
#include "rpcparser.h"
#include "tcp/buffer.h"
#include <cassert>
#include <endian.h>
#include <iostream>
#include <ostream>
#include <vector>
namespace utils
{
auto RpcClient::write_worker() -> Coroutine<>
{
    DoneGuard guard{wg_};
    if (!is_connected_.exchange(true))
    {
        if (auto res = co_await socket_.connect(server_addr_); res < 0)
        {
            co_return;
        }
    }
    while (true)
    {
        auto [req, state] = co_await pending_.recv();
        if (state != State::OK)
        {
            break;
        }
        RpcMessage msg;
        // TODO
        msg.header.set_sequence_id(req.sequence_id);
        msg.header.set_method_length(req.method.size());
        msg.header.set_body_length(req.payload.size());

        msg.method = std::move(req.method);
        msg.payload = std::move(req.payload);

        auto str = msg.string();
        auto count = co_await socket_.send(str);
        if (count <= 0)
        {
            break;
        }
    }
}
auto RpcClient::read_worker() -> Coroutine<>
{
    DoneGuard guard{wg_};
    if (!is_connected_.exchange(true))
    {
        if (auto res = co_await socket_.connect(server_addr_); res < 0)
        {
            co_return;
        }
    }
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
        auto n = co_await socket_.recv(buffer.writable_span(bytes_to_read));
        if (n <= 0)
        {
            break; // 对端关闭或出错
        }
        buffer.commit_write(n);

        // 2. 循环丢给 Parser 切包
        while (!buffer.empty())
        {
            auto result = parser.parse(buffer.readable_span(), msg);

            if (result == RpcParseResult::Error)
            {
                std::println(std::cout, "Protocol error, closing connection");
                // 协议错误，立刻断开连接
                socket_.close();
                std::vector<ReadyAwaiter*> awaiters;
                {
                    auto guard = std::lock_guard(mutex_);
                    for (auto it = awaiters_.begin(); it != awaiters_.end();)
                    {
                        awaiters.push_back(it->second);
                        it = awaiters_.erase(it);
                    }
                }
                for (auto awaiter : awaiters)
                {
                    auto handle = awaiter->set_value({{}, true});
                    assert(handle);
                    co_spawn(handle);
                }
                break;
            }
            else if (result == RpcParseResult::Incomplete)
            {
                // 半包，跳出内层循环，去外层 co_await 读更多数据
                break;
            }
            else if (result == RpcParseResult::Success)
            {
                Promise* handle = nullptr;
                {
                    auto guard = std::lock_guard(mutex_);
                    auto id = msg.header.get_sequence_id();
                    if (auto it = awaiters_.find(id); it == awaiters_.end())
                    {
                        responses_.emplace(id, RpcResponse{std::move(msg.payload), false});
                    }
                    else
                    {
                        handle = it->second->set_value({std::move(msg.payload), false});
                        awaiters_.erase(it);
                    }
                }
                if (handle)
                {
                    co_spawn(handle);
                }
                buffer.retrieve(parser.get_consumed_bytes());
                parser.reset();
            }
        }
    }
}
} // namespace utils