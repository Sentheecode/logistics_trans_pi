// Test harness only. Not the read-only diagnose CLI, not installed or used by Pi.
#include "core.hpp"
#include "control.hpp"
#include <iostream>
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;
        auto view=sim::read(std::filesystem::u8path(argv[1]),"status");
        if(view.runtime.liveness!="recent")throw std::runtime_error("collector heartbeat unavailable");
        auto result=sim::send_control(std::filesystem::u8path(argv[1]),view.runtime.boot,view.runtime.revision,argv[2]);
        std::cout<<result<<'\n';return result=="ok"?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
