// LX_new 模块 smoke：验证 new_common / new_core 的 C++20 module 编译链接与跨
// target 依赖。 import 声明必须在其它声明（含 #include）之前。
import LX_New_Common.Platform;
import LX_New_Common.Math;
import LX_New_Core;
import LX_New_Test.MemoryTest;

#include <iostream>

int main() {
  LX_New_Common::Clock clock = LX_New_Core::MakeBootClock();

  std::cout << "LX new modules smoke ok: core version=" << LX_New_Core::kVersion
            << " clock_us=" << clock.GetTimeMicroseconds() << "\n";

  bool ok = LX_New_Test::run_memory_tests();
  std::cout << (ok ? "\nAll memory tests PASSED\n" : "\nSome memory tests FAILED\n");
  return ok ? 0 : 1;
}
