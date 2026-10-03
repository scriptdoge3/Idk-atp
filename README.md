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
    src/bsp.*         BSP v19/v20 loader + world polygon builder
    tools/vpktool     list / extract / CRC-verify VPKs
    tools/bsp2obj     export a map's world geometry to OBJ for Blender
    tests/run.sh      builds everything and runs the synthetic-data tests

## Try it on your install

    cmake -S . -B build && cmake --build build
    ./build/bsp2obj -g "$HOME/.local/share/Steam/steamapps/common/Half-Life 2/hl2" \
        maps/d1_trainstation_01.bsp trainstation.obj

## Roadmap

Engine:
- [x] VPK, filesystem, BSP geometry
- [ ] Displacements, lightmaps
- [ ] VTF textures and VMT materials
- [ ] Vulkan renderer (desktop first, then Android)
- [ ] Brush collision and player movement tuned to match the real game
- [ ] MDL/VVD/VTX models, then animation
- [ ] Audio, touch controls, Android app shell

Game module (own code, written against observed behavior):
- [ ] Entity system reading the BSP entity lump
- [ ] Triggers, doors, level transitions
- [ ] Weapons, NPCs, physics props
