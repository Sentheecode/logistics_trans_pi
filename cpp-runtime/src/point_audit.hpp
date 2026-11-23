#pragma once
#include <string>
#include <vector>

namespace audit {
// Source rows from the station 140 sheet. Doubts are kept; nothing here is an executable write.
struct Row {
    std::string source_row;
    std::string section;
    std::string name;
    std::string address;
    std::string type_text;
    std::string direction_text;
    std::string length_text;
    std::string note;
    bool reserved=false;
    bool duplicate_address=false;
    std::string doubts;
    bool executable_write=false;
};

std::vector<Row> station_140_template();
std::vector<Row> for_station(const std::string& station_id);
}
