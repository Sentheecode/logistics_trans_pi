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
    directory.factories = {{kFactory, "QT2", "钱塘二厂"},
        {"fac.demo-north", "DEMO-N", "北岸示例厂"}, {"fac.demo-south", "DEMO-S", "南湾示例厂"}};
    directory.areas = {
        {kAreaPick, kFactory, "pick", "分拣"},
        {kAreaSeq, kFactory, "seq", "排序区"},
        {"area.demo-north.sps", "fac.demo-north", "pick", "分拣"},
        {"area.demo-north.sort", "fac.demo-north", "seq", "排序区"},
        {"area.demo-south.sps", "fac.demo-south", "pick", "分拣"},
        {"area.demo-south.sort", "fac.demo-south", "seq", "排序区"},
    };
    directory.plcs = {
        {kPlcPack, kAreaPick, "pack", "外装"},
        {kPlcMeter1, kAreaPick, "meter1", "仪表一"},
        {kPlcMeter2, kAreaPick, "meter2", "仪表二"},
        {kPlcSeq1, kAreaSeq, "seq1", "排序区一"},
        {kPlcSeq2, kAreaSeq, "seq2", "排序区二"},
        {kPlcSeq3, kAreaSeq, "seq3", "排序区三"},
        {kPlcSeq4, kAreaSeq, "seq4", "排序区四"},
        {"plc.cfg.demo-north", "area.demo-north.sps", "demo-n", "示例 PLC N"},
        {"plc.cfg.demo-south", "area.demo-south.sort", "demo-s", "示例 PLC S"},
        {kPlcEmpty, "area.demo-north.sort", "demo-empty", "未配置示例"},
    };
    // Station code is the 工位号. Name is the 工位名称. Only 140 carries the simulation stream.
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
        {kStation170, kPlcSeq1, "170", "左后门手柄机械臂排序区", ""},
        {kStation180, kPlcSeq1, "180", "右后门手柄机械臂排序区", ""},
        {kStation190, kPlcSeq2, "190", "左前门手柄机械臂排序区", ""},
        {kStation200, kPlcSeq2, "200", "右前门手柄机械臂排序区", ""},
        {kStation210, kPlcSeq3, "210", "前悬架右前下摆臂总成", ""},
        {kStation220, kPlcSeq3, "220", "前悬架左前下摆臂总成", ""},
        {kStation230, kPlcSeq4, "230", "左后制动钳总成", ""},
        {kStation240, kPlcSeq4, "240", "右后制动钳总成", ""},
        {"stn.demo-n1", "plc.cfg.demo-north", "N1", "示例工位 N1", ""},
        {"stn.demo-n2", "plc.cfg.demo-north", "N2", "示例工位 N2", ""},
        {"stn.demo-s1", "plc.cfg.demo-south", "S1", "示例工位 S1", ""},
    };
    directory.lines = {
        {"line.pack", kAreaPick, "外装", "待补映射", "分拣"},
        {"line.meter1", kAreaPick, "仪表一", "待补映射", "分拣"},
        {"line.meter2", kAreaPick, "仪表二", "待补映射", "分拣"},
        {"line.seq1", kAreaSeq, "排序区一", "不适用", "排序区"},
        {"line.seq2", kAreaSeq, "排序区二", "不适用", "排序区"},
        {"line.seq3", kAreaSeq, "排序区三", "不适用", "排序区"},
        {"line.seq4", kAreaSeq, "排序区四", "不适用", "排序区"},
        {"line.demo-north", "area.demo-north.sps", "北岸示例线", "示例立库 N", "示例库区 N"},
        {"line.demo-empty", "area.demo-north.sort", "预留空线", "不适用", "未配置"},
        {"line.demo-south", "area.demo-south.sort", "南湾示例线", "不适用", "示例排序区 S"},
    };
    directory.line_links = {
        {"line.pack", kStation140}, {"line.pack", kStation150}, {"line.pack", kStation160},
        {"line.meter1", kStation20}, {"line.meter1", kStation30}, {"line.meter1", kStation90},
        {"line.meter2", kStation40}, {"line.meter2", kStation10}, {"line.meter2", kStation70}, {"line.meter2", kStation80},
        {"line.seq1", kStation170}, {"line.seq1", kStation180},
        {"line.seq2", kStation190}, {"line.seq2", kStation200},
        {"line.seq3", kStation210}, {"line.seq3", kStation220},
        {"line.seq4", kStation230}, {"line.seq4", kStation240},
        {"line.demo-north", "stn.demo-n1"}, {"line.demo-north", "stn.demo-n2"},
        {"line.demo-south", "stn.demo-s1"},
    };
    return directory;
}

bool candidate_workbook(const std::string& station_id) {
    return station_id==kStation170 || station_id==kStation180
        || station_id==kStation190 || station_id==kStation200
        || station_id==kStation230 || station_id==kStation240;
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
    for (const auto& line : directory.lines) {
        if (auto error=take_id(line.id); !error.empty()) return error;
        if (!directory.find_area(line.area_id)) return "dangling-ref";
        if (line.name.empty() || line.storage.empty() || line.zone.empty()) return "empty-name";
    }
    std::vector<std::string> linked;
    for (const auto& link : directory.line_links) {
        const auto* line=directory.find_line(link.line_id);
        const auto* area=directory.area_of_station(link.station_id);
        if (!line || !area || line->area_id!=area->id) return "line-scope";
        if (std::find(linked.begin(), linked.end(), link.station_id)!=linked.end()) return "multiple-lines";
        linked.push_back(link.station_id);
    }
    std::vector<std::pair<std::string, std::string>> line_plc, plc_line;
    for (const auto& link : directory.line_links) {
        const auto* station=directory.find_station(link.station_id);
        if (!station) return "dangling-ref";
        const auto known_plc=std::find_if(line_plc.begin(), line_plc.end(), [&](const auto& row){ return row.first==link.line_id; });
        if (known_plc!=line_plc.end() && known_plc->second!=station->plc_id) return "line-spans-plcs";
        if (known_plc==line_plc.end()) line_plc.emplace_back(link.line_id, station->plc_id);
        const auto known_line=std::find_if(plc_line.begin(), plc_line.end(), [&](const auto& row){ return row.first==station->plc_id; });
        if (known_line!=plc_line.end() && known_line->second!=link.line_id) return "plc-split-across-lines";
        if (known_line==plc_line.end()) plc_line.emplace_back(station->plc_id, link.line_id);
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
    if (!find_plc(proposed.plc_id) || find_plc(proposed.plc_id)->area_id!=resolved.area_id) {
        const auto choices=plcs_of(resolved.area_id);
        if (choices.empty()) return resolved;
        resolved.plc_id=choices.front()->id;
        resolved.station_id.clear();
        resolved.line_id.clear();
        return reconcile(resolved);
    }
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
    if (const auto* line=line_of_station(station->id)) resolved.line_id=line->id;
    return resolved;
}

std::optional<Selection> Directory::selection_for_station(const std::string& station_id) const {
    auto* station=find_station(station_id);
    auto* plc=station?find_plc(station->plc_id):nullptr;
    auto* area=plc?find_area(plc->area_id):nullptr;
    auto* factory=area?find_factory(area->factory_id):nullptr;
    if (!factory) return std::nullopt;
    const auto* line=line_of_station(station->id);
    return Selection{factory->id, area->id, plc->id, station->id, line ? line->id : ""};
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

const Line* Directory::find_line(const std::string& id) const { return by_id(lines, id); }
const Line* Directory::line_of_station(const std::string& station_id) const {
    for (const auto& link : line_links) if (link.station_id==station_id) return find_line(link.line_id);
    return nullptr;
}
std::vector<const Line*> Directory::lines_of(const std::string& area_id) const {
    std::vector<const Line*> rows;
    for (const auto& line : lines) if (line.area_id==area_id) rows.push_back(&line);
    return rows;
}
std::vector<const Station*> Directory::stations_on_line(const std::string& line_id) const {
    std::vector<const Station*> rows;
    for (const auto& link : line_links) if (link.line_id==line_id)
        if (const auto* station=find_station(link.station_id)) rows.push_back(station);
    return rows;
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
