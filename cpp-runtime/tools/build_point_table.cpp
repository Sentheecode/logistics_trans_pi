#include "point_audit.hpp"

#include <sqlite3.h>

#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct ParsedAddress {
    int db=0;
    std::string area;
    int start=0;
    int offset=0;
};

struct SourceRow {
    std::string source_row;
    std::string section;
    std::string station_code;
    std::string name;
    std::string address;
    std::string type_text;
    std::string length_text;
    std::string direction_text;
    std::string note;
    int length=0;
    ParsedAddress parsed;
    bool outbound=false;
    bool reserved=false;
    bool duplicate_address=false;
    std::string doubts;
};

struct Database {
    sqlite3* handle=nullptr;
    explicit Database(sqlite3* opened) : handle(opened) {}
    ~Database() { if (handle) sqlite3_close(handle); }
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::vector<std::string> split_row(std::string_view line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted=false;
    for (std::size_t index=0; index<line.size(); ++index) {
        const char character=line[index];
        if (character=='"') {
            if (quoted && index+1<line.size() && line[index+1]=='"') {
                field.push_back('"');
                ++index;
            } else {
                quoted=!quoted;
            }
        } else if (character==',' && !quoted) {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field.push_back(character);
        }
    }
    if (quoted) fail("station 140 baseline contains an unterminated CSV field");
    fields.push_back(std::move(field));
    return fields;
}

int parse_positive(const std::string& text, const std::string& source_row) {
    if (text.empty()) fail(source_row+" has an empty length");
    int value=0;
    for (unsigned char character : text) {
        if (!std::isdigit(character)) fail(source_row+" has a non-numeric length");
        value=value*10+(character-'0');
    }
    if (value<=0) fail(source_row+" has a non-positive length");
    return value;
}

ParsedAddress parse_address(const std::string& text, const std::string& source_row) {
    std::size_t index=0;
    if (text.rfind("DB", 0)!=0) fail(source_row+" address is not a DB address: "+text);
    index=2;
    if (index>=text.size() || !std::isdigit(static_cast<unsigned char>(text[index])))
        fail(source_row+" address has no DB block: "+text);
    ParsedAddress parsed;
    while (index<text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
        parsed.db=parsed.db*10+(text[index]-'0');
        ++index;
    }
    if (parsed.db<=0 || index>=text.size() || text[index]!='.')
        fail(source_row+" address has no block separator: "+text);
    ++index;
    const auto area_begin=index;
    while (index<text.size() && std::isalpha(static_cast<unsigned char>(text[index]))) ++index;
    if (index==area_begin) fail(source_row+" address has no area: "+text);
    parsed.area=text.substr(area_begin, index-area_begin);
    if (index>=text.size() || !std::isdigit(static_cast<unsigned char>(text[index])))
        fail(source_row+" address has no start: "+text);
    while (index<text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
        parsed.start=parsed.start*10+(text[index]-'0');
        ++index;
    }
    if (index>=text.size() || text[index]!='.')
        fail(source_row+" address has no offset separator: "+text);
    ++index;
    if (index>=text.size() || !std::isdigit(static_cast<unsigned char>(text[index])))
        fail(source_row+" address has no offset: "+text);
    while (index<text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
        parsed.offset=parsed.offset*10+(text[index]-'0');
        ++index;
    }
    if (index!=text.size()) fail(source_row+" address has trailing text: "+text);
    return parsed;
}

void add_flag(SourceRow& row, const std::string& flag) {
    if (!row.doubts.empty()) row.doubts.push_back(';');
    row.doubts+=flag;
}

std::vector<SourceRow> read_baseline(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) fail("cannot read "+path.u8string());
    std::ostringstream buffer;
    buffer<<input.rdbuf();
    auto csv=buffer.str();
    constexpr std::string_view header=
        "source_row_id,section,station_code,name,address,data_type,length,direction,description";
    const auto head=csv.find('\n');
    std::string_view view=csv;
    if (head==std::string_view::npos || view.substr(0, head)!=header)
        fail("station 140 baseline header changed");
    view.remove_prefix(head+1);

    std::vector<SourceRow> rows;
    while (!view.empty()) {
        const auto end=view.find('\n');
        auto line=view.substr(0, end);
        view.remove_prefix(end==std::string_view::npos ? view.size() : end+1);
        if (!line.empty() && line.back()=='\r') line.remove_suffix(1);
        if (line.empty()) continue;
        const auto fields=split_row(line);
        if (fields.size()!=9) fail("invalid station 140 baseline row");
        SourceRow row;
        row.source_row=fields[0];
        row.section=fields[1];
        row.station_code=fields[2];
        row.name=fields[3];
        row.address=fields[4];
        row.type_text=fields[5];
        row.length_text=fields[6];
        row.direction_text=fields[7];
        row.note=fields[8];
        if (row.station_code!="140") fail(row.source_row+" is not station 140");
        const bool inbound=row.section=="PLC给上位机";
        row.outbound=row.section=="上位机给PLC";
        if (inbound==row.outbound) fail(row.source_row+" has an unknown section");
        row.length=parse_positive(row.length_text, row.source_row);
        row.parsed=parse_address(row.address, row.source_row);
        row.reserved=row.name.find_first_not_of(" /\t")==std::string::npos;
        rows.push_back(std::move(row));
    }

    std::map<std::string, int> counts;
    for (const auto& row : rows) ++counts[row.address];
    for (auto& row : rows) {
        row.duplicate_address=counts[row.address]>1;
        if (row.duplicate_address) add_flag(row, "duplicate-address");
        if (row.reserved) add_flag(row, row.name.empty() ? "blank-name" : "reserved");
        if (row.outbound) add_flag(row, "not-executable-write");
        if (row.outbound && row.direction_text.find("PLC→PC")!=std::string::npos) add_flag(row, "direction-conflict");
        if (row.address.find(".DBX")!=std::string::npos && row.type_text!="BOOL") add_flag(row, "dbx-non-bool");
        if (row.name=="读码完成") add_flag(row, "handshake-unconfirmed");
        if (row.name=="小车二维码信息") {
            add_flag(row, "shared-cart");
            add_flag(row, "clear-handshake-unconfirmed");
        }
        if (row.name.find("取料次序")!=std::string::npos) add_flag(row, "shared-cart");
        if (row.name.find("补料完成")!=std::string::npos) add_flag(row, "do-not-rewrite-db");
        if (row.name.find("请求补料")!=std::string::npos) add_flag(row, "replenish-request-not-lwms-complete");
        if (row.name=="设备运行中") add_flag(row, "plc-auto-not-host-mode");
        if (row.name=="机器人心跳") add_flag(row, "heartbeat-not-passage");
        if (row.name.find("放料完成")!=std::string::npos) add_flag(row, "cart-complete-scope");
    }
    return rows;
}

void exec(sqlite3* database, const char* sql) {
    char* error=nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &error)!=SQLITE_OK) {
        const std::string message=error ? error : "sqlite error";
        sqlite3_free(error);
        fail(message);
    }
}

void bind_text(sqlite3_stmt* statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT)!=SQLITE_OK)
        fail("bind text failed");
}

void bind_optional_side(sqlite3_stmt* statement, int origin, const SourceRow* row) {
    if (!row) {
        for (int index=0; index<9; ++index) {
            if (sqlite3_bind_null(statement, origin+index)!=SQLITE_OK) fail("bind null failed");
        }
        return;
    }
    bind_text(statement, origin, row->name);
    if (sqlite3_bind_int(statement, origin+1, row->parsed.db)!=SQLITE_OK) fail("bind db failed");
    bind_text(statement, origin+2, row->source_row);
    if (sqlite3_bind_int(statement, origin+3, row->parsed.start)!=SQLITE_OK) fail("bind start failed");
    if (sqlite3_bind_int(statement, origin+4, row->parsed.offset)!=SQLITE_OK) fail("bind offset failed");
    if (sqlite3_bind_int(statement, origin+5, row->length)!=SQLITE_OK) fail("bind length failed");
    bind_text(statement, origin+6, row->parsed.area);
    bind_text(statement, origin+7, row->type_text);
    bind_text(statement, origin+8, row->direction_text);
}

void write_database(const std::filesystem::path& path, const std::vector<SourceRow>& rows) {
    std::error_code error;
    std::filesystem::remove(path, error);
    sqlite3* opened=nullptr;
    if (sqlite3_open_v2(path.u8string().c_str(), &opened, SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE, nullptr)!=SQLITE_OK || !opened)
        fail("cannot create "+path.u8string());
    Database database(opened);
    exec(database.handle, "PRAGMA journal_mode=DELETE;");
    exec(database.handle, ("PRAGMA application_id="+std::to_string(audit::kPointTableApplicationId)+";").c_str());
    exec(database.handle, ("PRAGMA user_version="+std::to_string(audit::kPointTableUserVersion)+";").c_str());
    exec(database.handle, R"SQL(
CREATE TABLE meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);
CREATE TABLE points (
  ordinal INTEGER PRIMARY KEY,
  station_id TEXT NOT NULL,
  station_code TEXT NOT NULL,
  source_row_id TEXT NOT NULL UNIQUE,
  section TEXT NOT NULL,
  code TEXT NOT NULL DEFAULT '',
  name TEXT NOT NULL DEFAULT '',
  address_text TEXT NOT NULL,
  note TEXT NOT NULL DEFAULT '',
  reserved INTEGER NOT NULL CHECK (reserved IN (0, 1)),
  duplicate_address INTEGER NOT NULL CHECK (duplicate_address IN (0, 1)),
  doubts TEXT NOT NULL DEFAULT '',
  executable_write INTEGER NOT NULL CHECK (executable_write = 0),
  pairing_status TEXT NOT NULL CHECK (pairing_status = 'single_sided'),
  read_name TEXT,
  read_db INTEGER,
  read_source_row_id TEXT,
  read_start INTEGER,
  read_offset INTEGER,
  read_length INTEGER,
  read_area TEXT,
  read_type TEXT,
  read_direction TEXT,
  write_name TEXT,
  write_db INTEGER,
  write_source_row_id TEXT,
  write_start INTEGER,
  write_offset INTEGER,
  write_length INTEGER,
  write_area TEXT,
  write_type TEXT,
  write_direction TEXT,
  CHECK (
    (
      read_db IS NOT NULL AND read_name IS NOT NULL AND read_source_row_id IS NOT NULL
      AND read_start IS NOT NULL AND read_offset IS NOT NULL AND read_length > 0
      AND read_area IS NOT NULL AND read_type IS NOT NULL AND read_direction IS NOT NULL
      AND write_name IS NULL AND write_db IS NULL AND write_source_row_id IS NULL
      AND write_start IS NULL AND write_offset IS NULL AND write_length IS NULL
      AND write_area IS NULL AND write_type IS NULL AND write_direction IS NULL
    ) OR (
      write_db IS NOT NULL AND write_name IS NOT NULL AND write_source_row_id IS NOT NULL
      AND write_start IS NOT NULL AND write_offset IS NOT NULL AND write_length > 0
      AND write_area IS NOT NULL AND write_type IS NOT NULL AND write_direction IS NOT NULL
      AND read_name IS NULL AND read_db IS NULL AND read_source_row_id IS NULL
      AND read_start IS NULL AND read_offset IS NULL AND read_length IS NULL
      AND read_area IS NULL AND read_type IS NULL AND read_direction IS NULL
    )
  )
);
)SQL");
    exec(database.handle, "BEGIN");
    sqlite3_stmt* meta_statement=nullptr;
    if (sqlite3_prepare_v2(database.handle, "INSERT INTO meta(key,value) VALUES(?1,?2)", -1, &meta_statement, nullptr)!=SQLITE_OK)
        fail(sqlite3_errmsg(database.handle));
    const auto insert_meta=[&](const std::string& key, const std::string& value) {
        sqlite3_reset(meta_statement);
        sqlite3_clear_bindings(meta_statement);
        bind_text(meta_statement, 1, key);
        bind_text(meta_statement, 2, value);
        if (sqlite3_step(meta_statement)!=SQLITE_DONE) fail(sqlite3_errmsg(database.handle));
    };
    int named=0;
    for (const auto& row : rows) if (!row.reserved) ++named;
    insert_meta("schema", audit::kPointTableSchema);
    insert_meta("source", "data/baselines/station-140.csv");
    insert_meta("source_rows", std::to_string(rows.size()));
    insert_meta("named_rows", std::to_string(named));
    insert_meta("pairing", "single_sided");
    insert_meta("pairing_note",
        "每条来源行单独成记录，读写字段只填该行所在分节对应的一侧。"
        "基线没有标明同一动作的读侧和写侧，因此不合并机器人心跳与 PC 心跳、补料请求与补料完成、允许离开与回复离开。"
        "写段中仍使用 DB1900 且方向仍为读取的行保持原地址和原方向，executable_write 保持为 0。");
    sqlite3_finalize(meta_statement);

    sqlite3_stmt* insert=nullptr;
    const char* insert_sql=
        "INSERT INTO points(ordinal,station_id,station_code,source_row_id,section,code,name,address_text,note,"
        "reserved,duplicate_address,doubts,executable_write,pairing_status,"
        "read_name,read_db,read_source_row_id,read_start,read_offset,read_length,read_area,read_type,read_direction,"
        "write_name,write_db,write_source_row_id,write_start,write_offset,write_length,write_area,write_type,write_direction) "
        "VALUES(?1,'stn.140',?2,?3,?4,'',?5,?6,?7,?8,?9,?10,0,'single_sided',"
        "?11,?12,?13,?14,?15,?16,?17,?18,?19,?20,?21,?22,?23,?24,?25,?26,?27,?28)";
    if (sqlite3_prepare_v2(database.handle, insert_sql, -1, &insert, nullptr)!=SQLITE_OK)
        fail(sqlite3_errmsg(database.handle));
    for (std::size_t index=0; index<rows.size(); ++index) {
        const auto& row=rows[index];
        sqlite3_reset(insert);
        sqlite3_clear_bindings(insert);
        if (sqlite3_bind_int(insert, 1, static_cast<int>(index+1))!=SQLITE_OK) fail("bind ordinal failed");
        bind_text(insert, 2, row.station_code);
        bind_text(insert, 3, row.source_row);
        bind_text(insert, 4, row.section);
        bind_text(insert, 5, row.name);
        bind_text(insert, 6, row.address);
        bind_text(insert, 7, row.note);
        if (sqlite3_bind_int(insert, 8, row.reserved ? 1 : 0)!=SQLITE_OK) fail("bind reserved failed");
        if (sqlite3_bind_int(insert, 9, row.duplicate_address ? 1 : 0)!=SQLITE_OK) fail("bind duplicate failed");
        bind_text(insert, 10, row.doubts);
        bind_optional_side(insert, 11, row.outbound ? nullptr : &row);
        bind_optional_side(insert, 20, row.outbound ? &row : nullptr);
        if (sqlite3_step(insert)!=SQLITE_DONE) fail(std::string(row.source_row)+": "+sqlite3_errmsg(database.handle));
    }
    sqlite3_finalize(insert);
    exec(database.handle, "COMMIT");

    sqlite3_stmt* check=nullptr;
    if (sqlite3_prepare_v2(database.handle, "PRAGMA quick_check", -1, &check, nullptr)!=SQLITE_OK)
        fail(sqlite3_errmsg(database.handle));
    if (sqlite3_step(check)!=SQLITE_ROW) fail("quick_check failed");
    const auto* check_text=sqlite3_column_text(check, 0);
    const std::string check_value=check_text ? reinterpret_cast<const char*>(check_text) : "";
    sqlite3_finalize(check);
    if (check_value!="ok") fail("quick_check: "+check_value);
}
}

int main(int argc, char** argv) {
    try {
        if (argc!=3) {
            std::cerr<<"Usage: point_table_build STATION_140_CSV OUTPUT_SQLITE\n";
            return 2;
        }
        const auto rows=read_baseline(std::filesystem::u8path(argv[1]));
        if (rows.empty()) fail("station 140 baseline has no rows");
        write_database(std::filesystem::u8path(argv[2]), rows);
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"point_table_build: "<<error.what()<<'\n';
        return 1;
    }
}
