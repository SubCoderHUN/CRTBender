# CRTBender

Software geometry correction for CRT monitors on Windows. The program pre-distorts the desktop image so that after the monitor's own distortion, it becomes straight-without physical adjustments, service menus, or a soldering iron.

The correction applies to the **entire screen**: a 15×15 grid (adjustable, 3×3 … 21×21) lies over the whole image, and by moving any of its points up/down (or left/right as needed), you shape the geometry-corners, edges, center, anywhere.
<img width="1151" height="835" alt="bender" src="https://github.com/user-attachments/assets/b0aab765-eba5-4e36-a244-a87d3068498a" />

---

## Features

- **System-wide**: corrects the entire primary monitor image, not on a per-application basis.
- **15×15 grid over the full image** (225 control points), using monotonic cubic interpolation (see below why not Catmull-Rom). Adjustable from 3×3 up to 21×21 as needed: fewer points are more convenient for coarse shaping, while a denser grid is needed for local irregularities.
- **Profiles by resolution + refresh rate.** Geometry on a CRT is timing-dependent: `1600x1200@85` and `1280x960@75` distort differently. The program detects mode switches and automatically changes profiles.
- **Test pattern** (grid + frame + center cross) over the desktop or on a black background to serve as an alignment guide.
- **Hotkeys** for calibration:
  - `Ctrl+Alt+B` - toggle correction on/off (A/B comparison, also serves as a **panic button**)
  - `Ctrl+Alt+G` - cycle test pattern
  - `Esc` - disable test pattern (only active while the pattern is visible, without stealing input from other applications)
  - `Ctrl+Alt+E` - open calibration window
- **Language**: English and Hungarian, switchable from the tray menu or the calibration window. Default is English; the choice is saved to the CFG file and loaded at startup.
- **Tray icon**, settings saved to a CFG file, optional launch with Windows.

## Limitations (Honestly)

- **Cannot draw over exclusive fullscreen games.** It works in borderless windowed mode. (For exclusive fullscreen, the warp shader would need to be integrated into the game's render pipeline - see the Roadmap section.)
- **DRM-protected video** (Netflix, Disney+ in browsers) appears black in captures because Desktop Duplication returns it that way. In such cases, disable correction with `Ctrl+Alt+B`.
- **Does not distort the mouse cursor.** The cursor is rendered above everything by hardware; it offsets from content near the top of the screen by the amount of correction (a few pixels). This is an intentional choice: a software-rendered, distorted cursor would blur.
- **Single monitor only.** Currently, it always targets the primary monitor.
- **Resampling**, meaning curved regions soften slightly. This is a mathematical necessity: if an image is shifted by a fractional pixel, it must be resampled. However, the loss can be minimized-see the "Image Quality" section.
- Adds one frame of input lag. Unnoticeable for desktop work.

---

## Pre-built Executable

The compiled `dist/CRTBender.exe` is included in the repository - download, run, and you're set. A single executable with no installation and no runtime dependencies (static CRT); settings are saved under `%APPDATA%\CRTBender`.

Rebuilding on Linux: `./tools/build.sh` (this updates `dist/CRTBender.exe` and runs tests).

## Compiling on Windows

Requirements: **Visual Studio 2019/2022** (C++ desktop workload) and **CMake 3.20+**. No external dependencies.

```powershell
cmake -B build -A x64
cmake --build build --config Release
# Output: build\Release\CRTBender.exe
```

`d3dcompiler_47.dll` is part of Windows and does not need to be bundled. The executable is built with a static CRT runtime, so it does not require the Visual C++ Redistributable.

The warp math is platform-independent and can be tested separately:

```powershell
cmake --build build --target warp_tests
.\build\Debug\warp_tests.exe
```

---

## Usage

On first launch, the calibration window opens automatically. Afterwards, the application lives in the system tray; left-click to open calibration, right-click for the context menu.

### Calibration Workflow

1. **Enable the test pattern** (`Ctrl+Alt+G` or via the dropdown on the panel). Start with the "Over Desktop" option so you can see the editor at the same time.
2. **Go through the image.** The grid covers the entire screen, meaning you aren't limited to adjusting just the top edge: side curvature (pincushion/barrel), corner pull-in, trapezoid distortion, and even local irregularities in the center are adjusted the same way. Wherever a line should be straight but isn't, drag the nearest grid points.
3. **Correction direction is inverted relative to the flaw.** If the monitor bends the image *upward* in an area, drag the point *downward*. With "Left/Right Mirroring" enabled, the opposite side automatically mirrors your movements - CRT distortion is almost always symmetrical across the vertical axis.
4. **Lock what's already good.** Each row has a small padlock icon on the left side of the grid. Once a band is aligned, lock it so it won't move by accident.
5. **Fine-tune with arrow keys.** Arrow key = 0.25 px, `Shift`+Arrow = 1 px, `Ctrl`+Arrow = 0.05 px. The panel always displays the selected point's offset in screen pixels.
6. **Messed up a point?** Double-click it to snap it back to zero. `Ctrl+Z` undoes the last step.
7. **Verify with `Ctrl+Alt+B`.** Toggling back and forth instantly shows whether geometry improved.
8. When finished, click `Save` - or simply close the window, as it saves automatically.

Anything you do not move **remains exactly in place** - thanks to monotonic interpolation, correction does not "bleed" into good parts of the image. You can safely fix problematic areas individually.

### What grid size to use?

15×15 is a solid default: on a 1600×1200 screen, control points are spaced ~107 pixels apart. If you are only adjusting large-scale curves, 7×7 or 9×9 is more convenient (fewer points, faster workflow). If you need to fix a small local anomaly, switch to 21×21. When switching, the existing shape is preserved: the program resamples the curve onto the new grid density.

### Editor Zoom

Actual correction is only a few pixels out of 1200, which would be invisible on the preview grid. The "Editor Zoom" slider **only scales the preview** (default is 8×) and has no effect on the rendered output. It also sets drag sensitivity: higher zoom = finer control.

### Image Quality

Wherever the image is shifted by a subpixel amount, resampling occurs, which inevitably causes slight softening. Three factors determine quality:

- **Resampling Quality** (selectable on the panel):
  - *Bilinear* - fastest and softest.
  - *Bicubic* - balanced (former default).
  - *Sharp - Lanczos + Anti-ringing* - **the new default.** The Lanczos-3 window function preserves high frequencies that bicubic smooths out, keeping corrected areas nearly as sharp as untouched ones. However, sharper filters overshoot on high-contrast edges, appearing as halos around text; to prevent this, the program clamps the result to the value range of the four nearest samples. This eliminates ringing without affecting anything else.
- **Untouched areas remain bit-exact.** At zero offset, all filters simplify to identity, meaning pristine regions experience zero degradation. It is best to drag points only where strictly necessary.
- **Overscan degrades the entire image.** Above 100%, the full image is scaled, forcing resampling across the entire screen. Keep overscan at 100%; automatic edge fill is the proper solution for black borders.

### Overscan and Edge Fill

If you pull the top of the image downward, a black strip would theoretically remain at the top. Two solutions are available:

- **Edge Fill** (automatic by default): the outer ring of the grid extends beyond the screen, smearing the outermost pixel row. This is default behavior and preserves screen content.
- **Overscan (zoom)**: 100-115% scaling centered on the screen. More foolproof, but crops the edges. Only necessary for very large corrections.

---

## Configuration File

`%APPDATA%\CRTBender\crtbender.cfg` - plain text, editable manually.
If `crtbender.cfg` is placed alongside the executable, the program uses it directly (portable mode).

```ini
[general]
enabled         = 1
autostart       = 0
pattern_mode    = 0      # 0=off, 1=over desktop, 2=black background
present_mode    = bitblt # bitblt or flip
preview_gain    = 8
language        = en     # en or hu
quality         = 2      # 0=bilinear, 1=bicubic, 2=sharp (Lanczos)

[profile:1600x1200@85]
grid     = 15
overscan = 1.0000
bleed    = auto
locked   = 3,4
row.0    = +0.00000,+0.00333 +0.00000,+0.00417 ...
```

Offsets are normalized as fractions of screen width/height. `+0.005` on a 1200-pixel high display represents 6 pixels downward.

Log file: `%APPDATA%\CRTBender\crtbender.log` (accessible directly from the tray menu).

## Launching with Windows

Can be enabled via the tray menu or calibration window. Writes to the `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` registry key - requires no administrator privileges and installs no system services. When launched at system startup, the program runs with the `--silent` flag: it minimizes to the system tray and loads saved corrections automatically.

---

## How It Works

```
DXGI Desktop Duplication ─► D3D11 Texture ─► Draw Warped Grid
                                          ─► Fullscreen Click-Through Overlay
```

Key design decisions:

**The overlay excludes itself from capture.** Without this, Desktop Duplication would capture its own rendered overlay, resulting in an infinite feedback loop. `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` resolves this: the window remains visible on the physical display while remaining invisible to the desktop capture pipeline. If this call fails (on Windows 10 versions older than 2004), the program **will not initiate** correction and reports an error instead. Side effect: screenshots taken by the user will capture the un-warped desktop - which is correct behavior.

**Rendering runs on a dedicated thread.** System tray handling and the editor UI reside on the main thread. If unified on a single thread, dragging a window title bar would enter a modal loop and freeze screen updates.

**Forward mapping, not inverse.** Vertices of a densely tessellated grid store offsets, while texture coordinates remain on a regular grid. Rasterization performs interpolation, keeping the shader trivial. Inverse mapping would require iteratively inverting the offset field. Subdivision adapts dynamically to grid size: at least 10 subdivisions per grid cell, translating to 140×140 for 15×15 and 200×200 for 21×21.

**Splines are precalculated.** This decision stemmed from profiling: if tangents were recalculated on every sample during interpolation, `Eval` scaled at O(n²), requiring 118 ms to rebuild the GPU grid at 21×21 (~8 FPS during drag operations). Since mapping is separable, per-row curves are built once, reducing total rebuild time from O(stride² · n²) to O(stride · (stride + n)): dropping from 118 ms down to 0.93 ms.

**Identity scaling when disabled.** When turned off, the grid represents identity mapping; every vertex maps precisely to its corresponding texel, and bilinear sampling evaluates to exact pixels. Consequently, toggling `Ctrl+Alt+B` truly compares correction versus non-correction without introducing resampling artifacts.

**Monotonic cubic vs. Catmull-Rom.** Switched based on measurement: with Catmull-Rom, a 6 px top correction caused an opposing ~0.45 px deflection near the upper third of the screen - distorting areas that were already correct. Fritsch-Carlson monotonic cubic Hermite interpolation passes through all control points precisely (dragging 6 px moves exactly 6 px) without overshooting: unadjusted points remain fixed in place. Evaluated in `tests/test_warp.cpp`.

---

## Roadmap

- Multi-monitor support (currently targets primary display).
- Installer (`installer/crtbender.iss`, Inno Setup skeleton included).
- Per-channel warping = software convergence correction (independent R/G/B grids).
- Profile export to ReShade shader format for exclusive fullscreen games with zero added latency.
- Camera-based automated calibration: photographing a point grid and computing inverse warp via OpenCV.

## About the Project

- Created by: **SubCoderHUN**
- Project Page: <https://github.com/SubCoderHUN/CRTBender>

Within the application, the tray menu options *Project Page (GitHub)* and *About CRTBender...*, as well as the *GitHub* button in the calibration window, lead here.

## License

See the [LICENSE](LICENSE) file.
