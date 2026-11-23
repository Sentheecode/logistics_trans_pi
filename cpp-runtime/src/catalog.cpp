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
bool id_ok(const std::string& id) {
    if (id.empty()) return false;
    for (unsigned char c : id) if (!(std::islower(c) || std::isdigit(c) || c=='.' || c=='-')) return false;
    return true;
}
}

Directory Directory::qiantang() {
    Directory directory;
    directory.factories = {{kFactory, "QT2", "钱塘二厂"}};
    directory.areas = {
        {kAreaPick, kFactory, "pick", "分拣区"},
        {kAreaSeq, kFactory, "seq", "排序区"},
    };
    directory.plcs = {
        {kPlcPack, kAreaPick, "pack", "外装"},
        {kPlcMeter1, kAreaPick, "meter1", "仪表一"},
        {kPlcMeter2, kAreaPick, "meter2", "仪表二"},
        {kPlcSeq1, kAreaSeq, "seq1", "排序一"},
        {kPlcSeq2, kAreaSeq, "seq2", "排序二"},
        {kPlcSeq3, kAreaSeq, "seq3", "排序三"},
        {kPlcSeq4, kAreaSeq, "seq4", "排序四"},
    };
    // Station numbers are the given membership, in the source order. Names are those numbers.
    directory.stations = {
        {kStation140, kPlcPack, "140", "140", kStream},
        {kStation150, kPlcPack, "150", "150", ""},
        {kStation160, kPlcPack, "160", "160", ""},
        {kStation20, kPlcMeter1, "20", "20", ""},
        {kStation30, kPlcMeter1, "30", "30", ""},
        {kStation90, kPlcMeter1, "90", "90", ""},
        {kStation40, kPlcMeter2, "40", "40", ""},
        {kStation10, kPlcMeter2, "10", "10", ""},
        {kStation70, kPlcMeter2, "70", "70", ""},
        {kStation80, kPlcMeter2, "80", "80", ""},
        {kStation210, kPlcSeq1, "210", "210", ""},
        {kStation220, kPlcSeq1, "220", "220", ""},
    };
    return directory;
}

std::string Directory::validate(const Directory& directory) {
    std::vector<std::string> ids;
    auto take_id = [&](const std::string& id) -> std::string {
        if (!id_ok(id)) return id.empty() ? "empty-id" : "id-charset";
        if (std::find(ids.begin(), ids.end(), id)!=ids.end()) return "duplicate-id";
        ids.push_back(id);
        return {};
    };
    for (auto& row : directory.factories) {
        if (auto error=take_id(row.id); !error.empty()) return error;
        if (row.code.empty() || row.name.empty()) return "empty-name";
    }
    for (auto& row : directory.areas) {
        if (auto error=take_id(row.id); !error.empty()) return error;
        if (row.name.empty()) return "empty-name";
        if (!by_id(directory.factories, row.factory_id)) return "dangling-ref";
        if (row.code=="manual" || row.code=="automatic") return "runtime-code-as-area";
        if (row.code!="pick" && row.code!="seq") return "area-code";
    }
    for (auto& row : directory.plcs) {
        if (auto error=take_id(row.id); !error.empty()) return error;
        if (row.name.empty() || row.code.empty()) return "empty-name";
        if (row.id.rfind("plc.cfg.", 0)!=0) return "plc-id-scope";
        if (!by_id(directory.areas, row.area_id)) return "dangling-ref";
    }
    int live=0;
    std::vector<std::string> codes;
    for (auto& row : directory.stations) {
        if (auto error=take_id(row.id); !error.empty()) return error;
        if (row.code.empty() || row.name.empty()) return "empty-name";
        if (!by_id(directory.plcs, row.plc_id)) return "dangling-ref";
        if (std::find(codes.begin(), codes.end(), row.code)!=codes.end()) return "duplicate-code";
        codes.push_back(row.code);
        if (!row.stream.empty()) {
            if (row.stream!=kStream) return "stream";
            ++live;
        }
    }
    if (live>1) return "multiple-live";
    for (auto& link : directory.line_links) {
        if (auto error=take_id(link.id); !error.empty()) return error;
        if (!by_id(directory.stations, link.station_id)) return "dangling-ref";
    }
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
    auto area_rows=areas_of(resolved.factory_id);
    if (area_rows.empty()) return resolved;
    const Area* area=nullptr;
    for (auto* item : area_rows) if (item->id==proposed.area_id) area=item;
    if (!area) area=area_rows.front();
    resolved.area_id=area->id;
    auto plc_rows=plcs_of(resolved.area_id);
    if (plc_rows.empty()) return resolved;
    const Plc* plc=nullptr;
    for (auto* item : plc_rows) if (item->id==proposed.plc_id) plc=item;
    if (!plc) plc=plc_rows.front();
    resolved.plc_id=plc->id;
    auto station_rows=stations_of(resolved.plc_id);
    if (station_rows.empty()) return resolved;
    const Station* station=nullptr;
    for (auto* item : station_rows) if (item->id==proposed.station_id) station=item;
    if (!station) station=station_rows.front();
    resolved.station_id=station->id;
    return resolved;
}

std::optional<Selection> Directory::selection_for_station(const std::string& station_id) const {
    auto* station=find_station(station_id);
    auto* plc=station?find_plc(station->plc_id):nullptr;
    auto* area=plc?find_area(plc->area_id):nullptr;
    auto* factory=area?find_factory(area->factory_id):nullptr;
    if (!factory) return std::nullopt;
    return Selection{factory->id, area->id, plc->id, station->id};
}

std::optional<Selection> Directory::selection_for_plc(const std::string& plc_id) const {
    auto* plc=find_plc(plc_id);
    auto* area=plc?find_area(plc->area_id):nullptr;
    auto* factory=area?find_factory(area->factory_id):nullptr;
    if (!factory) return std::nullopt;
    Selection selection{factory->id, area->id, plc->id, ""};
    return reconcile(selection);
}

std::string Directory::live_station_id() const {
    for (auto& station : stations) if (station.stream==kStream) return station.id;
    return {};
}

std::string Directory::empty_plc_id() const {
    for (auto& plc : plcs) if (stations_of(plc.id).empty()) return plc.id;
    return {};
}

const Factory* Directory::find_factory(const std::string& id) const { return by_id(factories, id); }
const Area* Directory::find_area(const std::string& id) const { return by_id(areas, id); }
const Plc* Directory::find_plc(const std::string& id) const { return by_id(plcs, id); }
const Station* Directory::find_station(const std::string& id) const { return by_id(stations, id); }

std::vector<const Area*> Directory::areas_of(const std::string& factory_id) const {
    std::vector<const Area*> out;
    for (auto& row : areas) if (row.factory_id==factory_id) out.push_back(&row);
    return out;
}
std::vector<const Plc*> Directory::plcs_of(const std::string& area_id) const {
    std::vector<const Plc*> out;
    for (auto& row : plcs) if (row.area_id==area_id) out.push_back(&row);
    return out;
}
std::vector<const Station*> Directory::stations_of(const std::string& plc_id) const {
    std::vector<const Station*> out;
    for (auto& row : stations) if (row.plc_id==plc_id) out.push_back(&row);
    return out;
}
const Plc* Directory::plc_of_station(const std::string& station_id) const {
    auto* station=find_station(station_id);
    return station?find_plc(station->plc_id):nullptr;
}
const Area* Directory::area_of_station(const std::string& station_id) const {
    auto* plc=plc_of_station(station_id);
    return plc?find_area(plc->area_id):nullptr;
}
const Factory* Directory::factory_of_station(const std::string& station_id) const {
    auto* area=area_of_station(station_id);
    return area?find_factory(area->factory_id):nullptr;
}
bool Directory::is_live(const std::string& station_id) const {
    auto* station=find_station(station_id);
    return station && station->stream==kStream;
}
}
