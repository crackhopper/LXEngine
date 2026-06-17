module;
#include <iostream>

export module LX_New_Test.MemoryTest;

import LX_New_Test.Test.ResourceHandleTest;

export namespace LX_New_Test {

inline bool run_memory_tests() {
    bool all_pass = true;

    std::cout << "[ResourceHandle]\n";
    if (!run_resource_handle_tests()) all_pass = false;
    else std::cout << "  OK\n";

    return all_pass;
}

} // namespace LX_New_Test
