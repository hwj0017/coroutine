// router.h
#pragma once
#include "routernode.h"
#include <string_view>
#include <vector>

namespace utils
{

class Router
{
  public:
    void add_route(Method method, std::string_view path, HttpHandler handler)
    {
        auto& root = method_trees_[static_cast<size_t>(method)];
        if (!root)
        {
            root = std::make_unique<RouterNode>();
        }
        root->insert(path, handler);
    }

    // 返回 handler + 填充 params
    std::pair<HttpHandler, RouteParams> find_handler(Method method, std::string_view path)
    {
        auto it = method_trees_.find(static_cast<size_t>(method));
        if (it == method_trees_.end() || !it->second)
        {
            return {nullptr, {}};
        }
        RouteParams params;
        HttpHandler handler = it->second->find(path, params);
        return {handler, params};
    }

    auto& get_middlewares() { return middlewares_; }
    void add_middleware(HttpHandler mw) { middlewares_.push_back(std::move(mw)); }
    // TODO:去除尾部/
    auto init_path(std::string_view path) { return path; }

  private:
    // 每个 HTTP 方法一个前缀树
    std::unordered_map<size_t, std::unique_ptr<RouterNode>> method_trees_;
    std::vector<HttpHandler> middlewares_;
};

} // namespace utils