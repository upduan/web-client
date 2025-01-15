#pragma once

#include <cstdlib>
#include <functional>
#include <iostream>
#include <queue>
#include <string>
#include <thread>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <boost/thread.hpp>

namespace util::WebSocket {
    void push_send_alarm_info(WarnInfo info) noexcept;
    // void push_send_normal_info(ErrorCode code, WarnInfo info = {"", "0", "0", ""}) noexcept;
    std::string error_to_string(ErrorCode code) noexcept;

    boost::json::array get_current_errors() noexcept;

    // 会话类用于管理 WebSocket 连接
    class session : public std::enable_shared_from_this<session> {
    public:
        // 构造函数
        explicit session(std::string const& host, std::string const& port, std::string const& target) noexcept
            : resolver_(boost::asio::make_strand(ioc_)), ws_(boost::asio::make_strand(ioc_)), timer_(ioc_), host_(host), port_(port), target_(target) {};

        // 启动异步操作连接
        void run() noexcept;
        // void start() noexcept;
        void stop() noexcept;

    private:
        void on_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results) noexcept;
        void on_connect(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type::endpoint_type) noexcept;
        void on_handshake(boost::beast::error_code ec) noexcept;
        void on_write(boost::beast::error_code ec, std::size_t bytes_transferred) noexcept;
        void on_read(boost::beast::error_code ec, std::size_t bytes_transferred) noexcept;
        void on_close(boost::beast::error_code ec) noexcept;
        // void reconnect() noexcept;
        void on_timer(boost::beast::error_code ec) noexcept;
        // void run_event_loop() noexcept; // Method to run the event loop
        void fail(boost::beast::error_code ec, char const* what) noexcept;

    private:
        boost::asio::io_context ioc_; // Reference to the io_context
        boost::asio::ip::tcp::resolver resolver_;
        boost::beast::websocket::stream<boost::beast::tcp_stream> ws_;
        boost::asio::steady_timer timer_; // 添加定时器
        boost::beast::flat_buffer buffer_;
        std::string host_;
        std::string port_;
        std::string target_;
        const std::chrono::seconds heartbeat_interval_{10}; // 心跳间隔时间
        // std::atomic<bool> exit_flag_{false}; // Flag to signal exit
        int retry_count = 0; // 网络断开后重连次数
        // std::shared_ptr<boost::thread> thread_; // Thread to run the event loop
    };
} // namespace util::WebSocket
