#pragma once
#include <optional>
#include <string>
#include <vector>

namespace catalog {
// Software configuration ids. They are not PLC models, IPs, or protocol endpoints.
inline constexpr const char* kStream = "sim.three-cart.v1";
inline constexpr const char* kFactory = "fac.qiantang-2";
inline constexpr const char* kAreaPick = "area.pick";
inline constexpr const char* kAreaSeq = "area.seq";
inline constexpr const char* kPlcPack = "plc.cfg.pack";
inline constexpr const char* kPlcMeter1 = "plc.cfg.meter1";
inline constexpr const char* kPlcMeter2 = "plc.cfg.meter2";
inline constexpr const char* kPlcSeq1 = "plc.cfg.seq1";
inline constexpr const char* kPlcSeq2 = "plc.cfg.seq2";
inline constexpr const char* kPlcSeq3 = "plc.cfg.seq3";
inline constexpr const char* kPlcSeq4 = "plc.cfg.seq4";
inline constexpr const char* kStation140 = "stn.140";
inline constexpr const char* kStation150 = "stn.150";
inline constexpr const char* kStation160 = "stn.160";
inline constexpr const char* kStation20 = "stn.20";
inline constexpr const char* kStation30 = "stn.30";
inline constexpr const char* kStation90 = "stn.90";
inline constexpr const char* kStation40 = "stn.40";
inline constexpr const char* kStation10 = "stn.10";
inline constexpr const char* kStation70 = "stn.70";
inline constexpr const char* kStation80 = "stn.80";
inline constexpr const char* kStation210 = "stn.210";
inline constexpr const char* kStation220 = "stn.220";

struct Factory { std::string id, code, name; };
struct Area { std::string id, factory_id, code, name; };
struct Plc { std::string id, area_id, code, name; };
struct Station { std::string id, plc_id, code, name, stream; };
struct Line { std::string id, area_id, name, storage, zone; };
struct LineLink { std::string line_id, station_id; };

struct Selection {
    std::string factory_id, area_id, plc_id, station_id, line_id{};
    bool operator==(const Selection& other) const {
        return factory_id==other.factory_id && area_id==other.area_id && plc_id==other.plc_id && station_id==other.station_id && line_id==other.line_id;
    }
};

// Directory of confirmed station membership. It does not collect, write, or infer line scope.
class Directory {
public:
    std::vector<Factory> factories;
    std::vector<Area> areas;
    std::vector<Plc> plcs;
    std::vector<Station> stations;
    std::vector<Line> lines;
    std::vector<LineLink> line_links;

    static Directory qiantang();
    static std::string validate(const Directory& directory);

    Selection live_selection() const;
    Selection reconcile(Selection proposed) const;
    std::optional<Selection> selection_for_station(const std::string& station_id) const;
    std::optional<Selection> selection_for_plc(const std::string& plc_id) const;
    std::string live_station_id() const;
    std::string empty_plc_id() const;

    const Line* find_line(const std::string& id) const;
    const Line* line_of_station(const std::string& station_id) const;
    std::vector<const Line*> lines_of(const std::string& area_id) const;
    std::vector<const Station*> stations_on_line(const std::string& line_id) const;
    const Factory* find_factory(const std::string& id) const;
    const Area* find_area(const std::string& id) const;
    const Plc* find_plc(const std::string& id) const;
    const Station* find_station(const std::string& id) const;
    std::vector<const Area*> areas_of(const std::string& factory_id) const;
    std::vector<const Plc*> plcs_of(const std::string& area_id) const;
    std::vector<const Station*> stations_of(const std::string& plc_id) const;
    const Plc* plc_of_station(const std::string& station_id) const;
    const Area* area_of_station(const std::string& station_id) const;
    const Factory* factory_of_station(const std::string& station_id) const;
    bool is_live(const std::string& station_id) const;
};
}
