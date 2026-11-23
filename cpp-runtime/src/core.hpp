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
struct View { std::vector<Row> rows; bool more{}; std::string freshness; };
class Writer final : public HistorySink {
public:
    explicit Writer(const std::filesystem::path& directory);
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    void append(const Observation& observation) override;
private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
View read(const std::filesystem::path& directory, const std::string& command, int limit=20);
std::string render(const View& view);
std::string help();
}
