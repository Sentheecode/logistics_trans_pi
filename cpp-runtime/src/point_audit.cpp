#include "point_audit.hpp"
#include "point_baseline.hpp"

#include <map>
#include <stdexcept>
#include <string_view>

namespace audit {
namespace {
constexpr std::string_view header=
    "source_row_id,section,station_code,name,address,data_type,length,direction,description";

std::vector<std::string> splitRow(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted=false;
    for (std::size_t i=0;i<line.size();++i) {
        const char ch=line[i];
        if (ch=='"') {
            if (quoted && i+1<line.size() && line[i+1]=='"') {
                field+='"';
                ++i;
            } else {
                quoted=!quoted;
            }
        } else if (ch==',' && !quoted) {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field+=ch;
        }
    }
    if (quoted) throw std::runtime_error("station 140 baseline contains an unterminated CSV field");
    fields.push_back(std::move(field));
    return fields;
}

void addFlag(Row& row, std::string_view flag) {
    if (!row.doubts.empty()) row.doubts+=';';
    row.doubts+=flag;
}
}

std::vector<Row> station_140_template() {
    std::vector<Row> rows;
    auto csv=station140Csv;
    const auto head=csv.find('\n');
    if (head==std::string_view::npos || csv.substr(0,head)!=header)
        throw std::runtime_error("station 140 baseline header changed");
    csv.remove_prefix(head+1);

    while (!csv.empty()) {
        const auto end=csv.find('\n');
        auto line=csv.substr(0,end);
        csv.remove_prefix(end==std::string_view::npos ? csv.size() : end+1);
        if (!line.empty() && line.back()=='\r') line.remove_suffix(1);
        if (line.empty()) continue;
        auto fields=splitRow(line);
        if (fields.size()!=9 || fields[2]!="140") throw std::runtime_error("invalid station 140 baseline row");
        Row row;
        row.source_row=fields[0];
        row.section=fields[1];
        row.name=fields[3];
        row.address=fields[4];
        row.type_text=fields[5];
        row.length_text=fields[6];
        row.direction_text=fields[7];
        row.note=fields[8];
        row.reserved=row.name.find_first_not_of(" /\t")==std::string::npos;
        rows.push_back(std::move(row));
    }

    std::map<std::string,int> counts;
    for (const auto& row:rows) if (!row.address.empty()) ++counts[row.address];
    for (auto& row:rows) {
        row.duplicate_address=counts[row.address]>1;
        if (row.duplicate_address) addFlag(row,"duplicate-address");
        if (row.reserved) addFlag(row,row.name.empty() ? "blank-name" : "reserved");
        const bool outbound=row.section=="上位机给PLC";
        if (outbound) addFlag(row,"not-executable-write");
        if (outbound && row.direction_text.find("PLC→PC")!=std::string::npos) addFlag(row,"direction-conflict");
        if (row.address.find(".DBX")!=std::string::npos && row.type_text!="BOOL") addFlag(row,"dbx-non-bool");
        if (row.name=="读码完成") addFlag(row,"handshake-unconfirmed");
        if (row.name=="小车二维码信息") {
            addFlag(row,"shared-cart");
            addFlag(row,"clear-handshake-unconfirmed");
        }
        if (row.name.find("取料次序")!=std::string::npos) addFlag(row,"shared-cart");
        if (row.name.find("补料完成")!=std::string::npos) addFlag(row,"do-not-rewrite-db");
        if (row.name.find("请求补料")!=std::string::npos) addFlag(row,"replenish-request-not-lwms-complete");
        if (row.name=="设备运行中") addFlag(row,"plc-auto-not-host-mode");
        if (row.name=="机器人心跳") addFlag(row,"heartbeat-not-passage");
        if (row.name.find("放料完成")!=std::string::npos) addFlag(row,"cart-complete-scope");
    }
    return rows;
}

std::vector<Row> for_station(const std::string& station_id) {
    return station_id=="stn.140" ? station_140_template() : std::vector<Row>{};
}
}
