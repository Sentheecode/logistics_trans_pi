#include "mcp.hpp"
#include "catalog.hpp"
#include "core.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <vector>

namespace mcp {
namespace {
using std::string_view;
constexpr std::int64_t max_utc_ms = 253402300799999;
constexpr int max_rows = 40;
constexpr auto station = catalog::kStation140;

Json rpc_error(const Json& id, int code, const char* message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

Json response(const Json& id, Json result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

Json enum_value(const std::string& value, std::initializer_list<string_view> allowed) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end() ? Json(value) : Json(nullptr);
}

Json timestamp(std::int64_t value) {
    return value ? Json(value) : Json(nullptr);
}

Json frame(const sim::Row& row) {
    Json carts = Json::array();
    for (const auto& state : row.carts) {
        carts.push_back(row.quality == "good"
            ? enum_value(state, {"waiting", "scanning", "sorting", "blocked", "station_complete"})
            : Json(nullptr));
    }
    return {{"sequence", std::to_string(row.seq)}, {"received_utc_ms", row.received_ms},
            {"source_utc_ms", timestamp(row.source_ms)},
            {"quality", enum_value(row.quality, {"good", "bad", "uncertain"})},
            {"link", enum_value(row.link, {"up", "down"})},
            {"alarm_condition", enum_value(row.alarm, {"active", "cleared", "unknown"})},
            {"carts", std::move(carts)}};
}

Json schema(bool range, bool buckets) {
    Json properties = {{"station", {{"type", "string"}, {"enum", Json::array({station})}}}};
    if (range) {
        properties["limit"] = {{"type", "integer"}, {"minimum", 1}, {"maximum", max_rows}};
        const Json stamp = {{"type", "integer"}, {"minimum", 0}, {"maximum", max_utc_ms}};
        properties["from_utc_ms"] = stamp;
        properties["to_utc_ms"] = stamp;
    }
    if (buckets)
        properties["bucket_ms"] = {{"type", "integer"}, {"enum", {1000, 60000, 3600000}}};
    return {{"type", "object"}, {"properties", std::move(properties)},
            {"required", Json::array({"station"})}, {"additionalProperties", false}};
}

Json tool_list() {
    const auto tool = [](const char* name, const char* description, Json input) {
        return Json{{"name", name}, {"description", description}, {"inputSchema", std::move(input)},
                    {"annotations", {{"readOnlyHint", true}, {"openWorldHint", false}}}};
    };
    return {{"tools", Json::array({
        tool("status", "Read the simulation collector heartbeat and latest observed frame.", schema(false, false)),
        tool("alarms", "Read the latest observed simulation alarm condition and current-evidence flag.", schema(false, false)),
        tool("logs", "Read redacted simulation log metadata. Set both UTC bounds for a receive-time window.", schema(true, false)),
        tool("history", "Read simulation frames. Set both UTC bounds for a receive-time window, or also set bucket_ms for event-time counts.", schema(true, true))
    })}};
}

struct InvalidArguments {};
struct ScopeDenied {};

std::int64_t integer(const Json& value, std::int64_t low, std::int64_t high) {
    if (!value.is_number_integer() || (value.is_number_unsigned() && value > Json(high)))
        throw InvalidArguments{};
    const auto result = value.get<std::int64_t>();
    if (result < low || result > high) throw InvalidArguments{};
    return result;
}

Json read_tool(const std::filesystem::path& dir, const std::string& name, const Json& args) {
    if (!args.is_object() || !args.contains("station") || !args["station"].is_string())
        throw InvalidArguments{};
    if (args["station"] != station) throw ScopeDenied{};
    const bool range = name == "logs" || name == "history";
    for (auto it = args.begin(); it != args.end(); ++it) {
        const auto& key = it.key();
        if (key == "station") continue;
        if (range && (key == "limit" || key == "from_utc_ms" || key == "to_utc_ms")) continue;
        if (name == "history" && key == "bucket_ms") continue;
        throw InvalidArguments{};
    }

    const auto limit = static_cast<int>(args.contains("limit") ? integer(args["limit"], 1, max_rows) : 10);
    std::optional<sim::TimeWindow> window;
    if (args.contains("from_utc_ms") != args.contains("to_utc_ms")) throw InvalidArguments{};
    if (args.contains("from_utc_ms")) {
        const auto from = integer(args["from_utc_ms"], 0, max_utc_ms);
        const auto to = integer(args["to_utc_ms"], 0, max_utc_ms);
        if (to <= from || to - from > 86400000) throw InvalidArguments{};
        window = sim::TimeWindow{from, to};
    }
    Json result = {{"station", station}, {"simulation_only", true}, {"coverage", "unknown"}};
    if (args.contains("bucket_ms")) {
        const auto width = static_cast<int>(integer(args["bucket_ms"], 1000, 3600000));
        if (!window || args.contains("limit") || (width != 1000 && width != 60000 && width != 3600000)
            || window->from_ms % width || window->to_ms % width
            || (window->to_ms - window->from_ms) / width > 120)
            throw InvalidArguments{};
        result["window"] = {{"from_inclusive", window->from_ms}, {"to_exclusive", window->to_ms},
                            {"basis", "event_utc_ms"}};
        result["bucket_ms"] = width;
        auto& buckets = result["buckets"] = Json::array();
        for (const auto& b : sim::event_buckets(dir, *window, width)) {
            buckets.push_back({{"start_utc_ms", b.start_ms}, {"samples", b.samples}, {"good", b.good},
                               {"bad", b.bad}, {"uncertain", b.uncertain}, {"unknown_quality", b.unknown_quality},
                               {"source_time", b.source}, {"server_time", b.server}, {"received_time", b.received},
                               {"good_complete_frames", b.complete}, {"good_active_alarm_frames", b.active_alarm}});
        }
        return result;
    }

    const auto detail = name == "logs" ? sim::LogDetail::metadata : sim::LogDetail::none;
    const auto view = window ? sim::read_window(dir, *window, limit, detail)
        : sim::read(dir, range ? "history" : name, limit, detail);
    if (window) result["window"] = {{"from_inclusive", window->from_ms}, {"to_exclusive", window->to_ms},
                                     {"basis", "received_utc_ms"}};
    if (name == "status") {
        result["collector"] = {{"heartbeat_utc_ms", timestamp(view.runtime.heartbeat_ms)},
                               {"liveness", view.runtime.liveness},
                               {"mode", enum_value(view.runtime.mode, {"manual", "automatic"})}};
        result["frame_freshness"] = view.freshness;
        result["current_frame_usable"] = sim::current_frame_usable(view);
        result["latest_observed"] = view.rows.empty() ? Json(nullptr) : frame(view.rows.front());
    } else if (name == "alarms") {
        result["current_evidence"] = sim::current_frame_usable(view);
        result["latest_observed"] = nullptr;
        if (!view.rows.empty()) {
            const auto& row = view.rows.front();
            result["latest_observed"] = {{"condition", enum_value(row.alarm, {"active", "cleared", "unknown"})},
                                         {"received_utc_ms", row.received_ms}};
        }
    } else if (name == "logs") {
        result["availability"] = view.logs_available ? "available" : "unsupported_schema";
        result["has_more_retained"] = view.logs_available ? Json(view.logs.size() > static_cast<std::size_t>(limit)) : Json(nullptr);
        result["retention"] = nullptr;
        if (view.log_retention) {
            const auto& log = *view.log_retention;
            result["retention"] = {{"evicted_since_v4", std::to_string(log.evicted)},
                                   {"evicted_through_id", log.evicted_through_id
                                       ? Json(std::to_string(log.evicted_through_id)) : Json(nullptr)}};
        }
        auto& logs = result["logs"] = Json::array();
        for (auto i = std::size_t{}; i < view.logs.size() && i < static_cast<std::size_t>(limit); ++i) {
            const auto& log = view.logs[i];
            logs.push_back({{"id", std::to_string(log.id)}, {"received_utc_ms", log.received_ms},
                            {"level", enum_value(log.level, {"info", "warn"})},
                            {"category", enum_value(log.category, {"audit", "business", "collector"})},
                            {"frame_sequence", log.has_frame ? Json(std::to_string(log.frame_seq)) : Json(nullptr)}});
        }
    } else {
        result["has_more_retained"] = view.more;
        auto& frames = result["frames"] = Json::array();
        for (const auto& row : view.rows) frames.push_back(frame(row));
    }
    return result;
}

Json tool_result(Json data, bool error) {
    return {{"isError", error}, {"structuredContent", data},
            {"content", Json::array({{{"type", "text"}, {"text", data.dump()}}})}};
}

Json call(const std::filesystem::path& dir, const Json& params) {
    if (!params.contains("name") || !params["name"].is_string() || params.size() > 2
        || (params.size() == 2 && !params.contains("arguments"))
        || (params.contains("arguments") && !params["arguments"].is_object()))
        throw InvalidArguments{};
    const auto name = params["name"].get<std::string>();
    if (name != "status" && name != "alarms" && name != "logs" && name != "history")
        throw InvalidArguments{};
    try {
        return tool_result(read_tool(dir, name, params.value("arguments", Json::object())), false);
    } catch (const ScopeDenied&) {
        return tool_result({{"error", "scope_denied"}}, true);
    } catch (const InvalidArguments&) {
        return tool_result({{"error", "invalid_tool_arguments"}}, true);
    } catch (const std::exception&) {
        return tool_result({{"error", "evidence_unavailable"}}, true);
    }
}

Json parse(std::string_view request) {
    if (request.size() > max_request) return Json::value_t::discarded;
    std::vector<std::set<std::string>> keys;
    return Json::parse(request, [&keys](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 24) throw InvalidArguments{};
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        if (event == Json::parse_event_t::object_end) keys.pop_back();
        if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            throw InvalidArguments{};
        return true;
    }, false);
}
}

bool supported_version(std::string_view value) {
    return value == version || value == "2025-06-18" || value == "2025-03-26";
}

std::optional<Json> process(const std::filesystem::path& directory, std::string_view request, Phase* phase) {
    Json message;
    try {
        message = parse(request);
    } catch (const InvalidArguments&) {
        return rpc_error(nullptr, -32700, "Invalid JSON input");
    }
    if (message.is_discarded()) return rpc_error(nullptr, -32700, "Invalid JSON input");
    if (!message.is_object() || message.value("jsonrpc", Json()) != "2.0"
        || !message.contains("method") || !message["method"].is_string())
        return rpc_error(nullptr, -32600, "Invalid request");
    const auto id = message.find("id");
    const auto method = message["method"].get<std::string>();
    const auto params = message.value("params", Json::object());
    if (id == message.end()) {
        if (method == "notifications/initialized" && phase && *phase == Phase::initialized)
            *phase = Phase::ready;
        return std::nullopt;
    }
    if (!id->is_string() && !id->is_number_integer())
        return rpc_error(nullptr, -32600, "Invalid request ID");
    if (!params.is_object()) return rpc_error(*id, -32602, "Invalid parameters");
    if (method == "ping") return response(*id, Json::object());
    if (method == "initialize") {
        if (phase && *phase != Phase::new_connection) return rpc_error(*id, -32600, "Already initialized");
        if (!params.contains("protocolVersion") || !params["protocolVersion"].is_string()
            || !params.contains("capabilities") || !params["capabilities"].is_object()
            || !params.contains("clientInfo") || !params["clientInfo"].is_object())
            return rpc_error(*id, -32602, "Invalid initialize parameters");
        const auto requested = params["protocolVersion"].get<std::string>();
        if (phase) *phase = Phase::initialized;
        return response(*id, {{"protocolVersion", supported_version(requested) ? requested : version},
                              {"serverInfo", {{"name", "logistics-diagnostics"}, {"version", "0.1.0"}}},
                              {"capabilities", {{"tools", Json::object()}}}});
    }
    if (phase && *phase != Phase::ready) return rpc_error(*id, -32002, "Initialization required");
    if (method == "tools/list") {
        if (!params.empty()) return rpc_error(*id, -32602, "List is not paginated");
        return response(*id, tool_list());
    }
    if (method == "tools/call") {
        try {
            return response(*id, call(directory, params));
        } catch (const InvalidArguments&) {
            return rpc_error(*id, -32602, "Invalid tool or arguments");
        }
    }
    return rpc_error(*id, -32601, "Method not found");
}
}
