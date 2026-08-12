#!/usr/bin/env bash
#
# Cross-builds CRTBender.exe for Windows from Linux with mingw-w64, and drops
# the result in dist/. This is how the committed dist/CRTBender.exe is produced.
#
# On Windows itself, prefer the normal path instead:
#   cmake -B build -A x64 && cmake --build build --config Release
#
# Requires: g++-mingw-w64-x86-64  (Debian/Ubuntu: apt install g++-mingw-w64-x86-64)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/dist"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

CXX=x86_64-w64-mingw32-g++
RC=x86_64-w64-mingw32-windres
STRIP=x86_64-w64-mingw32-strip

for tool in "$CXX" "$RC" "$STRIP"; do
    command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done

mkdir -p "$OUT"

echo "==> compiling resources"
"$RC" -I "$ROOT/res" "$ROOT/res/app.rc" -O coff -o "$WORK/app_rc.o"

echo "==> compiling and linking"
"$CXX" \
    -std=c++17 -O2 -municode -mwindows \
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
    -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
    -I "$ROOT/src" -I "$ROOT/res" \
    -finput-charset=UTF-8 -fexec-charset=UTF-8 \
    -Wall -Wextra \
    -o "$OUT/CRTBender.exe" \
    "$ROOT"/src/*.cpp "$WORK/app_rc.o" \
    -ld3d11 -ldxgi -ldxguid -ld3dcompiler \
    -lcomctl32 -lshell32 -lole32 -luuid -luser32 -lgdi32 -ladvapi32 \
    -static -static-libgcc -static-libstdc++

echo "==> stripping"
"$STRIP" --strip-all "$OUT/CRTBender.exe"

echo "==> running the warp tests"
g++ -std=c++17 -O1 -Wall -Wextra -I "$ROOT/src" \
    -o "$WORK/warp_tests" "$ROOT/tests/test_warp.cpp" "$ROOT/src/warpmesh.cpp" "$ROOT/src/geometry.cpp"
"$WORK/warp_tests" | tail -1

echo "==> running the geometry tests"
g++ -std=c++17 -O1 -Wall -Wextra -I "$ROOT/src" \
    -o "$WORK/geometry_tests" "$ROOT/tests/test_geometry.cpp" \
    "$ROOT/src/geometry.cpp" "$ROOT/src/warpmesh.cpp"
"$WORK/geometry_tests" | tail -1

echo "==> running the config parser tests"
g++ -std=c++17 -O1 -Wall -Wextra -I "$ROOT/src" \
    -o "$WORK/config_tests" "$ROOT/tests/test_config.cpp"
"$WORK/config_tests" | tail -1

echo "==> running the string table tests"
g++ -std=c++17 -O1 -Wall -Wextra -I "$ROOT/src" \
    -finput-charset=UTF-8 -fexec-charset=UTF-8 \
    -o "$WORK/i18n_tests" "$ROOT/tests/test_i18n.cpp" "$ROOT/src/i18n.cpp"
"$WORK/i18n_tests" | tail -1

echo
ls -lh "$OUT/CRTBender.exe"
