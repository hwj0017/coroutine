#include "http/httpserver.h"
#include "coroutine/coroutine.h"
#include "coroutine/syscall.h"
#include "filesystem"
#include "http/enums.h"
#include "http/httpcontext.h"
#include "httpparser.h"
#include "router.h"
#include "tcp/tcpserver.h"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
namespace utils
{
// === HttpServer 实现 ===

HttpServer::HttpServer(std::string_view host, uint16_t port) : router_(std::make_unique<Router>())
{
    // 创建 TcpServer，handler 为 HTTP 连接处理器
    auto tcp_handler = [this](Socket conn) -> Coroutine<> { return handle_http_connection(std::move(conn)); };
    tcp_server_ = std::make_unique<TcpServer>(InetAddress(port, host));
    tcp_server_->set_connection_handler(std::move(tcp_handler));
}

void HttpServer::GET(std::string path, HttpHandler handler)
{
    router_->add_route(Method::Get, std::move(path), std::move(handler));
}

void HttpServer::POST(std::string path, HttpHandler handler)
{
    router_->add_route(Method::Post, std::move(path), std::move(handler));
}

void HttpServer::PUT(std::string path, HttpHandler handler)
{
    router_->add_route(Method::Put, std::move(path), std::move(handler));
}

void HttpServer::DELETE(std::string path, HttpHandler handler)
{
    router_->add_route(Method::DeLete, std::move(path), std::move(handler));
}

void HttpServer::add_route(Method method, std::string path, HttpHandler handler)
{
    router_->add_route(method, std::move(path), std::move(handler));
}

auto resolve_static_file_path(std::string_view root_dir, std::string_view url_path, std::string_view url_prefix)
    -> std::filesystem::path;
auto get_content_type(const std::filesystem::path& path) -> std::string;
void HttpServer::serve_static_files(std::string url_prefix, std::string root_dir)
{
    // 确保 url_prefix 以 / 结尾
    if (!url_prefix.empty() && url_prefix.back() != '/')
    {
        url_prefix += '/';
    }
    // 确保 root_dir 是绝对路径
    root_dir = std::filesystem::canonical(root_dir).string();

    auto handler = [root_dir, url_prefix](HttpContext* ctx) -> Coroutine<> {
        const auto& req = ctx->request();
        auto url_path = req.path;

        // 1. 解析文件路径
        auto file_path = resolve_static_file_path(root_dir, url_path, url_prefix);
        if (file_path.empty())
        {
            ctx->response().status_code = StatusCode::Forbidden; // Forbidden (path traversal attempt)
            co_return;
        }

        // 2. 检查文件是否存在
        if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path))
        {
            ctx->response().status_code = StatusCode::NotFound;
            co_return;
        }

        // 3. 读取文件
        // TODO
        std::string content;
        content.resize(std::filesystem::file_size(file_path));
        auto n = co_await read(std::string_view(file_path.c_str()), content.data(), content.size());
        if (content.empty())
        {
            ctx->response().status_code = StatusCode::Unknow;
            co_return;
        }

        // 4. 设置响应
        ctx->response().body = std::move(content);
        ctx->response().headers.emplace("Content-Type", get_content_type(file_path));
        ctx->response().headers.emplace("Content-Length", std::to_string(ctx->response().body.size()));
        // 可选：添加缓存头
        ctx->response().headers.emplace("Cache-Control", "public, max-age=3600");

        co_return;
    };

    // 注册通配路由：/static/*filepath
    add_route(Method::Get, url_prefix + "*", handler);
}
void HttpServer::use(HttpHandler middleware) { router_->add_middleware(std::move(middleware)); }

auto HttpServer::start() -> Coroutine<> { return tcp_server_->start(); }

bool is_safe_path(std::string_view path)
{
    if (path.empty() || path[0] == '/')
        return false;
    if (path.find("..") != std::string::npos)
        return false;
    // 可选：检查 ~, :, 等
    return true;
}

auto resolve_static_file_path(std::string_view root_dir, std::string_view url_path, std::string_view url_prefix)
    -> std::filesystem::path
{
    // 移除 url_prefix（确保匹配）
    if (url_path.size() < url_prefix.size() || url_path.substr(0, url_prefix.size()) != url_prefix)
    {
        return {};
    }
    auto relative_path = url_path.substr(url_prefix.size());

    // 移除开头的 '/'
    if (!relative_path.empty() && relative_path[0] == '/')
    {
        relative_path = relative_path.substr(1);
    }

    // 安全检查
    if (!is_safe_path(relative_path))
    {
        return {};
    }

    // 拼接路径
    std::filesystem::path full_path = std::filesystem::path(root_dir) / relative_path;

    // 标准化路径（解析 . 和 ..）
    std::error_code ec;
    full_path = std::filesystem::canonical(full_path, ec);
    if (ec)
    {
        return {}; // 文件不存在或无法访问
    }

    // 确保在 root_dir 内（防越权）
    auto root_canon = std::filesystem::canonical(root_dir, ec);
    if (ec)
        return {};

    // 比较字符串前缀（注意 trailing slash）
    std::string full_str = full_path.string();
    std::string root_str = root_canon.string();
    if (root_str.back() != '/')
    {
        root_str += '/';
    }
    if (full_str.substr(0, root_str.size()) != root_str)
    {
        return {};
    }

    return full_path;
}

std::string get_content_type(const std::filesystem::path& path)
{
    static const std::unordered_map<std::string, std::string> types = {
        {".html", "text/html"}, {".css", "text/css"},   {".js", "application/javascript"},
        {".png", "image/png"},  {".jpg", "image/jpeg"}, {".json", "application/json"},
        {".txt", "text/plain"},
        // ... 其他类型
    };

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    auto it = types.find(ext);
    return (it != types.end()) ? it->second : "application/octet-stream";
}

auto HttpServer::handle_http_connection(Socket tcp_conn) -> Coroutine<>
{
    constexpr size_t buffer_size = 4096;
    HttpContext ctx;
    HttpParser parser;
    // 核心修改 1：引入连接级别的累积缓冲区
    std::vector<char> buffer;
    while (true)
    {
        // 1. 动态准备可写空间（直接在尾部预留 4096 字节）
        size_t old_size = buffer.size();
        buffer.resize(old_size + 4096);

        // 2. 直接读到 vector 的尾部空闲区域！完全没有 temp_buffer 的拷贝！
        auto n = co_await tcp_conn.read(buffer.data() + old_size, 4096);

        if (n <= 0)
        {
            break; // 客户端断开或出错
        }

        // 3. 截断多余的预留空间，让 vector 的 size 变成实际有效数据的总长度
        buffer.resize(old_size + n);

        // 4. 开始解析 (直接拿 buffer 开刀)
        while (!buffer.empty())
        {
            auto result = parser.parse({buffer.data(), buffer.size()}, ctx.request());

            if (result == HttpParser::ParseResult::error)
            {
                std::string resp = "HTTP/1.1 400 Bad Request\r\n\r\n";
                co_await tcp_conn.write(resp.data(), resp.size());
                co_return;
            }

            if (result == HttpParser::ParseResult::incomplete)
            {
                break; // 数据不够，直接跳出内层循环，外层 read 会自动在尾部追加数据
            }
            // 5. 成功解析出一个完整请求，处理它！
            // 路由匹配
            auto [handler, params] = router_->find_handler(ctx.request().method, ctx.request().path);
            if (!handler)
            {
                std::string resp = "HTTP/1.1 404 Not Found\r\n\r\n";
                co_await tcp_conn.write(resp);
            }
            else
            {
                auto middlewares = router_->get_middlewares();
                middlewares.push_back(handler);
                ctx.set_params(std::move(params));
                ctx.set_middlewares(std::move(middlewares));

                co_await ctx.run();
                co_await tcp_conn.write(ctx.response().message());
            }
            buffer.clear();

            if (!ctx.request().is_keep_alive())
            {
                break;
            }
        }
    }
}
HttpServer::~HttpServer() = default;
} // namespace utils