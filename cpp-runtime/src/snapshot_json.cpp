#include "snapshot_json.hpp"
#include "catalog.hpp"
#include <locale>
#include <sstream>

namespace sim {
namespace {
std::string quote(const std::string& value) {
    constexpr char hex[]="0123456789abcdef";
    std::string result="\"";
    for (unsigned char c : value) {
        if (c=='"' || c=='\\') {
            result+='\\';
            result+=static_cast<char>(c);
        } else if (c<32) {
            result+="\\u00";
            result+=hex[c>>4];
            result+=hex[c&15];
        } else {
            result+=static_cast<char>(c);
        }
    }
    return result+'"';
}
std::string nullable_stamp(std::int64_t value) {
    return value==0 ? "null" : std::to_string(value);
}
}

std::string snapshot_json(const View& v,int limit,std::optional<TimeWindow> window) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const auto& runtime=v.runtime;
    const bool current=!window && !v.rows.empty() && v.freshness=="recent" &&
        runtime.liveness=="recent" && v.rows.front().boot==runtime.boot &&
        v.rows.front().quality=="good" && v.rows.front().link=="up";
    out<<"{\"schema\":\"ltp.cpp.sim-snapshot.v1\",\"simulation_only\":true"
       <<",\"scope\":{\"stream_id\":"<<quote(catalog::kStream)
       <<",\"station_id\":"<<quote(catalog::kStation140)
       <<",\"log_scope\":\"single_simulation_runtime_including_local_audit\"}"
       <<",\"coverage\":\"unknown\",\"order\":\"sequence_descending\",\"query\":"
       <<quote(window?"retained_receive_window":"retained_tail");
    if (window) out<<",\"window\":{\"basis\":\"received_utc_ms\",\"from_inclusive\":"
                   <<window->from_ms<<",\"to_exclusive\":"<<window->to_ms<<'}';
    out
       <<",\"limit\":"<<limit<<",\"frame_freshness\":"<<quote(v.freshness)
       <<",\"current_frame_usable\":"<<(current?"true":"false")
       <<",\"frame_has_more\":"<<(v.more?"true":"false")
       <<",\"log_availability\":"<<quote(v.logs_available?"available":"unsupported_schema")
       <<",\"log_has_more\":";
    if (!v.logs_available) out<<"null";
    else out<<(v.logs.size()>static_cast<std::size_t>(limit)?"true":"false");
    out<<",\"runtime\":{\"mode\":"<<quote(runtime.mode)<<",\"liveness\":"<<quote(runtime.liveness)
       <<",\"boot\":"<<quote(runtime.boot)<<",\"revision\":"<<quote(std::to_string(runtime.revision))
       <<",\"heartbeat_utc_ms\":"<<nullable_stamp(runtime.heartbeat_ms)<<"},\"frames\":[";
    bool first=true;
    for (const auto& row:v.rows) {
        if (!first) out<<',';
        first=false;
        out<<"{\"seq\":"<<quote(std::to_string(row.seq))<<",\"boot\":"<<quote(row.boot)
           <<",\"received_utc_ms\":"<<row.received_ms<<",\"source_utc_ms\":"<<nullable_stamp(row.source_ms)
           <<",\"quality\":"<<quote(row.quality)<<",\"reason\":"<<quote(row.reason)
           <<",\"link\":"<<quote(row.link)<<",\"alarm_condition\":"<<quote(row.alarm)
           <<",\"event\":"<<quote(row.event)<<",\"carts\":[";
        for (int i=0;i<3;++i) {
            if (i!=0) out<<',';
            out<<(row.carts[i]=="UNKNOWN"?"null":quote(row.carts[i]));
        }
        out<<"]}";
    }
    out<<"],\"logs\":[";
    for (std::size_t i=0;i<v.logs.size() && i<static_cast<std::size_t>(limit);++i) {
        if (i!=0) out<<',';
        const auto& row=v.logs[i];
        out<<"{\"id\":"<<quote(std::to_string(row.id))<<",\"received_utc_ms\":"<<row.received_ms
           <<",\"level\":"<<quote(row.level)<<",\"category\":"<<quote(row.category)
           <<",\"object\":"<<quote(row.object)<<",\"text\":"<<quote(row.text)
           <<",\"frame_seq\":"<<(row.has_frame?quote(std::to_string(row.frame_seq)):"null")<<'}';
    }
    out<<"]}\n";
    return out.str();
}
}
