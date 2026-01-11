#pragma once
#include "coroutine/coroutine.h"
#include "http/enums.h"
#include "http/httphandler.h"
#include <coroutine>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
namespace utils
{
class HttpContext;
class Router;
class TcpServer;
class TcpConnection;
// TODO: just get file
class HttpServer
{
  public:
    // 构造：监听地址、端口
    HttpServer(std::string_view host, uint16_t port);

    // 注册路由（支持通配、参数）
    void GET(std::string path, HttpHandler handler);
    void POST(std::string path, HttpHandler handler);
    void PUT(std::string path, HttpHandler handler);
    void DELETE(std::string path, HttpHandler handler);
    // 或通用方法：
    void add_route(Method method, std::string path, HttpHandler handler);

    // 静态文件服务
    void serve_static_files(std::string url_prefix, std::string root_dir);

    // 中间件（可选）
    void use(HttpHandler middleware);

    // 启动（非阻塞）
    void start();

    // 停止 & 等待
    void stop();
    auto join() -> std::suspend_always;

    ~HttpServer();

  private:
    auto handle_http_connection(std::shared_ptr<TcpConnection>) -> Coroutine<>;
    std::unique_ptr<TcpServer> tcp_server_;
    std::unique_ptr<Router> router_; // 路由表（基于前缀树）
};

} // namespace utils