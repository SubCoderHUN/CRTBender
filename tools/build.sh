#!/usr/bin/env bash
#
# Cross-builds both CRTBender editions from Linux:
#   - Win10/11 x64: DXGI Desktop Duplication + Direct3D 11
#   - Windows XP x86: GDI BitBlt + Direct3D 9
#
# On Windows, CMake can build the same two targets; see README.md.
#
# Requires:
#   apt install g++-mingw-w64-x86-64 g++-mingw-w64-i686 zip
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/dist"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

MODERN_CXX=x86_64-w64-mingw32-g++
MODERN_RC=x86_64-w64-mingw32-windres
MODERN_STRIP=x86_64-w64-mingw32-strip
XP_CXX=i686-w64-mingw32-g++-posix
XP_RC=i686-w64-mingw32-windres
XP_STRIP=i686-w64-mingw32-strip

for tool in "$MODERN_CXX" "$MODERN_RC" "$MODERN_STRIP" \
            "$XP_CXX" "$XP_RC" "$XP_STRIP" zip; do
    command -v "$tool" >/dev/null || { echo "missing: $tool" >&2; exit 1; }
done

mkdir -p "$OUT"

COMMON_SOURCES=(
    "$ROOT/src/main.cpp"
    "$ROOT/src/config.cpp"
    "$ROOT/src/display.cpp"
    "$ROOT/src/geometry.cpp"
    "$ROOT/src/i18n.cpp"
    "$ROOT/src/warpmesh.cpp"
    "$ROOT/src/editor.cpp"
    "$ROOT/src/autostart.cpp"
    "$ROOT/src/util.cpp"
)

echo "==> Win10/11 x64: compiling resources"
"$MODERN_RC" -I "$ROOT/res" "$ROOT/res/app.rc" -O coff -o "$WORK/app_modern.o"

echo "==> Win10/11 x64: compiling and linking"
"$MODERN_CXX" \
    -std=c++17 -O2 -municode -mwindows \
    -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
    -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 \
    -I "$ROOT/src" -I "$ROOT/res" \
    -finput-charset=UTF-8 -fexec-charset=UTF-8 \
    -Wall -Wextra \
    -o "$OUT/CRTBender-Win10-x64.exe" \
    "${COMMON_SOURCES[@]}" "$ROOT/src/render.cpp" "$WORK/app_modern.o" \
    -ld3d11 -ldxgi -ldxguid -ld3dcompiler \
    -lcomctl32 -lshell32 -lole32 -luuid -luser32 -lgdi32 -ladvapi32 \
    -static -static-libgcc -static-libstdc++

echo "==> Windows XP x86: compiling resources"
"$XP_RC" -DCRTB_XP=1 -I "$ROOT/res" "$ROOT/res/app.rc" \
    -O coff -o "$WORK/app_xp.o"

echo "==> Windows XP x86: compiling and linking"
"$XP_CXX" \
    -std=c++17 -O2 -municode -mwindows \
    -DCRTB_XP=1 -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX \
    -DWINVER=0x0501 -D_WIN32_WINNT=0x0501 -DNTDDI_VERSION=0x05010000 \
    -I "$ROOT/src" -I "$ROOT/res" \
    -finput-charset=UTF-8 -fexec-charset=UTF-8 \
    -Wall -Wextra \
    -Wl,--major-subsystem-version,5,--minor-subsystem-version,1 \
    -o "$OUT/CRTBender-WinXP-x86.exe" \
    "${COMMON_SOURCES[@]}" "$ROOT/src/render_xp.cpp" "$ROOT/src/xp_compat.cpp" \
    "$WORK/app_xp.o" \
    -ld3d9 -lcomctl32 -lshell32 -lole32 -luuid -luser32 -lgdi32 -ladvapi32 \
    -static -static-libgcc -static-libstdc++

echo "==> stripping and packaging"
"$MODERN_STRIP" --strip-all "$OUT/CRTBender-Win10-x64.exe"
"$XP_STRIP" --strip-all "$OUT/CRTBender-WinXP-x86.exe"

# Keep the original filename as the default Win10/11 download.
cp "$OUT/CRTBender-Win10-x64.exe" "$OUT/CRTBender.exe"
(
    cd "$OUT"
    zip -q -9 -FS CRTBender-Win10-x64.zip CRTBender-Win10-x64.exe
    zip -q -9 -FS CRTBender-WinXP-x86.zip CRTBender-WinXP-x86.exe
    zip -q -9 -FS CRTBender.zip CRTBender.exe
)

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

echo "==> checking the embedded shader source"
g++ -std=c++17 -O1 -Wall -Wextra -I "$ROOT/src" \
    -o "$WORK/shader_source_tests" "$ROOT/tests/test_shader_source.cpp"
"$WORK/shader_source_tests"

echo
ls -lh "$OUT/CRTBender.exe" \
       "$OUT/CRTBender-Win10-x64.exe" \
       "$OUT/CRTBender-WinXP-x86.exe"
