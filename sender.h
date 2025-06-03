#pragma once

#include <string>

bool send_file(std::string const& task_id, std::string const& file_path, std::string const& file_name, std::string const& camera_sn = "") noexcept;
//void upload_directory(const std::string& root, const std::string& organization_id, const std::string& big_task_id, const std::string& robot_id,
//    const std::string& task_id) noexcept;

void upload_one_task_to_cloud(std::string const& task_id) noexcept;
void upload_one_task_to_cloud(core::task const& task) noexcept;
void stop_thread_pool() noexcept;
