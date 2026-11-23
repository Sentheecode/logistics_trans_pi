#include "adapters.hpp"
namespace sim {
Observation SimulationAdapter::next(){
    using domain::Action;
    if(phase==0)state=domain::State{};
    switch(phase){
    case 0:case 3:case 9:state=domain::advance(state,Action::scan);break;
    case 1:case 4:case 12:state=domain::advance(state,Action::sort);break;
    case 2:case 8:case 14:state=domain::advance(state,Action::complete);break;
    case 5:state=domain::advance(state,Action::block);break;
    case 7:state=domain::advance(state,Action::resume);break;
    default:break;
    }
    Observation o;o.state=state;o.phase=phase;
    const bool disconnected=phase==10||phase==11;
    o.available=!(disconnected||phase==6);
    o.quality=o.available?"good":"bad";
    o.reason=disconnected?"disconnected":phase==6?"source_bad":"simulated";
    o.link=disconnected?"down":"up";
    o.alarm=!o.available?"unknown":phase==5?"active":"cleared";
    o.event=disconnected?"SIM_LINK_DOWN":phase==12?"SIM_LINK_RECOVERED":phase==6?"SIM_BAD_QUALITY":phase==5?"SIM_TASK_BLOCKED":phase==7?"SIM_ALARM_CLEARED":phase==15?"SIM_STATION_COMPLETE":"SIM_PROGRESS";
    phase=(phase+1)%16;return o;
}
}
