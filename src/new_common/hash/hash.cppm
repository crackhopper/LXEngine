module;
#include <cstddef>
#include <functional>

export module LX_New_Common.Hash;

import LX_New_Common.Platform;

export namespace LX_New_Common {
template <class T> inline void hash_combine(usize &seed, const T &value) {
  std::hash<T> hasher;
  seed ^= hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
};
} // namespace LX_New_Common
