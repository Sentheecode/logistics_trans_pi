#include "control.hpp"
#include "core.hpp"
#include <chrono>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace sim {
namespace {
// Lock lifetime is an open OS handle; process death cannot leave a stale mutex.
class Mailbox {
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    OVERLAPPED overlap{};
#else
    int handle=-1;
#endif
public:
    Mailbox(const std::filesystem::path& dir,bool create=false){
        auto path=dir/"control.mailbox";
        if(std::filesystem::is_symlink(path))throw std::runtime_error("unsafe control path");
#ifdef _WIN32
        handle=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
                           nullptr,create?OPEN_ALWAYS:OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("control unavailable");
        if(!LockFileEx(handle,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,1,0,&overlap)){
            CloseHandle(handle);handle=INVALID_HANDLE_VALUE;throw std::runtime_error("control busy");
        }
#else
        handle=::open(path.c_str(),O_RDWR|O_NOFOLLOW|(create?O_CREAT:0),0600);
        if(handle<0)throw std::runtime_error("control unavailable");
        struct stat info{};
        if(fstat(handle,&info)!=0||!S_ISREG(info.st_mode)){::close(handle);handle=-1;throw std::runtime_error("unsafe control path");}
        if(flock(handle,LOCK_EX|LOCK_NB)!=0){::close(handle);handle=-1;throw std::runtime_error("control busy");}
#endif
    }
    ~Mailbox(){
#ifdef _WIN32
        if(handle!=INVALID_HANDLE_VALUE){UnlockFileEx(handle,0,1,0,&overlap);CloseHandle(handle);}
#else
        if(handle>=0){flock(handle,LOCK_UN);::close(handle);}
#endif
    }
    std::string read(){
        char data[513];
#ifdef _WIN32
        SetFilePointer(handle,0,nullptr,FILE_BEGIN);DWORD n=0;
        if(!ReadFile(handle,data,sizeof data,&n,nullptr))throw std::runtime_error("control read failed");
#else
        if(lseek(handle,0,SEEK_SET)<0)throw std::runtime_error("control seek failed");
        auto n=::read(handle,data,sizeof data);if(n<0)throw std::runtime_error("control read failed");
#endif
        if(n>512)throw std::runtime_error("control message too large");
        return std::string(data,static_cast<std::size_t>(n));
    }
    void write(const std::string& value){
        if(value.size()>512)throw std::runtime_error("control message too large");
#ifdef _WIN32
        SetFilePointer(handle,0,nullptr,FILE_BEGIN);DWORD n=0;
        if(!WriteFile(handle,value.data(),static_cast<DWORD>(value.size()),&n,nullptr)||n!=value.size()||!SetEndOfFile(handle))throw std::runtime_error("control write failed");
#else
        if(lseek(handle,0,SEEK_SET)<0||::write(handle,value.data(),value.size())!=static_cast<ssize_t>(value.size())||ftruncate(handle,static_cast<off_t>(value.size()))!=0)throw std::runtime_error("control write failed");
#endif
    }
};
std::string identifier(){std::random_device r;std::ostringstream s;s<<std::hex<<r()<<r()<<r()<<r();return s.str();}
bool hex(const std::string& s){return !s.empty()&&s.size()<=64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
}
bool control_command(const std::string& c){return c=="manual"||c=="automatic_confirmed"||c=="step"||c=="disconnect";}
void initialize_control(const std::filesystem::path& dir){Mailbox box(dir,true);box.write("");}
void poll_control(const std::filesystem::path& dir,const std::function<std::string(const ControlRequest&)>& apply){
    try{
        Mailbox box(dir);std::string message;
        try{message=box.read();}catch(const std::exception& e){
            if(std::string(e.what())!="control message too large")throw;
            box.write("R invalid "+std::to_string(now_ms())+" invalid");return;
        }
        if(message.empty())return;
        std::istringstream in(message);std::string type,extra;ControlRequest r;
        in>>type;
        if(type=="R"){
            std::string result;
            if(!(in>>r.id>>r.expires>>result)||!hex(r.id)||r.expires>now_ms()+1800||(in>>extra))box.write("");
            return; // A valid receipt stays until its client acknowledges or its deadline expires.
        }
        in>>r.id>>r.boot>>r.revision>>r.expires>>r.command;
        std::string result="invalid";
        if(in&&! (in>>extra)&&type=="Q"&&hex(r.id)&&hex(r.boot)&&r.revision>=0&&control_command(r.command)){
            auto now=now_ms();
            result=r.expires<=now||r.expires>now+1800?"expired":apply(r);
        }
        box.write("R "+(hex(r.id)?r.id:"invalid")+" "+std::to_string((result=="invalid"||result=="expired")?now_ms():r.expires)+" "+result);
    }catch(const std::exception& e){
        if(std::string(e.what())!="control busy")throw;
    }
}
std::string send_control(const std::filesystem::path& dir,const std::string& boot,std::int64_t revision,const std::string& command){
    if(!control_command(command)||!hex(boot)||revision<0)throw std::runtime_error("invalid control request");
    const auto id=identifier();const auto expires=now_ms()+1500;
    {
        Mailbox box(dir);auto existing=box.read();
        if(!existing.empty()){
            std::istringstream in(existing);std::string type,old_id,old_boot;std::int64_t old_revision=0,deadline=0;
            in>>type>>old_id;
            if(type=="Q")in>>old_boot>>old_revision>>deadline;else if(type=="R")in>>deadline;
            if(!in||deadline>now_ms())throw std::runtime_error("control busy");
        }
        box.write("Q "+id+" "+boot+" "+std::to_string(revision)+" "+std::to_string(expires)+" "+command);
    }
    auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(1900);
    while(std::chrono::steady_clock::now()<until){
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        try{
            Mailbox box(dir);std::istringstream in(box.read());std::string type,reply_id,result;std::int64_t deadline;
            if(in>>type>>reply_id>>deadline>>result&&type=="R"&&reply_id==id){box.write("");return result;}
        }catch(const std::exception& e){if(std::string(e.what())!="control busy")throw;}
    }
    // A timed-out operation may have committed; never retry automatically.
    throw std::runtime_error("control timeout; outcome unknown, refresh status before a new action");
}
}
