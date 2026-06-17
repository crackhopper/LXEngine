module;
#include <iostream>

export module LX_New_Test.MemoryTest;

import LX_New_Test.Test.ResourceHandleTest;
import LX_New_Test.Test.RawBufferTest;
import LX_New_Test.Test.TypedResourceTableTest;
import LX_New_Test.Test.VariableResourceTableTest;
import LX_New_Test.Test.SpillPoolTest;
import LX_New_Test.Test.GameObjectManagerTest;
import LX_New_Test.Test.GcIntegrationTest;
import LX_New_Test.Test.LeakTest;

export namespace LX_New_Test {

inline bool run_memory_tests() {
    bool all_pass = true;

    std::cout << "[ResourceHandle]\n";
    if (!run_resource_handle_tests()) all_pass = false;
    else std::cout << "  OK\n";

    std::cout << "[RawBuffer]\n";
    if (!run_raw_buffer_tests()) all_pass = false;
    else std::cout << "  OK\n";

    std::cout << "[TypedResourceTable]\n";
    if (!run_typed_resource_table_tests()) all_pass = false;
    else std::cout << "  OK\n";

    std::cout << "[VariableResourceTable]\n";
    if (!run_variable_resource_table_tests()) all_pass = false;
    else std::cout << "  OK\n";

    std::cout << "[SpillPool]\n";
    if (!run_spill_pool_tests()) all_pass = false;
    else std::cout << "  OK\n";

    std::cout << "[GameObjectManager]\n";
    if (!run_game_object_manager_tests()) all_pass = false;
    else std::cout << "  OK\n";

    std::cout << "[GcIntegration]\n";
    if (!run_gc_integration_tests()) all_pass = false;
    else std::cout << "  OK\n";

    std::cout << "[LeakTest]\n";
    if (!run_leak_tests()) all_pass = false;
    else std::cout << "  OK\n";

    return all_pass;
}

} // namespace LX_New_Test
