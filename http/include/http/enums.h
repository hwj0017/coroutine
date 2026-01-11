#pragma once
#include <string>
#include <string_view>
namespace utils
{
enum class Method
{
    Invalid,
    Get,
    Post,
    Head,
    Put,
    DeLete,
};
enum class Version
{
    Unknow,
    Http10,
    Http11,
};

constexpr auto string_to_method(std::string_view method) -> Method
{
    if (method == "GET")
    {
        return Method::Get;
    }
    if (method == "POST")
    {
        return Method::Post;
    }
    if (method == "HEAD")
    {
        return Method::Head;
    }
    if (method == "PUT")
    {
        return Method::Put;
    }
    if (method == "DELETE")
    {
        return Method::DeLete;
    }
    return Method::Invalid;
}
constexpr auto method_to_string(Method method)
{
    const char* result = "UNKNOW";
    switch (method)
    {
    case Method::Get:
        result = "GET";
        break;
    case Method::Post:
        result = "POST";
        break;
    case Method::Head:
        result = "HEAD";
        break;
    case Method::Put:
        result = "PUT";
        break;
    case Method::DeLete:
        result = "DELETE";
        break;
    default:
        break;
    }
    return result;
}
// end with space
constexpr static auto version_to_string(Version version)
{
    switch (version)
    {
    case Version::Http10:
        return "HTTP/1.0 ";
    case Version::Http11:
        return "HTTP/1.1 ";
    case Version::Unknow:
        return "HTTP/1.1 ";
    }
    return "";
}
enum class StatusCode
{
    Unknow = 0,
    OK = 200,
    MovedPermanently = 301,
    BadRequest = 400,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
};

constexpr auto code_to_string(StatusCode code)
{
    switch (code)
    {
    case StatusCode::OK:
        return "200 OK";
    case StatusCode::MovedPermanently:
        return "301 Moved Permanently";
    case StatusCode::BadRequest:
        return "400 Bad Request";
    case StatusCode::Forbidden:
        return "403 Forbidden";
    case StatusCode::NotFound:
        return "404 Not Found";
    case StatusCode::MethodNotAllowed:
        return "405 Method Not Allowed";
    case StatusCode::Unknow:
        return "500 Internal Server Error";
    }
    return "";
}

} // namespace utils
