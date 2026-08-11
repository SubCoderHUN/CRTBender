# CRTBender

Software geometry correction for CRT monitors on Windows. Pre-distorts the desktop image via DXGI Desktop Duplication and Direct3D 11 so that after your CRT's physical distortion, the image ends up straight—no hardware mods or service menu tweaking required.

<img width="1151" height="835" alt="bender" src="https://github.com/user-attachments/assets/22555c8a-007c-45b9-b8e5-3dc6b8373db8" />


## Features

- **System-wide:** Corrects the entire primary display in real time.
- **Customizable Grid:** 15×15 default control grid (adjustable from 3×3 up to 21×21). Drag points to fix pincushioning, barrel distortion, trapezoids, or local sag.
- **Monotonic Interpolation:** Uses Fritsch-Carlson monotonic cubic Hermite splines. Untouched grid points don't overshoot or "bleed" into good areas.
- **Resolution/Hz Profiles:** CRT geometry changes with timings. CRTBender auto-switches profiles when display mode changes (e.g. `1600x1200@85` vs `1280x960@75`).
- **Image Quality:** Uses anti-ringing Lanczos-3 filtering to keep text crisp. Untouched pixels remain 1:1 bit-exact.
- **Hotkeys:**
  - `Ctrl+Alt+B` - Toggle correction on/off (panic key / quick A-B test)
  - `Ctrl+Alt+G` - Toggle built-in alignment grid / test pattern
  - `Ctrl+Alt+E` - Open calibration window
  - `Esc` - Hide test pattern

## Known Limitations

- **Borderless Windowed Only:** Cannot draw over exclusive fullscreen games. (ReShade shader export is planned).
- **Cursor Offset:** Hardware mouse cursor isn't warped to prevent software blur, so it may misalign slightly near heavy warping zones.
- **Latency:** Adds ~1 frame of latency from desktop capture.
- **DRM Content:** Netflix / DRM video renders black under Desktop Duplication (toggle off with `Ctrl+Alt+B`).

---

## Getting Started

1. Download `CRTBender.exe` from releases or build it yourself.
2. Run the executable (portable, saves settings to `%APPDATA%\CRTBender\crtbender.cfg`).
3. Press `Ctrl+Alt+G` to bring up the test grid.
4. Drag grid control points to straighten lines. Use arrow keys for fine adjustments (Shift = 1px, Ctrl = 0.05px). Double-click a point to reset it.
5. Click **Save** or close the calibration window.

---

## Building from Source

### Requirements
- Visual Studio 2019 / 2022 (Desktop development with C++)
- CMake 3.20+

### Windows Build
```powershell
cmake -B build -A x64
cmake --build build --config Release
# Output: build\Release\CRTBender.exe
