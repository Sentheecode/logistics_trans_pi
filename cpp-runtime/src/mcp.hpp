#pragma once

#include <filesystem>
#include <optional>
#include <string_view>
#include <nlohmann/json.hpp>

namespace mcp {
inline constexpr std::size_t max_request = 16 * 1024;
inline constexpr auto version = "2025-11-25";
using Json = nlohmann::json;

enum class Phase { new_connection, initialized, ready };
std::optional<Json> process(const std::filesystem::path& directory, std::string_view request,
                            Phase* phase);
bool supported_version(std::string_view value);
}
