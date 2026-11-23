#include "core.hpp"
#include <csignal>
#include <iostream>
#include <thread>
namespace { volatile std::sig_atomic_t stopped=0; void stop(int){stopped=1;} }
int main(int argc,char** argv){
    try {
        if(argc<2||argc>4){std::cerr<<"Usage: collector DATA_DIR [ticks:1..1000000; omitted=continuous] [interval_ms:20..1000]\n";return 2;}
        int count=argc>=3?sim::number(argv[2],1,1000000):0,interval=argc>=4?sim::number(argv[3],20,1000):500;
        std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
        sim::Writer writer(std::filesystem::u8path(argv[1]));
        sim::SimulationAdapter source;
        std::cout<<"SIMULATION_ONLY: independent collector; dedicated SQLite writer\n"<<std::flush;
        for(int i=0;!stopped&&(count==0||i<count);i=(i+1)%100000000){
            writer.append(source.next());
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"collector error: "<<e.what()<<'\n';return 1;}
}
