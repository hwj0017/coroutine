#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/cospawn.h"
#include "coroutine/main.h"
#include "coroutine/waitgroup.h"
#include <cassert>
#include <iostream>
#include <vector>

namespace utils
{

// ============================================================================
// 测试1: 无缓冲 Channel 基本操作
// ============================================================================
auto test_unbuffered_basic() -> Coroutine<>
{
    std::cout << "=== Test 1: Unbuffered Channel Basic ===" << std::endl;

    Channel<int, 0> ch;
    WaitGroup wg;
    wg.add(2);
    auto sender = [](WaitGroup& wg, Channel<int, 0>& ch) -> Coroutine<> {
        auto done = DoneGuard(wg);
        auto state = co_await ch.send(42);
        assert(state == State::OK);
        std::cout << "  Sent: 42" << std::endl;
    };

    auto receiver = [](WaitGroup& wg, Channel<int, 0>& ch) -> Coroutine<> {
        auto done = DoneGuard(wg);
        auto [value, state] = co_await ch.recv();
        assert(state == State::OK);
        assert(value == 42);
        std::cout << "  Received: " << value << std::endl;
    };

    co_spawn(sender(wg, ch));
    co_spawn(receiver(wg, ch));

    co_await wg.wait();

    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试2: 有缓冲 Channel 基本操作
// ============================================================================
auto test_buffered_basic() -> Coroutine<>
{
    std::cout << "=== Test 2: Buffered Channel Basic ===" << std::endl;

    Channel<int, 4> ch;
    WaitGroup wg;
    wg.add(1);

    // 先发送，不阻塞
    auto sender1 = [](WaitGroup& wg, Channel<int, 4>& ch) -> Coroutine<> {
        auto done = DoneGuard(wg);
        for (int i = 0; i < 3; ++i)
        {
            auto state = co_await ch.send(i);
            assert(state == State::OK);
            std::cout << "  Sent: " << i << std::endl;
        }
    };

    co_spawn(sender1(wg, ch));
    co_await wg.wait(); // 等待发送完成

    // 接收
    for (int i = 0; i < 3; ++i)
    {
        auto [value, state] = co_await ch.recv();
        assert(state == State::OK);
        assert(value == i);
        std::cout << "  Received: " << value << std::endl;
    }

    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试3: Channel 满时阻塞发送
// ============================================================================
auto test_send_block_when_full() -> Coroutine<>
{
    std::cout << "=== Test 3: Send Blocks When Full ===" << std::endl;

    Channel<int, 2> ch;
    WaitGroup wg;
    wg.add(2);

    auto sender = [](WaitGroup& wg, Channel<int, 2>& ch) -> Coroutine<> {
        auto done = DoneGuard(wg);
        std::cout << "  Sending 1..." << std::endl;
        co_await ch.send(1);
        std::cout << "  Sending 2..." << std::endl;
        co_await ch.send(2);
        std::cout << "  Sending 3 (will block)..." << std::endl;

        // 这里会阻塞，因为缓冲区已满
        co_await ch.send(3);
        std::cout << "  Send 3 completed!" << std::endl;

        co_await ch.send(4);
        std::cout << "  Send 4 completed!" << std::endl;
    };

    auto receiver = [](WaitGroup& wg, Channel<int, 2>& ch) -> Coroutine<> {
        auto done = DoneGuard(wg);
        // 稍后接收，解除阻塞
        for (int i = 1; i <= 4; ++i)
        {
            auto [value, state] = co_await ch.recv();
            assert(state == State::OK);
            assert(value == i);
            std::cout << "  Received: " << value << std::endl;
        }
    };

    co_spawn(sender(wg, ch));
    co_spawn(receiver(wg, ch));

    co_await wg.wait();

    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试4: 空 Channel 时阻塞接收
// ============================================================================
auto test_recv_block_when_empty() -> Coroutine<>
{
    std::cout << "=== Test 4: Recv Blocks When Empty ===" << std::endl;

    Channel<int, 4> ch;
    WaitGroup wg;
    wg.add(2);
    Channel<void, 0> ready;

    auto sender = [](WaitGroup& wg, Channel<int, 4>& ch, Channel<void, 0>& ready) -> Coroutine<> {
        auto done = DoneGuard(wg);
        co_await ready.recv(); // 等待 receiver 启动

        std::cout << "  Sending values..." << std::endl;
        co_await ch.send(100);
        co_await ch.send(200);
    };

    auto receiver = [](WaitGroup& wg, Channel<int, 4>& ch, Channel<void, 0>& ready) -> Coroutine<> {
        auto done = DoneGuard(wg);
        co_await ready.send(); // 通知 sender 可以开始

        std::cout << "  Receiving (will block)..." << std::endl;
        auto [value1, state1] = co_await ch.recv();
        assert(state1 == State::OK);
        assert(value1 == 100);
        std::cout << "  Received: " << value1 << std::endl;

        auto [value2, state2] = co_await ch.recv();
        assert(state2 == State::OK);
        assert(value2 == 200);
        std::cout << "  Received: " << value2 << std::endl;
    };

    co_spawn(sender(wg, ch, ready));
    co_spawn(receiver(wg, ch, ready));

    co_await wg.wait();

    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试5: Channel<void> (同步信号)
// ============================================================================
auto test_void_channel() -> Coroutine<>
{
    std::cout << "=== Test 5: Void Channel ===" << std::endl;

    Channel<void, 0> ch;
    WaitGroup wg;
    wg.add(2);
    auto sender = [](WaitGroup& wg, Channel<void, 0>& ch) -> Coroutine<> {
        auto done = DoneGuard(wg);
        std::cout << "  Sending signal..." << std::endl;
        auto state = co_await ch.send();
        assert(state == State::OK);
    };

    auto receiver = [](WaitGroup& wg, Channel<void, 0>& ch) -> Coroutine<> {
        auto done = DoneGuard(wg);
        auto state = co_await ch.recv();
        assert(state == State::OK);
        std::cout << "  Signal received!" << std::endl;
    };

    co_spawn(sender(wg, ch));
    co_spawn(receiver(wg, ch));

    co_await wg.wait();

    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试6: Channel close() 行为
// ============================================================================
auto test_channel_close() -> Coroutine<>
{
    std::cout << "=== Test 6: Channel Close ===" << std::endl;

    Channel<int, 8> ch;
    WaitGroup wg;
    wg.add(3);
    Channel<void, 0> ready;

    auto sender = [](WaitGroup& wg, Channel<int, 8>& ch, Channel<void, 0>& ready) -> Coroutine<> {
        auto done = DoneGuard(wg);
        co_await ch.send(1);
        co_await ch.send(2);

        // 尝试在已关闭的 channel 上发送
        co_await ch.send(3); // 应该返回 CLOSED
    };

    auto closer = [](WaitGroup& wg, Channel<int, 8>& ch, Channel<void, 0>& ready) -> Coroutine<> {
        auto done = DoneGuard(wg);
        co_await ready.recv(); // 等待 sender 发送了前2个元素

        std::cout << "  Closing channel..." << std::endl;
        ch.close();
    };

    auto receiver = [](WaitGroup& wg, Channel<int, 8>& ch, Channel<void, 0>& ready) -> Coroutine<> {
        auto done = DoneGuard(wg);
        co_await ready.send(); // 让 closer 知道可以关闭了

        auto [v1, s1] = co_await ch.recv();
        assert(s1 == State::OK);
        assert(v1 == 1);

        auto [v2, s2] = co_await ch.recv();
        assert(s2 == State::OK);
        assert(v2 == 2);

        // 再接收，channel 已关闭但还有数据
        auto [v3, s3] = co_await ch.recv();
        assert(s3 == State::OK);
        assert(v3 == 3);

        // 再接收，channel 已关闭且无数据
        auto [v4, s4] = co_await ch.recv();
        assert(s4 == State::CLOSED);
        std::cout << "  Received CLOSED state" << std::endl;
    };

    co_spawn(sender(wg, ch, ready));
    co_spawn(receiver(wg, ch, ready));
    co_spawn(closer(wg, ch, ready));

    co_await wg.wait();
    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试7: 多生产者多消费者
// ============================================================================
auto test_multi_producer_consumer() -> Coroutine<>
{
    std::cout << "=== Test 7: Multi Producer/Consumer ===" << std::endl;

    Channel<int, 16> ch;
    const int num_producers = 3;
    const int num_consumers = 3;
    const int items_per_producer = 5;

    std::atomic<int> total_sent{0};
    std::atomic<int> total_recv{0};
    Channel<void, 0> done;

    auto producer = [](Channel<int, 16>& ch, std::atomic<int>& total_sent, Channel<void, 0>& done,
                       int id) -> Coroutine<> {
        int start = id * items_per_producer;
        for (int i = 0; i < items_per_producer; ++i)
        {
            auto state = co_await ch.send(start + i);
            assert(state == State::OK);
            total_sent++;
            std::cout << "  Producer " << id << " sent: " << start + i << std::endl;
        }
        co_await done.send();
    };

    auto consumer = [](Channel<int, 16>& ch, std::atomic<int>& total_recv, Channel<void, 0>& done,
                       int id) -> Coroutine<> {
        int recv_count = 0;
        while (recv_count < items_per_producer * num_producers / num_consumers)
        {
            auto [value, state] = co_await ch.recv();
            if (state == State::CLOSED)
            {
                std::cout << "  Consumer " << id << " got CLOSED, exiting" << std::endl;
                break;
            }
            std::cout << "  Consumer " << id << " received: " << value << std::endl;
            total_recv++;
            recv_count++;
        }
        co_await done.send();
    };

    // 启动生产者
    for (int i = 0; i < num_producers; ++i)
    {
        co_spawn(producer(ch, total_sent, done, i));
    }

    // 启动消费者
    for (int i = 0; i < num_consumers; ++i)
    {
        co_spawn(consumer(ch, total_recv, done, i));
    }

    // 等待所有生产者和消费者完成
    for (int i = 0; i < num_producers + num_consumers; ++i)
    {
        co_await done.recv();
    }

    ch.close();

    assert(total_sent == items_per_producer * num_producers);
    assert(total_recv == items_per_producer * num_producers);

    std::cout << "PASSED: Sent " << total_sent << ", Received " << total_recv << std::endl;
}

// ============================================================================
// 主协程：运行所有测试
// ============================================================================
auto main_coro() -> MainCoroutine
{
    std::cout << "========================================" << std::endl;
    std::cout << "      Channel Test Suite                " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    co_await test_unbuffered_basic();
    std::cout << std::endl;

    co_await test_buffered_basic();
    std::cout << std::endl;

    co_await test_send_block_when_full();
    std::cout << std::endl;

    co_await test_recv_block_when_empty();
    std::cout << std::endl;

    co_await test_void_channel();
    std::cout << std::endl;

    co_await test_channel_close();
    std::cout << std::endl;

    co_await test_multi_producer_consumer();
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "   All Tests PASSED!   " << std::endl;
    std::cout << "========================================" << std::endl;

    co_return 0;
}

} // namespace utils