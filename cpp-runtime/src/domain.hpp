#pragma once
#include <array>
#include <string>
namespace domain {
// Pure synthetic ordering demonstration; no production handshake semantics.
enum class Action { scan, sort, complete, block, resume };
struct State {
    std::array<std::string,3> carts{{"waiting","waiting","waiting"}};
    unsigned current=0;
};
State advance(State state, Action action);
}
