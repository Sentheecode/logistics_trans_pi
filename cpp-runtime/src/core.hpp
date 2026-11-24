#pragma once
#include "adapters.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
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
struct LogRecord {
    std::int64_t id{}, received_ms{}, frame_seq{};
    bool has_frame=false;
    std::string level, category, object, text;
};
struct TimeWindow { std::int64_t from_ms, to_ms; };
struct EventBucket {
    std::int64_t start_ms{}, samples{}, good{}, bad{}, uncertain{}, unknown_quality{};
    std::int64_t source{}, server{}, received{}, complete{}, active_alarm{};
};
std::vector<EventBucket> event_buckets(const std::filesystem::path& directory,
                                      TimeWindow window, int bucket_ms);
struct LogRetention { std::int64_t evicted{}, evicted_through_id{}; };
struct View { std::vector<Row> rows; std::vector<LogRecord> logs; std::optional<LogRetention> log_retention; bool more{}, logs_available{}; std::string freshness; RuntimeInfo runtime; };
class Writer final : public HistorySink {
public:
    explicit Writer(const std::filesystem::path& directory);
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    void append(const Observation& observation) override;
    void publish(const Observation* observation, const std::string& mode, bool command=false, const std::string& audit_text={});
    void note(const std::string& level, const std::string& category, const std::string& object, const std::string& text, std::int64_t frame_seq=-1);
    RuntimeInfo info() const;
    int last_phase() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
enum class LogDetail { full, metadata, none };
View read(const std::filesystem::path& directory, const std::string& command, int limit=20,
          LogDetail log_detail=LogDetail::full);
View read_window(const std::filesystem::path& directory, TimeWindow window, int limit,
                 LogDetail log_detail=LogDetail::full);
bool current_frame_usable(const View& view);
std::string render(const View& view);
std::string help();
}
