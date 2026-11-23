#pragma once
#include <optional>
#include <string>
#include <vector>

namespace catalog {
// Synthetic directory only. These ids are not plant, PLC, or protocol identifiers.
inline constexpr const char* kStream = "sim.three-cart.v1";
inline constexpr const char* kFactoryNorth = "fac.north";
inline constexpr const char* kFactorySouth = "fac.south";
inline constexpr const char* kFactoryEmpty = "fac.empty";
inline constexpr const char* kModeNorthSps = "bm.north.sps";
inline constexpr const char* kModeNorthSort = "bm.north.sort";
inline constexpr const char* kModeSouthSps = "bm.south.sps";
inline constexpr const char* kModeSouthSort = "bm.south.sort";
inline constexpr const char* kLineNorthSps = "ln.north.sps.a";
inline constexpr const char* kLineNorthEmpty = "ln.north.sps.empty";
inline constexpr const char* kLineNorthSort = "ln.north.sort.b";
inline constexpr const char* kLineSouthSps = "ln.south.sps.c";
inline constexpr const char* kLineSouthSort = "ln.south.sort.d";
inline constexpr const char* kStationA1 = "stn.north.sps.a1";
inline constexpr const char* kStationA2 = "stn.north.sps.a2";
inline constexpr const char* kStationA3 = "stn.north.sps.a3";
inline constexpr const char* kStationB1 = "stn.north.sort.b1";
inline constexpr const char* kStationB2 = "stn.north.sort.b2";
inline constexpr const char* kStationC1 = "stn.south.sps.c1";
inline constexpr const char* kStationD1 = "stn.south.sort.d1";
inline constexpr const char* kStationD2 = "stn.south.sort.d2";
inline constexpr const char* kControllerAlpha = "ctl.alpha";
inline constexpr const char* kControllerBeta = "ctl.beta";
inline constexpr const char* kControllerGamma = "ctl.gamma";
inline constexpr const char* kZoneRack = "zone.north.rack";
inline constexpr const char* kZoneSort = "zone.north.sortbin";
inline constexpr const char* kZoneSouth = "zone.south.rack";

struct Factory { std::string id, code, name; };
struct BusinessMode { std::string id, factory_id, code, name; };
struct Line { std::string id, business_mode_id, code, name; };
struct Station { std::string id, line_id, code, name, stream; };
struct Controller { std::string id, code, name; };
struct Zone { std::string id, code, name; };
struct Location { std::string id, zone_id, code, name; };
struct ControllerStation { std::string controller_id, station_id; };
struct ZoneStation { std::string zone_id, station_id; };

struct Selection {
    std::string factory_id, business_mode_id, line_id, station_id;
    bool operator==(const Selection& other) const {
        return factory_id==other.factory_id && business_mode_id==other.business_mode_id
            && line_id==other.line_id && station_id==other.station_id;
    }
};

// Presentation catalog. It does not own collector mode, history, or device I/O.
class Directory {
public:
    std::vector<Factory> factories;
    std::vector<BusinessMode> business_modes;
    std::vector<Line> lines;
    std::vector<Station> stations;
    std::vector<Controller> controllers;
    std::vector<Zone> zones;
    std::vector<Location> locations;
    std::vector<ControllerStation> controller_stations;
    std::vector<ZoneStation> zone_stations;

    static Directory synthetic();
    // Empty when the directory is internally consistent. Tokens are stable for tests.
    static std::string validate(const Directory& directory);

    Selection live_selection() const;
    Selection reconcile(Selection proposed) const;
    std::optional<Selection> selection_for_station(const std::string& station_id) const;
    std::string live_station_id() const;
    std::string empty_line_id() const;

    const Factory* find_factory(const std::string& id) const;
    const BusinessMode* find_mode(const std::string& id) const;
    const Line* find_line(const std::string& id) const;
    const Station* find_station(const std::string& id) const;
    const Controller* find_controller(const std::string& id) const;
    const Zone* find_zone(const std::string& id) const;

    std::vector<const BusinessMode*> modes_of(const std::string& factory_id) const;
    std::vector<const Line*> lines_of(const std::string& mode_id) const;
    std::vector<const Station*> stations_of(const std::string& line_id) const;
    std::vector<const Station*> stations_of_controller(const std::string& controller_id) const;
    std::vector<const Location*> locations_of_zone(const std::string& zone_id) const;
    const Controller* controller_of_station(const std::string& station_id) const;
    const Zone* zone_of_station(const std::string& station_id) const;
    const Line* line_of_station(const std::string& station_id) const;
    const BusinessMode* mode_of_station(const std::string& station_id) const;
    const Factory* factory_of_station(const std::string& station_id) const;
    bool is_live(const std::string& station_id) const;
};
}
