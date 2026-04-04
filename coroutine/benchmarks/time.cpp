#include "coroutine/channel.h"
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "coroutine/waitgroup.h"
#include <fstream>
#include <iomanip>
#include <thread>
namespace utils
{

size_t get_mem_sys_bytes()
{
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
    {
        if (line.compare(0, 6, "VmRSS:") == 0)
        {
            size_t mem_kb = 0;
            sscanf(line.c_str(), "VmRSS: %zu kB", &mem_kb);
            return mem_kb * 1024;
        }
    }
    return 0;
}
auto main_coro() -> Coroutine<int>
{
    size_t beforeAlloc = get_mem_sys_bytes();
    WaitGroup wg;
    auto count = 1000;
    Channel<> ch;
    for (int i = 0; i < count; i++)
    {
        wg.add(1);
        co_spawn([](Channel<>& ch, WaitGroup& wg) -> Coroutine<> {
            wg.done();
            co_await ch.send();
        }(ch, wg));
    }
    co_await wg.wait();
    // 给系统一点点稳定时间 (等待底层调度器或内存分配器收敛)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 3. 记录创建后的内存
    size_t afterAlloc = get_mem_sys_bytes();
    size_t totalUsed = (afterAlloc > beforeAlloc) ? (afterAlloc - beforeAlloc) : 0;
    size_t avgUsed = totalUsed / count;

    // 打印测试报告，保持和 Go 版本一致的格式
    std::cout << "--- 测试报告 (数量: " << count << ") ---\n";
    std::cout << "总新增内存占用 (RSS): " << totalUsed / 1024 << " KB\n";
    std::cout << "平均每个协程占用: " << avgUsed << " 字节 (约 " << std::fixed << std::setprecision(2)
              << static_cast<double>(avgUsed) / 1024.0 << " KB)\n";

    // 假设你的调度器有获取当前协程数量的接口
    // std::cout << "当前运行中的协程数: " << utils::num_goroutines() << "\n";
    ch.close();
    co_return 0;
}

} // namespace utils