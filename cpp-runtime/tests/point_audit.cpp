#include "point_audit.hpp"
#include <iostream>
#include <stdexcept>
namespace {
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error("failed: " #condition); } while (0)
int count_address(const std::vector<audit::Row>& rows, const std::string& address) {
    int count=0;
    for (auto& row : rows) if (row.address==address) ++count;
    return count;
}
const audit::Row* find_name(const std::vector<audit::Row>& rows, const std::string& name) {
    for (auto& row : rows) if (row.name==name) return &row;
    return nullptr;
}
}

int main() {
    try {
        CHECK(audit::for_station("stn.150").empty());
        CHECK(audit::for_station("stn.210").empty());
        const auto rows=audit::station_140_template();
        CHECK(!rows.empty());
        for (auto& row : rows) CHECK(!row.executable_write);
        CHECK(count_address(rows, "DB1900.DBX3.7")==0);
        CHECK(count_address(rows, "DB1900.DBX3.6")>=3);
        CHECK(count_address(rows, "DB1900.DBX7.6")==2);
        CHECK(rows.front().source_row=="140-001" && rows.back().source_row=="140-116");
        auto* alarm=find_name(rows, "报警 代码");
        CHECK(alarm && alarm->type_text=="int");
        CHECK(alarm->doubts.find("dbx-non-bool")!=std::string::npos);
        auto* code=find_name(rows, "小车二维码信息");
        CHECK(code && code->length_text=="40");
        CHECK(code->note.find("三台小车公用")!=std::string::npos);
        CHECK(code->doubts.find("shared-cart")!=std::string::npos);
        auto* order=find_name(rows, "小车取料次序1");
        CHECK(order && order->address=="DB1901.DBX6.0");
        CHECK(order->direction_text.find("读取")!=std::string::npos);
        CHECK(order->type_text=="INT");
        bool saw_complete=false;
        int reserved=0;
        for (auto& row : rows) {
            if (row.name.find("补料完成")!=std::string::npos) {
                saw_complete=true;
                CHECK(row.address.find("DB1900.")==0);
                CHECK(row.address.find("DB1901")==std::string::npos);
                CHECK(row.doubts.find("do-not-rewrite-db")!=std::string::npos);
            }
            if (row.reserved) ++reserved;
            CHECK(row.address.find("DB1900.DBX3.7")==std::string::npos);
        }
        CHECK(saw_complete);
        CHECK(reserved>10);
        std::cout<<"PASS point audit: station 140 keeps source doubts and has no executable writes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
