#pragma once
#include "domain.hpp"
namespace sim {
struct Observation {
    domain::State state;
    int phase{};
    bool available=true;
    std::string quality="good",reason="simulated",link="up",alarm="cleared",event="SIM_PROGRESS";
};
// Future device implementations must supply observed evidence; no write interface.
// No OPC UA / AGV / external service adapter is implemented by this release.
class SampleSource { public: virtual ~SampleSource()=default; virtual Observation next()=0; };
class HistorySink { public: virtual ~HistorySink()=default; virtual void append(const Observation&)=0; };
class SimulationAdapter final : public SampleSource {
public: Observation next() override;
private: int phase=0; domain::State state;
};
}
