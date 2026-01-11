#pragma once
#include "coroutine/coroutine.h"
#include "http/httprequest.h"
#include "http/httpresponse.h"
#include "http/httpserver.h"
#include <any>
#include <unordered_map>
#include <vector>
namespace utils
{

class HttpContext
{
  public:
    // 1. 请求/响应
    auto& request() { return request_; }
    auto& response() { return response_; }

    // 2. 路由参数（如 /user/:id → {{ "id", "123" }}）
    const std::map<std::string, std::string>& params() const;

    // 3. 临时存储（中间件传数据）
    template <typename T> void set(std::string_view key, T&& value);

    template <typename T> T& get(const std::string& key); // 或返回 optional<T>

    // 4. 全局上下文（数据库、日志等）
    // const AppContext& app() const { return *app_ctx_; }

    // 5. 错误收集
    void add_error(std::string msg);
    auto& errors() const;
    void set_middlewares(std::vector<HttpHandler>&& middlewares) { middlewares_ = std::move(middlewares); }
    // TODO:洋葱模型
    auto run() -> Coroutine<>
    {
        for (auto& mw : middlewares_)
        {
            co_await mw(this);
        }
    }
    void set_params(std::unordered_map<std::string_view, std::string_view>&& params)
    {
        path_params_ = std::move(params);
    }

  private:
    HttpRequest request_;
    HttpResponse response_;
    std::unordered_map<std::string_view, std::string_view> path_params_;
    std::unordered_map<std::string, std::any> keys_; // 或更安全的 variant
    std::vector<std::string> errors_;
    std::vector<HttpHandler> middlewares_;
    size_t next_middleware_ = 0;
};

} // namespace utils