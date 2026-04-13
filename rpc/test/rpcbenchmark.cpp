#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string>
#include <vector>

// 框架相关头文件
#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "coroutine/waitgroup.h"
#include "rpc/rpcclient.h"
#include "service.pb.h"

using namespace std::chrono;

namespace utils
{

// --- 结果收集器：支持无锁/轻量锁合并 ---
struct BenchResult
{
    std::mutex mtx;
    std::vector<double> latencies; // 毫秒单位
    std::atomic<size_t> success_count{0};
    std::atomic<size_t> fail_count{0};

    void reserve(size_t n) { latencies.reserve(n); }

    // 协程运行结束时批量合并结果，避免频繁加锁
    void merge(std::vector<double>& local_latencies, size_t success, size_t fail)
    {
        success_count += success;
        fail_count += fail;
        std::lock_guard<std::mutex> lock(mtx);
        latencies.insert(latencies.end(), local_latencies.begin(), local_latencies.end());
    }

    void reset()
    {
        latencies.clear();
        success_count = 0;
        fail_count = 0;
    }
};

// --- 生成负载数据 ---
std::string make_payload(size_t bytes) { return std::string(bytes, 'x'); }

// --- 核心测试会话协程 ---
auto run_session(RpcClient& client, int req_count, const std::string& payload, BenchResult& result, WaitGroup& wg)
    -> Coroutine<void>
{
    // RAII 确保协程退出时调用 wg.done()
    struct DoneGuard
    {
        WaitGroup& g;
        ~DoneGuard() { g.done(); }
    } guard{wg};

    std::vector<double> local_latencies;
    local_latencies.reserve(req_count);
    size_t local_success = 0;
    size_t local_fail = 0;

    rpc::EchoRequest req;
    req.set_data(payload); // 载入测试负载

    for (int i = 0; i < req_count; ++i)
    {
        rpc::EchoResponse res;
        auto start = high_resolution_clock::now();

        // 执行 RPC 调用
        if (co_await client.call("echo", req, res))
        {
            auto end = high_resolution_clock::now();
            double ms = duration_cast<microseconds>(end - start).count() / 1000.0;
            local_latencies.push_back(ms);
            local_success++;
        }
        else
        {
            local_fail++;
        }
    }

    result.merge(local_latencies, local_success, local_fail);
    co_return;
}

// --- 统计输出逻辑 ---
void print_report(size_t size_bytes, int conc, double duration_s, BenchResult& res)
{
    if (res.latencies.empty())
        return;

    std::sort(res.latencies.begin(), res.latencies.end());

    double qps = res.success_count / duration_s;
    double throughput_mbs = (qps * size_bytes) / (1024.0 * 1024.0);
    double avg = std::accumulate(res.latencies.begin(), res.latencies.end(), 0.0) / res.latencies.size();
    double p95 = res.latencies[static_cast<size_t>(res.latencies.size() * 0.95)];
    double p99 = res.latencies[static_cast<size_t>(res.latencies.size() * 0.99)];

    std::cout << std::setw(8) << size_bytes << std::setw(8) << conc << std::setw(12) << std::fixed
              << std::setprecision(1) << qps << std::setw(10) << std::setprecision(2) << avg << std::setw(10) << p99
              << std::setw(10) << throughput_mbs << std::endl;
}

// --- 主入口：测试矩阵 ---
auto main_coro() -> MainCoroutine
{
    // 测试维度配置
    std::vector<size_t> payload_sizes = {64, 1024, 16384, 131072}; // 64B, 1K, 16K, 128K
    std::vector<int> concurrency_levels = {1, 10, 100, 1000};      // 并发阶梯
    const int TEST_SAMPLES = 20000;                                // 每个组合的总请求数

    RpcClient client("127.0.0.1", 8888);
    BenchResult result;

    // 1. 预热 (Warm-up)
    std::cout << "Warming up systems..." << std::endl;
    {
        WaitGroup warm_wg;
        rpc::EchoRequest req;
        rpc::EchoResponse res;
        for (int i = 0; i < 500; ++i)
            co_await client.call("echo", req, res);
    }

    // 2. 打印表头
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << std::left << std::setw(8) << "Size(B)" << std::setw(8) << "Conc" << std::setw(12) << "QPS"
              << std::setw(10) << "Avg(ms)" << std::setw(10) << "P99(ms)" << std::setw(10) << "MB/s" << std::endl;
    std::cout << std::string(60, '-') << std::endl;

    // 3. 开始矩阵测试
    for (size_t size : payload_sizes)
    {
        std::string payload = make_payload(size);

        for (int conc : concurrency_levels)
        {
            result.reset();
            result.reserve(TEST_SAMPLES);

            WaitGroup wg;
            int req_per_coro = TEST_SAMPLES / conc;

            auto start_time = high_resolution_clock::now();

            for (int i = 0; i < conc; ++i)
            {
                wg.add(1);
                co_spawn(run_session(client, req_per_coro, payload, result, wg));
            }

            co_await wg.wait(); // 异步等待这一轮结束

            auto end_time = high_resolution_clock::now();
            double duration_s = duration_cast<milliseconds>(end_time - start_time).count() / 1000.0;

            print_report(size, conc, duration_s, result);
        }
        std::cout << std::string(60, '-') << std::endl;
    }
    co_await client.join();
    co_return 0;
}

} // namespace utils