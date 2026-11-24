#include "../commands.hpp"
#ifndef LOGISTICS_TOOL
#error LOGISTICS_TOOL must name the entry point
#endif
#define LOGISTICS_CALL LOGISTICS_TOOL
int main(int argc, char** argv) { return LOGISTICS_CALL(argc, argv); }
