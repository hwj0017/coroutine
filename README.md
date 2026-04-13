

# Coroutine-Framework

基于 **C++20 Coroutines** 和 **Linux io_uring** 的高性能异步框架。

## 🌟 项目亮点

* **轻量级调度**：采用 M:N 协程模型，支持 **Work-Stealing** 调度和 **RunNext** 缓存优化。
* **异步 IO**：深度集成 **io_uring**，提供全异步的网络读写（Read/Write/Accept/Connect）。
* **同步原语**：提供协程安全的 `Channel`（类 Go 设计）、`WaitGroup`、`Mutex` 和 `ConditionVariable`。

## 🛠️ 快速开始

### 依赖环境

* Linux Kernel >= 5.10 (需支持 io_uring)
* GCC 11+ / Clang 13+
* 库依赖: `liburing`

### 编译

```bash
mkdir build && cd build
cmake ..

```




## 💻 简单示例

```cpp
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "tcp/inetaddress.h"
#include "tcp/socket.h"
#include "tcp/tcpserver.h"
#include <array>
#include <cstddef>
#include <iostream>
#include <span>
auto echo(utils::Socket conn) -> utils::Coroutine<>
{
    constexpr size_t buffer_size = 1024;
    std::array<char, buffer_size> buffer;
    while (true)
    {
        if (auto n = co_await conn.recv(buffer); n > 0)
        {
            std::cout << "Received: " << std::string{buffer.data(), static_cast<size_t>(n)} << std::endl;
            co_await conn.send({buffer.data(), static_cast<size_t>(n)});
        }
        else
        {
            co_return;
        }
    }
}
auto utils::main_coro() -> MainCoroutine
{
    InetAddress addr(8888, "127.0.0.1");
    utils::TcpServer server(addr);
    server.set_connection_handler(echo);
    co_await server.start();
    co_return 0;
}

```
