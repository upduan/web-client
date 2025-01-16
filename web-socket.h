#pragma once

#include <cstdlib>
#include <functional>
#include <iostream>
#include <queue>
#include <span>
#include <string>
#include <thread>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>
#include <boost/thread.hpp>
#include <boost/url/parse.hpp>

#include "Log.h"
#include "root_certificates.hpp"

namespace util::WebSocket {
    class Client;
    class Manager {
    public:
        enum class Error {
            URL_FORMAT_ERROR,
            SCHEME_MISTAKEN,
            RESOVLE_FAILED,
            CONNECTION_FAILED,
            SSL_NOT_DOMAIN,
            SSL_HANDSHAKE_FAILED,
            HANDSHAKE_FAILED,
            RECEIVE_MESSAGE_ERROR,
            SEND_MESSAGE_ERROR,
            HEARTBEAT_ERROR,
        };
        static std::string to_string(Error error) noexcept;

        virtual ~Manager() {}
        virtual void on_connect(std::shared_ptr<Client> self) = 0;
        virtual void on_disconnect(std::shared_ptr<Client> self) = 0;
        virtual void on_message(std::shared_ptr<Client> self, std::string_view msg) = 0;
        virtual void on_message(std::shared_ptr<Client> self, std::span<std::byte> msg) = 0;
        virtual void on_error(std::shared_ptr<Client> self, Error ec) = 0;
        virtual std::string get_ping(std::shared_ptr<Client> self) {
            return "";
        }
    };

    class Client : public std::enable_shared_from_this<Client> {
    public:
        Client(std::shared_ptr<Manager> manager, std::string const& url) : manager_(manager), resolver_(ioc_), reconnect_timer_(ioc_), heartbeat_timer_(ioc_), url_(url) {
            if (boost::urls::result<boost::urls::url_view> r = boost::urls::parse_uri(url); r.has_value()) {
                auto u = r.value();
                scheme_ = u.scheme();
                host_ = u.encoded_host();
                port_ = u.port();
                path_ = u.encoded_path();
                if (scheme_ == "ws") {
                    ws_ = std::make_shared<boost::beast::websocket::stream<boost::beast::tcp_stream>>(boost::asio::make_strand(ioc_));
                } else if (scheme_ == "wss") {
                    // The SSL context is required, and holds certificates
                    boost::asio::ssl::context ctx{boost::asio::ssl::context::tlsv12_client};
                    // This holds the root certificate used for verification
                    boost::beast::error_code ec;
                    load_root_certificates(ctx, ec);
                    // ctx.set_verify_mode(boost::asio::ssl::verify_peer);
                    ctx.set_verify_mode(boost::asio::ssl::verify_none);
                    wss_ = std::make_shared<boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(boost::asio::make_strand(ioc_), ctx);
                } else {
                    log_error << "WebSocket Client constructor error, use unknown scheme: " << scheme_;
                    if (auto m = manager_.lock(); m) {
                        m->on_error(shared_from_this(), Manager::Error::SCHEME_MISTAKEN);
                    }
                }
            } else {
                log_error << "WebSocket Client constructor error, use misformat url: " << url;
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::URL_FORMAT_ERROR);
                }
            }
        }

        void start() {
            do_resolve();
            start_heartbeat();
            ioc_.run();
        }

        void write(std::string_view content) noexcept {
            if (ws_) {
                ws_->text(true);
                boost::beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(2));
                ws_->async_write(boost::asio::buffer(content), boost::beast::bind_front_handler(&Client::on_write, shared_from_this()));
            } else if (wss_) {
                wss_->text(true);
                boost::beast::get_lowest_layer(*wss_).expires_after(std::chrono::seconds(2));
                wss_->async_write(boost::asio::buffer(content), boost::beast::bind_front_handler(&Client::on_write, shared_from_this()));
            }
        }

        void write(std::span<std::byte> content) noexcept {
            if (ws_) {
                ws_->binary(true);
                boost::beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(2));
                // FIXME ws_->async_write(boost::asio::buffer(content), boost::beast::bind_front_handler(&Client::on_write, shared_from_this()));
            } else if (wss_) {
                wss_->binary(true);
                boost::beast::get_lowest_layer(*wss_).expires_after(std::chrono::seconds(2));
                // FIXME wss_->async_write(boost::asio::buffer(content), boost::beast::bind_front_handler(&Client::on_write, shared_from_this()));
            }
        }

        void stop() noexcept {
            is_closing_ = true;
            reconnect_timer_.cancel();
            heartbeat_timer_.cancel();
            do_close();
            ioc_.stop();
        }

    private:
        void do_resolve() {
            resolver_.async_resolve(host_, port_, boost::beast::bind_front_handler(&Client::on_resolve, shared_from_this()));
        }

        void on_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results) {
            if (!ec) {
                do_connect(results);
            } else {
                log_error << "Resolve failed: " << ec.message();
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::RESOVLE_FAILED);
                }
                handle_error(ec);
            }
        }

        void do_connect(boost::asio::ip::tcp::resolver::results_type results) {
            if (ws_) {
                // Set the timeout for the operation
                boost::beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(30));
                // Make the connection on the IP address we get from a lookup
                boost::beast::get_lowest_layer(*ws_).async_connect(results, boost::beast::bind_front_handler(&Client::on_connect, shared_from_this()));
            } else if (wss_) {
                // Set the timeout for the operation
                boost::beast::get_lowest_layer(*wss_).expires_after(std::chrono::seconds(30));
                // Make the connection on the IP address we get from a lookup
                boost::beast::get_lowest_layer(*wss_).async_connect(results, boost::beast::bind_front_handler(&Client::on_connect, shared_from_this()));
            }
        }

        void on_connect(boost::beast::error_code ec, boost::asio::ip::tcp::endpoint) {
            if (!ec) {
                if (ws_) {
                    do_handshake();
                } else if (wss_) {
                    do_ssl_handshake();
                }
            } else {
                std::cerr << "Connect failed: " << ec.message() << "\n";
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::CONNECTION_FAILED);
                }
                handle_error(ec);
            }
        }

        void do_ssl_handshake() {
            // Set a timeout on the operation
            boost::beast::get_lowest_layer(*wss_).expires_after(std::chrono::seconds(30));
            // Set SNI Hostname (many hosts need this to handshake successfully)
            if (SSL_set_tlsext_host_name(wss_->next_layer().native_handle(), host_.c_str())) {
                // Perform the SSL handshake
                wss_->next_layer().async_handshake(boost::asio::ssl::stream_base::client, boost::beast::bind_front_handler(&Client::on_ssl_handshake, shared_from_this()));
            } else {
                auto ec = boost::beast::error_code(static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category());
                // return fireEventAndLog(WebSocket::Event::SSL_ERROR, ec, "connect");
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::SSL_NOT_DOMAIN);
                }
            }
        }

        void on_ssl_handshake(boost::beast::error_code ec) noexcept {
            if (ec) {
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::SSL_HANDSHAKE_FAILED);
                }
                // fireEventAndLog(WebSocket::Event::SSL_ERROR, ec, "ssl_handshake");
            } else {
                do_handshake();
            }
        }

        void do_handshake() {
            auto host = host_;
            if (!port_.empty()) {
                host += ":" + port_;
            }
            if (ws_) {
                // Turn off the timeout on the tcp_stream, because
                // the websocket stream has its own timeout system.
                boost::beast::get_lowest_layer(*ws_).expires_never();
                // Set suggested timeout settings for the websocket
                ws_->set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
                // Set a decorator to change the User-Agent of the handshake
                ws_->set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::request_type& req) {
                    req.set(boost::beast::http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-async");
                    req.set(boost::beast::http::field::sec_websocket_protocol, "janus-protocol");
                }));
                // Perform the websocket handshake
                ws_->async_handshake(host, path_, boost::beast::bind_front_handler(&Client::on_handshake, shared_from_this()));
            } else if (wss_) {
                // Turn off the timeout on the tcp_stream, because
                // the websocket stream has its own timeout system.
                boost::beast::get_lowest_layer(*wss_).expires_never();
                // Set suggested timeout settings for the websocket
                wss_->set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
                // Set a decorator to change the User-Agent of the handshake
                wss_->set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::request_type& req) {
                    req.set(boost::beast::http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-async-ssl");
                    req.set(boost::beast::http::field::sec_websocket_protocol, "janus-protocol");
                }));
                // Perform the websocket handshake
                wss_->async_handshake(host_, path_, boost::beast::bind_front_handler(&Client::on_handshake, shared_from_this()));
            }
        }

        void on_handshake(boost::beast::error_code ec) {
            if (!ec) {
                if (auto m = manager_.lock(); m) {
                    m->on_connect(shared_from_this());
                }
                if (ws_) {
                    ws_->control_callback(boost::beast::bind_front_handler(&Client::on_control, shared_from_this()));
                } else if (wss_) {
                    wss_->control_callback(boost::beast::bind_front_handler(&Client::on_control, shared_from_this()));
                }
                do_read();
            } else {
                log_error << "Handshake failed: " << ec.message();
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::HANDSHAKE_FAILED);
                }
                handle_error(ec);
            }
        }

        void on_control(boost::beast::websocket::frame_type type, boost::string_view payload) noexcept {
            switch (type) {
            case boost::beast::websocket::frame_type::close:
                // Log::log("%s %p receive close frame", label_.c_str(), this);
                break;
            case boost::beast::websocket::frame_type::ping:
                // Log::log("%s %p receive ping frame", label_.c_str(), this);
                break;
            case boost::beast::websocket::frame_type::pong:
                // Log::log("%s %p receive pong frame", label_.c_str(), this);
                break;
            }
        }

        void do_read() {
            if (ws_) {
                // Set the timeout for the operation
                boost::beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(28));
                ws_->async_read(buffer_, boost::beast::bind_front_handler(&Client::on_read, shared_from_this()));
            } else if (wss_) {
                // Set the timeout for the operation
                boost::beast::get_lowest_layer(*wss_).expires_after(std::chrono::seconds(28));
                wss_->async_read(buffer_, boost::beast::bind_front_handler(&Client::on_read, shared_from_this()));
            }
        }

        void on_read(boost::beast::error_code ec, std::size_t bytes_transferred) {
            if (!ec) {
                auto data = buffer_.data();
                log_info << "Received: " << boost::beast::make_printable(data);
                if (auto m = manager_.lock(); m) {
                    int type = 1; // TEXT
                    if (ws_) {
                        type = ws_->got_binary() ? 2 : 1;
                    } else if (wss_) {
                        type = wss_->got_binary() ? 2 : 1;
                    }
                    switch (type) {
                    case 1:
                        m->on_message(shared_from_this(), std::string_view(reinterpret_cast<char*>(data.data()), data.size()));
                        break;
                    case 2:
                        // FIXME m->on_message(shared_from_this(), std::span<std::byte>(data.data(), data.size()));
                        break;
                    }
                }
                buffer_.consume(bytes_transferred);
                // buffer_.clear();
                if (!is_closing_) {
                    do_read();
                    // do_write("Hello, WebSocket!");
                }
            } else if (ec == boost::beast::websocket::error::closed) {
                log_error << "Connection closed by server";
                if (auto m = manager_.lock(); m) {
                    m->on_disconnect(shared_from_this());
                }
                handle_error(ec);
            } else {
                log_error << "Read failed: " << ec.message();
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::RECEIVE_MESSAGE_ERROR);
                }
                handle_error(ec);
            }
        }

        void on_write(boost::beast::error_code ec, std::size_t bytes_transferred) {
            if (!ec) {
                // Optionally handle the sent message here
            } else {
                log_error << "Write failed: " << ec.message();
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::SEND_MESSAGE_ERROR);
                }
                handle_error(ec);
            }
        }

        void on_close(boost::beast::error_code ec) noexcept {
            if (ec) {
                if (auto m = manager_.lock(); m) {
                    m->on_error(shared_from_this(), Manager::Error::SEND_MESSAGE_ERROR);
                }
                // fireEventAndLog(WebSocket::Event::DISCONNECT_ERROR, ec, "close");
            } else {
                // If we get here then the connection is closed gracefully
                // fireEvent(WebSocket::Event::DISCONNECT_OK);
                // The make_printable() function helps print a ConstBufferSequence
                log_info << boost::beast::make_printable(buffer_.data());
            }
        }

        void handle_error(boost::beast::error_code ec) {
            do_close();
            // Schedule reconnection after a delay
            reconnect_timer_.expires_after(std::chrono::seconds(5));
            reconnect_timer_.async_wait([self = shared_from_this()](const boost::beast::error_code& error) {
                if (!error) {
                    log_info << "Reconnecting...";
                    self->do_resolve();
                    self->start_heartbeat();
                } else {
                    log_error << "Reconnect timer error: " << error.message();
                }
            });
        }

        void do_close() {
            if (ws_) {
                boost::beast::get_lowest_layer(*ws_).expires_after(std::chrono::seconds(2));
                ws_->async_close(boost::beast::websocket::close_code::normal, boost::beast::bind_front_handler(&Client::on_close, shared_from_this()));
            } else if (wss_) {
                boost::beast::get_lowest_layer(*wss_).expires_after(std::chrono::seconds(2));
                wss_->async_close(boost::beast::websocket::close_code::normal, boost::beast::bind_front_handler(&Client::on_close, shared_from_this()));
            }
        }

        void start_heartbeat() {
            heartbeat_timer_.expires_after(std::chrono::seconds(5));
            heartbeat_timer_.async_wait([self = shared_from_this()](const boost::beast::error_code& error) {
                if (!error) {
                    self->do_ping();
                    self->start_heartbeat(); // Restart the heartbeat timer
                } else {
                    log_error << "Heartbeat timer error: " << error.message();
                    if (auto m = self->manager_.lock(); m) {
                        m->on_error(self->shared_from_this(), Manager::Error::HEARTBEAT_ERROR);
                    }
                    self->handle_error(error);
                }
            });
        }

        void do_ping() {
            if (auto m = manager_.lock(); m) {
                write(m->get_ping(shared_from_this()));
            }
            // ws_.async_ping(boost::beast::websocket::ping_data(), [this](boost::beast::error_code ec) {
            //     if (!ec) {
            //         // Ping successful, reset heartbeat timer
            //         heartbeat_timer_.expires_after(std::chrono::seconds(5));
            //     } else {
            //         std::cerr << "Ping failed: " << ec.message() << "\n";
            //         handle_error(ec);
            //     }
            // });
        }

        std::weak_ptr<Manager> manager_;
        boost::asio::io_context ioc_;
        boost::asio::ip::tcp::resolver resolver_;
        boost::asio::steady_timer reconnect_timer_;
        boost::asio::steady_timer heartbeat_timer_;
        // boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws_;
        std::shared_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>> ws_;
        std::shared_ptr<boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>> wss_;
        boost::beast::flat_buffer buffer_;
        std::string url_;
        std::string scheme_;
        std::string host_;
        std::string port_;
        std::string path_ = "/";
        std::atomic<bool> is_closing_ = false;
    };
} // namespace util::WebSocket
