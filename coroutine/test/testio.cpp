#include "coroutine/main.h"
#include "coroutine/syscall.h"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <unistd.h>
auto utils::main_coro() -> MainCoroutine
{
    // struct io_uring ring;
    // int ret = io_uring_queue_init(8, &ring, 0);
    // auto sqe = io_uring_get_sqe(&ring);
    // double timeout = 1;
    // struct __kernel_timespec ts;
    // ts.tv_sec = timeout;
    // ts.tv_nsec = (timeout - ts.tv_sec) * 1e9;
    // io_uring_prep_timeout(sqe, &ts, 0, 0);

    // std::this_thread::sleep_for(std::chrono::seconds(2));
    // io_uring_submit(&ring);
    // std::this_thread::sleep_for(std::chrono::seconds(2));
    // // io_uring_submit_and_wait(&ring, 1);
    // unsigned head;
    // struct io_uring_cqe* cqe;
    // // 批量遍历所有完成事件
    // io_uring_for_each_cqe(&ring, head, cqe) { std::cout << "timeout" << std::endl; }
    co_await utils::delay(1);
    std::cout << "timeout" << std::endl;
    co_return 0;
}