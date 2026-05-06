## 1. Component Infrastructure

- [x] 1.1 Add `src/core/scene/component.*` with the typed `IComponent` base, process-local type ID generation, and owner attach/detach contract.
- [x] 1.2 Add `MeshComponent`, `MaterialComponent`, and `SkeletonComponent` under `src/core/scene/components/`, including material pass-listener ownership inside `MaterialComponent`.

## 2. SceneNode Migration

- [x] 2.1 Refactor `SceneNode` to own a typed component container, expose `addComponent/getComponent/removeComponent/listComponents`, and remove dedicated mesh/material/skeleton fields plus their legacy getters/setters.
- [x] 2.2 Update `SceneNode` validation, pipeline-signature, descriptor-resource, and buffer access paths to read structural payload through the new components while preserving current fatal-validation behavior.

## 3. Call Site Migration

- [x] 3.1 Migrate scene/demo/infra construction paths from constructor-time mesh/material/skeleton injection to component attachment.
- [x] 3.2 Migrate tests and remaining call sites that read or mutate node mesh/material/skeleton state through the removed legacy APIs.

## 4. Verification

- [x] 4.1 Add or update tests for typed component attachment/removal, duplicate-type rejection, list ordering, and material-listener cleanup.
- [x] 4.2 Run the relevant integration/demo validation coverage for component-backed `SceneNode` rendering behavior and confirm no regression in existing pass-validation expectations.
