#!/bin/sh
# Builds everything, generates synthetic data, and runs all checks.
set -e
cd "$(dirname "$0")/.."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug > /dev/null
cmake --build build > /dev/null
T=build/testdata
rm -rf "$T" && mkdir -p "$T"

echo "== vpk =="
python3 tests/make_test_vpk.py "$T/vpk" > /dev/null
for v in v1 v2; do
  vpk="$T/vpk/test_${v}_dir.vpk"
  ./build/vpktool "$vpk" verify
  (cd "$T/vpk/expected" && find . -type f | sed 's|^\./||') | while read -r f; do
    ./build/vpktool "$vpk" extract "$f" "$T/out.bin" > /dev/null
    cmp "$T/out.bin" "$T/vpk/expected/$f"
  done
done

echo "== filesystem =="
G="$T/game"
mkdir -p "$G/MAPS"
cp "$T"/vpk/test_v1_* "$G/"
printf 'loose override\n' > "$G/credits.txt"
python3 tests/make_test_bsp.py "$G/MAPS/Room.BSP" > /dev/null
./build/fs_test "$G"

echo "== bsp =="
./build/bsp_test "$G/MAPS/Room.BSP"
./build/bsp2obj -g "$G" maps/room.bsp "$T/room.obj"
python3 tests/check_obj.py "$T/room.obj"
echo "all tests passed"
