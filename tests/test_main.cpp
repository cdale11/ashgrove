#include "test_utils.h"
#include "simulation/simulation.h"
#include "world/world.h"
#include "npc/npc.h"
#include "investigation/investigation.h"

using namespace ashgrove;

int main() {
    std::cout << "Running Ashgrove tests..." << std::endl;
    int failed = ashgrove_test::run_all();
    std::cout << (failed == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return failed;
}