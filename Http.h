#pragma once

#include <functional>
#include <map>
#include <string>

namespace util::Http {
    void transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string_view body,
        std::function<void(std::string&& r)>&& post_processor) noexcept;

    void transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string const& body,
        std::function<void(std::string&& r)>&& post_processor) noexcept;

    /*void transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string_view body, std::string const& send_file_name,
        std::function<void(int r)>&& post_processor) noexcept;*/

    void async_transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string const& body,
        std::function<void(std::string&& r)>&& post_processor) noexcept;

    

    void stop_all_therads();
    /*
    void async_transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string const& body, std::string const&
        send_file_name, std::function<void(int r)>&& post_processor) noexcept;
    */
} // namespace util::Http
