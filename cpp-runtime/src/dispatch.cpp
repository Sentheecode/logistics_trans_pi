#include "commands.hpp"
#include <string>
int main(int argc, char** argv) {
    if (argc>=2) {
        const std::string command=argv[1];
        if (command=="collector") return collector_main(argc-1, argv+1);
        if (command=="diagnose") return diagnose_main(argc-1, argv+1);
        if (command=="archive") return archive_main(argc-1, argv+1);
        if (command=="mcp") return mcp_main(argc-1, argv+1);
        if (command=="ui") return hmi_main(argc-1, argv+1);
    }
    return hmi_main(argc, argv);
}
