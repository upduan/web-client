#include "web-socket.h"

#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>

#include "Log.h"
#include "chassis.h"
#include "robot.h"

namespace util::WebSocket {
    namespace {
        std::mutex send_info_mutex;
        std::vector<WarnInfo> send_info_vector_;

        /*
        std::mutex send_info_map_mutex;
        std::unordered_map<ErrorCode, WarnInfo> unique_map;
        */
    } // namespace

    // 启动异步操作连接
    void session::run() noexcept {
        log_info << "web_socket host:" << host_;
        log_info << "web_socket port:" << port_;
        log_info << "web_socket target:" << target_;
        // 解析地址
        resolver_.async_resolve(host_, port_, boost::beast::bind_front_handler(&session::on_resolve, shared_from_this()));
        // boost::system::error_code ec{};
        // ioc_.run(ec);
        ioc_.run();
    }

    // void session::start() noexcept {
    //     run_event_loop();
    // }

    void session::stop() noexcept {
        // exit_flag_.store(true);
        log_info << "web_socket stop 1";
        boost::system::error_code ec{};
        ws_.close(boost::beast::websocket::close_code::normal, ec);
        log_info << "web_socket stop 2";
        robot::dfa.fire(robot::Event::WarnSystemDisconnect);
        // timer_.cancel(ec);
        timer_.cancel();
        log_info << "web_socket stop 3";
        ioc_.stop();
        log_info << "web_socket stop 4";
        // resolver_.cancel();
        // ioc_.stop();
    }

    // void session::run_event_loop() noexcept {
    //     while (!exit_flag_.load()) {
    //         try {
    //             ioc_.run();
    //         } catch (const std::exception& e) {
    //             std::cerr << "Exception in event loop: " << e.what() << std::endl;
    //         }
    //     }
    // }

    void session::on_resolve(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results) noexcept {
        if (ec)
            return fail(ec, "web_socket resolve");
        // 连接到解析后的地址
        boost::beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));
        boost::beast::get_lowest_layer(ws_).async_connect(results, boost::beast::bind_front_handler(&session::on_connect, shared_from_this()));
        log_info << "web_socket on_resolve OK";
    }

    void session::on_connect(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type::endpoint_type) noexcept {
        if (ec)
            return fail(ec, "web_socket connect");

        // 完成连接
        boost::beast::get_lowest_layer(ws_).expires_never();
        ws_.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
        ws_.set_option(boost::beast::websocket::stream_base::decorator([](boost::beast::websocket::request_type& req) {
            req.set(boost::beast::http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-async");
        }));

        // 发起 WebSocket 握手
        ws_.async_handshake(host_, target_, boost::beast::bind_front_handler(&session::on_handshake, shared_from_this()));

        log_info << "web_socket on_connect OK";
    }

    void session::on_handshake(boost::beast::error_code ec) noexcept {
        if (ec)
            return fail(ec, "web_socket handshake");

        robot::dfa.fire(robot::Event::WarnSystemConnected);
        retry_count = 0;

        ws_.async_read(buffer_, boost::beast::bind_front_handler(&session::on_read, shared_from_this()));

        // 重置心跳定时器
        timer_.expires_after(heartbeat_interval_);

        timer_.async_wait(boost::beast::bind_front_handler(&session::on_timer, shared_from_this()));

        log_info << "web_socket on_handshake OK";
    }

    void session::on_timer(boost::beast::error_code ec) noexcept {
        // log_info << "web_socket on_timer";
        if (ec)
            return fail(ec, "web_socket timer");
        // 发送心跳消息
        if (send_info_vector_.size() > 0) {
            for (auto const& info : send_info_vector_) {
                log_info << "web_socket push alarm info " << info.code_;
                auto send_info_ = boost::json::serialize(boost::json::object{{"uniqueId", info.unique_id}, {"time", info.time_}, {"id", ""}, {"code", (int)info.code_},
                    {"msg", info.msg_}, {"data", info.data_}, {"alarm_flag", info.alarm_flag_}, {"pose", info.pose_}});
                ws_.async_write(boost::asio::buffer(send_info_), boost::beast::bind_front_handler(&session::on_write, shared_from_this()));
            }
        } else {
            auto send_info_ = boost::json::serialize(boost::json::object{{"code", 0}});
            ws_.async_write(boost::asio::buffer(send_info_), boost::beast::bind_front_handler(&session::on_write, shared_from_this()));
        }
    }

    void push_send_alarm_info(WarnInfo info) noexcept {
        log_info << "web_socket push_send_alarm_info " << (int)info.code_;
        info.pose_ = chassis::get_robot_position();
        std::lock_guard<std::mutex> lock{send_info_mutex};
        {
            auto code = info.code_;
            auto it = std::find_if(send_info_vector_.begin(), send_info_vector_.end(), [&code](const WarnInfo& item) { return item.code_ == code; });
            if (it == send_info_vector_.end()) {
                send_info_vector_.push_back(info);
            } else {
                log_info << "web_socket errorCode is exist " << info.code_;
            }
        }
        /*
        std::lock_guard<std::mutex> map_lock{send_info_map_mutex};
        auto it = unique_map.find(info.code_);
        if (it == unique_map.end()) {
            unique_map[info.code_] = info;
        } else if (it->second.alarm_flag_ == "1" && info.alarm_flag_ == "0") {
            unique_map.erase(it);
        }
        */
    }

    boost::json::array get_current_errors() noexcept {
        boost::json::array erros;
        std::lock_guard<std::mutex> lock{send_info_mutex};
        for (auto const& info : send_info_vector_) {
            erros.push_back({{"error_code", info.code_}, {"error_msg", error_to_string(info.code_)}});
        }
        if (erros.size() > 0) {
            log_info << "web_socket get_current_errors" << boost::json::serialize(erros);
        }
        return erros;
    }

    /*
    void push_send_normal_info(ErrorCode code, WarnInfo info) noexcept {
        log_info << "push_send_normal_info " << (int)code;
        std::lock_guard<std::mutex> lock{send_info_mutex};
        if (send_info_map_.contains(code)) {
            send_info_map_[code] = info;
        }
    }
    */

    void session::on_write(boost::beast::error_code ec, std::size_t bytes_transferred) noexcept {
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            return fail(ec, "web_socket write");
        }
        // log_info << "web_socket on_write OK";
        // 重置心跳定时器
        timer_.expires_after(heartbeat_interval_);

        timer_.async_wait(boost::beast::bind_front_handler(&session::on_timer, shared_from_this()));
    }

    void session::on_read(boost::beast::error_code ec, std::size_t bytes_transferred) noexcept {
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            return fail(ec, "web_socket read");
        }
        std::string_view replay = {static_cast<const char*>(buffer_.data().data()), buffer_.size()};
        try {
            boost::json::object warn_info = boost::json::parse(replay).as_object();
            // log_info << boost::json::serialize(warn_info);
            // TODO 后台下发恢复指令，待定如何去做
            // log_info << "websocket on_read OK";
            if (warn_info.contains("id")) {
            } else {
                if (warn_info.contains("uniqueId") && warn_info["uniqueId"].is_int64()) {
                    std::uint64_t uniqueId = (std::uint64_t)warn_info["uniqueId"].as_int64();
                    log_info << "web_socket uniqueId:" << uniqueId;
                    std::lock_guard<std::mutex> lock{send_info_mutex};
                    send_info_vector_.erase(
                        std::remove_if(send_info_vector_.begin(), send_info_vector_.end(), [&uniqueId](const WarnInfo& info) { return info.unique_id == uniqueId; }),
                        send_info_vector_.end());
                }
                /*
                else {
                    log_info << "websocket no receive uniqueId";
                }
                */
            }
        } catch (const boost::system::system_error& e) {
            log_error << "JSON parse error: " << e.what() << "\n"; // 处理 JSON 解析错误
        } catch (const std::exception& e) {
            log_error << "Other error: " << e.what() << "\n"; // 处理其他异常
        }
        // 清空缓冲区
        buffer_.consume(buffer_.size());
        // 继续读取下一条消息
        ws_.async_read(buffer_, boost::beast::bind_front_handler(&session::on_read, shared_from_this()));
    }

    void session::on_close(boost::beast::error_code ec) noexcept {
        if (ec)
            return fail(ec, "web_socket close");

        // 连接关闭
        log_info << "web_socket connection closed successfully";
    }

    // void session::reconnect() noexcept {
    //     log_info << "web_socket reconnect";
    //     ws_.next_layer().close();
    //    // boost::beast::bind_front_handler(&session::run, shared_from_this(), host_.c_str(), port_.c_str());
    //     if (retry_count == 3) {
    //         robot::dfa.fire(robot::Event::NetworkDisconnect);
    //     } else {
    //         retry_count++;
    //         retry_count = 0;
    //     }
    // }

    void session::fail(boost::beast::error_code ec, char const* what) noexcept {
        log_info << "web_socket " << what << ": " << ec.message();
        ws_.next_layer().close();
        ioc_.stop();
        log_info << "web_socket ioc_stop";
        // reconnect();
    }

    std::string error_to_string(ErrorCode code) noexcept {
        switch (code) {
        case CAMERA_ID_NOT_EXIST:
            return "摄像头编号不存在";
        case DEVICE_MODEL_MISMATCH:
            return "设备型号与当前设备不一致";
        case INCORRECT_CAMERA_COUNT:
            return "摄像头个数不正确";
        case CAMERA_OPEN_FAILED:
            return "摄像头打开失败";
        case CAPTURE_IMAGE_FAILED:
            return "采图失败";
        case INCORRECT_NAV_INFO:
            return "航道信息不正确，无法导航";
        case CAMERA_RECORD_START_FAILED:
            return "摄像头未能正确开始录制";
        case CAMERA_RECORD_END_FAILED:
            return "摄像头未能正确结束录制";
        case CAMERA_VIDEO_UPLOAD_FAILED:
            return "摄像头上传视频失败";
        case MAIN_CONTROL_POSITION_UPLOAD_FAILED:
            return "主控上传位姿失败";
        case SEPARATION_FAILED:
            return "分架失败";
        case RECOGNITION_FAILED:
            return "识别失败";
        case SHELVING_FAILED:
            return "上架失败";
        case CAMERA_ERROR:
            return "摄像头异常";
        case ROUTE_PLANNING_FAILED:
            return "规划路径失败";
        case NAVIGATION_CANCELLED:
            return "导航取消";
        case WALK_TIMEOUT:
            return "行走超时";
        case POINT_PHOTO_TIMEOUT:
            return "定点采图超时";
        case INVALID_ROUTE_POINT:
            return "路径点不合法";
        case CHASSIS_NAVIGATION_FAILED:
            return "底盘导航失败";
        case TARGET_POINT_NOT_FOUND:
            return "找不到目标点";
        case POSITIONING_ERROR:
            return "定位异常";
        case FIXED_ROUTE_POINT_TOO_FAR:
            return "固定路线点位间距过大";
        case FIXED_ROUTE_NOT_FOUND:
            return "未找到固定路线";
        case READ_POINT_FAILED:
            return "读取点位失败";
        case AGV_DOCKING_FAILED:
            return "AGV对接失败";
        case NAVIGATION_RECOVERY_FAILED:
            return "导航恢复失败";
        case NTP_SERVICE_ERROR:
            return "NTP服务异常";
        case PHOTO_CAPTURE_FAILED:
            return "采图失败";
        case FREE_NAVIGATOR_FAILED:
            return "自由导航失败";
        case FIX_NAVIGATOR_FAILED:
            return "固定导航失败";
        case SINGLE_NAVIGATION_RESTART:
            return "单次导航失败，自动重新开始";
        case CHASSIS_DISCONNECT:
            return "底盘失联";
        case CHASSIS_IMU_ERROR:
            return "IMU异常";
        case CHASSIS_LIDAR_ERROR:
            return "雷达异常";
        case CHASSIS_ODOM_ERROR:
            return "里程计异常";
        case CHASSIS_3DCAM_ERROR:
            return "3D摄像头异常";
        case CHASSIS_ROS_ERROR:
            return "ROS异常";
        case CHASSIS_CHARGE_LOWER:
            return "底盘低电量";
        case CHASSIS_CHARGE_SLEEP:
            return "底盘进入休眠模式";
        case CHASSIS_NAV_CHARGE_FAILED:
            return "底盘导航回充失败";
        case CHARGE_POWER_OFF:
            return "充电桩断电";
        case EMERGENCY_STOP_PRESSED:
            return "急停开关被按下";
        case LIGHT_PROBLEM:
            return "灯故障";
        case LIFT_PROBLEM:
            return "杆故障";
        case LOCAL_PATH_OBSTACLE:
            return "局部路径有障碍";
        case CONNECT_CHARGE_FAILD:
            return "对接充电桩失败";
        case CONNECT_UPLOAD_SERVER_FAILD:
            return "上传服务器连接失败";
        default:
            return "未知错误代码";
        }
    }
} // namespace util::WebSocket
