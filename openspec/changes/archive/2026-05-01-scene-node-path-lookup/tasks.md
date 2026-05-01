## 1. SceneNode Naming And Paths

- [x] 1.1 Add `SceneNode` name storage plus `setName/getName/getPath` API in `src/core/scene/object.{hpp,cpp}`
- [x] 1.2 Implement name sanitization rules, empty-name handling, and unnamed-node placeholder segments for `getPath()`
- [x] 1.3 Keep parent/child storage unchanged while making `getPath()` reflect reparenting immediately without path cache

## 2. Scene Lookup And Tree Dump

- [x] 2.1 Add `Scene::findByPath(const std::string&)` rooted path traversal in `src/core/scene/scene.{hpp,cpp}`
- [x] 2.2 Implement duplicate-sibling deterministic lookup and `WARN` behavior
- [x] 2.3 Add `Scene::dumpTree() const -> std::string` with box-drawing output that mirrors hierarchy structure

## 3. Tests And Docs

- [x] 3.1 Add `src/test/integration/test_scene_path_lookup.cpp` covering root lookup, nested lookup, reparent updates, duplicates, and dumpTree reversibility
- [x] 3.2 Update human-facing scene documentation/source analysis for path lookup and naming semantics
- [x] 3.3 Run targeted build/test verification for the new scene path lookup coverage before close-out
