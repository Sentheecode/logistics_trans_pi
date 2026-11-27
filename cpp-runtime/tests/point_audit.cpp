#include "point_audit.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error("failed: " #condition); } while (0)

int count_address(const std::vector<audit::Row>& rows, const std::string& address) {
    int count=0;
    for (const auto& row : rows) if (row.address==address) ++count;
    return count;
}
const audit::Row* find_name(const std::vector<audit::Row>& rows, const std::string& name) {
    for (const auto& row : rows) if (row.name==name) return &row;
    return nullptr;
}
const audit::Row* find_source(const std::vector<audit::Row>& rows, const std::string& source_row) {
    for (const auto& row : rows) if (row.source_row==source_row) return &row;
    return nullptr;
}
void expect_load_error(const std::filesystem::path& path, const std::string& fragment) {
    bool threw=false;
    std::string text;
    try {
        audit::load_point_table(path);
    } catch (const std::exception& error) {
        threw=true;
        text=error.what();
    }
    if (!threw) throw std::runtime_error("load succeeded for "+path.u8string());
    if (text.find(fragment)==std::string::npos || text.find("不会回退到内置模板")==std::string::npos)
        throw std::runtime_error("unexpected point-table error: "+text);
}
void exec_sql(sqlite3* database, const char* sql) {
    char* error=nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &error)!=SQLITE_OK) {
        const std::string message=error ? error : "sqlite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
}

int main(int argc, char** argv) {
    try {
        const auto scratch=std::filesystem::temp_directory_path()/"ltp-point-table-test";
        std::filesystem::remove_all(scratch);
        std::filesystem::create_directory(scratch);
        expect_load_error(scratch/"missing.sqlite", "找不到点表数据库");
        {
            std::ofstream garbage(scratch/"garbage.sqlite", std::ios::binary);
            garbage<<"this is not a sqlite database";
        }
        expect_load_error(scratch/"garbage.sqlite", "无法读取点表数据库");
        {
            sqlite3* created=nullptr;
            const auto path=(scratch/"simulation-id.sqlite").u8string();
            if (sqlite3_open(path.c_str(), &created)!=SQLITE_OK) throw std::runtime_error("cannot create fixture");
            exec_sql(created, "PRAGMA application_id=1279471699; PRAGMA user_version=4; CREATE TABLE frames(seq INTEGER);");
            sqlite3_close(created);
            expect_load_error(scratch/"simulation-id.sqlite", "采集库");
        }

        CHECK(argc==2);
        CHECK(audit::for_station("stn.150").empty());
        CHECK(audit::for_station("stn.160").empty());
        CHECK(audit::for_station("stn.210").empty());
        const auto rows=audit::for_station("stn.140");
        CHECK(rows.size()==116);
        int named=0;
        for (const auto& row : rows) {
            if (!row.reserved) ++named;
            CHECK(row.code.empty());
            CHECK(row.station_id=="stn.140");
            CHECK(row.pairing_status=="single_sided");
            CHECK(!row.executable_write);
            CHECK(row.read.present!=row.write.present);
            const auto& side=row.read.present ? row.read : row.write;
            CHECK(side.source_row==row.source_row);
            CHECK(audit::compose_address(side)==row.address);
            CHECK(side.area=="DBX");
            CHECK(side.length>0);
            CHECK(row.length_text==std::to_string(side.length));
        }
        CHECK(named==75);

        std::ifstream baseline(argv[1]);
        CHECK(baseline.good());
        std::string source;
        CHECK(static_cast<bool>(std::getline(baseline, source)));
        for (const auto& row : rows) {
            CHECK(static_cast<bool>(std::getline(baseline, source)));
            if (!source.empty() && source.back()=='\r') source.pop_back();
            const auto exported=row.source_row+","+row.section+",140,"+row.name+","+row.address+","+
                row.type_text+","+row.length_text+","+row.direction_text+","+row.note;
            CHECK(exported==source);
        }
        CHECK(!std::getline(baseline, source));

        CHECK(count_address(rows, "DB1900.DBX3.7")==0);
        CHECK(count_address(rows, "DB1900.DBX3.6")>=3);
        CHECK(count_address(rows, "DB1900.DBX7.6")==2);
        CHECK(rows.front().source_row=="140-001" && rows.back().source_row=="140-116");

        const auto* running=find_source(rows, "140-001");
        CHECK(running && running->read.present && !running->write.present);
        CHECK(running->read.db==1900 && running->read.start==0 && running->read.offset==0 && running->read.length==1);
        CHECK(running->name=="设备运行中");
        CHECK(running->doubts.find("plc-auto-not-host-mode")!=std::string::npos);

        const auto* robot=find_source(rows, "140-002");
        const auto* pc=find_source(rows, "140-068");
        CHECK(robot && pc && robot->read.present && pc->write.present);
        CHECK(robot->read.db==1900 && robot->read.start==0 && robot->read.offset==1);
        CHECK(pc->write.db==1901 && pc->write.start==0 && pc->write.offset==1);
        CHECK(robot->name=="机器人心跳" && pc->name=="PC心跳");

        auto* alarm=find_name(rows, "报警 代码");
        CHECK(alarm && alarm->type_text=="int" && alarm->read.present && !alarm->write.present);
        CHECK(alarm->read.db==1900 && alarm->read.start==8 && alarm->read.offset==0);
        CHECK(alarm->doubts.find("dbx-non-bool")!=std::string::npos);

        auto* code=find_name(rows, "小车二维码信息");
        CHECK(code && code->length_text=="40" && code->read.present);
        CHECK(code->read.db==1900 && code->read.start==10 && code->read.offset==0 && code->read.length==40);
        CHECK(code->note.find("三台小车公用")!=std::string::npos);
        CHECK(code->doubts.find("shared-cart")!=std::string::npos);

        auto* order=find_name(rows, "小车取料次序1");
        CHECK(order && order->address=="DB1901.DBX6.0" && order->write.present && !order->read.present);
        CHECK(order->write.db==1901 && order->write.start==6 && order->write.offset==0);
        CHECK(order->direction_text.find("读取")!=std::string::npos);
        CHECK(order->type_text=="INT");
        CHECK(order->doubts.find("direction-conflict")!=std::string::npos);

        const auto* order2=find_name(rows, "小车取料次序2");
        CHECK(order2 && order2->write.present && order2->address=="DB1901.DBX8.0");
        CHECK(alarm->source_row!=order2->source_row);

        bool saw_complete=false;
        int reserved=0;
        for (const auto& row : rows) {
            if (row.name.find("补料完成")!=std::string::npos) {
                saw_complete=true;
                CHECK(row.write.present && !row.read.present);
                CHECK(row.write.db==1900);
                CHECK(row.address.find("DB1900.")==0);
                CHECK(row.address.find("DB1901")==std::string::npos);
                CHECK(row.direction_text.find("读取")!=std::string::npos);
                CHECK(row.direction_text.find("PLC→PC")!=std::string::npos);
                CHECK(row.doubts.find("do-not-rewrite-db")!=std::string::npos);
                CHECK(row.doubts.find("direction-conflict")!=std::string::npos);
            }
            if (row.reserved) ++reserved;
            CHECK(row.address.find("DB1900.DBX3.7")==std::string::npos);
        }
        CHECK(saw_complete);
        CHECK(reserved>10);

        const auto tampered=scratch/"tampered.sqlite";
        std::filesystem::copy_file(audit::default_point_table_path(), tampered);
        sqlite3* editor=nullptr;
        if (sqlite3_open(tampered.u8string().c_str(), &editor)!=SQLITE_OK) throw std::runtime_error("cannot open tampered copy");
        exec_sql(editor, "UPDATE points SET read_db=1901 WHERE source_row_id='140-001'");
        sqlite3_close(editor);
        expect_load_error(tampered, "拆分地址与原地址不一致");
        std::filesystem::remove_all(scratch);

        std::cout<<"PASS point audit: station 140 loads from SQLite, keeps source doubts and has no executable writes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
