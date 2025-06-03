#include "sender.h"

#include <cstdio>
#include <filesystem>

#include <boost/json.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include "Http.h" // for util::Http::transfer
#include "Log.h"
#include "thread-pool.h"

namespace util::FileTransfer {
    // start project
    // start session: prepare file meta-info
    // send chunk: prepare chunk info & send, receive response, update transfer state
    // ... repeat chunk
    // stop session: close file(delete it?).
    // ... repeat session
    // stop project

    struct meta_info_t {
        std::size_t total;
        std::size_t position;
        // type: video, timestamp, image, json(meta)
        // state: idle, transfering, finally
        // file_path
        // file_name
        // organization_id
        // big_task_id
        // task_id
        // camera_no
        // file_name
    };

    meta_info_t prepare_meta_info(std::string const& file_path) noexcept {
        // TODO use file-system
        // TODO query via DB
        return {};
    }

    bool need_transfer(meta_info_t const& meta_info) noexcept {
        // TODO use file-system
        // TODO query via DB
        return {};
    }

    struct chunk_transfer_result_t {
        // code
        // message
    };

    chunk_transfer_result_t chunk_transfer(meta_info_t const& meta_info, void const* buffer, std::size_t len) noexcept {
        // TODO construct HTTP request and receive result
        // --or--
        // TODO construct chunk-info for tcp transfer
        // TODO send it
        // TODO receive result
        return {};
    }

    bool check_chunk_transfer_result(chunk_transfer_result_t const& r) noexcept {
        // TODO check r, write DB state?
        return {};
    }

    void update_meta_info(meta_info_t& meta_info, std::size_t len) noexcept {
        meta_info.position += len;
        // TODO write to DB
    }

    void mark_file_transfer_finally(meta_info_t& meta_info) noexcept {
        // TODO send finally marker to server?
        // TODO change DB state
        // TODO remove file?
        // TODO guarantee need_transfer(file_path) return false
    }

    void session_process(std::string const& file_path, std::size_t chunk_size) noexcept {
        auto meta_info = prepare_meta_info(file_path);
        if (need_transfer(meta_info)) {
            auto file = std::fopen(file_path.c_str(), "rb");
            if (file) {
                auto buffer = std::malloc(chunk_size);
                if (buffer) {
                    for (;;) {
                        auto bytes = std::min(chunk_size, meta_info.total - meta_info.position);
                        auto len = bytes;
                        for (;;) {
                            if (bytes == 0) {
                                break;
                            }
                            std::fseek(meta_info.position);
                            len = std::fread(buffer, 1, bytes, file);
                            if (len == bytes) {
                                auto r = chunk_transfer(meta_info, buffer, len);
                                if (check_chunk_transfer_result(r)) {
                                    update_meta_info(meta_info, len);
                                    break;
                                }
                            }
                        }
                        if (len < chunk_size) {
                            mark_file_transfer_finally(meta_info);
                            break;
                        }
                    }
                    std::free(buffer);
                }
                std::fclose(file);
            }
        }
    }
} // namespace util::FileTransfer

namespace {
    std::shared_ptr<util::thread_pool> start_thread_pool(std::size_t number) noexcept {
        auto thread_pool = std::make_shared<util::thread_pool>();
        thread_pool->init(number);
        thread_pool->start();
        // do anything ...
        // tp.wait_for_all_done();
        // tp.stop();
        return thread_pool;
    }

    std::shared_ptr<util::thread_pool> thread_pool = nullptr;
    std::string request_url = "";
} // namespace

void start_thread_pool() {
    if (!thread_pool) {
#ifdef LOCAL_DBG
        thread_pool = start_thread_pool(1);
#else
        thread_pool = start_thread_pool(10);
#endif

        auto config = get_program_config();
        request_url = config.remote.upload_url;
    }
}

void stop_thread_pool() noexcept {
    if (thread_pool) {
        thread_pool->stop();
        thread_pool = nullptr;
    }
}

int check_transfer_result(std::string const& r, std::string& transfer_id, std::string& error_info) {
    log_info << "check_transfer_result:" << r;
    int result = -1;
    try {
        boost::json::object info = boost::json::parse(r).as_object();
        if (info.contains("code") && info.at("code").is_int64()) {
            auto ret_code = info.at("code").as_int64();
            result = ret_code;
        }
        if (info.contains("data") && info.at("data").is_object()) {
            auto data = info.at("data").as_object();
            if (data.contains("file_id") && data.at("file_id").is_string()) {
                transfer_id = data.at("file_id").as_string().c_str();
            }
        }
        if (info.contains("msg") && info.at("msg").is_string()) {
            error_info = info.at("msg").as_string().c_str();
            log_info << "check_transfer_result:error_info:" << error_info;
        } else if (info.contains("meaage") && info.at("meaage").is_string()) {
            error_info = info.at("message").as_string().c_str();
            log_info << "check_transfer_result:error_info:" << error_info;
        }
    } catch (const boost::system::system_error& e) {
        log_error << "JSON parse error: " << e.what(); // 处理 JSON 解析错误
    } catch (const std::exception& e) {
        log_error << "Other error: " << e.what(); // 处理其他异常
    }
    return result;
}

bool send_file(std::string const& task_id, std::string const& file_path, std::string const& file_name, std::string const& camera_sn) noexcept {
    bool result = true;
    log_info << "send_file file_path:" << file_path;
    constexpr std::size_t const chunk_size = 5 * 1024 * 1024; // 5 MiB
    auto file_transfer = core::query_file_transfer_by_path(file_path);
    if (file_transfer.id.empty()) {
        log_info << "send_file file_path not exist in database";
        return true;
    }
    if (file_transfer.transfer_state == core::file_transfer_state::Completed) {
        return true;
    }

    auto file = std::fopen(file_path.c_str(), "rb");
    if (!file) {
        log_error << "send_file open file failed:" << file_path;
        result = false;
    } else {
        // send metainfo
        if (file_transfer.transfer_id.empty()) {
            std::fseek(file, 0, SEEK_END);
            auto size = (std::uint64_t)std::ftell(file);
            std::fseek(file, 0, SEEK_SET);
            auto file_type = "";
            if (file_name == "position.txt") {
                file_type = "position";
            } else if (file_name == "task.json") {
                file_type = "task";
            } else if (file_name.ends_with(".mjpeg")) {
                file_type = "video";
            } else if (file_name.ends_with(".txt")) {
                file_type = "timestamp";
            } else if (file_name.ends_with(".jpeg")) {
                file_type = "image";
            }
            auto meta_info =
                boost::json::object{{"req_type", "collect"}, {"file_type", file_type}, {"file_name", file_name}, {"cmd", "upload"}, {"task_id", task_id}, {"file_size", size}};
            if (camera_sn != "") {
                meta_info["camera_id"] = camera_sn;
            }
            // TODO: calculate md5 hash for whole file, add to "Content-MD5" header
            std::string request_info = boost::json::serialize(meta_info);
            log_info << "send_file request_info:" << request_info;
            util::Http::transfer("POST", request_url, {{"Content-Type", "application/json"}}, request_info, [&file_transfer](auto&& r) {
                std::string info = std::move(r);
                if (info.empty()) {
                    log_error << "send_file meta respone is empty ";
                } else {
                    std::string transfer_id = "";
                    std::string error_info = "";
                    int rr = check_transfer_result(info, transfer_id, error_info);
                    switch (rr) {
                    case 200:
                        if (transfer_id.empty()) {
                            log_error << "file_transfer.transfer_id is empty:" << file_transfer.id << " " << file_transfer.file_path;
                        } else {
                            file_transfer.transfer_id = transfer_id;
                            core::update_items("file_transfers", file_transfer.id, {{"transfer_id", transfer_id}});
                        }
                        break;
                    case 406: // 文件已经传输过了，直接修改状态
                        file_transfer.transfer_state = (int)core::file_transfer_state::Completed;
                        core::update_items("file_transfers", file_transfer.id, {{"transfer_state", core::file_transfer_state::Completed}});
                        break;
                    default:
                        log_error << "send_file meta respone is errorCode:" << rr;
                    }
                }
            });
        }
        if (file_transfer.transfer_state == core::file_transfer_state::Completed) {
            return true;
        }
        result = !file_transfer.transfer_id.empty();
        if (!result) {
            return result;
        }
        auto buffer = std::malloc(chunk_size);
        std::size_t offset = file_transfer.transferred_size;
        while (result) {
            std::memset(buffer, 0, chunk_size);
            std::size_t read_size = std::min(chunk_size, file_transfer.file_size - offset);
            auto read_length = std::fread(buffer, 1, read_size, file);
            if (read_length == read_size) {
                log_info << "file_transfer.transfer_id:" << file_transfer.transfer_id;
                util::Http::transfer("PUT", request_url + ("/" + file_transfer.transfer_id),
                    {{"Content-Type", "application/octet-stream"},
                        {"Part-Info", std::to_string(offset) + "-" + std::to_string(read_length) + (read_size < chunk_size ? "; finally" : "")}},
                    std::string_view((char const*)buffer, read_size), [&result, &file_transfer](auto&& chunk_result) {
                        std::string info = std::move(chunk_result);
                        if (!info.empty()) {
                            std::string id = "";
                            std::string error_info = "";
                            result = check_transfer_result(info, id, error_info);
                            if (!result) {
                                log_error << "send_file post chunk error:" << error_info;
                                if (error_info == "not find file_id") {
                                    file_transfer.transfer_id = "";
                                    file_transfer.transferred_size = 0;
                                    log_info << "server not find file_id reset file_transfer:" << file_transfer.id;
                                    core::update_file_transfers(file_transfer);
                                }
                            }
                        } else {
                            log_error << "send_file post chunk eroor chunk_result is empty";
                            result = false;
                        }
                    });
            } else {
                log_error << "send_file read_length != read_size";
                result = false;
            }
            if (result) {
                offset += read_length;
                file_transfer.transferred_size = offset;
                core::update_items("file_transfers", file_transfer.id, {{"transferred_size", file_transfer.transferred_size}});
                if (read_size < chunk_size) {
                    log_info << "send_file read file finish :" << file_transfer.file_path << " " << file_transfer.id;
                    file_transfer.transfer_state = core::file_transfer_state::Completed;
                    core::update_items("file_transfers", file_transfer.id, {{"transfer_state", core::file_transfer_state::Completed}});
                    break;
                }
            }
        }
        std::free(buffer);
        std::fclose(file);
    }
    log_info << "send_file:" << file_path << " result:" << result;
    return result;
}

// void upload_directory(const std::string& root, const std::string& organization_id, const std::string& big_task_id, const std::string& robot_id,
//     const std::string& task_id) noexcept {
//     auto meta_info = boost::json::object{{"req_type", "collect"}, {"device", "robot"}, {"rec_type", "book"}, {"cmd", "upload-start"}, {"org_id", organization_id},
//         {"big_task_id", big_task_id}, {"robot_id", robot_id}, {"task_id", task_id}};
//     std::string transfer_id;
//     std::string request_body = boost::json::serialize(meta_info);
//     std::printf("query: %s\n", request_body.c_str());
//     util::Http::transfer("POST", request_url, {{"Content-Type", "application/json"}}, boost::json::serialize(meta_info), [&transfer_id](auto r) {
//         std::printf("return %s\n", r.c_str());
//         if (!r.empty()) {
//             auto j = boost::json::parse(std::move(r)).as_object();
//             transfer_id = j.at("data").as_object().at("task_id").as_string().c_str();
//             std::printf("transfer_id is %s\n", transfer_id.c_str());
//         }
//     });
//     const std::filesystem::path sandbox{root};
//     // std::cout << "directory_iterator:\n";
//     // directory_iterator can be iterated using a range-for loop
//     for (auto const& dir_entry : std::filesystem::directory_iterator{sandbox}) {
//         // std::cout << dir_entry.path() << '\n';
//         if (dir_entry.is_directory()) {
//             auto camera_sn = dir_entry.path().filename().string();
//             for (auto const& file : std::filesystem::directory_iterator{dir_entry}) {
//                 if (file.is_regular_file()) {
//                     auto whole_file_name = file.path().string();
//                     auto file_name = file.path().filename().string();
//                     auto ext = file.path().extension().string();
//                     if (ext == ".mjpeg") {
//                         meta_info = boost::json::object{{"req_type", "collect"}, {"file_type", "video"}, {"file_name", file_name}, {"cmd", "upload"}, {"task_id", task_id},
//                             {"camera_id", camera_sn}};
//                         send_file(meta_info, whole_file_name, "video", task_id);
//                     } else if (ext == ".txt") {
//                         meta_info = boost::json::object{{"req_type", "collect"}, {"file_type", "timestamp"}, {"file_name", file_name}, {"cmd", "upload"}, {"task_id", task_id},
//                             {"camera_id", camera_sn}};
//                         send_file(meta_info, whole_file_name, "timestamp", task_id);
//                     }
//                 }
//             }
//         } else if (dir_entry.is_regular_file()) {
//             if (dir_entry.path().filename().string() == "position.txt") {
//                 meta_info = boost::json::object{{"req_type", "collect"}, {"file_type", "position"}, {"file_name", "position.txt"}, {"cmd", "upload"}, {"task_id", task_id}};
//                 send_file(meta_info, dir_entry.path().string(), "position",task_id);
//             } else if (dir_entry.path().filename().string() == "task.json") {
//                 meta_info = boost::json::object{{"req_type", "collect"}, {"file_type", "task"}, {"file_name", "task.json"}, {"cmd", "upload"}, {"task_id", task_id}};
//                 send_file(meta_info, dir_entry.path().string(), "task", task_id);
//             }
//         }
//     }
//     // 192.168.1.22:24111/robot/collect
//     meta_info = boost::json::object{{"req_type", "ocr"}, {"task_id", task_id}, {"cmd", "upload-end"}};
//     util::Http::transfer("POST", request_url, {{"Content-Type", "application/json"}}, boost::json::serialize(meta_info), [](auto r) {
//         std::printf("end result: %s\n", r.c_str());
//         // TODO: do nothing
//         // {
//         //     "ret_code": 200,
//         //     "msg": "0K",
//         // }
//     });
//
// }

void upload_one_task_to_cloud(core::task const& task) noexcept {
    start_thread_pool();
    std::string task_id = task.id;
    log_info << "upload_one_task:task_id:" << task_id << " big_task_id:" << task.big_task_id;
    log_info << "task.id:" << task.id << "task.state:" << task.state;
    if (task.state == (int)core::TaskState::Wait_File_Transfer) {
        core::update_items("tasks", task_id, {{"state", (int)core::TaskState::File_Transfering}});
        auto task_dir_path = task.get_full_workplace();
        log_info << "task_dir_path:" << task_dir_path;
#ifdef LOCAL_DBG
#else
        task.report_tier_state();
#endif
        // std::vector<std::shared_ptr<std::thread>> all_threads;
        for (auto const& dir_entry : std::filesystem::directory_iterator{task_dir_path}) {
            if (dir_entry.is_directory()) {
                auto camera_sn = dir_entry.path().filename().string();
                for (auto const& file : std::filesystem::directory_iterator{dir_entry}) {
                    // auto camera_sn2 = camera_sn;
                    if (file.is_regular_file()) {
                        auto whole_file_name = file.path().string();
                        auto file_name = file.path().filename().string();
                        auto file_transfer = core::query_file_transfer_by_path(whole_file_name);
                        if (file_transfer.id.empty()) {
                            log_info << "send_file file_path not exist in database ";
                        } else if (file_transfer.transfer_state != core::file_transfer_state::Completed) {
                            thread_pool->exec(0, nullptr, [&task_id, whole_file_name_ = std::move(whole_file_name), camera_sn, file_name_ = std::move(file_name)]() {
                                send_file(task_id, whole_file_name_, file_name_, camera_sn);
                            });
                        }
                    }
                }
            } else if (dir_entry.is_regular_file()) {
                auto whole_file_name = dir_entry.path().string();
                log_info << "whole_file_name:" << whole_file_name;
                auto file_name = dir_entry.path().filename().string();
                log_info << "file_name:" << file_name;
                auto file_transfer = core::query_file_transfer_by_path(whole_file_name);
                if (file_transfer.id.empty()) {
                    log_info << "send_file file_path not exist in database ";
                } else if (file_transfer.transfer_state != core::file_transfer_state::Completed) {
                    thread_pool->exec(0, nullptr,
                        [&task_id, whole_file_name_ = std::move(whole_file_name), file_name_ = std::move(file_name)]() { send_file(task_id, whole_file_name_, file_name_); });
                }
            }
        }
        thread_pool->wait_for_all_done();
        // TODO 判断文件是否全部完成了
        bool result = core::is_finish_upload_by_task_id(task_id);
        if (result) {
            log_info << "upload_one_task_to_cloud success";
            auto meta_info = boost::json::object{{"req_type", "ocr"}, {"task_id", task_id}, {"cmd", "upload-end"}};
            std::string request_string = boost::json::serialize(meta_info);
            log_info << "upload_one_task_to_cloud request_string:" << request_string;
            util::Http::transfer("POST", request_url, {{"Content-Type", "application/json"}}, request_string, [&task_id](auto&& r) {
                auto info = std::move(r);
                log_info << "upload-end:" << task_id << "http result:" << info;
                core::update_items("tasks", task_id, {{"state", (int)core::TaskState::Wait_Extract_Frame}});
            });
        } else {
            log_info << "upload_one_task_to_cloud faild";
            core::update_items("tasks", task_id, {{"state", (int)core::TaskState::Wait_File_Transfer}});
        }
    }
}

void upload_one_task_to_cloud(std::string const& task_id) noexcept {
    auto task = core::query_task_by_id(task_id);
    upload_one_task_to_cloud(task);
}
