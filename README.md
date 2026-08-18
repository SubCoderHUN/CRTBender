# CRTBender

Software geometry correction for CRT monitors on Windows. Your monitor bends the
picture; CRTBender bends it back before it gets there. No hardware mods, no
service menu. Separate builds support Windows 10/11 and Windows XP.

<img width="1151" height="835" alt="image" src="https://github.com/user-attachments/assets/b56b0f8d-3ec3-4c9d-a08e-850ca03952bc" />


## Features

- Corrects the whole picture, on every connected monitor.
<img width="700" height="394" alt="ezgif com-resize" src="https://github.com/user-attachments/assets/f0581e9d-aa1d-44de-9786-9eb61452c951" />

- A 15x15 grid on top of that, for anything the sliders cannot reach.
<img width="777" height="633" alt="image" src="https://github.com/user-attachments/assets/a2ca4ebd-5786-47d7-88f8-02bbc5285274" />

- Sliders for the usual things first: rotation, pincushion, trapezoid, size,
  position, and separate top and bottom edge bows.
<img width="346" height="649" alt="image" src="https://github.com/user-attachments/assets/a63842cd-7b71-43b6-bf58-8239d28902cb" />

- Centre, edge and per-corner RGB convergence correction for coloured fringes.
<img width="345" height="288" alt="image" src="https://github.com/user-attachments/assets/14cfe8ec-2b8e-4c9d-9ab2-8c22df9a95ea" />
<img width="343" height="513" alt="image" src="https://github.com/user-attachments/assets/28e74eda-6200-481d-9b7e-4982c914dd56" />

- Selective adaptive sharpening keeps warped text crisp without sharpening
  untouched parts of the desktop.
<img width="314" height="245" alt="image" src="https://github.com/user-attachments/assets/349d5dd1-27b1-447c-bcac-5fd6fd7efba8" />
  
- Remembers a separate setting for every monitor and every resolution.
<img width="349" height="81" alt="image" src="https://github.com/user-attachments/assets/57f2727f-f141-4041-ab19-003584d00263" />

- Six built-in test patterns: geometry, colour, greyscale, convergence,
  sharpness and overscan.
<img width="352" height="145" alt="image" src="https://github.com/user-attachments/assets/67eabcb0-117c-4376-9a1f-a9e7396c0d58" />


- English and Hungarian.

## Hotkeys

- `Ctrl+Alt+B` - correction on/off. Also the panic key.
- `Ctrl+Alt+G` - test grid on/off.
- `Ctrl+Alt+E` - open the calibration window.
- `Esc` - hide the test grid.

## System Requirements
| Requirement | Windows 10 / 11 Version | Windows XP Version (Legacy) |
| :--- | :--- | :--- |
| **Operating System** | Windows 10 (1809+) or Windows 11 (64-bit) | Windows XP SP2 (32-bit / x86) |
| **Graphics API** | Direct3D 11 (`DXGI Desktop Duplication`) | Direct3D 9 (`GDI Capture`) |
| **GPU Hardware** | DirectX 11 compatible GPU (Intel HD 4000+, GTX 400+, HD 5000+) | DirectX 9.0c GPU with **Pixel Shader 2.0** support (e.g., Radeon 9000+, GeForce FX / 6 series+) |
| **RAM** | 512 MB | 128 MB |
| **Dependencies** | None (Portable, single `.exe`) | None (Portable, single `.exe`) |

> **Note on Performance:** The pixel shader transformation overhead is practically zero on any dedicated GPU (including GCN 1.0+ Radeon cards using CRT Emudriver at high resolutions like 1920x1440). Legacy pre-DX9 hardware without Pixel Shader 2.0 support is not supported.

## Getting Started

1. On Windows 10/11, run `CRTBender-Win10-x64.exe`. On 32-bit Windows XP SP2/SP3 with
   DirectX 9, use `CRTBender-WinXP-x86.exe`. Both live in the tray.
2. Press `Ctrl+Alt+G` to show the test grid.
3. On the **Basic geometry** tab, drag the sliders until the lines look straight.
   This does most of the work.
4. Use the **Grid** tab for whatever is left. Drag a point, or select one and
   nudge it with the arrow keys.
5. Press `Ctrl+Alt+B` a few times to compare before and after.
6. Close the window. Everything is saved automatically.

Double-click any slider or grid point to put it back to default, so an
experiment is never a one-way trip.

## Known Limitations

- Cannot draw over exclusive fullscreen games. Borderless windowed works.
- On Windows 10/11, CRTBender steps aside automatically for protected video.
- The XP build uses GDI capture and D3D9. It is lighter on old hardware but
  limited to bilinear filtering and usually runs at 30-60 FPS.
- The mouse cursor is not bent, so it can sit a pixel or two off in heavily
  corrected areas.

## Building from Source

### Requirements
- Visual Studio with Desktop development for C++
- For XP: the `v141_xp` toolset and a Win32 generator
- CMake 3.20+

### Windows Build
```powershell
# Windows 10/11 x64
cmake -B build -A x64
cmake --build build --config Release
# Output: build\Release\CRTBender.exe

# Windows XP x86
cmake -B build-xp -A Win32 -T v141_xp -DCRTB_XP=ON
cmake --build build-xp --config Release
# Output: build-xp\Release\CRTBender-WinXP-x86.exe
```

On Linux, `./tools/build.sh` cross-builds and packages both editions.

## Roadmap

- [x] Per-corner RGB convergence controls
- [ ] Auto-calibration from a photo or webcam image
- [ ] Presets for specific CRT monitors and TVs
- [ ] Linux port using GLSL, Vulkan or Gamescope

## About

Made by SubCoderHUN - https://github.com/SubCoderHUN/CRTBender
