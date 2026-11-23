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
    void null(int i){if(sqlite3_bind_null(s,i)!=SQLITE_OK)throw std::runtime_error("bind null failed");}
    std::string text(int i){auto t=sqlite3_column_text(s,i); return t?reinterpret_cast<const char*>(t):"UNKNOWN";}
};
std::int64_t scalar(Db& d,const char* sql){Stmt s(d,sql); if(s.step()!=SQLITE_ROW)throw std::runtime_error("missing metadata");return sqlite3_column_int64(s.s,0);}
void validate(Db& d){auto v=scalar(d,"PRAGMA user_version"); if(scalar(d,"PRAGMA application_id")!=app_id || (v!=1 && v!=2 && v!=3))throw std::runtime_error("not a cpp-runtime simulation schema v1/v2/v3; refused");}
void write_log(Db& db, std::int64_t t, const std::string& level, const std::string& category, const std::string& object, const std::string& text, std::int64_t frame_seq){
    Stmt s(db,"INSERT INTO sim_logs(received_ms,level,category,object,text,frame_seq) VALUES(?,?,?,?,?,?)");
    s.integer(1,t); s.text(2,level); s.text(3,category); s.text(4,object); s.text(5,text);
    if(frame_seq<0) s.null(6); else s.integer(6,frame_seq);
    s.step();
    db.exec("DELETE FROM sim_logs WHERE id < COALESCE((SELECT MIN(id) FROM (SELECT id FROM sim_logs ORDER BY id DESC LIMIT 300)), 0)");
}
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
    RuntimeInfo info;
    bool seen_frame=false;
    std::string prev_link, prev_quality, prev_event, prev_carts[3];
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
};
Writer::Writer(const std::filesystem::path& dir):p(std::make_unique<Impl>()) {
    std::filesystem::create_directories(dir);
    // A dedicated directory is mandatory; never reuse legacy runtime.sqlite.
    for(const auto& e:std::filesystem::directory_iterator(dir)){
        const auto n=e.path().filename().string();
        if(e.is_symlink() || !e.is_regular_file() || (n!="simulation.sqlite"&&n!="simulation.sqlite-wal"&&n!="simulation.sqlite-shm"&&n!="writer.lease"&&n!="writer.lease-journal"&&n!="control.mailbox"))
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
    if(scalar(*p->db,"PRAGMA user_version")==1){
        p->db->exec("BEGIN IMMEDIATE; CREATE TABLE runtime_state(id INTEGER PRIMARY KEY CHECK(id=1),mode TEXT NOT NULL,revision INTEGER NOT NULL,heartbeat_ms INTEGER NOT NULL,boot TEXT NOT NULL); INSERT INTO runtime_state VALUES(1,'automatic',0,0,''); PRAGMA user_version=2; COMMIT;");
    }
    if(scalar(*p->db,"PRAGMA user_version")==2){
        p->db->exec("BEGIN IMMEDIATE; CREATE TABLE IF NOT EXISTS sim_logs(id INTEGER PRIMARY KEY AUTOINCREMENT,received_ms INTEGER NOT NULL,level TEXT NOT NULL,category TEXT NOT NULL,object TEXT NOT NULL,text TEXT NOT NULL,frame_seq INTEGER); PRAGMA user_version=3; COMMIT;");
    }
    Stmt state(*p->db,"SELECT mode,revision FROM runtime_state WHERE id=1");
    if(state.step()!=SQLITE_ROW)throw std::runtime_error("missing runtime state");
    p->info.mode=state.text(0);p->info.revision=sqlite3_column_int64(state.s,1);
    if(p->info.revision<0||(p->info.mode!="manual"&&p->info.mode!="automatic"))throw std::runtime_error("invalid runtime mode");
    p->info.boot=p->boot;
    Stmt last(*p->db,"SELECT link,quality,event,cart1,cart2,cart3 FROM frames ORDER BY seq DESC LIMIT 1");
    if(last.step()==SQLITE_ROW){
        p->seen_frame=true;
        p->prev_link=last.text(0); p->prev_quality=last.text(1); p->prev_event=last.text(2);
        for(int i=0;i<3;++i) p->prev_carts[i]=last.text(3+i);
    }
    publish(nullptr,p->info.mode);

}
void Writer::note(const std::string& level, const std::string& category, const std::string& object, const std::string& text, std::int64_t frame_seq){
    auto t=now_ms();
    p->db->exec("BEGIN IMMEDIATE");
    try { write_log(*p->db,t,level,category,object,text,frame_seq); p->db->exec("COMMIT"); }
    catch(...){ p->db->exec("ROLLBACK"); throw; }
}
Writer::~Writer()=default;
RuntimeInfo Writer::info() const{return p->info;}
int Writer::last_phase() const{
    auto phase=scalar(*p->db,"SELECT COALESCE((SELECT phase FROM frames ORDER BY seq DESC LIMIT 1),-1)");
    if(phase < -1 || phase > 15)throw std::runtime_error("invalid persisted simulation phase");
    return static_cast<int>(phase);
}
void Writer::append(const Observation& observation){publish(&observation,p->info.mode);}
void Writer::publish(const Observation* observation,const std::string& mode,bool command){
    if(mode!="manual"&&mode!="automatic")throw std::runtime_error("invalid mode");
    auto t=now_ms();
    auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-p->start).count();
    p->db->exec("BEGIN IMMEDIATE");
    try {
        if(observation){
        const auto& o=*observation;
        Stmt s(*p->db,"INSERT INTO frames(received_ms,source_ms,server_ms,boot,monotonic_ns,phase,cart1,cart2,cart3,quality,reason,link,alarm,event) VALUES(?,?,NULL,?,?,?,?,?,?,?,?,?,?,?)");
        s.integer(1,t);if(o.link!="down")s.integer(2,t);s.text(3,p->boot);s.integer(4,ns);s.integer(5,o.phase);
        for(int i=0;i<3;++i)if(o.available)s.text(6+i,o.state.carts[i]);
        s.text(9,o.quality);s.text(10,o.reason);s.text(11,o.link);s.text(12,o.alarm);s.text(13,o.event);s.step();
        const auto seq=sqlite3_last_insert_rowid(p->db->db);
        const bool link_changed=!p->seen_frame || o.link!=p->prev_link;
        const bool quality_changed=!p->seen_frame || o.quality!=p->prev_quality;
        bool carts_changed=o.available && !p->seen_frame;
        if(o.available) for(int i=0;i<3;++i) if(o.state.carts[i]!=p->prev_carts[i]) carts_changed=true;
        if(link_changed || quality_changed){
            const auto level=o.link=="down" || o.quality!="good" ? "warn" : "info";
            std::string text;
            if(o.event=="SIM_MANUAL_DISCONNECT") text="手动模拟断链，当前值未知。不补零。";
            else if(o.link=="down") text=o.quality!="good" ? "模拟链路断开，数据质量不可用。不补零。" : "模拟链路断开";
            else if(p->seen_frame && p->prev_link=="down") text=o.quality=="good" ? "模拟链路恢复，数据质量正常" : "模拟链路恢复，数据质量不可用。不补零。";
            else if(o.quality!="good") text="数据质量不可用，原因 "+o.reason+"。不补零。";
            else if(quality_changed && p->seen_frame) text="数据质量恢复为正常";
            else text="模拟链路接通，数据质量正常";
            write_log(*p->db,t,level,"collector","sim.three-cart.v1",text,seq);
        }
        if(carts_changed){
            write_log(*p->db,t,"info","business","sim.three-cart.v1",
                "三车阶段 "+o.state.carts[0]+" / "+o.state.carts[1]+" / "+o.state.carts[2]+"。未连接的协议不记为成功。",seq);
        }
        p->seen_frame=true; p->prev_link=o.link; p->prev_quality=o.quality; p->prev_event=o.event;
        for(int i=0;i<3;++i) p->prev_carts[i]=o.state.carts[i];
        }
        Stmt status(*p->db,"UPDATE runtime_state SET mode=?,revision=?,heartbeat_ms=?,boot=? WHERE id=1");
        status.text(1,mode);status.integer(2,p->info.revision+(command?1:0));status.integer(3,t);status.text(4,p->boot);status.step();
        p->db->exec("COMMIT");
        p->info.mode=mode;p->info.revision+=command?1:0;p->info.heartbeat_ms=t;p->info.liveness="recent";
    }catch(...){p->db->exec("ROLLBACK");throw;}
}
View read(const std::filesystem::path& dir,const std::string& command,int limit){
    if(limit<1||limit>200)throw std::runtime_error("limit must be 1..200");
    if(command!="status"&&command!="history"&&command!="recent"&&command!="alarms")throw std::runtime_error("unknown command; use help");
    Db d(dir/"simulation.sqlite",SQLITE_OPEN_READONLY);d.exec("PRAGMA query_only=ON");validate(d);d.exec("BEGIN");
    // Indexed, bounded tail queries. alarms reports the latest condition, not a fabricated occurrence history.
    if(command=="status"||command=="alarms")limit=1;
    Stmt s(d,"SELECT seq,received_ms,source_ms,boot,cart1,cart2,cart3,quality,reason,link,alarm,event FROM frames ORDER BY seq DESC LIMIT ?");
    s.integer(1,limit+1);View v;
    if(scalar(d,"PRAGMA user_version")>=2){
        Stmt state(d,"SELECT mode,revision,heartbeat_ms,boot FROM runtime_state WHERE id=1");
        if(state.step()==SQLITE_ROW){v.runtime.mode=state.text(0);v.runtime.revision=sqlite3_column_int64(state.s,1);v.runtime.heartbeat_ms=sqlite3_column_int64(state.s,2);v.runtime.boot=state.text(3);auto age=now_ms()-v.runtime.heartbeat_ms;v.runtime.liveness=age<0?"clock_uncertain":age>1500?"stale":"recent";}
    }
    while(s.step()==SQLITE_ROW){
        if(static_cast<int>(v.rows.size())==limit){v.more=true;break;}
        Row r;r.seq=sqlite3_column_int64(s.s,0);r.received_ms=sqlite3_column_int64(s.s,1);r.source_ms=sqlite3_column_int64(s.s,2);r.boot=s.text(3);
        for(int i=0;i<3;++i)r.carts[i]=s.text(4+i);
        r.quality=s.text(7);r.reason=s.text(8);r.link=s.text(9);r.alarm=s.text(10);r.event=s.text(11);v.rows.push_back(r);
    }
    if(scalar(d,"PRAGMA user_version")>=3){
        Stmt logs(d,"SELECT id,received_ms,level,category,object,text,frame_seq FROM sim_logs ORDER BY id DESC LIMIT 80");
        while(logs.step()==SQLITE_ROW){
            LogRecord row; row.id=sqlite3_column_int64(logs.s,0); row.received_ms=sqlite3_column_int64(logs.s,1);
            row.level=logs.text(2); row.category=logs.text(3); row.object=logs.text(4); row.text=logs.text(5);
            row.has_frame=sqlite3_column_type(logs.s,6)!=SQLITE_NULL; if(row.has_frame) row.frame_seq=sqlite3_column_int64(logs.s,6);
            v.logs.push_back(row);
        }
    }
    v.freshness=v.rows.empty()?"no_evidence":fresh(v.rows.front().received_ms);return v;
}
std::string render(const View& v){
    std::ostringstream out;
    out<<"SIMULATION_ONLY\tcoverage=unknown\tfreshness="<<v.freshness<<"\tmode="<<v.runtime.mode<<"\tprocess="<<v.runtime.liveness<<"\tmore="<<(v.more?"true":"false")<<"\n";
    out<<"seq\treceived_utc_ms\tsource_utc_ms\tboot\tcart1\tcart2\tcart3\tquality\treason\tlink\talarm_condition\tevent\n";
    for(const auto& r:v.rows){out<<r.seq<<'\t'<<r.received_ms<<'\t'<<(r.source_ms?std::to_string(r.source_ms):"UNKNOWN")<<'\t'<<r.boot;
        for(const auto& c:r.carts)out<<'\t'<<c;
        out<<'\t'<<r.quality<<'\t'<<r.reason<<'\t'<<r.link<<'\t'<<r.alarm<<'\t'<<r.event<<'\n';}
    out<<"Evidence only; no root-cause inference. Historical good values do not prove current availability.\n";return out.str();
}
std::string help(){return "SIMULATION only. Commands: status | recent [1..200] | history [1..200] | alarms | help | exit\nTail history only; coverage unknown; alarms = latest observed condition. No SQL, shell, writes or network.\n";}
}
