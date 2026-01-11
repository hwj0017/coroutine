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

  public:
    Method method{Method::Invalid};
    Version version{Version::Unknow};
    std::string_view path;
    std::string_view query;
    std::map<std::string_view, std::string_view> headers_;
    std::string_view body_;
};
} // namespace utils
