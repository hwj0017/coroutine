#pragma once
#include "http/enums.h"
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace utils
{
class HttpResponse
{
    using Headers = std::map<std::string_view, std::string_view>;

  public:
    explicit HttpResponse() = default;

    auto message() -> std::string;

  public:
    Headers headers;
    Version version{Version::Http11};
    StatusCode status_code{StatusCode::Unknow};
    std::string body;
    bool close_connection{false};
};

inline auto HttpResponse::message() -> std::string
{
    std::string message;
    message.clear();
    message += version_to_string(version);
    message += code_to_string(status_code);
    message += "\r\n";
    if (close_connection)
    {
        message += "Connection: close\r\n";
    }
    else
    {
        message += "Content-Length: ";
        message += std::to_string(body.size());
        message += "\r\n";
        message += "Connection: Keep-Alive\r\n";
    }

    for (const auto& header : headers)
    {
        message += header.first;
        message += ": ";
        message += header.second;
        message += "\r\n";
    }

    message += "\r\n";
    message += body;
    return message;
}
} // namespace utils
