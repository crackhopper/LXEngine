#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"

#include <array>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace LX_core;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void testLeafDedup() {
  auto &t = GlobalStringTable::get();
  StringID a = t.Intern("foo");
  StringID b = t.Intern("foo");
  EXPECT(a == b, "Intern('foo') twice must dedupe");
  EXPECT(a.id != 0, "Intern must return non-zero id");
}

void testComposeDedup() {
  auto &t = GlobalStringTable::get();
  StringID a = t.Intern("compose_a");
  StringID b = t.Intern("compose_b");
  std::array<StringID, 2> fields{a, b};

  StringID k1 = t.compose(TypeTag::PipelineKey, fields);
  StringID k2 = t.compose(TypeTag::PipelineKey, fields);
  EXPECT(k1 == k2, "compose(PipelineKey,{a,b}) twice must dedupe");
}

void testComposeOrderSensitive() {
  auto &t = GlobalStringTable::get();
  StringID a = t.Intern("order_a");
  StringID b = t.Intern("order_b");
  std::array<StringID, 2> ab{a, b};
  std::array<StringID, 2> ba{b, a};

  StringID k1 = t.compose(TypeTag::PipelineKey, ab);
  StringID k2 = t.compose(TypeTag::PipelineKey, ba);
  EXPECT(k1 != k2, "compose is order sensitive");
}

void testComposeDifferentTags() {
  auto &t = GlobalStringTable::get();
  StringID a = t.Intern("tagdiff_a");
  StringID b = t.Intern("tagdiff_b");
  std::array<StringID, 2> fields{a, b};

  StringID kp = t.compose(TypeTag::PipelineKey, fields);
  StringID ko = t.compose(TypeTag::ObjectRender, fields);
  EXPECT(kp != ko, "different tags must produce different ids");
}

void testDecomposeRoundTrip() {
  auto &t = GlobalStringTable::get();
  StringID a = t.Intern("rt_a");
  StringID b = t.Intern("rt_b");
  std::array<StringID, 2> fields{a, b};

  StringID k = t.compose(TypeTag::PipelineKey, fields);
  auto d = t.decompose(k);
  EXPECT(d.has_value(), "decompose of composed id must have value");
  if (d) {
    EXPECT(d->tag == TypeTag::PipelineKey, "decompose tag must match");
    EXPECT(d->fields.size() == 2, "decompose fields size must be 2");
    if (d->fields.size() == 2) {
      EXPECT(d->fields[0] == a, "fields[0] == a");
      EXPECT(d->fields[1] == b, "fields[1] == b");
    }
  }
}

void testDecomposeLeaf() {
  auto &t = GlobalStringTable::get();
  StringID leaf = t.Intern("just_a_leaf");
  auto d = t.decompose(leaf);
  EXPECT(d.has_value(), "decompose of leaf must have value");
  if (d) {
    EXPECT(d->tag == TypeTag::String, "leaf tag must be String");
    EXPECT(d->fields.empty(), "leaf fields must be empty");
  }
}

void testDecomposeInvalid() {
  auto &t = GlobalStringTable::get();
  auto d = t.decompose(StringID{0xDEADBEEFu});
  EXPECT(!d.has_value(), "decompose of invalid id must be nullopt");
}

void testComposeEmpty() {
  auto &t = GlobalStringTable::get();
  StringID k = t.compose(TypeTag::MeshRender, {});
  auto d = t.decompose(k);
  EXPECT(d.has_value(), "compose with empty fields valid");
  if (d) {
    EXPECT(d->tag == TypeTag::MeshRender, "empty compose tag == MeshRender");
    EXPECT(d->fields.empty(), "empty compose fields empty");
  }
}

void testToDebugString() {
  auto &t = GlobalStringTable::get();
  StringID foo = t.Intern("foo");
  std::array<StringID, 1> inner{t.Intern("pos")};
  StringID vl = t.compose(TypeTag::VertexLayout, inner);

  std::array<StringID, 2> outer{foo, vl};
  StringID pk = t.compose(TypeTag::PipelineKey, outer);

  std::string s = t.toDebugString(pk);
  const std::string expected = "PipelineKey(foo, VertexLayout(pos))";
  EXPECT(s == expected,
         "toDebugString render: got=" + s + " expected=" + expected);
}

void testToDebugStringLeaf() {
  auto &t = GlobalStringTable::get();
  StringID leaf = t.Intern("justleaf");
  std::string s = t.toDebugString(leaf);
  EXPECT(s == "justleaf", "toDebugString leaf: got=" + s);
}

void testConcurrentCompose() {
  auto &t = GlobalStringTable::get();
  StringID a = t.Intern("cc_a");
  StringID b = t.Intern("cc_b");
  std::array<StringID, 2> fields{a, b};

  // Warm up the single entry so all threads race against the shared version.
  StringID expected = t.compose(TypeTag::PipelineKey, fields);

  constexpr int kThreads = 8;
  constexpr int kIters = 1000;
  std::atomic<int> mismatches{0};

  std::vector<std::thread> ts;
  ts.reserve(kThreads);
  for (int i = 0; i < kThreads; ++i) {
    ts.emplace_back([&]() {
      for (int j = 0; j < kIters; ++j) {
        StringID got = t.compose(TypeTag::PipelineKey, fields);
        if (got != expected)
          mismatches.fetch_add(1);
      }
    });
  }
  for (auto &th : ts)
    th.join();

  EXPECT(mismatches.load() == 0, "concurrent compose produced stable id");
}

} // namespace

int main() {
  expSetEnvVK();
  testLeafDedup();
  testComposeDedup();
  testComposeOrderSensitive();
  testComposeDifferentTags();
  testDecomposeRoundTrip();
  testDecomposeLeaf();
  testDecomposeInvalid();
  testComposeEmpty();
  testToDebugString();
  testToDebugStringLeaf();
  testConcurrentCompose();

  if (failures > 0) {
    std::cerr << "FAILED: " << failures << " assertion(s)\n";
    return 1;
  }
  std::cout << "OK: all string_table tests passed\n";
  return 0;
}
