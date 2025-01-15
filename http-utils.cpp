#include "http-utils.h"

// #include <boost/json/serialize.hpp>

std::string construct_multipart_formdata(std::string const& previous, std::string const& boundary, std::string const& name, std::string const& type, std::string const& file_name,
    std::string const& body, bool is_last) noexcept {
    std::string result = previous;
    result += "--" + boundary + "\r\n";
    result += "Content-Disposition: form-data; name=\"" + name + "\"; filename=\"" + file_name + "\"\r\n";
    result += "Content-Type: " + type + "\r\n";
    result += "\r\n";
    result += body;
    result += "\r\n";
    result += "--" + boundary + (is_last ? "--" : "") + "\r\n";
    // result += "\r\n";
    return result;
}

std::string construct_multipart_formdata(std::string const& previous, std::string const& boundary, std::string const& name, std::string const& value, bool is_last) noexcept {
    std::string result = previous;
    result += "--" + boundary + "\r\n";
    result += "Content-Disposition: form-data; name=\"" + name + "\"\r\n";
    result += "\r\n";
    result += value;
    result += "\r\n";
    result += "--" + boundary + (is_last ? "--" : "") + "\r\n";
    // result += "\r\n";
    return result;
}
