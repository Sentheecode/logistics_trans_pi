#include "core.hpp"
#include "catalog.hpp"
#include "snapshot_json.hpp"
#include <charconv>
#include <iomanip>
#include <iostream>
#include <locale>
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
void rollup(const std::filesystem::path& dir,sim::TimeWindow window,int width) {
    const auto buckets=sim::event_buckets(dir,window,width);
    std::cout<<"{\"schema\":\"ltp.cpp.event-counts.v1\",\"simulation_only\":true,\"scope\":\""
             <<catalog::kStation140<<"\",\"coverage\":\"unknown\",\"from_inclusive\":"<<window.from_ms
             <<",\"to_exclusive\":"<<window.to_ms<<",\"bucket_ms\":"<<width<<",\"buckets\":[";
    bool first=true;
    for (const auto& b:buckets) {
        if (!first) std::cout<<',';
        first=false;
        std::cout<<"{\"start_utc_ms\":"<<b.start_ms<<",\"samples\":"<<b.samples
                 <<",\"good\":"<<b.good<<",\"bad\":"<<b.bad<<",\"uncertain\":"<<b.uncertain
                 <<",\"unknown_quality\":"<<b.unknown_quality<<",\"source_time\":"<<b.source
                 <<",\"server_time\":"<<b.server<<",\"received_time\":"<<b.received
                 <<",\"good_complete_frames\":"<<b.complete<<",\"good_active_alarm_frames\":"<<b.active_alarm<<'}';
    }
    std::cout<<"]}\n";
}
void tag_rollup(const std::filesystem::path& dir,std::string_view id,sim::TimeWindow window,int width) {
    const auto tag=sim::numeric_tag(id);
    if (!tag) throw std::runtime_error("unsupported simulation tag");
    const auto buckets=sim::tag_buckets(dir,window,width,*tag);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<std::setprecision(17)<<"{\"schema\":\"ltp.cpp.tag-rollup.v1\",\"simulation_only\":true,"
        << "\"scope\":\""<<catalog::kStation140<<"\",\"coverage\":\"unknown\",\"tag\":\""<<id
        <<"\",\"from_inclusive\":"<<window.from_ms<<",\"to_exclusive\":"<<window.to_ms
        <<",\"bucket_ms\":"<<width<<",\"buckets\":[";
    bool first=true;
    for (const auto& b:buckets) {
        if (!first) out<<',';
        first=false;
        out<<"{\"start_utc_ms\":"<<b.start_ms<<",\"samples\":"<<b.samples
           <<",\"usable_values\":"<<b.usable_values<<",\"min\":";
        if (b.usable_values) out<<*b.minimum<<",\"max\":"<<*b.maximum<<",\"avg\":"<<*b.mean;
        else out<<"null,\"max\":null,\"avg\":null";
        out<<'}';
    }
    std::cout<<out.str()<<"]}\n";
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
    if (argc<2||argc>7) {
        std::cerr<<"Usage: diagnose DATA_DIR [command [limit]]\n"
                 <<"       diagnose DATA_DIR snapshot-json [limit]\n"
                 <<"       diagnose DATA_DIR window-json FROM_UTC_MS TO_UTC_MS [limit]\n"
                 <<"       diagnose DATA_DIR rollup-json FROM_UTC_MS TO_UTC_MS BUCKET_MS\n"
                 <<"       diagnose DATA_DIR tag-rollup-json TAG FROM_UTC_MS TO_UTC_MS BUCKET_MS\n";
        return 2;
    }
    try {
        const auto dir=std::filesystem::u8path(argv[1]);
        if (argc>=3 && std::string_view(argv[2])=="tag-rollup-json") {
            if (argc!=7) throw std::runtime_error("tag-rollup-json requires TAG FROM_UTC_MS TO_UTC_MS BUCKET_MS");
            tag_rollup(dir,argv[3],{utc_ms(argv[4]),utc_ms(argv[5])},sim::number(argv[6],1000,3600000));
            return 0;
        }
        if (argc>=3 && std::string_view(argv[2])=="rollup-json") {
            if (argc!=6) throw std::runtime_error("rollup-json requires FROM_UTC_MS TO_UTC_MS BUCKET_MS");
            rollup(dir,{utc_ms(argv[3]),utc_ms(argv[4])},sim::number(argv[5],1000,3600000));
            return 0;
        }
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
