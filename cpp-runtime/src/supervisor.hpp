#pragma once
#include <csignal>
#include <functional>

namespace sim {
// Pulse is valid only inside the owned child; call after a committed heartbeat.
using Pulse = std::function<void()>;
int supervise_collector(const std::function<int(const Pulse&)>& run, const volatile std::sig_atomic_t& stopping);
}
