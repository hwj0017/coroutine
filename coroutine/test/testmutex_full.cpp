#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "coroutine/mutex.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <vector>

namespace utils
{

// ============================================================================
// 测试1: Mutex 基本功能测试 (lock/unlock)
// ============================================================================
auto test_mutex_basic() -> Coroutine<>
{
    std::cout << "=== Test 1: Mutex Basic (lock/unlock) ===" << std::endl;

    Mutex mtx;
    int counter = 0;
    const int iterations = 1000;

    std::atomic<int> ready{0};
    Channel<void, 0> done;

    auto worker = [&](int id) -> Coroutine<> {
        for (int i = 0; i < iterations; ++i)
        {
            co_await mtx.lock();
            ++counter;
            mtx.unlock();
        }
        co_await done.send();
    };

    co_spawn(worker(1));
    co_spawn(worker(2));

    for (int i = 0; i < 2; ++i)
        co_await done.recv();

    assert(counter == iterations * 2);
    std::cout << "PASSED: Counter = " << counter << std::endl;
}

// ============================================================================
// 测试2: Mutex Guard RAII 测试
// ============================================================================
auto test_mutex_guard() -> Coroutine<>
{
    std::cout << "=== Test 2: Mutex Guard (RAII) ===" << std::endl;

    Mutex mtx;
    int counter = 0;
    const int iterations = 1000;

    Channel<void, 0> done;

    auto worker = [&](int id) -> Coroutine<> {
        for (int i = 0; i < iterations; ++i)
        {
            // Guard 自动管理锁的生命周期
            {
                auto guard = co_await mtx.guard();
                ++counter;
            } // 这里自动释放锁
        }
        co_await done.send();
    };

    co_spawn(worker(1));
    co_spawn(worker(2));

    for (int i = 0; i < 2; ++i)
        co_await done.recv();

    assert(counter == iterations * 2);
    std::cout << "PASSED: Counter = " << counter << std::endl;
}

// ============================================================================
// 测试3: ConditionVariable wait + notify_one 测试
// ============================================================================
auto test_cv_notify_one() -> Coroutine<>
{
    std::cout << "=== Test 3: ConditionVariable notify_one ===" << std::endl;

    Mutex mtx;
    ConditionVariable cv(mtx);
    int data = 0;
    Channel<void, 0> done;

    // 消费者：等待数据
    auto consumer = [&]() -> Coroutine<> {
        co_await mtx.lock();

        // 等待数据 > 0
        co_await cv.wait([&]() { return data > 0; });

        std::cout << "  Consumer received data: " << data << std::endl;
        assert(data == 42);

        mtx.unlock();
        co_await done.send();
    };

    // 生产者：生产数据
    auto producer = [&]() -> Coroutine<> {
        co_await mtx.lock();
        data = 42;
        std::cout << "  Producer set data: " << data << std::endl;
        mtx.unlock();

        cv.notify_one();
        co_await done.send();
    };

    co_spawn(consumer());
    co_spawn(producer());

    for (int i = 0; i < 2; ++i)
        co_await done.recv();

    std::cout << "PASSED" << std::endl;
}

// ============================================================================
// 测试4: ConditionVariable wait + notify_all 测试
// ============================================================================
auto test_cv_notify_all() -> Coroutine<>
{
    std::cout << "=== Test 4: ConditionVariable notify_all ===" << std::endl;

    Mutex mtx;
    ConditionVariable cv(mtx);
    int ready_count = 0;
    const int num_consumers = 5;
    std::atomic<int> completed{0};

    Channel<void, 0> done;

    // 消费者：等待所有准备就绪
    auto consumer = [&](int id) -> Coroutine<> {
        co_await mtx.lock();

        // 等待 ready_count == num_consumers
        co_await cv.wait([&]() { return ready_count == num_consumers; });

        std::cout << "  Consumer " << id << " awakened!" << std::endl;
        ++completed;

        mtx.unlock();
        co_await done.send();
    };

    // 生产者：启动所有消费者
    auto launcher = [&]() -> Coroutine<> {
        // 启动所有消费者
        for (int i = 0; i < num_consumers; ++i)
        {
            co_spawn(consumer(i + 1));
            co_yield {}; // 让协程运行起来
        }

        co_await mtx.lock();
        ready_count = num_consumers;
        std::cout << "  All consumers ready, notifying..." << std::endl;
        mtx.unlock();

        cv.notify_all();

        // 发送完成信号
        co_await done.send();
    };

    co_spawn(launcher());

    // 等待 launcher 完成后的信号
    for (int i = 0; i < num_consumers + 1; ++i)
        co_await done.recv();

    assert(completed == num_consumers);
    std::cout << "PASSED: All " << completed << " consumers completed" << std::endl;
}

// ============================================================================
// 测试5: ConditionVariable 谓词在唤醒时重新检查
// ============================================================================
auto test_cv_predicate_recheck() -> Coroutine<>
{
    std::cout << "=== Test 5: CV Predicate Recheck ===" << std::endl;

    Mutex mtx;
    ConditionVariable cv(mtx);
    int data = 0;
    std::atomic<int> notified_Consumers{0};
    std::atomic<int> successful_consumers{0};

    Channel<void, 0> done;

    auto consumer = [&](int id) -> Coroutine<> {
        co_await mtx.lock();
        int notification_count = 0;

        co_await cv.wait([&]() {
            notification_count++;
            return data > 0;
        });

        if (data > 0)
        {
            std::cout << "  Consumer " << id << " successfully consumed" << std::endl;
            ++successful_consumers;
        }

        mtx.unlock();
        co_await done.send();
    };

    auto notifier = [&]() -> Coroutine<> {
        // 第一次通知，但条件不满足
        co_await mtx.lock();
        data = 0;
        std::cout << "  Notifying with data=0 (should not wake consumers)" << std::endl;
        mtx.unlock();
        cv.notify_one();

        co_yield {}; // 让协程运行

        // 第二次通知，条件满足
        co_await mtx.lock();
        data = 100;
        std::cout << "  Notifying with data=100 (should wake consumer)" << std::endl;
        mtx.unlock();
        cv.notify_one();
        co_await done.send();
    };

    co_spawn(consumer(1));
    co_spawn(notifier());

    for (int i = 0; i < 2; ++i)
        co_await done.recv();

    assert(successful_consumers == 1);
    std::cout << "PASSED: Predicate recheck worked correctly" << std::endl;
}

// ============================================================================
// 测试6: 生产者-消费者模型
// ============================================================================
auto test_producer_consumer() -> Coroutine<>
{
    std::cout << "=== Test 6: Producer-Consumer Model ===" << std::endl;

    Mutex mtx;
    ConditionVariable not_empty_cv(mtx);
    ConditionVariable not_full_cv(mtx);

    const int buffer_size = 5;
    std::vector<int> buffer;
    int total_produced = 0;
    int total_consumed = 0;
    const int items_per_producer = 10;

    Channel<void, 0> done;

    // 生产者
    auto producer = [&](int id) -> Coroutine<> {
        for (int i = 0; i < items_per_producer; ++i)
        {
            auto guard = co_await mtx.guard();

            // 等待缓冲区未满
            co_await not_full_cv.wait([&]() { return buffer.size() < buffer_size; });

            buffer.push_back(++total_produced);
            std::cout << "  Producer " << id << " produced: " << total_produced << " (buffer size: " << buffer.size()
                      << ")" << std::endl;

            not_empty_cv.notify_one();
        }
        co_await done.send();
    };

    // 消费者
    auto consumer = [&](int id) -> Coroutine<> {
        while (total_consumed < items_per_producer * 2)
        {
            auto guard = co_await mtx.guard();

            // 等待缓冲区非空
            co_await not_empty_cv.wait([&]() { return !buffer.empty(); });

            int item = buffer.front();
            buffer.erase(buffer.begin());
            ++total_consumed;

            std::cout << "  Consumer " << id << " consumed: " << item << " (buffer size: " << buffer.size() << ")"
                      << std::endl;

            co_yield {}; // 让出CPU

            not_full_cv.notify_one();
        }
        co_await done.send();
    };

    co_spawn(producer(1));
    co_spawn(producer(2));
    co_spawn(consumer(1));

    for (int i = 0; i < 3; ++i)
        co_await done.recv();

    assert(total_produced == items_per_producer * 2);
    assert(total_consumed == items_per_producer * 2);
    std::cout << "PASSED: Produced " << total_produced << ", Consumed " << total_consumed << std::endl;
}

// ============================================================================
// 主协程：运行所有测试
// ============================================================================
auto main_coro() -> MainCoroutine
{
    std::cout << "========================================" << std::endl;
    std::cout << "   Mutex & ConditionVariable Test Suite   " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    co_await test_mutex_basic();
    std::cout << std::endl;

    co_await test_mutex_guard();
    std::cout << std::endl;

    co_await test_cv_notify_one();
    std::cout << std::endl;

    co_await test_cv_notify_all();
    std::cout << std::endl;

    co_await test_cv_predicate_recheck();
    std::cout << std::endl;

    co_await test_producer_consumer();
    std::cout << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "   All Tests PASSED!   " << std::endl;
    std::cout << "========================================" << std::endl;

    co_return 0;
}

} // namespace utils