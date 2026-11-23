#include "domain.hpp"
#include <stdexcept>
namespace domain {
State advance(State s, Action a){
    if(s.current>=s.carts.size())throw std::logic_error("station already complete");
    auto& cart=s.carts[s.current];
    if(a==Action::scan&&cart=="waiting")cart="scanning";
    else if(a==Action::sort&&cart=="scanning")cart="sorting";
    else if(a==Action::block&&cart=="sorting")cart="blocked";
    else if(a==Action::resume&&cart=="blocked")cart="sorting";
    else if(a==Action::complete&&cart=="sorting"){cart="station_complete";++s.current;}
    else throw std::logic_error("invalid demonstration transition");
    return s;
}
}
