#include "core.hpp"
#include <iostream>
#include <sstream>
namespace {
bool execute(const std::filesystem::path& dir,const std::string& line){
    if(line.size()>256)throw std::runtime_error("command too long");
    std::istringstream in(line);std::string command,arg,extra;in>>command>>arg>>extra;
    if(command.empty())return true;
    if(!extra.empty())throw std::runtime_error("too many arguments");
    if(command=="exit"||command=="help"){if(!arg.empty())throw std::runtime_error("unexpected argument");if(command=="help")std::cout<<sim::help();return command!="exit";}
    if((command=="status"||command=="alarms")&&!arg.empty())throw std::runtime_error("unexpected argument");
    int limit=arg.empty()?20:sim::number(arg,1,200);
    std::cout<<sim::render(sim::read(dir,command,limit));return true;
}
}
int main(int argc,char** argv){
    if(argc<2||argc>4){std::cerr<<"Usage: diagnose DATA_DIR [command [limit]]; omit command for REPL\n";return 2;}
    try {
        const auto dir=std::filesystem::u8path(argv[1]);
        if(argc>=3){execute(dir,std::string(argv[2])+(argc==4?" "+std::string(argv[3]):""));return 0;}
        std::cout<<sim::help();
        // Bounded input even for hostile or accidental huge stdin lines.
        std::string line;char c;bool overflow=false;
        while(std::cin.get(c)){
            if(c=='\n'){
                try{if(overflow)throw std::runtime_error("command too long");if(!execute(dir,line))break;}catch(const std::exception& e){std::cerr<<"diagnostic error: "<<e.what()<<'\n';}
                line.clear();overflow=false;std::cout<<std::flush;
            }else if(c!='\r'){if(line.size()<256)line+=c;else overflow=true;}
        }
        if(!line.empty()&&!overflow)execute(dir,line);
        return 0;
    }catch(const std::exception& e){std::cerr<<"diagnostic error: "<<e.what()<<'\n';return 1;}
}
