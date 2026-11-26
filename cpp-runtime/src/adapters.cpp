#include "adapters.hpp"

namespace sim {
namespace {
Observation interrupted(const domain::State& state, int phase, bool linkDown, const char* event) {
    Observation observation;
    observation.state=state;
    observation.phase=phase;
    observation.available=false;
    observation.quality="bad";
    observation.reason=linkDown ? "disconnected" : "source_bad";
    observation.link=linkDown ? "down" : "up";
    observation.alarm="unknown";
    observation.event=event;
    return observation;
}
}

Observation SimulationAdapter::observe(const char* event) const {
    Observation observation;
    observation.state=state_;
    observation.phase=lastPhase_;
    observation.alarm=state_.current<state_.carts.size() && state_.carts[state_.current]=="blocked" ? "active" : "cleared";
    observation.event=event;
    return observation;
}

Observation SimulationAdapter::disconnect() {
    manuallyDisconnected_=true;
    return interrupted(state_,lastPhase_,true,"SIM_MANUAL_DISCONNECT");
}

void SimulationAdapter::resume_after(int lastPhase, bool manualDisconnect) {
    for (int i=0;i<=lastPhase;++i) next();
    manuallyDisconnected_=manualDisconnect;
}

Observation SimulationAdapter::next() {
    using domain::Action;
    if (manuallyDisconnected_) {
        manuallyDisconnected_=false;
        if (lastPhase_!=10 && lastPhase_!=11) return observe("SIM_MANUAL_LINK_RECOVERED");
    }

    lastPhase_=nextPhase_;
    nextPhase_=(nextPhase_+1)%16;
    switch (lastPhase_) {
    case 0:
        state_=domain::State{};
        state_=domain::advance(state_,Action::scan);
        return observe("SIM_ROUND_STARTED");
    case 3: case 9:
        state_=domain::advance(state_,Action::scan);
        return observe("SIM_SCAN_STARTED");
    case 1: case 4: case 13:
        state_=domain::advance(state_,Action::sort);
        return observe("SIM_SCAN_CONFIRMED");
    case 2: case 8: case 14:
        state_=domain::advance(state_,Action::complete);
        return observe("SIM_CART_COMPLETE");
    case 5:
        state_=domain::advance(state_,Action::block);
        return observe("SIM_TASK_BLOCKED");
    case 6:
        return interrupted(state_,lastPhase_,false,"SIM_BAD_QUALITY");
    case 7:
        state_=domain::advance(state_,Action::resume);
        return observe("SIM_ALARM_CLEARED");
    case 10: case 11:
        return interrupted(state_,lastPhase_,true,"SIM_LINK_DOWN");
    case 12:
        return observe("SIM_LINK_RECOVERED");
    default:
        return observe("SIM_STATION_COMPLETE");
    }
}
}
