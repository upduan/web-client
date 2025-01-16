#include "Log.h"
#include "web-socket.h"

class MyClient : public util::WebSocket::Manager {
public:
    ~MyClient() final override {}
    void on_connect(std::shared_ptr<util::WebSocket::Client> self) final {
        log_info << "on_connect";
    }
    void on_disconnect(std::shared_ptr<util::WebSocket::Client> self) final {
        log_info << "on_disconnect";
    }
    void on_message(std::shared_ptr<util::WebSocket::Client> self, std::string_view msg) final {
        log_info << "on_message: " << msg;
    }
    void on_message(std::shared_ptr<util::WebSocket::Client> self, std::span<std::byte> msg) final {
        log_info << "on_message for binary";
    }
    void on_error(std::shared_ptr<util::WebSocket::Client> self, Error ec) final {
        log_info << "on_error" << Manager::to_string(ec);
    }
    std::string get_ping(std::shared_ptr<util::WebSocket::Client> self) final {
        return "";
    }
};

int main() {
    auto client = std::make_shared<MyClient>();
    auto impl = std::make_shared<util::WebSocket::Client>(client, "ws://localhost:8080/ws");
    impl->start();
    return 0;
}
