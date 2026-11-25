#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr auto database_name = "simulation.sqlite";
constexpr auto staging_name = ".snapshot.partial";
constexpr sqlite3_int64 max_bytes = 64 * 1024 * 1024;

using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Database open(const fs::path& path, int flags, Clock::time_point& deadline) {
    sqlite3* handle = nullptr;
    const auto code = sqlite3_open_v2(path.u8string().c_str(), &handle, flags, nullptr);
    Database db(handle, sqlite3_close);
    if (code != SQLITE_OK) throw std::runtime_error("cannot open simulation database");
    sqlite3_busy_timeout(db.get(), 100);
    sqlite3_progress_handler(db.get(), 1000, [](void* limit) {
        return Clock::now() >= *static_cast<Clock::time_point*>(limit) ? 1 : 0;
    }, &deadline);
    return db;
}

void exec(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}

Statement query(sqlite3* db, const char* sql) {
    sqlite3_stmt* handle = nullptr;
    const auto code = sqlite3_prepare_v2(db, sql, -1, &handle, nullptr);
    Statement stmt(handle, sqlite3_finalize);
    if (code != SQLITE_OK || sqlite3_step(stmt.get()) != SQLITE_ROW)
        throw std::runtime_error(sqlite3_errmsg(db));
    return stmt;
}

sqlite3_int64 number(sqlite3* db, const char* sql) {
    return sqlite3_column_int64(query(db, sql).get(), 0);
}

std::string text(sqlite3* db, const char* sql) {
    const auto stmt = query(db, sql);
    const auto* value = sqlite3_column_text(stmt.get(), 0);
    if (!value) throw std::runtime_error("missing archive metadata");
    return reinterpret_cast<const char*>(value);
}

void copy_pages(sqlite3* target, sqlite3* source, Clock::time_point deadline) {
    auto* handle = sqlite3_backup_init(target, "main", source, "main");
    if (!handle) throw std::runtime_error("could not initialize SQLite backup");
    int status = SQLITE_OK;
    while (Clock::now() < deadline) {
        status = sqlite3_backup_step(handle, 64);
        if (status == SQLITE_DONE) break;
        if (status == SQLITE_BUSY || status == SQLITE_LOCKED)
            std::this_thread::sleep_for(10ms);
        else if (status != SQLITE_OK) break;
    }
    if (sqlite3_backup_finish(handle) != SQLITE_OK || status != SQLITE_DONE)
        throw std::runtime_error("SQLite backup failed or exceeded time limit");
}

void archive(const fs::path& input, const fs::path& output, bool restoring) {
    if (!fs::is_directory(fs::symlink_status(input)) ||
        !fs::is_regular_file(fs::symlink_status(input / database_name)))
        throw std::runtime_error("expected a dedicated simulation directory with a regular database");
    for (const auto* name : {"simulation.sqlite-wal", "simulation.sqlite-shm", "simulation.sqlite-journal"})
        if (fs::is_symlink(fs::symlink_status(input / name)))
            throw std::runtime_error("symbolic-link sidecar refused");
    if (restoring) {
        for (const auto& entry : fs::directory_iterator(input))
            if (entry.path().filename() != database_name)
                throw std::runtime_error("restore requires a standalone simulation archive");
    }

    auto deadline = Clock::now() + 10s;
    const auto source = open(input / database_name, SQLITE_OPEN_READONLY, deadline);
    exec(source.get(), "PRAGMA query_only=ON; PRAGMA trusted_schema=OFF; BEGIN");
    const auto version = number(source.get(), "PRAGMA user_version");
    if (number(source.get(), "PRAGMA application_id") != 1279471699 || version < 1 || version > 4)
        throw std::runtime_error("unsupported simulation schema");
    if (restoring && text(source.get(), "PRAGMA journal_mode") != "delete")
        throw std::runtime_error("restore requires standalone DELETE journal mode");
    const auto page_size = number(source.get(), "PRAGMA page_size");
    if (page_size <= 0 || number(source.get(), "PRAGMA page_count") > max_bytes / page_size)
        throw std::runtime_error("simulation database exceeds the 64 MiB archive limit");

    if (!fs::create_directory(output)) throw std::runtime_error("output must be a new directory");
    const auto staging = output / staging_name;
    try {
#ifndef _WIN32
        fs::permissions(output, fs::perms::owner_all, fs::perm_options::replace);
#endif
        {
            const auto target = open(staging, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, deadline);
            exec(target.get(), "PRAGMA synchronous=FULL; PRAGMA trusted_schema=OFF");
            copy_pages(target.get(), source.get(), deadline);
            if (text(target.get(), "PRAGMA journal_mode=DELETE") != "delete" ||
                text(target.get(), "PRAGMA quick_check(1)") != "ok")
                throw std::runtime_error("archive integrity check failed");
            if (version >= 2)
                exec(target.get(), "UPDATE runtime_state SET heartbeat_ms=0, boot='' WHERE id=1");
        }
        if (fs::exists(fs::symlink_status(output / database_name)))
            throw std::runtime_error("output database already exists");
        fs::rename(staging, output / database_name);
    } catch (...) {
        std::error_code ignored;
        for (const auto* suffix : {"", "-journal", "-wal", "-shm"})
            fs::remove(output / (std::string(staging_name) + suffix), ignored);
        fs::remove(output, ignored);
        throw;
    }
}
}

int archive_main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (argc != 4 || (mode != "backup" && mode != "restore")) {
        std::cerr << "Usage: simulation-archive backup|restore SOURCE_DIR NEW_OUTPUT_DIR\n";
        return 2;
    }
    try {
        archive(fs::u8path(argv[2]), fs::u8path(argv[3]), mode == "restore");
        std::cout << "SIMULATION_ONLY: " << mode << " complete\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "simulation-archive: " << error.what() << '\n';
        return 1;
    }
}
