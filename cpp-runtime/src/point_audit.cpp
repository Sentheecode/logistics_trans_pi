#include "point_audit.hpp"

namespace audit {
namespace {
int sequence=0;
void add(std::vector<Row>& rows, const std::string& section, const std::string& name, const std::string& address,
         const std::string& type_text, const std::string& direction, const std::string& length_text,
         const std::string& note, bool reserved, bool duplicate, const std::string& doubts) {
    Row row;
    row.source_row="S140-"+std::to_string(++sequence);
    row.section=section;
    row.name=name;
    row.address=address;
    row.type_text=type_text;
    row.direction_text=direction;
    row.length_text=length_text;
    row.note=note;
    row.reserved=reserved;
    row.duplicate_address=duplicate;
    row.doubts=doubts;
    row.executable_write=false;
    rows.push_back(row);
}
void add_span(std::vector<Row>& rows, const std::string& section, const std::string& name, const std::string& db,
              int byte, int bit_first, int bit_last, const std::string& type_text, const std::string& direction,
              const std::string& note, bool reserved, const std::string& doubts) {
    for (int bit=bit_first; bit<=bit_last; ++bit) {
        add(rows, section, name, db+".DBX"+std::to_string(byte)+"."+std::to_string(bit), type_text, direction, "1", note, reserved, false, doubts);
    }
}
}

std::vector<Row> station_140_template() {
    sequence=0;
    std::vector<Row> rows;
    const std::string read="PLC→PC";
    const std::string read_text="上位机读取";
    const std::string write_section="PC→PLC";
    const std::string no_write="来源纵有写入字样，本程序也不生成写点";
    add(rows, read, "设备运行中", "DB1900.DBX0.0", "BOOL", read_text, "1", "1 表示 PLC 部分无故障且自动运行 ON；这不是上位机手动/自动", false, false, "plc-auto-not-host-mode");
    add(rows, read, "机器人心跳", "DB1900.DBX0.1", "BOOL", read_text, "1", "", false, false, "heartbeat-not-passage");
    add_span(rows, read, "////////", "DB1900", 0, 2, 7, "BOOL", read_text, "保留", true, "reserved");
    add(rows, read, "允许AGV进入", "DB1900.DBX1.0", "BOOL", read_text, "1", "PLC→PC 输入，不能由上位机写成放行", false, false, "read-not-grant");
    add(rows, read, "允许AGV离开", "DB1900.DBX1.1", "BOOL", read_text, "1", "PLC→PC 输入，不能由上位机写成放行", false, false, "read-not-grant");
    add_span(rows, read, "////////", "DB1900", 1, 2, 4, "BOOL", read_text, "保留", true, "reserved");
    add(rows, read, "读码完成", "DB1900.DBX1.5", "BOOL", read_text, "1", "置位/复位规则未确认", false, false, "handshake-unconfirmed");
    add_span(rows, read, "////////", "DB1900", 1, 6, 7, "BOOL", read_text, "保留", true, "reserved");
    for (int bit=0; bit<=7; ++bit) add(rows, read, "料仓上层"+std::to_string(bit+1)+"请求补料", "DB1900.DBX2."+std::to_string(bit), "BOOL", read_text, "1", "", false, false, "replenish-request-not-lwms-complete");
    for (int index=9; index<=15; ++index) add(rows, read, "料仓上层"+std::to_string(index)+"请求补料", "DB1900.DBX3."+std::to_string(index-9), "BOOL", read_text, "1", "", false, false, "replenish-request-not-lwms-complete");
    add(rows, read, "////////", "DB1900.DBX3.6", "BOOL", read_text, "1", "与上层15请求补料地址重复，不改成 3.7", true, true, "duplicate-address;reserved");
    for (int bit=0; bit<=7; ++bit) add(rows, read, "料仓下层"+std::to_string(bit+1)+"请求补料--多层物料不需要", "DB1900.DBX4."+std::to_string(bit), "BOOL", read_text, "1", "原文说明多层物料不需要", false, false, "lower-layer-not-enabled");
    for (int index=9; index<=15; ++index) add(rows, read, "料仓下层"+std::to_string(index)+"请求补料--多层物料不需要", "DB1900.DBX5."+std::to_string(index-9), "BOOL", read_text, "1", "原文说明多层物料不需要", false, false, "lower-layer-not-enabled");
    add(rows, read, "////////", "DB1900.DBX5.7", "BOOL", read_text, "1", "保留", true, false, "reserved");
    add(rows, read, "小车1放料完成", "DB1900.DBX6.0", "BOOL", read_text, "1", "表示该车当前工位本轮放料完成，不等于整线完成", false, false, "cart-complete-scope");
    add(rows, read, "小车2放料完成", "DB1900.DBX6.1", "BOOL", read_text, "1", "表示该车当前工位本轮放料完成，不等于整线完成", false, false, "cart-complete-scope");
    add(rows, read, "小车3放料完成", "DB1900.DBX6.2", "BOOL", read_text, "1", "表示该车当前工位本轮放料完成，不等于整线完成", false, false, "cart-complete-scope");
    add_span(rows, read, "", "DB1900", 6, 3, 7, "BOOL", read_text, "空白名保留", true, "blank-name;reserved");
    add_span(rows, read, "", "DB1900", 7, 0, 5, "BOOL", read_text, "空白名保留", true, "blank-name;reserved");
    add(rows, read, "", "DB1900.DBX7.6", "BOOL", read_text, "1", "空白名", true, false, "blank-name;reserved");
    add(rows, read, "", "DB1900.DBX7.6", "BOOL", read_text, "1", "原文再次出现 7.6，不改地址", true, true, "duplicate-address;blank-name;reserved");
    add(rows, read, "报警代码", "DB1900.DBX8.0", "int", read_text, "1", "原文类型 int，不是 BOOL", false, false, "dbx-non-bool;length-unit-unconfirmed");
    add(rows, read, "小车二维码信息", "DB1900.DBX10.0", "string", read_text, "40", "三台小车公用一个地址", false, false, "dbx-non-bool;shared-cart;length-unit-unconfirmed;clear-handshake-unconfirmed");

    add(rows, write_section, "////////", "DB1901.DBX0.0", "BOOL", "写入", "1", "保留", true, false, std::string("reserved;")+no_write);
    add(rows, write_section, "PC心跳", "DB1901.DBX0.1", "BOOL", "写入", "1", "来源写段", false, false, no_write);
    add_span(rows, write_section, "////////", "DB1901", 0, 2, 7, "BOOL", "写入", "保留", true, std::string("reserved;")+no_write);
    add(rows, write_section, "回复AGV到位", "DB1901.DBX1.0", "BOOL", "写入", "1", "触发扫码的写点未被点名确认", false, false, std::string("scan-trigger-unconfirmed;")+no_write);
    add(rows, write_section, "回复AGV离开", "DB1901.DBX1.1", "BOOL", "写入", "1", "", false, false, no_write);
    add_span(rows, write_section, "////////", "DB1901", 1, 2, 7, "BOOL", "写入", "保留", true, std::string("reserved;")+no_write);
    for (int bit=0; bit<=7; ++bit) add(rows, write_section, "料仓上层"+std::to_string(bit+1)+"补料完成", "DB1900.DBX2."+std::to_string(bit), "BOOL", "上位机读取（PLC→PC）", "1", "写段下列出，地址仍是 DB1900，方向仍是读取；不改成 DB1901", false, true, "direction-conflict;do-not-rewrite-db;not-executable-write");
    for (int index=9; index<=15; ++index) add(rows, write_section, "料仓上层"+std::to_string(index)+"补料完成", "DB1900.DBX3."+std::to_string(index-9), "BOOL", "上位机读取（PLC→PC）", "1", "写段下列出，地址仍是 DB1900，方向仍是读取；不改成 DB1901", false, true, "direction-conflict;do-not-rewrite-db;not-executable-write");
    add(rows, write_section, "////////", "DB1900.DBX3.6", "BOOL", "上位机读取（PLC→PC）", "1", "写段内再次与 3.6 重复，不改成 3.7", true, true, "duplicate-address;reserved;direction-conflict;do-not-rewrite-db");
    for (int bit=0; bit<=7; ++bit) add(rows, write_section, "料仓下层"+std::to_string(bit+1)+"请求补料--多层物料不需要", "DB1900.DBX4."+std::to_string(bit), "BOOL", "上位机读取（PLC→PC）", "1", "不推定为补料完成写信号", false, true, "direction-conflict;not-complete-write;lower-layer-not-enabled");
    for (int index=9; index<=15; ++index) add(rows, write_section, "料仓下层"+std::to_string(index)+"请求补料--多层物料不需要", "DB1900.DBX5."+std::to_string(index-9), "BOOL", "上位机读取（PLC→PC）", "1", "不推定为补料完成写信号", false, true, "direction-conflict;not-complete-write;lower-layer-not-enabled");
    add(rows, write_section, "////////", "DB1900.DBX5.7", "BOOL", "上位机读取（PLC→PC）", "1", "保留", true, true, "reserved;direction-conflict");
    add(rows, write_section, "小车取料次序1", "DB1901.DBX6.0", "INT", "上位机读取（PLC→PC）", "1", "三台小车公用；写段标题与行内方向冲突", false, false, "direction-conflict;dbx-non-bool;shared-cart;not-executable-write");
    add(rows, write_section, "小车取料次序2", "DB1901.DBX8.0", "INT", "上位机读取（PLC→PC）", "1", "三台小车公用；写段标题与行内方向冲突", false, false, "direction-conflict;dbx-non-bool;shared-cart;not-executable-write");
    return rows;
}

std::vector<Row> for_station(const std::string& station_id) {
    if (station_id=="stn.140") return station_140_template();
    return {};
}
}
