#include "web-socket.h"

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include "Log.h"

namespace util::WebSocket {
    namespace {
        std::map<Manager::Error, std::string> error_strings = {
            {Manager::Error::URL_FORMAT_ERROR, "URL format error"},
            {Manager::Error::SCHEME_MISTAKEN, "Scheme not allowed"},
            {Manager::Error::RESOVLE_FAILED, "Resolve failed"},
            {Manager::Error::CONNECTION_FAILED, "Connection failed"},
            {Manager::Error::SSL_NOT_DOMAIN, "SSL no domain name"},
            {Manager::Error::SSL_HANDSHAKE_FAILED, "SSL handshake failure"},
            {Manager::Error::HANDSHAKE_FAILED, "WebSocket handshake failure"},
            {Manager::Error::RECEIVE_MESSAGE_ERROR, "Receive message error"},
            {Manager::Error::SEND_MESSAGE_ERROR, "Send message error"},
            {Manager::Error::HEARTBEAT_ERROR, "Heartbeat error"},
        };
    }

    std::string Manager::to_string(Error error) noexcept {
        std::string result = "Unknown error";
        if (auto it = error_strings.find(error); it != error_strings.end()) {
            result = it->second;
        }
        return result;
    }
} // namespace util::WebSocket
