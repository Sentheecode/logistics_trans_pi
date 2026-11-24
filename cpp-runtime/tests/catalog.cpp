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
        const auto directory=catalog::Directory::qiantang();
        CHECK(catalog::Directory::validate(directory).empty());
        CHECK(directory.factories.size()==3);
        CHECK(directory.find_factory(catalog::kFactory)->name=="钱塘二厂");
        CHECK(directory.areas_of(catalog::kFactory).size()==2);
        CHECK(directory.line_links.size()==directory.stations.size());
        CHECK(directory.stations_on_line("line.demo-empty").empty());
        CHECK(directory.line_of_station(catalog::kStation140)->id==directory.line_of_station(catalog::kStation160)->id);
        CHECK(directory.line_of_station(catalog::kStation140)->name=="外装");
        CHECK(directory.line_of_station(catalog::kStation20)->id!=directory.line_of_station(catalog::kStation140)->id);
        CHECK(directory.plc_of_station(catalog::kStation140)==directory.plc_of_station(catalog::kStation160));
        auto invalidLine=directory;
        invalidLine.line_links.push_back({"line.demo-south",catalog::kStation140});
        expect(catalog::Directory::validate(invalidLine), "line-scope");
        CHECK(directory.plcs_of(catalog::kAreaPick).size()==3);
        CHECK(directory.plcs_of(catalog::kAreaSeq).size()==4);
        CHECK(directory.stations_of(catalog::kPlcSeq1).size()==2);
        CHECK(directory.stations_of(catalog::kPlcSeq2).size()==2);
        CHECK(directory.stations_of(catalog::kPlcSeq3).size()==2);
        CHECK(directory.stations_of(catalog::kPlcSeq4).size()==2);
        CHECK(directory.find_station(catalog::kStation170)->code=="170");
        CHECK(directory.find_station(catalog::kStation170)->name=="左后门手柄机械臂排序区");
        CHECK(directory.find_station(catalog::kStation240)->name=="右后制动钳总成");
        CHECK(directory.plc_of_station(catalog::kStation170)->name=="排序区一");
        CHECK(directory.plc_of_station(catalog::kStation210)->id==catalog::kPlcSeq3);
        CHECK(directory.plc_of_station(catalog::kStation230)->id==catalog::kPlcSeq4);
        CHECK(directory.stations_of(catalog::kPlcEmpty).empty());
        CHECK(directory.empty_plc_id()==catalog::kPlcEmpty);
        CHECK(directory.stations.size()==21);
        int live=0;
        for (auto& station : directory.stations) if (directory.is_live(station.id)) ++live;
        CHECK(live==1);
        CHECK(directory.is_live(catalog::kStation140));
        CHECK(!directory.is_live(catalog::kStation150));
        CHECK(!directory.is_live(catalog::kStation160));
        CHECK(directory.live_station_id()==catalog::kStation140);
        CHECK(directory.plc_of_station(catalog::kStation140)->id==catalog::kPlcPack);
        CHECK(directory.plc_of_station(catalog::kStation150)->id==catalog::kPlcPack);
        CHECK(directory.stations_of(catalog::kPlcPack).size()==3);
        CHECK(directory.stations_of(catalog::kPlcMeter2).size()==4);
        CHECK(directory.stations_of(catalog::kPlcMeter2)[0]->code=="40");
        CHECK(directory.stations_of(catalog::kPlcMeter2)[1]->code=="10");
        CHECK(directory.area_of_station(catalog::kStation210)->code=="seq");
        CHECK(directory.area_of_station(catalog::kStation140)->code=="pick");
        for (auto& plc : directory.plcs) {
            CHECK(plc.id.rfind("plc.cfg.", 0)==0);
            CHECK(plc.name.find("192.")==std::string::npos);
        }

        catalog::Selection kept{catalog::kFactory, catalog::kAreaPick, catalog::kPlcPack, catalog::kStation160};
        CHECK(directory.reconcile(kept).station_id==catalog::kStation160);
        catalog::Selection emptied=kept;
        emptied.factory_id="fac.demo-north";
        emptied.area_id="area.demo-north.sort";
        emptied.plc_id=catalog::kPlcEmpty;
        auto empty_plc=directory.reconcile(emptied);
        CHECK(empty_plc.plc_id==catalog::kPlcEmpty);
        CHECK(empty_plc.station_id.empty());
        catalog::Selection switched=directory.live_selection();
        switched.area_id=catalog::kAreaSeq;
        auto sort_area=directory.reconcile(switched);
        CHECK(directory.area_of_station(sort_area.station_id)->code=="seq");
        CHECK(sort_area.station_id==catalog::kStation170);
        CHECK(!directory.is_live(sort_area.station_id));
        catalog::Selection named=directory.live_selection();
        named.station_id=catalog::kStation210;
        named.plc_id=catalog::kPlcPack;
        auto not_by_name=directory.reconcile(named);
        CHECK(not_by_name.plc_id==catalog::kPlcPack);
        CHECK(not_by_name.station_id==catalog::kStation140);

        auto duplicate=directory;
        duplicate.stations.push_back(directory.stations.front());
        expect(catalog::Directory::validate(duplicate), "duplicate-id");
        auto second=directory;
        for (auto& station : second.stations) if (station.id==catalog::kStation150) station.stream=catalog::kStream;
        expect(catalog::Directory::validate(second), "multiple-live");
        auto endpoint=directory;
        for (auto& station : endpoint.stations) if (station.id==catalog::kStation150) station.stream="opc.tcp://127.0.0.1";
        expect(catalog::Directory::validate(endpoint), "stream");
        auto runtime=directory;
        for (auto& area : runtime.areas) if (area.id==catalog::kAreaSeq) area.code="manual";
        expect(catalog::Directory::validate(runtime), "runtime-code-as-area");
        auto invented=directory;
        invented.plcs.push_back({"plc.s7.192.168.0.1", catalog::kAreaPick, "ip", "捏造"});
        expect(catalog::Directory::validate(invented), "plc-id-scope");
        std::cout<<"PASS catalog: Qiantang factory, pick/sequence PLCs, one live station, synthetic line membership and no endpoints\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
