# PBRT BMW M6 Conversion And Render Plan

## Goal

Build a converter for PBRT v3 `bmw-m6`, preserve all source material data, generate LXEngine-readable assets, deploy the code remotely through MCP, convert the remote dataset into `data/scenes/bmw-m6`, and render one BMW M6 image with the current offline renderer.

## Phases

1. **Local converter implementation** — completed
   - Add `src/tools/lxe_pbrt_scene_convert`.
   - Parse the BMW M6 PBRT subset.
   - Convert binary little-endian PLY to OBJ.
   - Emit runtime `.material` approximations.
   - Emit PBRT source material YAML with no original material data loss.
   - Emit scene YAML, manifest, and conversion report.

2. **Local tests** — completed
   - Add parser / PLY / source material preservation / conversion tests.
   - Run focused tests locally.

3. **Local BMW M6 conversion smoke** — completed
   - Run converter against local `data/pbrt-v3-scenes/pbrt-v3-scenes/bmw-m6`.
   - Verify generated scene/material/manifest structure.
   - Local smoke render completed at `artifacts/pbrt/bmw-m6/local-smoke/render.png`.

4. **Commit and remote deployment** — completed
   - Commit only this converter/documentation scope.
   - Push or otherwise make the commit visible to the MCP-managed checkout.
   - Pull/build remotely through `lxe_manager`.

5. **Remote conversion** — completed
   - Run converter on remote dataset at `data\pbrt-v3-scenes\pbrt-v3-scenes\bmw-m6`.
   - Save converted assets under `data/scenes/bmw-m6`.

6. **Offline render** — completed
   - Run current offline renderer against the converted scene.
   - Produce a BMW M6 image artifact.
   - Report output paths and any current-renderer limitations.

## Completed Outputs

- Remote converted scene: `data/scenes/bmw-m6/pbrt_bmw_m6.scene.yaml`
- Remote conversion manifest: `data/scenes/bmw-m6/pbrt_bmw_m6.converted.json`
- Remote render image: `artifacts/pbrt/bmw-m6/offline-smoke/render.png`
- Remote render EXR: `artifacts/pbrt/bmw-m6/offline-smoke/render.exr`
