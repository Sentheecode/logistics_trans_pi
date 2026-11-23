#include "catalog.hpp"
#include <iostream>
#include <stdexcept>
namespace {
#define CHECK(condition) do { if (!(condition)) throw std::runtime_error("failed: " #condition); } while (0)
void expect(const std::string& actual, const std::string& token) {
    if (actual!=token) throw std::runtime_error("expected "+token+" got "+actual);
}
}

int main() {
    try {
        const auto directory=catalog::Directory::synthetic();
        CHECK(catalog::Directory::validate(directory).empty());
        CHECK(directory.factories.size()>=2);
        CHECK(directory.find_factory(catalog::kFactoryNorth));
        CHECK(directory.find_factory(catalog::kFactorySouth));
        CHECK(directory.find_factory(catalog::kFactoryEmpty));
        CHECK(directory.modes_of(catalog::kFactoryEmpty).empty());
        bool sps=false, sort=false;
        for (auto* mode : directory.modes_of(catalog::kFactoryNorth)) {
            CHECK(mode->code!="manual" && mode->code!="automatic");
            if (mode->code=="sps") sps=true;
            if (mode->code=="sort") sort=true;
        }
        CHECK(sps && sort);
        CHECK(directory.stations.size()==8);
        CHECK(directory.empty_line_id()==catalog::kLineNorthEmpty);
        CHECK(directory.stations_of(catalog::kLineNorthEmpty).empty());
        int live=0;
        for (auto& station : directory.stations) if (directory.is_live(station.id)) ++live;
        CHECK(live==1);
        CHECK(directory.is_live(catalog::kStationA1));
        CHECK(!directory.is_live(catalog::kStationB1));
        CHECK(!directory.is_live(catalog::kStationA2));
        CHECK(directory.live_station_id()==catalog::kStationA1);
        CHECK(directory.live_selection().station_id==catalog::kStationA1);
        CHECK(directory.mode_of_station(catalog::kStationA1)->code=="sps");

        catalog::Selection kept{catalog::kFactoryNorth, catalog::kModeNorthSps, catalog::kLineNorthSps, catalog::kStationA3};
        CHECK(directory.reconcile(kept).station_id==catalog::kStationA3);
        catalog::Selection emptied=kept;
        emptied.line_id=catalog::kLineNorthEmpty;
        auto empty_line=directory.reconcile(emptied);
        CHECK(empty_line.line_id==catalog::kLineNorthEmpty);
        CHECK(empty_line.station_id.empty());
        catalog::Selection named{catalog::kFactorySouth, catalog::kModeSouthSort, catalog::kLineSouthSort, catalog::kStationA1};
        auto not_by_name=directory.reconcile(named);
        CHECK(not_by_name.factory_id==catalog::kFactorySouth);
        CHECK(not_by_name.line_id==catalog::kLineSouthSort);
        CHECK(not_by_name.station_id==catalog::kStationD1);
        CHECK(not_by_name.station_id!=catalog::kStationC1);
        CHECK(directory.find_station(catalog::kStationA1)->name==directory.find_station(catalog::kStationC1)->name);
        CHECK(directory.controller_of_station(catalog::kStationA1)->id!=directory.controller_of_station(catalog::kStationC1)->id);

        catalog::Selection switched=directory.live_selection();
        switched.business_mode_id=catalog::kModeNorthSort;
        auto sort_mode=directory.reconcile(switched);
        CHECK(directory.mode_of_station(sort_mode.station_id)->code=="sort");
        CHECK(sort_mode.factory_id==catalog::kFactoryNorth);
        CHECK(!directory.is_live(sort_mode.station_id));

        catalog::Selection cross=directory.live_selection();
        cross.factory_id=catalog::kFactorySouth;
        auto south=directory.reconcile(cross);
        CHECK(south.factory_id==catalog::kFactorySouth);
        CHECK(south.station_id!=catalog::kStationA1);
        CHECK(directory.factory_of_station(south.station_id)->id==catalog::kFactorySouth);

        catalog::Selection bare;
        bare.factory_id=catalog::kFactoryEmpty;
        auto no_children=directory.reconcile(bare);
        CHECK(no_children.factory_id==catalog::kFactoryEmpty);
        CHECK(no_children.business_mode_id.empty());
        CHECK(no_children.line_id.empty());
        CHECK(no_children.station_id.empty());

        auto alpha=directory.stations_of_controller(catalog::kControllerAlpha);
        CHECK(alpha.size()==3);
        CHECK(alpha[0]->id==catalog::kStationA1 && alpha[1]->id==catalog::kStationA2 && alpha[2]->id==catalog::kStationA3);
        auto gamma=directory.stations_of_controller(catalog::kControllerGamma);
        CHECK(gamma.size()==2);
        CHECK(directory.mode_of_station(gamma[0]->id)->code!=directory.mode_of_station(gamma[1]->id)->code);
        CHECK(directory.zone_of_station(catalog::kStationA1)->id==catalog::kZoneRack);
        CHECK(directory.zone_of_station(catalog::kStationA2)->id==catalog::kZoneRack);
        CHECK(directory.zone_of_station(catalog::kStationA3)==nullptr);
        CHECK(directory.locations_of_zone(catalog::kZoneRack).size()==2);
        CHECK(directory.locations_of_zone(catalog::kZoneSort).empty());
        CHECK(directory.controller_of_station(catalog::kStationD2)==nullptr);

        catalog::Directory empty;
        CHECK(catalog::Directory::validate(empty).empty());
        CHECK(empty.reconcile({}).factory_id.empty());

        auto duplicate=directory;
        duplicate.factories.push_back(directory.factories.front());
        expect(catalog::Directory::validate(duplicate), "duplicate-id");
        auto charset=directory;
        charset.factories.push_back({"北岸", "X", "名称"});
        expect(catalog::Directory::validate(charset), "id-charset");
        auto dangling=directory;
        dangling.controller_stations.push_back({catalog::kControllerAlpha, "stn.missing"});
        expect(catalog::Directory::validate(dangling), "dangling-ref");
        auto two=directory;
        two.controller_stations.push_back({catalog::kControllerBeta, catalog::kStationA1});
        expect(catalog::Directory::validate(two), "station-two-controllers");
        auto zones=directory;
        zones.zone_stations.push_back({catalog::kZoneSort, catalog::kStationA1});
        expect(catalog::Directory::validate(zones), "station-two-zones");
        auto again=directory;
        again.controller_stations.push_back(directory.controller_stations.front());
        expect(catalog::Directory::validate(again), "duplicate-relation");
        auto second_live=directory;
        for (auto& station : second_live.stations) if (station.id==catalog::kStationA2) station.stream=catalog::kStream;
        expect(catalog::Directory::validate(second_live), "multiple-live");
        auto endpoint=directory;
        for (auto& station : endpoint.stations) if (station.id==catalog::kStationA2) station.stream="opc.tcp://127.0.0.1";
        expect(catalog::Directory::validate(endpoint), "stream");
        auto runtime=directory;
        for (auto& mode : runtime.business_modes) if (mode.id==catalog::kModeNorthSort) mode.code="manual";
        expect(catalog::Directory::validate(runtime), "runtime-code-as-business");
        auto collision=directory;
        for (auto& station : collision.stations) if (station.id==catalog::kStationA2) station.name=catalog::kStationA1;
        expect(catalog::Directory::validate(collision), "name-collides-with-id");
        std::cout<<"PASS catalog: two factories, empty lists, retain-by-id, controller fan-out, zone links, simulated stream isolation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
