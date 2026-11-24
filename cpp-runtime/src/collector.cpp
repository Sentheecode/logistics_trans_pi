#include "core.hpp"
#include "control.hpp"
#include "supervisor.hpp"
#include <csignal>
#include <iostream>
#include <thread>
namespace { volatile std::sig_atomic_t stopped=0; void stop(int){stopped=1;} }
namespace {
int collect(const std::filesystem::path& dir,int count,int interval,const sim::Pulse& pulse){
    sim::Writer writer(dir);sim::SimulationAdapter source;source.resume_after(writer.last_phase());
    sim::initialize_control(dir);
    pulse();
    std::cout<<"SIMULATION_ONLY: independent collector; dedicated SQLite writer\n"<<std::flush;
    auto next=std::chrono::steady_clock::now(),heartbeat=next;
    int samples=0;
    while(!stopped&&(count==0||samples<count)){
        sim::poll_control(dir,[&](const sim::ControlRequest& request){
            auto audit=[&](const std::string& result){return "本地模拟操作发出 "+request.command+"，结果 "+result+"。没有操作者身份，不记录凭据。";};
            auto reject=[&](const std::string& result){
                writer.note("warn","audit","local-sim",audit(result),-1);
                return result;
            };
            auto info=writer.info();
            if(request.boot!=info.boot)return reject("session_changed");
            if(request.revision!=info.revision)return reject("stale_revision");
            if(request.command=="manual"){
                if(info.mode=="manual")return reject("already_manual");
                writer.publish(nullptr,"manual",true,audit("ok"));
            }else if(request.command=="automatic_confirmed"){
                if(info.mode!="manual")return reject("already_automatic");
                writer.publish(nullptr,"automatic",true,audit("ok"));
                next=std::chrono::steady_clock::now()+std::chrono::milliseconds(interval);
            }else{
                if(info.mode!="manual")return reject("manual_required");
                sim::Observation observation;
                if(request.command=="step")observation=source.next();
                else{observation.phase=writer.last_phase();observation.available=false;observation.quality="bad";observation.reason="disconnected";observation.link="down";observation.alarm="unknown";observation.event="SIM_MANUAL_DISCONNECT";}
                writer.publish(&observation,"manual",true,audit("ok"));++samples;
            }
            return std::string("ok");
        });
        auto now=std::chrono::steady_clock::now();
        if(writer.info().mode=="automatic"&&now>=next){writer.append(source.next());++samples;next=now+std::chrono::milliseconds(interval);}
        if(now>=heartbeat){writer.publish(nullptr,writer.info().mode);pulse();heartbeat=now+std::chrono::milliseconds(200);}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}
}
int collector_main(int argc,char** argv){
    try {
        if(argc<2||argc>4){
            std::cerr<<"Usage: collector DATA_DIR [ticks:1..1000000] [interval_ms:20..1000]\n"
                     <<"       collector --supervise DATA_DIR [interval_ms] (Linux)\n";
            return 2;
        }
        std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
        if (std::string(argv[1])=="--supervise"){
            if (argc<3) throw std::runtime_error("supervisor needs DATA_DIR");
            const auto dir=std::filesystem::u8path(argv[2]);
            const int interval=argc==4?sim::number(argv[3],20,1000):500;
            return sim::supervise_collector([&](const sim::Pulse& pulse){return collect(dir,0,interval,pulse);},stopped);
        }
        const int count=argc>=3?sim::number(argv[2],1,1000000):0;
        const int interval=argc>=4?sim::number(argv[3],20,1000):500;
        return collect(std::filesystem::u8path(argv[1]),count,interval,[]{});
    }catch(const std::exception& e){std::cerr<<"collector error: "<<e.what()<<'\n';return 1;}
}
