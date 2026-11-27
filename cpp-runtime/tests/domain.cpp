#include "domain.hpp"
#include <iostream>
#include <stdexcept>
int main(){
    using namespace domain;
    State s;
    auto reject=[&](Action a){try{advance(s,a);}catch(const std::logic_error&){return;}throw std::runtime_error("invalid transition accepted");};
    reject(Action::complete);
    for(unsigned i=0;i<3;++i){
        s=advance(s,Action::scan);reject(Action::complete);
        s=advance(s,Action::sort);
        s=advance(s,Action::block);reject(Action::complete);
        s=advance(s,Action::resume);s=advance(s,Action::complete);
        if(s.current!=i+1||s.carts[i]!="station_complete")throw std::runtime_error("order failed");
    }
    reject(Action::scan);reject(Action::complete);
    std::cout<<"PASS: pure domain ordering, blocked completion refusal, duplicate completion refusal\n";
}
