// http_parser.h
#pragma once
#include "http/enums.h"
#include "http/httprequest.h"
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace utils
{
// TODO:没有考虑分包的情况
class HttpParser
{
  public:
    enum class ParseResult
    {
        incomplete,
        success,
        error
    };

    auto parse(std::span<char> data, HttpRequest& req) -> ParseResult;
    bool is_keep_alive() const { return keep_alive_; }

  private:
    bool parse_line(std::span<char> line, HttpRequest& req);
    // 去除空格
    inline auto trim(std::string_view str) -> std::string_view;
    std::string buffer_;
    bool keep_alive_ = true;
};
inline bool HttpParser::parse_line(std::span<char> line, HttpRequest& req)
{
    auto start = line.begin();
    auto end = line.end();
    auto space = std::find(start, end, ' ');
    if (space == end)
    {
        return false;
    }
    auto method = string_to_method({start, space});
    if (method == Method::Invalid)
    {
        return false;
    }
    req.method = method;
    start = space + 1;
    space = std::find(start, end, ' ');
    if (space == end)
    {
        return false;
    }
    auto question = std::find(start, space, '?');
    if (question != space)
    {
        req.path = {start, question};
        req.query = {question, space};
    }
    else
    {
        req.path = {start, space};
    }
    start = space + 1;
    if (end - start != 8 || !std::equal(start, end - 1, "HTTP/1."))
    {
        return false;
    }

    if (*(end - 1) == '1')
    {
        req.version = Version::Http11;
    }
    else if (*(end - 1) == '0')
    {
        req.version = Version::Http10;
    }
    else
    {
        return false;
        ;
    }
    return true;
}
inline auto HttpParser::parse(std::span<char> data, HttpRequest& req) -> ParseResult
{
    static char CRLF[] = "\r\n";
    auto begin = data.begin();
    auto end = data.end();
    auto now = begin;
    enum State
    {
        ExpectRequestLine,
        ExpectHeaders,
        ExpectBody,
        GotAll
    };
    auto state = State::ExpectRequestLine;

    while (true)
    {
        switch (state)
        {
        case ExpectRequestLine: {
            auto crlf = std::search(begin, end, CRLF, CRLF + 2);
            if (crlf == end)
            {
                return ParseResult::error;
            }

            if (auto ok = parse_line({begin, crlf}, req); !ok)
            {
                return ParseResult::error;
            }
            state = ExpectHeaders;
            now = crlf + 2;
            break;
        }
        case ExpectHeaders: {
            auto crlf = std::search(now, end, CRLF, CRLF + 2);
            // 没有空行
            if (crlf == end)
            {
                return ParseResult::error;
            }
            // 空行
            if (crlf == now)
            {
                state = ExpectBody;
                now = crlf + 2;
                break;
            }
            auto colon = std::find(now, crlf, ':');
            // 不是键值对？
            if (colon == crlf)
            {
                return ParseResult::error;
            }
            auto key = trim({now, colon});
            if (key.empty())
            {
                return ParseResult::error;
            }
            auto value = trim({colon + 1, crlf});

            req.headers_.emplace(key, value);
            now = crlf + 2;
            break;
        }
        case ExpectBody: {
            req.body_ = {now, end};
            state = GotAll;
            break;
        }
        case GotAll: {
            return ParseResult::success;
        }
        }
    }
    return ParseResult::incomplete;
}
inline auto HttpParser::trim(std::string_view str) -> std::string_view
{
    auto begin = str.begin();
    auto end = str.end();
    while (begin < end && isspace(*begin))
    {
        ++begin;
    }
    while (end > begin && isspace(*(end - 1)))
    {
        --end;
    }
    return {begin, end};
}
} // namespace utils
