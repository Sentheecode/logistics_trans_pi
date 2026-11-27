#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
namespace sim {
// Same-user, local-filesystem-only mailbox. No sockets, SQL, shell, or devices.
struct ControlRequest {
    std::string id, boot, command;
    std::int64_t revision{}, expires{};
};
void initialize_control(const std::filesystem::path& directory);
void poll_control(const std::filesystem::path& directory,
                  const std::function<std::string(const ControlRequest&)>& apply);
std::string send_control(const std::filesystem::path& directory, const std::string& boot,
                         std::int64_t revision, const std::string& command);
bool control_command(const std::string& command);
}
