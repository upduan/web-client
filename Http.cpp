#include "Http.h"

#include <thread>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/url.hpp>

#include "root_certificates.hpp"

namespace util::Http {
    namespace {
        namespace beast = boost::beast; // from <boost/beast.hpp>
        namespace http = beast::http; // from <boost/beast/http.hpp>
        // namespace net = boost::asio; // from <boost/asio.hpp>
        using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

        std::tuple<bool, std::string, std::string, std::string, std::string> parse_url(std::string const& url) noexcept {
            std::tuple<bool, std::string, std::string, std::string, std::string> result{false, "", "", "", ""};
            boost::urls::result<boost::urls::url_view> r = boost::urls::parse_uri(url);
            if (r.has_value()) {
                auto u = r.value();
                auto s = u.scheme();
                auto host = u.encoded_host();
                auto port = u.port();
                if (port == "") {
                    port = s == "https" ? "https" : "http";
                }
                auto target = u.encoded_target();
                result = {true, std::string(s), std::string(host), std::string(port), std::string(target)};
            }
            return result;
        }

        boost::beast::http::request<boost::beast::http::string_body> construct_request(std::string const& method, std::string const& host, std::string const& target,
            std::map<std::string, std::string> const& headers, std::string const& body) noexcept {
            // Set up an HTTP GET request message
            constexpr int version = 11;
            boost::beast::http::verb httpMethod = boost::beast::http::verb::unknown;
            if (method == "GET") {
                httpMethod = boost::beast::http::verb::get;
            } else if (method == "POST") {
                httpMethod = boost::beast::http::verb::post;
            } else if (method == "PUT") {
                httpMethod = boost::beast::http::verb::put;
            } else if (method == "DELETE") {
                httpMethod = boost::beast::http::verb::delete_;
            } else if (method == "HEAD") {
                httpMethod = boost::beast::http::verb::head;
            } else if (method == "PATCH") {
                httpMethod = boost::beast::http::verb::patch;
            } else if (method == "PURGE") {
                httpMethod = boost::beast::http::verb::purge;
            } else if (method == "LINK") {
                httpMethod = boost::beast::http::verb::link;
            } else if (method == "UNLINK") {
                httpMethod = boost::beast::http::verb::unlink;
            } else if (method == "CONNECT") {
                httpMethod = boost::beast::http::verb::connect;
            } else if (method == "OPTIONS") {
                httpMethod = boost::beast::http::verb::options;
            } else if (method == "TRACE") {
                httpMethod = boost::beast::http::verb::trace;
            }
            boost::beast::http::request<boost::beast::http::string_body> req{httpMethod, target, version};
            for (auto const& [key, value] : headers) {
                req.insert(key, value);
            }
            req.set(boost::beast::http::field::host, host);
            req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            if (!body.empty()) {
                req.set(boost::beast::http::field::content_length, std::to_string(body.length()));
                req.body() = body;
                req.prepare_payload();
            }
            return req;
        }

        boost::beast::http::request<boost::beast::http::string_body> construct_request(std::string const& method, std::string const& host, std::string const& target,
            std::map<std::string, std::string> const& headers, std::string_view body) noexcept {
            // Set up an HTTP GET request message
            constexpr int version = 11;
            boost::beast::http::verb httpMethod = boost::beast::http::verb::unknown;
            if (method == "GET") {
                httpMethod = boost::beast::http::verb::get;
            } else if (method == "POST") {
                httpMethod = boost::beast::http::verb::post;
            } else if (method == "PUT") {
                httpMethod = boost::beast::http::verb::put;
            } else if (method == "DELETE") {
                httpMethod = boost::beast::http::verb::delete_;
            } else if (method == "HEAD") {
                httpMethod = boost::beast::http::verb::head;
            } else if (method == "PATCH") {
                httpMethod = boost::beast::http::verb::patch;
            } else if (method == "PURGE") {
                httpMethod = boost::beast::http::verb::purge;
            } else if (method == "LINK") {
                httpMethod = boost::beast::http::verb::link;
            } else if (method == "UNLINK") {
                httpMethod = boost::beast::http::verb::unlink;
            } else if (method == "CONNECT") {
                httpMethod = boost::beast::http::verb::connect;
            } else if (method == "OPTIONS") {
                httpMethod = boost::beast::http::verb::options;
            } else if (method == "TRACE") {
                httpMethod = boost::beast::http::verb::trace;
            }
            boost::beast::http::request<boost::beast::http::string_body> req{httpMethod, target, version};
            for (auto const& [key, value] : headers) {
                req.insert(key, value);
            }
            req.set(boost::beast::http::field::host, host);
            req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            if (!body.empty()) {
                req.set(boost::beast::http::field::content_length, std::to_string(body.length()));
                req.body() = std::move(std::string(body));
                req.prepare_payload();
            }
            return req;
        }

        template <typename Body, typename Fields> std::pair<int, std::string> get_redirect_url(boost::beast::http::response<Body, Fields> const& response) noexcept {
            auto headers = response.base();
            auto code = headers.result_int();
            std::string url;
            if (code >= 300 && code < 400) {
                if (headers.find("Location") != headers.end()) {
                    url = headers.at("Location");
                }
            }
            return {code, url};
        }

        template <typename Stream>
        std::pair<int, std::string> transeiver(Stream& stream, boost::beast::http::request<boost::beast::http::string_body> const& req, boost::beast::flat_buffer& buffer,
            boost::beast::http::response<boost::beast::http::string_body>& res) noexcept {
            // Send the HTTP request to the remote host
            boost::beast::http::write(stream, req);
            // Receive the HTTP response
            boost::beast::http::read(stream, buffer, res);
            return get_redirect_url(res);
        }

        template <typename Stream>
        std::pair<int, std::string> transeiver(Stream& stream, boost::beast::http::request<boost::beast::http::string_body> const& req, boost::beast::flat_buffer& buffer,
            boost::beast::http::response_parser<boost::beast::http::file_body>& parser) noexcept {
            // Send the HTTP request to the remote host
            boost::beast::http::write(stream, req);
            // Receive the HTTP response
            // boost::beast::http::read(stream, buffer, parser.get());
            while (!parser.is_done()) {
                boost::beast::http::read_some(stream, buffer, parser);
            }
            std::pair<int, std::string> r{200, ""};
            try {
                auto& response = parser.get();
                r = get_redirect_url(response);
                response.body().close();
                // const_cast<boost::beast::http::basic_file_body<boost::beast::file_posix>::value_type>(response.body()).close();
            } catch (...) {}
            return r;
        }

        void shutdown_ssl(std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> stream) {
            if (!stream) {
                log_error << "SSL stream is null, cannot shut down.";
                return;
            }

            boost::beast::error_code ec;
            auto& socket = stream->next_layer().socket(); // 缓存底层套接字

            // 尝试优雅地关闭 SSL 流
            stream->shutdown(ec);

            // 处理 SSL 关闭时的特定错误
            if (ec && ec != boost::asio::ssl::error::stream_truncated) {
                log_error << "SSL shutdown error: " << ec.message();
                return;
            }

            // 如果有 stream_truncated 错误，意味着对方没有发送关闭通知，但通常这种情况可以忽略
            if (ec == boost::asio::ssl::error::stream_truncated) {
                log_trace << "SSL stream truncated, ignoring error.";
            }

            // 确保底层套接字关闭
            if (socket.is_open()) {
                socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
                if (ec && ec != boost::asio::error::not_connected) {
                    log_error << "Socket shutdown error: " << ec.message();
                    return;
                }

                socket.close(ec);
                if (ec) {
                    log_error << "Socket close error: " << ec.message();
                } else {
                    log_trace << "SSL stream and socket closed successfully.";
                }
            } else {
                log_trace << "Socket already closed or not connected.";
            }
        }

        void shutdown_plain(std::shared_ptr<boost::beast::tcp_stream> stream) {
            if (!stream) {
                log_error << "Stream is null, cannot shut down.";
                return;
            }

            boost::beast::error_code ec;
            auto& socket = stream->socket(); // 缓存 socket 对象

            // 优雅地关闭套接字
            socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

            // 对于 not_connected 错误，我们忽略并返回，不记录“关闭成功”的错误日志
            if (ec && ec != boost::beast::errc::not_connected) {
                log_error << "Error during shutdown: " << ec.message();
                return;
            }

            // 如果套接字仍然打开，尝试关闭它
            if (socket.is_open()) {
                socket.close(ec);
                if (ec) {
                    log_error << "Error during close: " << ec.message();
                } else {
                   // log_trace << "Socket successfully closed.";
                }
            } else {
                log_trace << "Socket already closed or not connected.";
            }
        }

        void process(std::pair<int, std::string> r, std::string const& method, std::map<std::string, std::string> const& headers, std::string_view body,
            std::string&& return_body, std::function<void(std::string&& reponse)>&& post_processor) noexcept {
            if (r.second.empty()) {
                if (post_processor) {
                    if (r.first >= 400 && r.first < 600) {
                        post_processor("");
                    } else {
                        post_processor(std::move(return_body));
                    }
                }
            } else {
                log_info << "process transfer again";
                transfer(method, r.second, headers, body, std::move(post_processor));
            }
        }

        /*void process(std::pair<int, std::string> r, std::string const& method, std::map<std::string, std::string> const& headers, std::string_view body,
            std::string const& file_name, std::function<void(int r)>&& post_processor) noexcept {
            if (r.second.empty()) {
                if (post_processor) {
                    post_processor(r.first);
                }
            } else {
                transfer(method, r.second, headers, body, file_name, std::move(post_processor));
            }
        }*/

        std::vector<std::shared_ptr<std::thread>> threads_;

        // Report a failure
        /*void fail(beast::error_code ec, char const* what) {
            log_error << what << ": " << ec.message() << "\n";
        }*/

        // Performs an HTTP GET and prints the response
        class session : public std::enable_shared_from_this<session> {
            tcp::resolver resolver_;
            beast::tcp_stream stream_;
            beast::flat_buffer buffer_; // (Must persist between reads)
            http::request<http::string_body> req_;
            http::response<http::string_body> res_;
            std::function<void(std::string&& r)> post_processor_;

        public:
            // Objects are constructed with a strand to
            // ensure that handlers do not execute concurrently.
            session(boost::asio::io_context& ioc, std::function<void(std::string&& r)>&& post_processor)
                : resolver_(boost::asio::make_strand(ioc)), stream_(boost::asio::make_strand(ioc)), post_processor_(std::move(post_processor)) {}

            // Start the asynchronous operation
            void run(std::string const& method, std::string const& host, std::string const& port, std::string const& target, std::map<std::string, std::string> const& headers,
                std::string const& body) {
                // Set up an HTTP GET request message
                req_ = construct_request(method, host, target, headers, body);

                // Look up the domain name
                resolver_.async_resolve(host, port, beast::bind_front_handler(&session::on_resolve, shared_from_this()));
            }

            void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
                if (ec)
                    return fail(ec, "resolve");

                // Set a timeout on the operation
                stream_.expires_after(std::chrono::seconds(30));

                // Make the connection on the IP address we get from a lookup
                stream_.async_connect(results, beast::bind_front_handler(&session::on_connect, shared_from_this()));
            }

            void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                if (ec)
                    return fail(ec, "connect");

                // Set a timeout on the operation
                stream_.expires_after(std::chrono::seconds(30));

                // Send the HTTP request to the remote host
                http::async_write(stream_, req_, beast::bind_front_handler(&session::on_write, shared_from_this()));
            }

            void on_write(beast::error_code ec, std::size_t bytes_transferred) {
                boost::ignore_unused(bytes_transferred);

                if (ec)
                    return fail(ec, "write");

                // Receive the HTTP response
                http::async_read(stream_, buffer_, res_, beast::bind_front_handler(&session::on_read, shared_from_this()));
            }

            void on_read(beast::error_code ec, std::size_t bytes_transferred) {
                boost::ignore_unused(bytes_transferred);

                if (ec)
                    return fail(ec, "read");

                // Write the message to standard out
                // log_info << res_ ;
                if (post_processor_) {
                    post_processor_(std::string(res_.body()));
                }

                // Gracefully close the socket
                stream_.socket().shutdown(tcp::socket::shutdown_both, ec);

                // not_connected happens sometimes so don't bother reporting it.
                if (ec && ec != beast::errc::not_connected)
                    return fail(ec, "shutdown");

                // If we get here then the connection is closed gracefully
                stream_.socket().close(ec);
                if (ec) {
                    // Log the error if closing fails
                    log_error << "Close error: " << ec.message();
                }
            }

        private:
            void fail(beast::error_code ec, char const* what) {
                log_error << what << ": " << ec.message();

                // Attempt to close the connection if open
                if (stream_.socket().is_open()) {
                    beast::error_code close_ec;
                    stream_.socket().close(close_ec);
                    if (close_ec) {
                        log_error << "Error closing socket: " << close_ec.message();
                    }
                }
                if (post_processor_) {
                    post_processor_("");
                }
            }
        };

        // Performs an HTTP GET and prints the response
        class ssl_session : public std::enable_shared_from_this<ssl_session> {
            tcp::resolver resolver_;
            ssl::stream<beast::tcp_stream> stream_;
            beast::flat_buffer buffer_; // (Must persist between reads)
            http::request<http::string_body> req_;
            http::response<http::string_body> res_;
            std::function<void(std::string&& r)> post_processor_;

        public:
            explicit ssl_session(boost::asio::any_io_executor ex, ssl::context& ctx, std::function<void(std::string&& r)>&& post_processor)
                : resolver_(ex), stream_(ex, ctx), post_processor_(std::move(post_processor)) {}

            // Start the asynchronous operation
            void run(std::string const& method, std::string const& host, std::string const& port, std::string const& target, std::map<std::string, std::string> const& headers,
                std::string const& body) {
                // Set SNI Hostname (many hosts need this to handshake successfully)
                if (!SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str())) {
                    beast::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
                    log_error << ec.message() << "\n";
                    return;
                }

                req_ = construct_request(method, host, target, headers, body);

                // Look up the domain name
                resolver_.async_resolve(host, port, beast::bind_front_handler(&ssl_session::on_resolve, shared_from_this()));
            }

            void on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
                if (ec)
                    return fail(ec, "resolve");

                // Set a timeout on the operation
                beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

                // Make the connection on the IP address we get from a lookup
                beast::get_lowest_layer(stream_).async_connect(results, beast::bind_front_handler(&ssl_session::on_connect, shared_from_this()));
            }

            void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                if (ec)
                    return fail(ec, "connect");

                // Perform the SSL handshake
                stream_.async_handshake(ssl::stream_base::client, beast::bind_front_handler(&ssl_session::on_handshake, shared_from_this()));
            }

            void on_handshake(beast::error_code ec) {
                if (ec)
                    return fail(ec, "handshake");

                // Set a timeout on the operation
                beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

                // Send the HTTP request to the remote host
                http::async_write(stream_, req_, beast::bind_front_handler(&ssl_session::on_write, shared_from_this()));
            }

            void on_write(beast::error_code ec, std::size_t bytes_transferred) {
                boost::ignore_unused(bytes_transferred);

                if (ec)
                    return fail(ec, "write");

                // Receive the HTTP response
                http::async_read(stream_, buffer_, res_, beast::bind_front_handler(&ssl_session::on_read, shared_from_this()));
            }

            void on_read(beast::error_code ec, std::size_t bytes_transferred) {
                boost::ignore_unused(bytes_transferred);

                if (ec)
                    return fail(ec, "read");

                // Write the message to standard out
                // log_info << res_;
                if (post_processor_) {
                    post_processor_(std::string(res_.body()));
                }

                // Set a timeout on the operation
                beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));

                // Gracefully close the stream
                stream_.async_shutdown(beast::bind_front_handler(&ssl_session::on_shutdown, shared_from_this()));
            }

            void on_shutdown(beast::error_code ec) {
                // ssl::error::stream_truncated, also known as an SSL "short read",
                // indicates the peer closed the connection without performing the
                // required closing handshake (for example, Google does this to
                // improve performance). Generally this can be a security issue,
                // but if your communication protocol is self-terminated (as
                // it is with both HTTP and WebSocket) then you may simply
                // ignore the lack of close_notify.
                //
                // https://github.com/boostorg/beast/issues/38
                //
                // https://security.stackexchange.com/questions/91435/how-to-handle-a-malicious-ssl-tls-shutdown
                //
                // When a short read would cut off the end of an HTTP message,
                // Beast returns the error beast::http::error::partial_message.
                // Therefore, if we see a short read here, it has occurred
                // after the message has been completed, so it is safe to ignore it.

                if (ec != boost::asio::ssl::error::stream_truncated)
                    return fail(ec, "shutdown");

                beast::get_lowest_layer(stream_).socket().close(ec);
                if (ec) {
                    // Log the error if closing fails
                    log_error << "Close error: " << ec.message();
                }
            }

        private:
            void fail(beast::error_code ec, char const* what) {
                log_error << what << ": " << ec.message();

                // Attempt to close the connection
                beast::error_code close_ec;
                if (beast::get_lowest_layer(stream_).socket().is_open()) {
                    beast::get_lowest_layer(stream_).socket().close(close_ec);
                    if (close_ec) {
                        log_error << "Error closing socket: " << close_ec.message();
                    }
                }

                // Notify the post processor of failure with an empty response
                if (post_processor_) {
                    post_processor_("");
                }
            }
        };
    } // namespace

    void transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string_view body,
        std::function<void(std::string&& r)>&& post_processor) noexcept {
        // bool need_close = false;
        std::shared_ptr<boost::beast::ssl_stream<boost::beast::tcp_stream>> ssl_stream = nullptr;
        std::shared_ptr<boost::beast::tcp_stream> plain_stream = nullptr;
        try {
            // log_info << "transfer1:" << url;
            auto const& [success, scheme, host, port, target] = parse_url(url);
            if (!success) {
                log_error << "url parse failure! url is " << url;
                if (post_processor) {
                    post_processor("");
                }
                return;
            }
            // The io_context is required for all I/O
            boost::asio::io_context ioc;
            // These objects perform our I/O
            boost::asio::ip::tcp::resolver resolver(ioc);
            // Look up the domain name
            auto const results = resolver.resolve(host, port);
            auto req = construct_request(method, host, target, headers, body);
            // This buffer is used for reading and must be persisted
            boost::beast::flat_buffer buffer;
            // Declare a container to hold the response
            // boost::beast::http::response<boost::beast::http::dynamic_body> res;
            boost::beast::http::response<boost::beast::http::string_body> res;
            if (scheme == "https") {
                // The SSL context is required, and holds certificates
                boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_client);
                // This holds the root certificate used for verification
                load_root_certificates(ctx);
                // Verify the remote server's certificate
                // ctx.set_verify_mode(boost::asio::ssl::verify_peer);
                ctx.set_verify_mode(boost::asio::ssl::verify_none);
                // boost::beast::ssl_stream<boost::beast::tcp_stream> stream(ioc, ctx);
                ssl_stream = std::make_shared<boost::beast::ssl_stream<boost::beast::tcp_stream>>(ioc, ctx);
                //  Set SNI Hostname (many hosts need this to handshake successfully)
                if (!SSL_set_tlsext_host_name((*ssl_stream).native_handle(), host.data())) {
                    boost::beast::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
                    // FIXME throw boost::beast::system_error{ec};
                    log_error << "SSL_set_tlsext_host_name:" << ec.message();
                    if (post_processor) {
                        post_processor("");
                    }
                    return;
                }
                // Make the connection on the IP address we get from a lookup
                boost::beast::get_lowest_layer(*ssl_stream).connect(results);
                // Perform the SSL handshake
                ssl_stream->handshake(boost::asio::ssl::stream_base::client);
                auto r = transeiver(*ssl_stream, req, buffer, res);
                shutdown_ssl(ssl_stream);
                boost::beast::get_lowest_layer(*ssl_stream).close();
                ssl_stream = nullptr;
                process(r, method, headers, body, std::string(res.body()), std::move(post_processor));
            } else {
                // boost::beast::tcp_stream stream(ioc);
                plain_stream = std::make_shared<boost::beast::tcp_stream>(ioc);
                // Make the connection on the IP address we get from a lookup
                plain_stream->connect(results);
                auto r = transeiver(*plain_stream, req, buffer, res);
                shutdown_plain(plain_stream);
                plain_stream->close();
                plain_stream = nullptr;
                process(r, method, headers, body, std::string(res.body()), std::move(post_processor));
            }
        } catch (std::exception const& e) {
            if (ssl_stream) {
                shutdown_ssl(ssl_stream);
                boost::beast::get_lowest_layer(*ssl_stream).close();
                ssl_stream = nullptr;
            } else if (plain_stream) {
                shutdown_plain(plain_stream);
                plain_stream->close();
                plain_stream = nullptr;
            }
            if (post_processor) {
                post_processor("");
            }
            // Log::log("Error: %s", e.what());
            log_error << "http transfer exception: " << url << " " << e.what();
        }

        //} else {
        //    log_info << "post_processor is null";
        //                }
        //    catch (std::exception const& e) {
        //        // shutdown_ssl(stream);
        //        try {
        //            boost::beast::get_lowest_layer(stream).close();
        //            log_error << "http transfer exception: " << url << " " << e.what();
        //        } catch (...) {
        //            log_error << "boost::beast::get_lowest_layer(stream).close error";
        //        }
        //
        //        //  throw; // 重新抛出异常供外层处理
        //    }
        //
        //                }
        // catch (std::exception const& e) {
        //    // shutdown_plain(stream);
        //    if (post_processor) {
        //        post_processor("");
        //    } else {
        //        log_info << "post_processor is null";
        //    }
        //    try {
        //        stream.close();
        //        log_error << "http transfer exception: " << url << " " << e.what();
        //    } catch (...) {
        //        log_error << "http stream.close()";
        //        if (post_processor) {
        //            post_processor("");
        //        } else {
        //            log_info << "post_processor is null";
        //        }
        //    }
        //  throw; // 重新抛出异常供外层处理
    }

    void transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string const& body,
        std::function<void(std::string&& r)>&& post_processor) noexcept {
        transfer(method, url, headers, std::string_view{body.begin(), body.end()}, std::move(post_processor));
    }

    //void transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string_view body, std::string const& file_name,
    //    std::function<void(int r)>&& post_processor) noexcept {
    //    try {
    //        // log_info << "transfer:" << url;
    //        auto const& [success, scheme, host, port, target] = parse_url(url);
    //        if (!success) {
    //            log_error << "url parse failure! url is " << url;
    //            post_processor(1);
    //            return;
    //        }
    //        // The io_context is required for all I/O
    //        boost::asio::io_context ioc;
    //        // These objects perform our I/O
    //        boost::asio::ip::tcp::resolver resolver(ioc);
    //        // std::printf("%s://%s:%s%s\n", std::string(s).c_str(), std::string(host).c_str(), std::string(port).c_str(), std::string(target).c_str());
    //        //  Look up the domain name
    //        auto const results = resolver.resolve(host, port);
    //        auto req = construct_request(method, std::string(host), std::string(target), headers, body);
    //        // This buffer is used for reading and must be persisted
    //        boost::beast::flat_buffer buffer;
    //        // Declare a container to hold the response
    //        boost::beast::error_code ec;
    //        boost::beast::http::response_parser<boost::beast::http::file_body> parser;
    //        parser.body_limit((std::numeric_limits<std::uint64_t>::max)());
    //        parser.get().body().open(file_name.c_str(), boost::beast::file_mode::write, ec);
    //        if (scheme == "https") {
    //            // The SSL context is required, and holds certificates
    //            boost::asio::ssl::context ctx(boost::asio::ssl::context::tlsv12_client);
    //            // This holds the root certificate used for verification
    //            load_root_certificates(ctx);
    //            // Verify the remote server's certificate
    //            ctx.set_verify_mode(boost::asio::ssl::verify_peer);
    //            // boost::beast::ssl_stream<boost::beast::tcp_stream> stream(ioc, ctx);
    //            auto stream = std::make_shared<boost::beast::ssl_stream<boost::beast::tcp_stream>>(ioc, ctx);
    //            try {
    //                //  Set SNI Hostname (many hosts need this to handshake successfully)
    //                if (!SSL_set_tlsext_host_name(stream->native_handle(), std::string(host).c_str())) {
    //                    boost::beast::error_code ec{static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category()};
    //                    // FIXME throw boost::beast::system_error{ec};
    //                    log_error << "SSL_set_tlsext_host_name:" << ec.message();
    //                }
    //                // Make the connection on the IP address we get from a lookup
    //                boost::beast::get_lowest_layer(*stream).connect(results);
    //                // Perform the SSL handshake
    //                stream->handshake(boost::asio::ssl::stream_base::client);
    //                auto r = transeiver(*stream, req, buffer, parser);
    //                shutdown_ssl(stream);
    //                stream = nullptr;
    //                process(r, method, headers, body, file_name, std::move(post_processor));
    //            } catch (std::exception const& e) {
    //                boost::beast::get_lowest_layer(*stream).close();
    //                log_error << "http transfer exception: " << url << " " << e.what();
    //                //  throw; // 重新抛出异常供外层处理
    //            }
    //        } else {
    //            // boost::beast::tcp_stream stream(ioc);
    //            auto stream = std::make_shared<boost::beast::tcp_stream>(ioc);
    //            try {
    //                // Make the connection on the IP address we get from a lookup
    //                stream->connect(results);
    //                auto r = transeiver(*stream, req, buffer, parser);
    //                shutdown_plain(stream);
    //                process(r, method, headers, body, file_name, std::move(post_processor));
    //            } catch (std::exception const& e) {
    //                try {
    //                    stream->close();
    //                } catch (...) {}

    //                log_error << "http transfer exception: " << url << " " << e.what();
    //                //  throw; // 重新抛出异常供外层处理
    //            }
    //        }
    //    } catch (std::exception const& e) {
    //        // std::printf("Error: %s", e.what());
    //        log_error << "https transfer exception: " << url << " " << e.what();
    //        if (post_processor) {
    //            post_processor(1);
    //        }
    //    }
    //}

    void async_transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string const& body,
        std::function<void(std::string&& r)>&& post_processor) noexcept {
        try {
            // log_trace << "async_transfer 1";
            auto const& [success, scheme, host, port, target] = parse_url(url);
            if (!success) {
                log_error << "url parse failure! url is " << url;
                post_processor("");
                return;
            }
            if (scheme == "https") {
                auto t = std::make_shared<std::thread>([post_processor, method, host, port, target, headers, body] {
                    auto pp = std::move(post_processor);
                    // The io_context is required for all I/O
                    boost::asio::io_context ioc;

                    // The SSL context is required, and holds certificates
                    ssl::context ctx{ssl::context::tlsv12_client};

                    // This holds the root certificate used for verification
                    load_root_certificates(ctx);

                    // Verify the remote server's certificate
                    ctx.set_verify_mode(ssl::verify_peer);

                    // Launch the asynchronous operation
                    // The session is constructed with a strand to
                    // ensure that handlers do not execute concurrently.
                    std::make_shared<ssl_session>(boost::asio::make_strand(ioc), ctx, std::move(pp))->run(method, host, port, target, headers, body);

                    // Run the I/O service. The call will return when
                    // the get operation is complete.
                    ioc.run();
                });
                threads_.push_back(t);
            } else {
                auto t = std::make_shared<std::thread>([post_processor, method, host, port, target, headers, body] {
                    auto pp = std::move(post_processor);
                    boost::asio::io_context ioc;
                    auto s = std::make_shared<session>(ioc, std::move(pp));
                    s->run(method, host, port, target, headers, body);
                    // auto s = session(ioc, std::move(pp));
                    // s.run(method, host, port, target, headers, body);
                    ioc.run();
                });
                threads_.push_back(t);
            }
        } catch (const std::exception& e) {
            log_error << "Exception: " << e.what() << std::endl;
        }
    }

    void stop_all_therads() {
        for (auto& thread : threads_) {
            if (thread && thread->joinable()) {
                thread->join();
            }
            thread = nullptr;
        }
    }
    // void async_transfer(std::string const& method, std::string const& url, std::map<std::string, std::string> const& headers, std::string const& body,
    //     std::string const& send_file_name, std::function<void(int r)>&& post_processor) noexcept {
    //     try {
    //         auto const& [success, scheme, host, port, target] = parse_url(url);
    //         if (!success) {
    //             log_error << "url parse failure! url is " << url;
    //             post_processor(1);
    //             return;
    //         }
    //         asio::io_context ioc;
    //         tcp::resolver resolver(ioc);
    //         auto endpoints = resolver.resolve(host, port);
    //         AsyncHttpClient client(ioc, endpoints);
    //         client.send_request();
    //         // 运行 IO 上下文
    //         std::thread io_thread([&ioc]() { ioc.run(); });
    //    } catch (const std::exception& e) {
    //        log_error << "Exception: " << e.what() << std::endl;
    //    }
    //}
} // namespace util::Http
