# lxe_pbrt_scene_convert

`lxe_pbrt_scene_convert` converts the PBRT v3 `bmw-m6` scene into two
LXEngine asset views:

- a runtime view that current realtime/offline loaders can consume
- a source-preserving view that keeps PBRT material semantics for future
  physically faithful rendering

The converter does not make the engine load PBRT directly. It turns the PBRT
scene into LXEngine scene/material/mesh files plus a manifest.

## Usage

```bash
python3 src/tools/lxe_pbrt_scene_convert/lxe_pbrt_scene_convert.py \
  --input <pbrt-v3-scenes>/bmw-m6/bmw-m6.pbrt \
  --out assets/models/bmw-m6 \
  --scene assets/models/bmw-m6/pbrt_bmw_m6.scene.yaml
```

Equivalent CMake target:

```bash
cmake -S . -B build -G Ninja \
  -DLXE_PBRT_BMW_M6_SOURCE=<pbrt-v3-scenes>/bmw-m6/bmw-m6.pbrt
ninja RunPbrtBmwM6Convert
```

## Outputs

```text
assets/models/bmw-m6/
  pbrt_bmw_m6.scene.yaml
  meshes/*.obj
  materials/runtime-pbr-approx/*.material
  materials/pbrt-source/*.pbrt-material.yaml
  textures/sky.exr
  spds/*.spd
  bsdfs/*.bsdf
  licenses/BLENDSWAP_LICENSE.txt
  pbrt_bmw_m6.converted.json
  pbrt_bmw_m6.conversion.md
```

Offline smoke render target:

```bash
ninja RunPbrtBmwM6OfflineSmoke
```

The scene file references only runtime-supported assets for actual rendering.
Each material binding also carries an `offline.pbrtSourceMaterialUri` extension
that points to the source-preserving PBRT material YAML.

## Fidelity Model

Runtime `.material` files are approximations. They map PBRT material families
onto the current PBR shader so the car can render today.

The source material YAML files preserve original PBRT information including:

- material type
- parameter type and order
- raw float/rgb/spectrum/string values
- SPD and BSDF resource references
- mix material references
- source file line ranges

Future physically faithful rendering should consume the source material files,
not reverse-engineer data from the PBR approximation.
