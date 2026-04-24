## Context

The current queue builder filters only by pass participation and scene-level resources. Visibility masks are a natural next step because they add selective scene visibility without yet requiring full spatial culling.

## Goals / Non-Goals

**Goals:**
- Add mask-based visibility control to cameras and renderables.
- Apply filtering in queue construction.
- Keep scene-level resource collection semantics separate from object visibility.

**Non-Goals:**
- No frustum culling yet.
- No hierarchical visibility propagation in this change.

## Decisions

- Store visibility at camera/renderable boundaries rather than on the whole scene.
- Filter during queue construction because that is where pass participation is already resolved.
- Keep mask logic orthogonal to scene-level camera resource collection.

## Risks / Trade-offs

- [More per-item checks during queue build] → Acceptable; bitmask checks are cheap and local.
- [Future hierarchy work may extend semantics] → Define flat-scene mask behavior first without overcommitting transform-tree semantics.
