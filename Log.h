#pragma once

#include <string> // for std::string

#include <boost/log/trivial.hpp>

#define log_trace BOOST_LOG_TRIVIAL(trace)
#define log_debug BOOST_LOG_TRIVIAL(debug)
#define log_info BOOST_LOG_TRIVIAL(info)
#define log_warning BOOST_LOG_TRIVIAL(warning)
#define log_error BOOST_LOG_TRIVIAL(error)
#define log_fatal BOOST_LOG_TRIVIAL(fatal)

namespace util::Log {
    void init(std::string const& filename_prefix) noexcept;
    void start_clean_routine(int days) noexcept;
    void stop_clean_routine() noexcept;
} // namespace util::Log
