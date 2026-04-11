#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "coroutine/mutex.h"
#include <cstddef>
#include <iostream>
#include <string>

namespace utils
{
// 模拟全局资源
int shared_resource = 0;
Mutex mtx;
ConditionVariable cv(mtx);
Channel<> ch;
// 生产者：增加资源并通知
Coroutine<> producer(int id)
{
    for (int i = 0; i < 6; ++i)
    {
        // 使用 guard 自动管理生命周期
        {
            auto gard = co_await mtx.guard();
            shared_resource++;
            std::cout << "[Producer " << id << "] Produced: " << shared_resource << std::endl;
        }
        // 模拟 notify_one 的行为 (需要你在 CV 中实现具体逻辑，这里演示逻辑流)
        // 实际上在你的代码里，你需要实现 notify 移动 Awaiter 从 CV 队列到 Mutex 队列
        // 下文补充说明
        cv.notify_one();

        co_yield {}; // 让出 CPU 模拟并发
    }
    co_await ch.send();
}

// 消费者：等待资源大于 0 时消费
Coroutine<> consumer(int id)
{
    // 关键点：传入 Predicate [shared_resource > 0]
    // 对应你的 WaitAwaiter 构造函数
    auto wait_predicate = []() { return shared_resource > 0; };

    for (int i = 0; i < 3; ++i)
    {
        co_await mtx.lock();

        // 如果条件不满足，wait 会释放锁并挂起
        // 当被唤醒时，unlock 里的 try_claim_lock 会重新检查 predicate
        co_await cv.wait(wait_predicate);

        shared_resource--;
        std::cout << "[Consumer " << id << "] Consumed. Remaining: " << shared_resource << std::endl;

        mtx.unlock();

        co_yield {}; // 让出 CPU 模拟并发
    }
    co_await ch.send();
}

auto main_coro() -> MainCoroutine
{
    co_spawn(producer(1));
    co_spawn(consumer(1));
    co_spawn(consumer(2));
    for (int i = 0; i < 3; ++i)
        co_await ch.recv();
    co_return 0;
}
} // namespace utils
