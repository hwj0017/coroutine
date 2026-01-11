// router_node.cpp
#include "routernode.h"
#include <algorithm>
#include <cassert>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace utils
{

auto RouterNode::split_path(std::string_view path) -> std::vector<std::string_view>
{
    std::vector<std::string_view> parts;
    if (path.empty())
        return parts;

    auto start = path.begin();
    auto end = path.end();
    if (path[0] == '/')
    {
        ++start;
    }

    while (start != end)
    {
        auto split = std::find(start, end, '/');
        if (split == end)
        {
            parts.emplace_back(start, split);
            break;
        }
        else
        {
            parts.emplace_back(start, split);
            start = split + 1;
        }
    }
    return parts;
}

bool RouterNode::insert(std::string_view path, HttpHandler& handler)
{
    // path以/开头，不以/结尾
    if (path.empty())
    {
        if (handler_)
        {
            return false;
        }
        handler_ = std::move(handler);
        return true;
    }
    //  忽略第一个/
    path.remove_prefix(1);
    auto split = std::find(path.begin(), path.end(), '/');
    auto left_path = std::string_view{split, path.end()};
    auto part_path = std::string_view{path.begin(), split};
    if (part_path.starts_with(':'))
    {
        if (param_child_)
        {
            if (param_child_->path_part_ == std::string_view{path.begin() + 1, split})
            {
                return param_child_->insert(left_path, handler);
            }
        }
        else
        {
            param_child_ = std::make_unique<RouterNode>();
            param_child_->type_ = NodeType::PARAM;
            param_child_->path_part_ = {path.begin() + 1, split};
            return param_child_->insert(left_path, handler);
        }
    }
    if (part_path.starts_with('*'))
    {
        if (catch_all_child_)
        {
            return false;
        }
        assert(split == path.end());
        catch_all_child_ = std::make_unique<RouterNode>();
        catch_all_child_->type_ = NodeType::CATCH_ALL;
        catch_all_child_->path_part_ = {path.begin() + 1, split};
        return true;
    }
    auto part_path_str = std::string{part_path};
    auto it = children_.find(part_path_str);
    if (it != children_.end())
    {
        return it->second->insert(left_path, handler);
    }
    auto node = std::make_unique<RouterNode>();
    node->path_part_ = std::move(part_path_str);
    node->type_ = NodeType::STATIC;
    assert(node->insert(left_path, handler));
    children_.emplace(std::move(part_path_str), std::move(node));
    return true;
}

auto RouterNode::find(std::string_view path, RouteParams& params) const -> HttpHandler
{
    // path不以 /开头和结尾
    if (path.empty())
    {
        return handler_;
    }
    path.remove_prefix(1);
    auto split = std::find(path.begin(), path.end(), '/');
    auto left_path = std::string_view{split, path.end()};
    auto part_path = std::string{path.begin(), split};
    auto it = children_.find(part_path);
    if (it != children_.end())
    {
        if (auto handler = it->second->find(left_path, params); handler)
        {
            return handler;
        }
    }
    if (param_child_)
    {
        if (auto handler = param_child_->find(left_path, params); handler)
        {
            params.emplace(param_child_->path_part_, std::move(part_path));
            return handler;
        }
    }
    if (catch_all_child_)
    {
        params.emplace(catch_all_child_->path_part_, std::string(path));
        return handler_;
    }

    // 未匹配
    return nullptr;
}

} // namespace utils