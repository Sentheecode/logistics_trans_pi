#include "point_audit.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace audit {
namespace {
struct Database {
    sqlite3* handle=nullptr;
    explicit Database(sqlite3* opened) : handle(opened) {}
    ~Database() { if (handle) sqlite3_close(handle); }
    Database(const Database&)=delete;
    Database& operator=(const Database&)=delete;
};

struct Statement {
    sqlite3_stmt* handle=nullptr;
    explicit Statement(sqlite3_stmt* prepared) : handle(prepared) {}
    ~Statement() { if (handle) sqlite3_finalize(handle); }
    Statement(const Statement&)=delete;
    Statement& operator=(const Statement&)=delete;
};

std::string failure(const std::string& detail) {
    return detail+" 点表不在程序内，不会回退到内置模板。";
}

[[noreturn]] void fail(const std::string& detail) {
    throw std::runtime_error(failure(detail));
}

std::filesystem::path executable_path() {
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const auto length=GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length==0 || length>=buffer.size()) fail("无法定位程序所在目录，因此不能加载点表数据库。");
    buffer.resize(length);
    return std::filesystem::path(buffer);
#else
    std::error_code error;
    auto path=std::filesystem::read_symlink("/proc/self/exe", error);
    if (error || path.empty()) fail("无法定位程序所在目录，因此不能加载点表数据库。");
    return path;
#endif
}

std::string column_text(sqlite3_stmt* statement, int index) {
    const auto* text=sqlite3_column_text(statement, index);
    if (!text) return {};
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(sqlite3_column_bytes(statement, index)));
}

bool is_null(sqlite3_stmt* statement, int index) {
    return sqlite3_column_type(statement, index)==SQLITE_NULL;
}

AddressFields read_side(sqlite3_stmt* statement, int origin, const std::string& source_row) {
    AddressFields side;
    const bool absent=is_null(statement, origin+1);
    if (absent) {
        for (int index=0; index<9; ++index) {
            if (!is_null(statement, origin+index))
                fail("点表行 "+source_row+" 的空地址侧仍带有字段。");
        }
        return side;
    }
    for (const int index : {0, 2, 3, 4, 5, 6, 7, 8}) {
        if (is_null(statement, origin+index))
            fail("点表行 "+source_row+" 的地址字段不完整。");
    }
    side.present=true;
    side.name=column_text(statement, origin);
    side.source_row=column_text(statement, origin+2);
    side.db=sqlite3_column_int(statement, origin+1);
    side.start=sqlite3_column_int(statement, origin+3);
    side.offset=sqlite3_column_int(statement, origin+4);
    side.length=sqlite3_column_int(statement, origin+5);
    side.area=column_text(statement, origin+6);
    side.type_text=column_text(statement, origin+7);
    side.direction_text=column_text(statement, origin+8);
    if (side.db<=0 || side.start<0 || side.offset<0 || side.length<=0 || side.area.empty()
        || side.type_text.empty() || side.direction_text.empty() || side.source_row.empty())
        fail("点表行 "+source_row+" 的地址字段无法解析。");
    return side;
}

void check_pragmas(sqlite3* database, const std::filesystem::path& path) {
    sqlite3_stmt* prepared=nullptr;
    if (sqlite3_prepare_v2(database, "PRAGMA application_id", -1, &prepared, nullptr)!=SQLITE_OK || !prepared)
        fail("无法读取点表数据库 "+path.u8string()+"。");
    Statement application(prepared);
    if (sqlite3_step(application.handle)!=SQLITE_ROW)
        fail("无法读取点表数据库 "+path.u8string()+"。");
    const auto application_id=sqlite3_column_int(application.handle, 0);
    if (application_id==1279471699)
        fail("文件 "+path.u8string()+" 是采集库，不是点表数据库。");
    if (application_id!=kPointTableApplicationId)
        fail("文件 "+path.u8string()+" 不是物流点表数据库。");

    sqlite3_stmt* version_statement=nullptr;
    if (sqlite3_prepare_v2(database, "PRAGMA user_version", -1, &version_statement, nullptr)!=SQLITE_OK || !version_statement)
        fail("无法读取点表数据库 "+path.u8string()+"。");
    Statement version(version_statement);
    if (sqlite3_step(version.handle)!=SQLITE_ROW || sqlite3_column_int(version.handle, 0)!=kPointTableUserVersion)
        fail("点表数据库 "+path.u8string()+" 的版本无法识别。");
}

std::string meta_value(sqlite3* database, const char* key) {
    sqlite3_stmt* prepared=nullptr;
    if (sqlite3_prepare_v2(database, "SELECT value FROM meta WHERE key=?1", -1, &prepared, nullptr)!=SQLITE_OK || !prepared)
        fail("点表数据库缺少 meta 表。");
    Statement statement(prepared);
    if (sqlite3_bind_text(statement.handle, 1, key, -1, SQLITE_TRANSIENT)!=SQLITE_OK
        || sqlite3_step(statement.handle)!=SQLITE_ROW)
        fail(std::string("点表数据库缺少元数据 ")+key+"。");
    return column_text(statement.handle, 0);
}
}

std::string compose_address(const AddressFields& fields) {
    return "DB"+std::to_string(fields.db)+"."+fields.area+std::to_string(fields.start)+"."+std::to_string(fields.offset);
}

std::filesystem::path default_point_table_path() {
    return executable_path().parent_path()/kPointTableFileName;
}

std::vector<Row> load_point_table(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        fail("找不到点表数据库 "+path.u8string()+"。");

    sqlite3* opened=nullptr;
    const auto opened_code=sqlite3_open_v2(path.u8string().c_str(), &opened, SQLITE_OPEN_READONLY, nullptr);
    Database database(opened);
    if (opened_code!=SQLITE_OK || !database.handle)
        fail("无法读取点表数据库 "+path.u8string()+"。");
    char* sql_error=nullptr;
    if (sqlite3_exec(database.handle, "PRAGMA query_only=ON; PRAGMA trusted_schema=OFF;", nullptr, nullptr, &sql_error)!=SQLITE_OK) {
        const std::string reason=sql_error ? sql_error : "query_only failed";
        sqlite3_free(sql_error);
        fail("无法读取点表数据库 "+path.u8string()+"："+reason+"。");
    }
    check_pragmas(database.handle, path);
    if (meta_value(database.handle, "schema")!=kPointTableSchema)
        fail("点表数据库 "+path.u8string()+" 的结构标识不匹配。");

    sqlite3_stmt* prepared=nullptr;
    const char* sql=
        "SELECT station_id,source_row_id,section,code,name,address_text,note,reserved,duplicate_address,doubts,"
        "executable_write,pairing_status,"
        "read_name,read_db,read_source_row_id,read_start,read_offset,read_length,read_area,read_type,read_direction,"
        "write_name,write_db,write_source_row_id,write_start,write_offset,write_length,write_area,write_type,write_direction "
        "FROM points ORDER BY ordinal";
    if (sqlite3_prepare_v2(database.handle, sql, -1, &prepared, nullptr)!=SQLITE_OK || !prepared)
        fail("点表数据库 "+path.u8string()+" 的 points 表无法读取。");
    Statement statement(prepared);

    std::vector<Row> rows;
    while (true) {
        const auto step=sqlite3_step(statement.handle);
        if (step==SQLITE_DONE) break;
        if (step!=SQLITE_ROW) fail("读取点表数据库 "+path.u8string()+" 时中断。");
        Row row;
        row.station_id=column_text(statement.handle, 0);
        row.source_row=column_text(statement.handle, 1);
        row.section=column_text(statement.handle, 2);
        row.code=column_text(statement.handle, 3);
        row.name=column_text(statement.handle, 4);
        row.address=column_text(statement.handle, 5);
        row.note=column_text(statement.handle, 6);
        if (is_null(statement.handle, 7) || is_null(statement.handle, 8) || is_null(statement.handle, 10))
            fail("点表行 "+row.source_row+" 缺少保留、重复或写权限标记。");
        const auto reserved=sqlite3_column_int(statement.handle, 7);
        const auto duplicate=sqlite3_column_int(statement.handle, 8);
        const auto executable=sqlite3_column_int(statement.handle, 10);
        if ((reserved!=0 && reserved!=1) || (duplicate!=0 && duplicate!=1))
            fail("点表行 "+row.source_row+" 的标记无法解析。");
        row.reserved=reserved==1;
        row.duplicate_address=duplicate==1;
        row.doubts=column_text(statement.handle, 9);
        row.pairing_status=column_text(statement.handle, 11);
        if (executable!=0)
            fail("点表行 "+row.source_row+" 被标成可执行写点。当前预览拒绝加载。");
        row.executable_write=false;
        if (row.station_id.empty() || row.source_row.empty() || row.section.empty() || row.address.empty() || row.pairing_status.empty())
            fail("点表行 "+row.source_row+" 缺少工位、来源行、分节或原地址。");

        row.read=read_side(statement.handle, 12, row.source_row);
        row.write=read_side(statement.handle, 21, row.source_row);
        if (row.read.present==row.write.present)
            fail("点表行 "+row.source_row+" 必须恰好有读侧或写侧一方。当前基线不合并两侧。");
        const auto& side=row.read.present ? row.read : row.write;
        if (side.source_row!=row.source_row)
            fail("点表行 "+row.source_row+" 的来源行号与地址侧不一致。");
        if (compose_address(side)!=row.address)
            fail("点表行 "+row.source_row+" 的拆分地址与原地址不一致。");
        row.type_text=side.type_text;
        row.direction_text=side.direction_text;
        row.length_text=std::to_string(side.length);
        rows.push_back(std::move(row));
    }
    if (rows.empty())
        fail("点表数据库 "+path.u8string()+" 里没有点位记录。");
    bool saw_station_140=false;
    for (const auto& row : rows) if (row.station_id=="stn.140") saw_station_140=true;
    if (!saw_station_140)
        fail("点表数据库 "+path.u8string()+" 没有工位 140 的记录。");
    return rows;
}

std::vector<Row> for_station(const std::string& station_id) {
    static const std::vector<Row> installed=load_point_table(default_point_table_path());
    std::vector<Row> rows;
    for (const auto& row : installed) if (row.station_id==station_id) rows.push_back(row);
    return rows;
}
}
