#include "coroutine/coroutine.h"
#include "coroutine/main.h"
#include "coroutine/syscall.h"
#include "coroutine/waitgroup.h"
#include "tcp/inetaddress.h"
#include "tcp/socket.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

using namespace std::chrono_literals;
namespace utils
{
class EchoClientTest
{
  public:
    struct Config
    {
        std::string host = "127.0.0.1";
        uint16_t port = 8888;
        int client_count = 100;
        int requests_per_client = 1000;
        size_t message_size = 1024;
        bool verbose = false;
    };

  private:
    Config config_;
    std::atomic<int> success_count_{0};
    std::atomic<int> fail_count_{0};
    std::atomic<long long> total_bytes_{0};
    std::chrono::steady_clock::time_point start_time_;

  public:
    explicit EchoClientTest(const Config& config) : config_(config) {}

    auto run_test() -> utils::Coroutine<int>
    {
        std::cout << "Starting echo client test..." << std::endl;
        std::cout << "Clients: " << config_.client_count << ", Requests per client: " << config_.requests_per_client
                  << ", Message size: " << config_.message_size << std::endl;

        start_time_ = std::chrono::steady_clock::now();
        utils::WaitGroup wg;

        // 启动多个客户端协程
        for (int i = 0; i < config_.client_count; ++i)
        {
            wg.add(1);
            utils::co_spawn(client_worker(i, wg));
        }

        // 等待所有客户端完成
        co_await wg.wait();

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_);

        std::cout << "\n=== Test Results ===" << std::endl;
        std::cout << "Total time: " << duration.count() << " ms" << std::endl;
        std::cout << "Successful requests: " << success_count_.load() << std::endl;
        std::cout << "Failed requests: " << fail_count_.load() << std::endl;
        std::cout << "Total bytes transferred: " << total_bytes_.load() << std::endl;

        if (success_count_.load() + fail_count_.load() > 0)
        {
            double req_per_sec = (double)(success_count_.load() + fail_count_.load()) * 1000.0 / duration.count();
            double mb_per_sec = (double)total_bytes_.load() / (1024.0 * 1024.0) * 1000.0 / duration.count();

            std::cout << "Requests/sec: " << req_per_sec << std::endl;
            std::cout << "Throughput: " << mb_per_sec << " MB/s" << std::endl;
        }

        co_return (fail_count_.load() == 0) ? 0 : -1;
    }

  private:
    auto client_worker(int client_id, utils::WaitGroup& wg) -> utils::Coroutine<void>
    {
        auto guard = utils::DoneGuard(wg);
        for (int i = 0; i < config_.requests_per_client; ++i)
        {
            if (co_await single_request(client_id, i))
            {
                success_count_.fetch_add(1);
            }
            else
            {
                fail_count_.fetch_add(1);
                if (config_.verbose)
                {
                    std::cout << "Client " << client_id << " request " << i << " failed" << std::endl;
                }
            }

            // 避免过于频繁的请求，可根据需要调整
            if (i % 100 == 0 && i > 0)
            {
                co_await utils::delay(1);
            }
        }

        co_return;
    }

    auto single_request(int client_id, int request_id) -> utils::Coroutine<bool>
    {
        constexpr size_t buffer_size = 4096;
        std::array<char, buffer_size> buffer;

        try
        {
            Socket socket = Socket::create_tcp();
            InetAddress server{config_.port, config_.host};

            // 连接到服务器
            if (auto res = co_await socket.connect(server); res < 0)
            {
                if (config_.verbose)
                {
                    std::cout << "Client " << client_id << " connect failed: " << res << std::endl;
                }
                co_return false;
            }

            // 创建要发送的消息
            std::string message =
                "Client[" + std::to_string(client_id) + "] Request[" + std::to_string(request_id) + "]";
            if (message.length() < config_.message_size)
            {
                message.resize(config_.message_size, 'X'); // 填充到指定大小
            }

            // 发送消息
            auto res = co_await socket.send(message.c_str(), message.length());
            if (res < 0)
            {
                if (config_.verbose)
                {
                    std::cout << "Client " << client_id << " write failed: " << res << std::endl;
                }
                co_return false;
            }

            total_bytes_.fetch_add(res);

            // 读取响应
            if (auto read_res = co_await socket.recv(buffer); read_res < 0)
            {
                if (config_.verbose)
                {
                    std::cout << "Client " << client_id << " read failed: " << read_res << std::endl;
                }
                co_return false;
            }
            else
            {
                total_bytes_.fetch_add(read_res);

                if (config_.verbose && request_id % 100 == 0)
                {
                    std::cout << "Client " << client_id << " request " << request_id << " received " << read_res
                              << " bytes" << std::endl;
                }
            }
        }
        catch (...)
        {
            co_return false;
        }

        co_return true;
    }
};

auto main_coro() -> MainCoroutine
{
    EchoClientTest::Config config;
    config.host = "127.0.0.1";
    config.port = 8888;
    config.client_count = 50;       // 并发客户端数量
    config.requests_per_client = 1; // 每个客户端的请求数量
    config.message_size = 1024;     // 消息大小
    config.verbose = false;         // 是否输出详细信息

    EchoClientTest test(config);
    int result = co_await test.run_test();

    co_return result;
}
} // namespace utils
