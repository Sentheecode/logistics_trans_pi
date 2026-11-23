#pragma once
#include "adapters.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace sim {
std::int64_t now_ms();
int number(const std::string& s, int lo, int hi);
struct Row {
    std::int64_t seq{}, received_ms{}, source_ms{};
    std::string boot, quality, reason, link, alarm, event;
    std::string carts[3];
};
struct RuntimeInfo {
    std::string mode="unknown", boot, liveness="unavailable";
    std::int64_t revision{}, heartbeat_ms{};
};
struct View { std::vector<Row> rows; bool more{}; std::string freshness; RuntimeInfo runtime; };
class Writer final : public HistorySink {
public:
    explicit Writer(const std::filesystem::path& directory);
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    void append(const Observation& observation) override;
    void publish(const Observation* observation, const std::string& mode, bool command=false);
    RuntimeInfo info() const;
    int last_phase() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
View read(const std::filesystem::path& directory, const std::string& command, int limit=20);
std::string render(const View& view);
std::string help();
}
