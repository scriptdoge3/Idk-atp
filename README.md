# cleanroom-src

A from-scratch engine that runs on Source-format game data you own,
targeting the Pixel 11 Pro (Android, arm64, Vulkan). Same idea as Xash3D
for GoldSrc: a new engine that reads the original game's files.

## Clean-room rules

- No Valve engine code, no leaked code, and no Source SDK code (its license
  ties it to Valve's engine). Nobody working on this reads them.
- File formats come from public documentation (Valve Developer Community wiki).
- Game behavior comes from observing the real game: console values,
  measurements, recordings.
- No game assets in the repo. The engine loads them from the user's install.

## Layout

    src/vpk.*         VPK v1/v2 archive reader
    src/filesystem.*  search paths: loose files, then VPKs, case-insensitive
    src/bsp.*         BSP v19/v20 loader: world polygons, displacements,
                      lightmaps and a lightmap atlas
    tools/vpktool     list / extract / CRC-verify VPKs
    tools/bsp2obj     export a map's world geometry to OBJ for Blender,
                      with -l to preview its lightmaps instead of textures
    tests/run.sh      builds everything and runs the synthetic-data tests

## Try it on your install

    cmake -S . -B build && cmake --build build
    ./build/bsp2obj -g "$HOME/.local/share/Steam/steamapps/common/Half-Life 2/hl2" \
        maps/d1_trainstation_01.bsp trainstation.obj

Add `-l` to get `trainstation_lightmap.tga` and UVs into it instead of the
textures, so the baked lighting shows up in Blender.

## Still to confirm on real maps

These follow the format docs but haven't been checked against the game's
own data yet. A map with terrain, such as `maps/d2_coast_01.bsp`, covers
all three:

- **Displacement grid layout.** bsp2obj prints the widest gap between sewn
  neighboring displacements. It should be close to 0; a wide gap (a
  transposed grid opens the test seam by 64 units) means the layout is wrong.
- **Displacement lightmaps** are projected from the flat base face, the
  same as interpolating its corners' luxel coordinates. With `-l`, shadows
  on terrain should line up with the objects casting them.
- **Displacement triangle diagonals** alternate in a checkerboard. Compare
  the phase with `mat_wireframe 1` in the game before collision relies on it.

## Roadmap

Engine:
- [x] VPK, filesystem, BSP geometry
- [x] Displacements, lightmaps (all styles and bump maps decode; the atlas
      holds each face's first style, flat)
- [ ] VTF textures and VMT materials
- [ ] Vulkan renderer (desktop first, then Android)
- [ ] Brush collision and player movement tuned to match the real game
- [ ] MDL/VVD/VTX models, then animation
- [ ] Audio, touch controls, Android app shell

Game module (own code, written against observed behavior):
- [ ] Entity system reading the BSP entity lump
- [ ] Triggers, doors, level transitions
- [ ] Weapons, NPCs, physics props
