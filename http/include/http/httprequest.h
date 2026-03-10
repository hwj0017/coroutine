#pragma once

#include "http/enums.h"
#include <cassert>
#include <map>
#include <string_view>

namespace utils
{

// all are string_view
class HttpRequest
{
  public:
    HttpRequest() = default;

    void reset()
    {
        method = Method::Invalid;
        version = Version::Unknow;
        path = {};
        query = {};
        headers_.clear();
        body_ = {};
    }
    bool is_keep_alive() const
    {
        auto it = headers_.find("Connection");
        if (it != headers_.end())
        {
            auto conn_val = it->second;
            if (conn_val == "close")
            {
                return false;
            }
            if (conn_val == "keep-alive")
            {
                return true;
            }
        }

        // 如果没有显式指定 Connection 头，则由 HTTP 版本决定默认行为
        // HTTP/1.1 默认 Keep-Alive，HTTP/1.0 默认 Close
        return version == Version::Http11;
    }

  public:
    Method method{Method::Invalid};
    Version version{Version::Unknow};
    std::string_view path;
    std::string_view query;
    std::map<std::string_view, std::string_view> headers_;
    std::string_view body_;
};
} // namespace utils
