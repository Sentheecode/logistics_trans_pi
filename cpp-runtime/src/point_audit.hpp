#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace audit {
// Distinct from the simulation database application id 1279471699.
inline constexpr int kPointTableApplicationId=1280594004;
inline constexpr int kPointTableUserVersion=1;
inline constexpr const char* kPointTableSchema="logistics-point-table-v1";
inline constexpr const char* kPointTableFileName="point-table.sqlite";

// One side of a logistics action. Absent sides stay empty; 0 is not used as a placeholder.
struct AddressFields {
    bool present=false;
    std::string name;
    std::string source_row;
    int db=0;
    int start=0;
    int offset=0;
    int length=0;
    std::string area;
    std::string type_text;
    std::string direction_text;
};

// One baseline record. Read and write are separate field sets; this baseline fills only one.
struct Row {
    std::string station_id;
    std::string source_row;
    std::string section;
    std::string code;
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
    std::string pairing_status;
    AddressFields read;
    AddressFields write;
};

std::string compose_address(const AddressFields& fields);
std::filesystem::path default_point_table_path();
std::vector<Row> load_point_table(const std::filesystem::path& path);
std::vector<Row> for_station(const std::string& station_id);
}
