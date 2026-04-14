#pragma once

// Shared helpers for integration tests that need to materialize a
// RenderingItem from a Scene. Introduced by REQ-008 to replace the deleted
// Scene::buildRenderingItem(pass) shortcut.
//
// Callers get a single-item convenience wrapper over RenderQueue::buildFromScene,
// with an assertion that the queue is non-empty (which is the most common bug
// surface when pass-mask / scene-level-resource wiring drifts).

#include "core/scene/pass.hpp"
#include "core/scene/render_queue.hpp"
#include "core/scene/scene.hpp"

#include <cassert>

namespace LX_test {

/// Build a local RenderQueue from `scene` for `pass` and return the first
/// RenderingItem. Asserts the queue is non-empty. Intended for tests that
/// still exercise a single item at a time.
inline LX_core::RenderingItem
firstItemFromScene(LX_core::Scene &scene, LX_core::StringID pass) {
  LX_core::RenderQueue q;
  q.buildFromScene(scene, pass);
  assert(!q.getItems().empty() && "scene produced no items for pass");
  return q.getItems().front();
}

} // namespace LX_test
