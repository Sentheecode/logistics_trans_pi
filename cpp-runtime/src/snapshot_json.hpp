#pragma once
#include "core.hpp"
#include <optional>

namespace sim {
constexpr int snapshot_limit=40;
std::string snapshot_json(const View& view, int limit, std::optional<TimeWindow> window={});
}
