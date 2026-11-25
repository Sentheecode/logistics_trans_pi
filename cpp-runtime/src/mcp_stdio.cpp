#include "mcp.hpp"

#include <iostream>
#include <string>

int mcp_main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: logistics-mcp DATA_DIR\n";
        return 2;
    }
    try {
        const auto directory = std::filesystem::u8path(argv[1]);
        auto phase = mcp::Phase::new_connection;
        std::string line;
        for (char ch; std::cin.get(ch);) {
            if (ch == '\n') {
                const auto reply = mcp::process(directory, line, &phase);
                if (reply) std::cout << reply->dump() << '\n' << std::flush;
                line.clear();
            } else {
                if (line.size() == mcp::max_request) {
                    std::cerr << "MCP request exceeds input limit\n";
                    return 1;
                }
                line += ch;
            }
        }
        return 0;
    } catch (const std::exception&) {
        std::cerr << "MCP stream failure\n";
        return 1;
    }
}
