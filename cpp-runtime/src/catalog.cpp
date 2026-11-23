#include "catalog.hpp"
#include <algorithm>
#include <cctype>

namespace catalog {
namespace {
template <class T>
const T* by_id(const std::vector<T>& rows, const std::string& id) {
    for (auto& row : rows) if (row.id==id) return &row;
    return nullptr;
}
}

Directory Directory::synthetic() {
    Directory directory;
    directory.factories = {
        {kFactoryNorth, "NORTH", "北岸合成厂"},
        {kFactorySouth, "SOUTH", "南湾合成厂"},
        {kFactoryEmpty, "EMPTY", "空目录厂"},
    };
    directory.business_modes = {
        {kModeNorthSps, kFactoryNorth, "sps", "SPS"},
        {kModeNorthSort, kFactoryNorth, "sort", "排序区"},
        {kModeSouthSps, kFactorySouth, "sps", "SPS"},
        {kModeSouthSort, kFactorySouth, "sort", "排序区"},
    };
    directory.lines = {
        {kLineNorthSps, kModeNorthSps, "A", "合成线甲"},
        {kLineNorthEmpty, kModeNorthSps, "E", "空线"},
        {kLineNorthSort, kModeNorthSort, "B", "排序线乙"},
        {kLineSouthSps, kModeSouthSps, "C", "合成线丙"},
        {kLineSouthSort, kModeSouthSort, "D", "排序线丁"},
    };
    directory.stations = {
        {kStationA1, kLineNorthSps, "A1", "示意工位甲", kStream},
        {kStationA2, kLineNorthSps, "A2", "示意工位乙", ""},
        {kStationA3, kLineNorthSps, "A3", "示意工位丙", ""},
        {kStationB1, kLineNorthSort, "B1", "排序工位丁", ""},
        {kStationB2, kLineNorthSort, "B2", "排序工位戊", ""},
        {kStationC1, kLineSouthSps, "C1", "示意工位甲", ""},
        {kStationD1, kLineSouthSort, "D1", "排序工位己", ""},
        {kStationD2, kLineSouthSort, "D2", "排序工位庚", ""},
    };
    directory.controllers = {
        {kControllerAlpha, "ALPHA", "合成控制器甲"},
        {kControllerBeta, "BETA", "合成控制器乙"},
        {kControllerGamma, "GAMMA", "合成控制器丙"},
    };
    directory.controller_stations = {
        {kControllerAlpha, kStationA1},
        {kControllerAlpha, kStationA2},
        {kControllerAlpha, kStationA3},
        {kControllerBeta, kStationB1},
        {kControllerBeta, kStationB2},
        {kControllerGamma, kStationC1},
        {kControllerGamma, kStationD1},
    };
    directory.zones = {
        {kZoneRack, "RACK", "合成库区甲"},
        {kZoneSort, "SORTBIN", "合成库区乙"},
        {kZoneSouth, "SRACK", "合成库区丙"},
    };
    directory.zone_stations = {
        {kZoneRack, kStationA1},
        {kZoneRack, kStationA2},
        {kZoneSort, kStationB1},
        {kZoneSouth, kStationD1},
    };
    directory.locations = {
        {"loc.north.rack.1", kZoneRack, "L1", "示意库位一"},
        {"loc.north.rack.2", kZoneRack, "L2", "示意库位二"},
    };
    return directory;
}

std::string Directory::validate(const Directory& directory) {
    std::vector<std::string> ids;
    auto take_id = [&](const std::string& id) -> std::string {
        if (id.empty()) return "empty-id";
        for (unsigned char c : id) {
            if (!(std::islower(c) || std::isdigit(static_cast<unsigned char>(c)) || c=='.' || c=='-')) return "id-charset";
        }
        if (std::find(ids.begin(), ids.end(), id)!=ids.end()) return "duplicate-id";
        ids.push_back(id);
        return {};
    };
    auto take_name = [](const std::string& name) -> std::string {
        return name.empty() ? "empty-name" : "";
    };
    std::vector<std::string> names;
    auto consume = [&](const std::string& id, const std::string& code, const std::string& name) -> std::string {
        if (auto error=take_id(id); !error.empty()) return error;
        if (code.empty()) return "empty-code";
        if (auto error=take_name(name); !error.empty()) return error;
        names.push_back(name);
        return {};
    };
    for (auto& row : directory.factories) if (auto error=consume(row.id, row.code, row.name); !error.empty()) return error;
    for (auto& row : directory.business_modes) if (auto error=consume(row.id, row.code, row.name); !error.empty()) return error;
    for (auto& row : directory.lines) if (auto error=consume(row.id, row.code, row.name); !error.empty()) return error;
    for (auto& row : directory.stations) if (auto error=consume(row.id, row.code, row.name); !error.empty()) return error;
    for (auto& row : directory.controllers) if (auto error=consume(row.id, row.code, row.name); !error.empty()) return error;
    for (auto& row : directory.zones) if (auto error=consume(row.id, row.code, row.name); !error.empty()) return error;
    for (auto& row : directory.locations) if (auto error=consume(row.id, row.code, row.name); !error.empty()) return error;
    for (auto& name : names) if (std::find(ids.begin(), ids.end(), name)!=ids.end()) return "name-collides-with-id";

    std::vector<std::string> codes;
    auto take_code = [&](const std::string& key) -> std::string {
        if (std::find(codes.begin(), codes.end(), key)!=codes.end()) return "duplicate-code";
        codes.push_back(key);
        return {};
    };
    for (auto& row : directory.factories) if (auto error=take_code("factory:"+row.code); !error.empty()) return error;
    for (auto& row : directory.business_modes) {
        if (!by_id(directory.factories, row.factory_id)) return "dangling-ref";
        if (row.code=="manual" || row.code=="automatic") return "runtime-code-as-business";
        if (row.code!="sps" && row.code!="sort") return "business-code";
        if (auto error=take_code("mode:"+row.factory_id+":"+row.code); !error.empty()) return error;
    }
    for (auto& row : directory.lines) {
        if (!by_id(directory.business_modes, row.business_mode_id)) return "dangling-ref";
        if (auto error=take_code("line:"+row.business_mode_id+":"+row.code); !error.empty()) return error;
    }
    int live=0;
    for (auto& row : directory.stations) {
        if (!by_id(directory.lines, row.line_id)) return "dangling-ref";
        if (!row.stream.empty()) {
            if (row.stream!=kStream) return "stream";
            ++live;
        }
        if (auto error=take_code("station:"+row.line_id+":"+row.code); !error.empty()) return error;
    }
    if (live>1) return "multiple-live";
    for (auto& row : directory.controllers) if (auto error=take_code("controller:"+row.code); !error.empty()) return error;
    for (auto& row : directory.zones) if (auto error=take_code("zone:"+row.code); !error.empty()) return error;

    std::vector<std::string> pairs;
    std::vector<std::string> station_controller;
    for (auto& link : directory.controller_stations) {
        if (!by_id(directory.controllers, link.controller_id) || !by_id(directory.stations, link.station_id)) return "dangling-ref";
        auto pair=link.controller_id+"|"+link.station_id;
        if (std::find(pairs.begin(), pairs.end(), pair)!=pairs.end()) return "duplicate-relation";
        pairs.push_back(pair);
        if (std::find(station_controller.begin(), station_controller.end(), link.station_id)!=station_controller.end()) return "station-two-controllers";
        station_controller.push_back(link.station_id);
    }
    std::vector<std::string> station_zone;
    for (auto& link : directory.zone_stations) {
        if (!by_id(directory.zones, link.zone_id) || !by_id(directory.stations, link.station_id)) return "dangling-ref";
        auto pair=link.zone_id+"|"+link.station_id;
        if (std::find(pairs.begin(), pairs.end(), pair)!=pairs.end()) return "duplicate-relation";
        pairs.push_back(pair);
        if (std::find(station_zone.begin(), station_zone.end(), link.station_id)!=station_zone.end()) return "station-two-zones";
        station_zone.push_back(link.station_id);
    }
    for (auto& row : directory.locations) if (!by_id(directory.zones, row.zone_id)) return "dangling-ref";
    return {};
}

Selection Directory::live_selection() const {
    for (auto& station : stations) if (station.stream==kStream) {
        if (auto selection=selection_for_station(station.id)) return *selection;
    }
    return {};
}

Selection Directory::reconcile(Selection proposed) const {
    Selection resolved;
    if (factories.empty()) return resolved;
    auto* factory=find_factory(proposed.factory_id);
    if (!factory) factory=&factories.front();
    resolved.factory_id=factory->id;
    auto modes=modes_of(resolved.factory_id);
    if (modes.empty()) return resolved;
    const BusinessMode* mode=nullptr;
    for (auto* item : modes) if (item->id==proposed.business_mode_id) mode=item;
    if (!mode) mode=modes.front();
    resolved.business_mode_id=mode->id;
    auto line_rows=lines_of(resolved.business_mode_id);
    if (line_rows.empty()) return resolved;
    const Line* line=nullptr;
    for (auto* item : line_rows) if (item->id==proposed.line_id) line=item;
    if (!line) line=line_rows.front();
    resolved.line_id=line->id;
    auto station_rows=stations_of(resolved.line_id);
    if (station_rows.empty()) return resolved;
    const Station* station=nullptr;
    for (auto* item : station_rows) if (item->id==proposed.station_id) station=item;
    if (!station) station=station_rows.front();
    resolved.station_id=station->id;
    return resolved;
}

std::optional<Selection> Directory::selection_for_station(const std::string& station_id) const {
    auto* station=find_station(station_id);
    auto* line=station?find_line(station->line_id):nullptr;
    auto* mode=line?find_mode(line->business_mode_id):nullptr;
    auto* factory=mode?find_factory(mode->factory_id):nullptr;
    if (!factory) return std::nullopt;
    return Selection{factory->id, mode->id, line->id, station->id};
}

std::string Directory::live_station_id() const {
    for (auto& station : stations) if (station.stream==kStream) return station.id;
    return {};
}

std::string Directory::empty_line_id() const {
    for (auto& line : lines) if (stations_of(line.id).empty()) return line.id;
    return {};
}

const Factory* Directory::find_factory(const std::string& id) const { return by_id(factories, id); }
const BusinessMode* Directory::find_mode(const std::string& id) const { return by_id(business_modes, id); }
const Line* Directory::find_line(const std::string& id) const { return by_id(lines, id); }
const Station* Directory::find_station(const std::string& id) const { return by_id(stations, id); }
const Controller* Directory::find_controller(const std::string& id) const { return by_id(controllers, id); }
const Zone* Directory::find_zone(const std::string& id) const { return by_id(zones, id); }

std::vector<const BusinessMode*> Directory::modes_of(const std::string& factory_id) const {
    std::vector<const BusinessMode*> out;
    for (auto& row : business_modes) if (row.factory_id==factory_id) out.push_back(&row);
    return out;
}
std::vector<const Line*> Directory::lines_of(const std::string& mode_id) const {
    std::vector<const Line*> out;
    for (auto& row : lines) if (row.business_mode_id==mode_id) out.push_back(&row);
    return out;
}
std::vector<const Station*> Directory::stations_of(const std::string& line_id) const {
    std::vector<const Station*> out;
    for (auto& row : stations) if (row.line_id==line_id) out.push_back(&row);
    return out;
}
std::vector<const Station*> Directory::stations_of_controller(const std::string& controller_id) const {
    std::vector<const Station*> out;
    for (auto& link : controller_stations) if (link.controller_id==controller_id) {
        if (auto* station=find_station(link.station_id)) out.push_back(station);
    }
    return out;
}
std::vector<const Location*> Directory::locations_of_zone(const std::string& zone_id) const {
    std::vector<const Location*> out;
    for (auto& row : locations) if (row.zone_id==zone_id) out.push_back(&row);
    return out;
}
const Controller* Directory::controller_of_station(const std::string& station_id) const {
    for (auto& link : controller_stations) if (link.station_id==station_id) return find_controller(link.controller_id);
    return nullptr;
}
const Zone* Directory::zone_of_station(const std::string& station_id) const {
    for (auto& link : zone_stations) if (link.station_id==station_id) return find_zone(link.zone_id);
    return nullptr;
}
const Line* Directory::line_of_station(const std::string& station_id) const {
    auto* station=find_station(station_id);
    return station?find_line(station->line_id):nullptr;
}
const BusinessMode* Directory::mode_of_station(const std::string& station_id) const {
    auto* line=line_of_station(station_id);
    return line?find_mode(line->business_mode_id):nullptr;
}
const Factory* Directory::factory_of_station(const std::string& station_id) const {
    auto* mode=mode_of_station(station_id);
    return mode?find_factory(mode->factory_id):nullptr;
}
bool Directory::is_live(const std::string& station_id) const {
    auto* station=find_station(station_id);
    return station && station->stream==kStream;
}
}
