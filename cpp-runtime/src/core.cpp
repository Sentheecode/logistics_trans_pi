#include "core.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace sim {
namespace {
constexpr int app_id=1279471699;
struct Db {
    sqlite3* db{};
    Db(const std::filesystem::path& path, int flags) {
        if(sqlite3_open_v2(path.u8string().c_str(), &db, flags, nullptr)!=SQLITE_OK) {
            std::string e=db ? sqlite3_errmsg(db):"cannot open database";
            if(db) sqlite3_close(db);
            db=nullptr; throw std::runtime_error(e);
        }
        sqlite3_busy_timeout(db, 250);
    }
    ~Db(){ if(db) sqlite3_close(db); }
    void exec(const char* sql) {
        char* error=nullptr;
        if(sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK){
            std::string e=error?error:"SQLite error"; sqlite3_free(error); throw std::runtime_error(e);
        }
    }
};
struct Stmt {
    sqlite3_stmt* s{};
    explicit Stmt(Db& d,const char* sql){
        if(sqlite3_prepare_v2(d.db,sql,-1,&s,nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(d.db));
    }
    ~Stmt(){sqlite3_finalize(s);}
    int step(){int r=sqlite3_step(s); if(r!=SQLITE_ROW && r!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(s))); return r;}
    void text(int i,const std::string& v){if(sqlite3_bind_text(s,i,v.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK)throw std::runtime_error("bind text failed");}
    void integer(int i,std::int64_t v){if(sqlite3_bind_int64(s,i,v)!=SQLITE_OK)throw std::runtime_error("bind integer failed");}
    std::string text(int i){auto t=sqlite3_column_text(s,i); return t?reinterpret_cast<const char*>(t):"UNKNOWN";}
};
std::int64_t scalar(Db& d,const char* sql){Stmt s(d,sql); if(s.step()!=SQLITE_ROW)throw std::runtime_error("missing metadata");return sqlite3_column_int64(s.s,0);}
void validate(Db& d){if(scalar(d,"PRAGMA application_id")!=app_id || scalar(d,"PRAGMA user_version")!=1)throw std::runtime_error("not a cpp-runtime simulation schema v1; refused");}
std::string fresh(std::int64_t t){auto delta=now_ms()-t;return delta<0?"clock_uncertain":delta>3000?"stale":"recent";}
}
std::int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
int number(const std::string& s,int lo,int hi){
    if(s.empty() || s.size()>9 || !std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';}))throw std::runtime_error("expected bounded unsigned integer");
    auto n=std::stoll(s);if(n<lo||n>hi)throw std::runtime_error("integer outside allowed range");return static_cast<int>(n);
}
struct Writer::Impl {
    std::unique_ptr<Db> lease,db;
    std::string boot;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
};
Writer::Writer(const std::filesystem::path& dir):p(std::make_unique<Impl>()) {
    std::filesystem::create_directories(dir);
    // A dedicated directory is mandatory; never reuse legacy runtime.sqlite.
    for(const auto& e:std::filesystem::directory_iterator(dir)){
        const auto n=e.path().filename().string();
        if(e.is_symlink() || !e.is_regular_file() || (n!="simulation.sqlite"&&n!="simulation.sqlite-wal"&&n!="simulation.sqlite-shm"&&n!="writer.lease"&&n!="writer.lease-journal"))
            throw std::runtime_error("data directory contains unrelated files; use a new dedicated directory");
    }
    p->lease=std::make_unique<Db>(dir/"writer.lease",SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE);
    p->lease->exec("BEGIN EXCLUSIVE"); // OS releases on normal exit and process crash.
    bool exists=std::filesystem::exists(dir/"simulation.sqlite");
    p->db=std::make_unique<Db>(dir/"simulation.sqlite",SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE);
    if(exists) validate(*p->db);
    else {
        p->db->exec("BEGIN IMMEDIATE; PRAGMA application_id=1279471699; PRAGMA user_version=1;"
        "CREATE TABLE frames(seq INTEGER PRIMARY KEY AUTOINCREMENT,received_ms INTEGER NOT NULL,source_ms INTEGER,server_ms INTEGER,boot TEXT NOT NULL,monotonic_ns INTEGER NOT NULL,phase INTEGER NOT NULL,cart1 TEXT,cart2 TEXT,cart3 TEXT,quality TEXT NOT NULL,reason TEXT NOT NULL,link TEXT NOT NULL,alarm TEXT NOT NULL,event TEXT NOT NULL); COMMIT;");
    }
    p->db->exec("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");
    Stmt s(*p->db,"SELECT lower(hex(randomblob(16)))");s.step();p->boot=s.text(0);
}
Writer::~Writer()=default;
void Writer::append(const Observation& observation){
    const auto& o=observation;
    auto t=now_ms();
    auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-p->start).count();
    p->db->exec("BEGIN IMMEDIATE");
    try {
        Stmt s(*p->db,"INSERT INTO frames(received_ms,source_ms,server_ms,boot,monotonic_ns,phase,cart1,cart2,cart3,quality,reason,link,alarm,event) VALUES(?,?,NULL,?,?,?,?,?,?,?,?,?,?,?)");
        s.integer(1,t);if(o.link!="down")s.integer(2,t);s.text(3,p->boot);s.integer(4,ns);s.integer(5,o.phase);
        for(int i=0;i<3;++i)if(o.available)s.text(6+i,o.state.carts[i]);
        s.text(9,o.quality);s.text(10,o.reason);s.text(11,o.link);s.text(12,o.alarm);s.text(13,o.event);s.step();
        p->db->exec("COMMIT");
    }catch(...){p->db->exec("ROLLBACK");throw;}
}
View read(const std::filesystem::path& dir,const std::string& command,int limit){
    if(limit<1||limit>200)throw std::runtime_error("limit must be 1..200");
    if(command!="status"&&command!="history"&&command!="recent"&&command!="alarms")throw std::runtime_error("unknown command; use help");
    Db d(dir/"simulation.sqlite",SQLITE_OPEN_READONLY);d.exec("PRAGMA query_only=ON");validate(d);
    // Indexed, bounded tail queries. alarms reports the latest condition, not a fabricated occurrence history.
    if(command=="status"||command=="alarms")limit=1;
    Stmt s(d,"SELECT seq,received_ms,source_ms,boot,cart1,cart2,cart3,quality,reason,link,alarm,event FROM frames ORDER BY seq DESC LIMIT ?");
    s.integer(1,limit+1);View v;
    while(s.step()==SQLITE_ROW){
        if(static_cast<int>(v.rows.size())==limit){v.more=true;break;}
        Row r;r.seq=sqlite3_column_int64(s.s,0);r.received_ms=sqlite3_column_int64(s.s,1);r.source_ms=sqlite3_column_int64(s.s,2);r.boot=s.text(3);
        for(int i=0;i<3;++i)r.carts[i]=s.text(4+i);
        r.quality=s.text(7);r.reason=s.text(8);r.link=s.text(9);r.alarm=s.text(10);r.event=s.text(11);v.rows.push_back(r);
    }
    v.freshness=v.rows.empty()?"no_evidence":fresh(v.rows.front().received_ms);return v;
}
std::string render(const View& v){
    std::ostringstream out;
    out<<"SIMULATION_ONLY\tcoverage=unknown\tfreshness="<<v.freshness<<"\tmore="<<(v.more?"true":"false")<<"\n";
    out<<"seq\treceived_utc_ms\tsource_utc_ms\tboot\tcart1\tcart2\tcart3\tquality\treason\tlink\talarm_condition\tevent\n";
    for(const auto& r:v.rows){out<<r.seq<<'\t'<<r.received_ms<<'\t'<<(r.source_ms?std::to_string(r.source_ms):"UNKNOWN")<<'\t'<<r.boot;
        for(const auto& c:r.carts)out<<'\t'<<c;
        out<<'\t'<<r.quality<<'\t'<<r.reason<<'\t'<<r.link<<'\t'<<r.alarm<<'\t'<<r.event<<'\n';}
    out<<"Evidence only; no root-cause inference. Historical good values do not prove current availability.\n";return out.str();
}
std::string help(){return "SIMULATION only. Commands: status | recent [1..200] | history [1..200] | alarms | help | exit\nTail history only; coverage unknown; alarms = latest observed condition. No SQL, shell, writes or network.\n";}
}
