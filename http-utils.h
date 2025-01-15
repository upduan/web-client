#pragma once

#include <map>
#include <string>

#include <boost/asio/ssl/error.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/http/fields.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/url/parse.hpp>

#include "Log.h"

inline void fail(boost::beast::error_code ec, char const* what) {
    if (ec == boost::asio::ssl::error::stream_truncated) {
        return;
    }
    log_error << what << ": " << ec.message() << "\n";
}

struct RequestInfo {
    boost::beast::http::verb method_verb;
    std::string method;
    std::string path;
    unsigned int version;
    std::map<std::string, std::string> headers;
    std::string body;
    bool keep_alive;
};

template <class Body, class Allocator> RequestInfo parse_request(boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator>>&& req) {
    RequestInfo result{};
    result.method_verb = req.method();
    result.method = boost::beast::http::to_string(req.method());
    boost::beast::string_view path;
    if (boost::urls::result<boost::urls::url_view> r = boost::urls::parse_origin_form(req.target()); r.has_value()) {
        auto u = r.value();
        result.path = u.encoded_path();
    }
    result.version = req.version();
    for (const auto& field : req) {
        result.headers[field.name_string()] = field.value();
    }
    auto body = req.body();
    if (body.size() >= 2 && body.front() == '\"' && body.back() == '\"') {
        result.body = body.substr(1, body.size() - 2);
    }
    result.keep_alive = req.keep_alive();
    return result;
}

struct ResponseInfo {
    int code;
    std::string message;
    std::map<std::string, std::string> headers;
    std::string data;
};

std::string construct_multipart_formdata(std::string const& previous, std::string const& boundary, std::string const& name, std::string const& type, std::string const& file_name,
    std::string const& body, bool is_last = true) noexcept;
std::string construct_multipart_formdata(std::string const& previous, std::string const& boundary, std::string const& name, std::string const& value,
    bool is_last = false) noexcept;
