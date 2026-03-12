
#include "rpc/rpcclient.h"
#include "coroutine/coroutine.h"
#include "coroutine/icallable.h"
#include "rpcparser.h"
#include "tcp/buffer.h"
#include <cassert>
#include <endian.h>
#include <iostream>
#include <vector>
namespace utils
{
auto RpcClient::write_worker() -> Coroutine<>
{
    while (true)
    {
        auto [req, state] = co_await pending_.recv();
        if (state != State::OK)
        {
            co_return;
        }
        RpcMessage msg;
        // TODO
        msg.header.set_sequence_id(sequence_id_);
        msg.header.set_method_length(req.method.size());
        msg.header.set_body_length(req.payload.size());

        msg.method = std::move(req.method);
        msg.payload = std::move(req.payload);

        auto str = msg.string();
        auto count = co_await socket_.write(str);
        if (count < 0)
        {
            co_return;
        }
        std::cout << "write " << count << " bytes" << std::endl;
    }
}
auto RpcClient::read_worker() -> Coroutine<>
{
    constexpr size_t BufferSize = 1024;
    Buffer buffer;
    RpcParser parser;
    RpcMessage msg;

    while (true)
    {
        // 1. 动态扩容并读取网络数据 (避免局部 temp_buffer 拷贝)

        auto n = co_await socket_.read(buffer.writable_span(BufferSize));
        std::cout << "read " << n << " bytes" << std::endl;
        if (n <= 0)
            break;
        buffer.commit_write(n);

        // 2. 循环丢给 Parser 切包
        while (!buffer.empty())
        {
            auto result = parser.parse(buffer.readable_span(), msg);

            if (result == RpcParseResult::Error)
            {
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
                co_return;
            }
            else if (result == RpcParseResult::Incomplete)
            {
                // 半包，跳出内层循环，去外层 co_await 读更多数据
                break;
            }
            else if (result == RpcParseResult::Success)
            {
                ICallable* handle = nullptr;
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