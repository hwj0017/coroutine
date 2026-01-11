// router_node.h
#pragma once
#include "http/httpserver.h"
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace utils
{

enum class NodeType
{
    STATIC,
    PARAM,
    CATCH_ALL // 通配符 *
};

using RouteParams = std::unordered_map<std::string_view, std::string_view>;
class RouterNode
{
  public:
    RouterNode() = default;

    // 插入路由
    bool insert(std::string_view path, HttpHandler& handler);

    // 查找路由 + 提取参数
    HttpHandler find(std::string_view path, RouteParams& params) const;

  private:
    HttpHandler handler_ = nullptr;
    NodeType type_ = NodeType::STATIC;
    std::string path_part_; // 如 "user", ":id", "*filepath"

    // 子节点
    std::unordered_map<std::string, std::unique_ptr<RouterNode>> children_;
    std::unique_ptr<RouterNode> param_child_{};
    std::unique_ptr<RouterNode> catch_all_child_{};

    // 辅助函数
    static auto split_path(std::string_view path) -> std::vector<std::string_view>;
    bool is_param() const { return type_ == NodeType::PARAM; }
    bool is_catch_all() const { return type_ == NodeType::CATCH_ALL; }
};

} // namespace utils