#include "core.hpp"
#include "snapshot_json.hpp"
#include <charconv>
#include <iostream>
#include <string_view>
#include <sstream>
namespace {
std::int64_t utc_ms(std::string_view value) {
    std::int64_t result;
    const auto end=value.data()+value.size();
    const auto parsed=std::from_chars(value.data(),end,result);
    if (parsed.ec!=std::errc{} || parsed.ptr!=end)
        throw std::runtime_error("expected integer UTC milliseconds");
    return result;
}
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
    if (argc<2||argc>6) {
        std::cerr<<"Usage: diagnose DATA_DIR [command [limit]]\n"
                 <<"       diagnose DATA_DIR snapshot-json [limit]\n"
                 <<"       diagnose DATA_DIR window-json FROM_UTC_MS TO_UTC_MS [limit]\n";
        return 2;
    }
    try {
        const auto dir=std::filesystem::u8path(argv[1]);
        if (argc>=3 && std::string_view(argv[2])=="window-json") {
            if (argc<5) throw std::runtime_error("window-json requires FROM_UTC_MS TO_UTC_MS [limit]");
            const sim::TimeWindow window{utc_ms(argv[3]),utc_ms(argv[4])};
            const int limit=argc==6?sim::number(argv[5],1,sim::snapshot_limit):5;
            std::cout<<sim::snapshot_json(sim::read_window(dir,window,limit),limit,window);
            return 0;
        }
        if (argc>4) throw std::runtime_error("too many arguments");
        if (argc>=3 && std::string_view(argv[2])=="snapshot-json") {
            const int limit=argc==4?sim::number(argv[3],1,sim::snapshot_limit):5;
            std::cout<<sim::snapshot_json(sim::read(dir,"recent",limit),limit);
            return 0;
        }
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
