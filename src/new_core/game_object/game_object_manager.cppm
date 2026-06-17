module;
#include <vector>
#include <span>
#include <algorithm>

export module LX_New_Core.GameObject:GameObjectManager;

import LX_New_Common.Platform;
import LX_New_Common.Memory;
import LX_New_Core.Resource;
import :GameObjectMeta;

export namespace LX_New_Core {

class GameObjectManager {
  std::vector<GameObjectMeta> m_metas;
  std::vector<u32> m_freeMetaList;
  std::vector<u32> m_roots;

  LX_New_Common::SpillPool<u32, 8> m_refPool;
  LX_New_Common::SpillPool<u64, 8> m_handlePool;

  ResourceManager *m_resourceManager = nullptr;

  static constexpr u32 kInvalidMetaIdx = 0xFFFFFFFF;

public:
  explicit GameObjectManager(ResourceManager *rm = nullptr) noexcept
      : m_resourceManager(rm) {}

  GameObjectManager(const GameObjectManager &) = delete;
  GameObjectManager &operator=(const GameObjectManager &) = delete;

  // ── Meta 管理 ──────────────────────────────────────────────

  u32 allocMeta() {
    if (!m_freeMetaList.empty()) {
      u32 idx = m_freeMetaList.back();
      m_freeMetaList.pop_back();
      m_metas[idx] = GameObjectMeta{};
      m_metas[idx].alive = 1;
      return idx;
    }
    u32 idx = (u32)m_metas.size();
    m_metas.emplace_back();
    m_metas[idx].alive = 1;
    return idx;
  }

  void freeMeta(u32 idx) {
    if (idx >= m_metas.size())
      return;
    if (!m_metas[idx].alive)
      return;
    freeSpillChains(m_metas[idx]);
    m_metas[idx] = GameObjectMeta{};
    m_freeMetaList.push_back(idx);
  }

  // ── 设置 refs / handles ────────────────────────────────────

  void setRefs(u32 metaIdx, std::span<const u32> targets) {
    auto &meta = m_metas[metaIdx];
    if (meta.refSpillHead != kNoneChunk) {
      m_refPool.freeChain(meta.refSpillHead);
      meta.refSpillHead = kNoneChunk;
    }
    meta.refCount = 0;
    for (u32 t : targets) {
      if (meta.refCount < 4) {
        meta.refs[meta.refCount++] = t;
      } else {
        meta.refSpillHead = m_refPool.append(meta.refSpillHead, t);
      }
    }
  }

  void setHandles(u32 metaIdx, std::span<const u64> handles) {
    auto &meta = m_metas[metaIdx];
    if (meta.handleSpillHead != kNoneChunk) {
      m_handlePool.freeChain(meta.handleSpillHead);
      meta.handleSpillHead = kNoneChunk;
    }
    meta.handleCount = 0;
    for (u64 h : handles) {
      if (meta.handleCount < 4) {
        meta.handles[meta.handleCount++] = h;
      } else {
        meta.handleSpillHead =
            m_handlePool.append(meta.handleSpillHead, h);
      }
    }
  }

  // ── Root 管理 ──────────────────────────────────────────────

  void addToRoot(u32 metaIdx) { m_roots.push_back(metaIdx); }

  void removeFromRoot(u32 metaIdx) {
    auto it = std::find(m_roots.begin(), m_roots.end(), metaIdx);
    if (it != m_roots.end()) {
      *it = m_roots.back();
      m_roots.pop_back();
    }
  }

  // ── Mark-Sweep GC ──────────────────────────────────────────

  void mark() {
    for (auto &meta : m_metas) {
      if (meta.alive)
        meta.marked = 0;
    }
    for (u32 rootIdx : m_roots) {
      markRecursive(rootIdx);
    }
  }

  void clearMarks() {
    for (auto &meta : m_metas)
      meta.marked = 0;
  }

  void sweep() {
    for (u32 i = 0; i < m_metas.size(); ++i) {
      auto &meta = m_metas[i];
      if (!meta.alive || meta.marked)
        continue;

      releaseHandles(meta);
      freeSpillChains(meta);

      meta.alive = 0;
      meta.marked = 0;
      meta.refCount = 0;
      meta.handleCount = 0;
      m_freeMetaList.push_back(i);
    }
  }

  void tick() {
    mark();
    sweep();
  }

  // ── 查询方法 ───────────────────────────────────────────────

  [[nodiscard]] bool isAlive(u32 idx) const {
    return idx < m_metas.size() && m_metas[idx].alive;
  }
  [[nodiscard]] bool isMarked(u32 idx) const {
    return idx < m_metas.size() && m_metas[idx].marked;
  }
  [[nodiscard]] u32 getRootCount() const { return (u32)m_roots.size(); }

  [[nodiscard]] u8 getRefCount(u32 idx) const {
    return m_metas[idx].refCount;
  }
  [[nodiscard]] u32 getSpillRefCount(u32 idx) const {
    return m_refPool.itemCount(m_metas[idx].refSpillHead);
  }
  [[nodiscard]] u8 getHandleCount(u32 idx) const {
    return m_metas[idx].handleCount;
  }
  [[nodiscard]] u32 getSpillHandleCount(u32 idx) const {
    return m_handlePool.itemCount(m_metas[idx].handleSpillHead);
  }

private:
  void markRecursive(u32 metaIdx) {
    if (metaIdx >= m_metas.size())
      return;
    auto &meta = m_metas[metaIdx];
    if (!meta.alive || meta.marked)
      return;
    meta.marked = 1;

    for (u8 i = 0; i < meta.refCount; ++i)
      markRecursive(meta.refs[i]);
    for (auto it = m_refPool.begin(meta.refSpillHead);
         it != m_refPool.end(); ++it)
      markRecursive(*it);
  }

  void releaseHandles(GameObjectMeta &meta) {
    if (!m_resourceManager)
      return;
    for (u8 j = 0; j < meta.handleCount; ++j) {
      if (meta.handles[j] != kInvalidHandle)
        m_resourceManager->release(meta.handles[j]);
    }
    if (meta.handleSpillHead != kNoneChunk) {
      for (auto it = m_handlePool.begin(meta.handleSpillHead);
           it != m_handlePool.end(); ++it) {
        if (*it != kInvalidHandle)
          m_resourceManager->release(*it);
      }
    }
  }

  void freeSpillChains(GameObjectMeta &meta) {
    if (meta.refSpillHead != kNoneChunk) {
      m_refPool.freeChain(meta.refSpillHead);
      meta.refSpillHead = kNoneChunk;
    }
    if (meta.handleSpillHead != kNoneChunk) {
      m_handlePool.freeChain(meta.handleSpillHead);
      meta.handleSpillHead = kNoneChunk;
    }
  }
};

} // namespace LX_New_Core
