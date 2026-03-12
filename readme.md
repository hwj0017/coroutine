

# Coroutine-Framework

基于 **C++20 Coroutines** 和 **Linux io_uring** 的高性能异步框架。

## 🌟 项目亮点

* **轻量级调度**：采用 M:N 协程模型，支持 **Work-Stealing** 调度和 **RunNext** 缓存优化。
* **异步 IO**：深度集成 **io_uring**，提供全异步的网络读写（Read/Write/Accept/Connect）。
* **同步原语**：提供协程安全的 `Channel`（类 Go 设计）、`WaitGroup`、`Mutex` 和 `ConditionVariable`。

## 🚀 性能表现 (Benchmark)

在 Ubuntu (WSL2) 环境下性能测试：
| 🧪 测试维度 | 🐹 Go (Goroutines) | 🚀 C++ 无栈协程 | 🏆 性能优势对比 |
| :--- | :--- | :--- | :--- |
| **[1] Yield 切换**<br>(100万并发) | 418 ns / op | **4 ns / op** | 🟢 **C++ 快 104.5 倍** |
| **[2] Ping-Pong 延迟**<br>(1v1 同步) | 144 ns / op | **46 ns / op** | 🟢 **C++ 快 3.1 倍** |
| **[3] MPMC 吞吐量**<br>(16v16 竞争) | 22.12 M msgs/sec | **33.06 M msgs/sec** | 🟢 **C++ 领先 49.5%** |

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
        if (auto n = co_await conn.read(buffer); n > 0)
        {
            std::cout << "Received: " << std::string{buffer.data(), static_cast<size_t>(n)} << std::endl;
            co_await conn.write({buffer.data(), static_cast<size_t>(n)});
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
