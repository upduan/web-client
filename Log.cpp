#include "Log.h"

#include <filesystem>
#include <functional> // for std::function
#include <thread>

#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>

namespace util::Log {
    namespace {
        std::string log_folder = "/var/log/webserver"; // becase first so write
        std::atomic<bool> clean_thread_stop_flag = false;
        std::shared_ptr<std::thread> clean_thread;
    } // namespace

    void init(std::string const& filename_prefix) noexcept {
        if (!std::filesystem::exists(log_folder)) {
            std::filesystem::create_directories(log_folder);
        }
        auto sink = boost::log::add_file_log(boost::log::keywords::file_name = log_folder + "/" + filename_prefix + "_%Y%m%dT%H%M%S.log",
            boost::log::keywords::rotation_size = 5 * 1024 * 1024, boost::log::keywords::time_based_rotation = boost::log::sinks::file::rotation_at_time_point(0, 0, 0),
            boost::log::keywords::format = "[%TimeStamp%] [%ThreadID%] [%Severity%]: %Message%", boost::log::keywords::auto_flush = true);
        sink->locked_backend()->auto_flush(true);
        auto console_sink = boost::log::add_console_log(std::cout, boost::log::keywords::format = "[%TimeStamp%] [%ThreadID%] [%Severity%]: %Message%");
        auto core = boost::log::core::get();
        core->add_sink(sink);
        core->add_sink(console_sink);
        core->set_filter(boost::log::trivial::severity >= boost::log::trivial::trace);
        boost::log::add_common_attributes();
    }

    void start_clean_routine(int days) noexcept {
        auto max_age = std::chrono::hours(24 * days);
        clean_thread_stop_flag = false;
        clean_thread = std::make_shared<std::thread>([max_age, days] {
            while (!clean_thread_stop_flag) {
                auto now = std::filesystem::file_time_type::clock::now();
                std::this_thread::sleep_for(std::chrono::hours(4));
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(log_folder)) {
                        if (std::filesystem::is_regular_file(entry)) {
                            auto last_write_time = std::filesystem::last_write_time(entry.path());
                            auto age = std::chrono::duration_cast<std::chrono::hours>(now - last_write_time);
                            if (age >= max_age) {
                                log_info << "delete log:" << entry.path();
                                std::filesystem::remove(entry.path());
                            }
                        }
                    }
                } catch (const std::filesystem::filesystem_error& ex) {
                    std::cerr << "Error deleting old logs: " << ex.what() << std::endl;
                }
            }
        });
    }

    void stop_clean_routine() noexcept {
        clean_thread_stop_flag = true;
    }
} // namespace util::Log
