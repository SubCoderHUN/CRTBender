// CRTBender - the capture + warp + present pipeline.
//
// Everything display related runs on its own thread and owns its own window.
// That matters: the editor and the tray live on the main thread, and modal
// loops there (dragging a window by its title bar, opening a menu) would
// otherwise stall the screen.
//
// The pipeline is:
//
//   DXGI Desktop Duplication  ->  D3D11 texture  ->  warped mesh draw
//                             ->  fullscreen click-through overlay window
//
// The overlay would normally capture itself and feed back, so it is marked
// WDA_EXCLUDEFROMCAPTURE: still visible on the physical screen, invisible to
// Desktop Duplication. If that call fails the engine refuses to start rather
// than producing an infinite hall of mirrors.
#pragma once

#include "config.h"
#include "warpmesh.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace crtb {

// Everything the render thread needs, snapshotted under a lock on update.
struct RenderState {
    WarpMesh mesh;
    bool  enabled        = true;
    float overscan       = 1.0f;
    float edgeBleed      = 0.0f;   // normalized units
    int   patternMode    = 0;      // 0 off, 1 over desktop, 2 on black
    int   patternCells   = 8;
    float patternOpacity = 0.85f;
    int   tessellation   = 96;
    int   quality        = 2;      // 0 bilinear, 1 bicubic, 2 Lanczos + anti-ringing
    // false = BitBlt present (required for the layered click-through overlay),
    // true = flip model. Read only when the pipeline is (re)initialized.
    bool  flipModel      = false;
};

class WarpEngine {
public:
    struct Impl;   // named by the overlay window procedure

    WarpEngine();
    ~WarpEngine();

    WarpEngine(const WarpEngine&) = delete;
    WarpEngine& operator=(const WarpEngine&) = delete;

    // notifyWindow receives modeChangeMsg (posted) when the display mode
    // changes, so the app can switch to the matching profile.
    bool Start(HWND notifyWindow, UINT modeChangeMsg);
    void Stop();

    // Thread-safe; applied on the next render iteration.
    void Update(const RenderState& state);

    // True while the overlay is up and presenting frames.
    bool Active() const { return active_.load(std::memory_order_relaxed); }

    // Last initialization / runtime failure, empty when healthy.
    std::wstring LastError() const;

    // Called from the overlay window procedure on WM_DISPLAYCHANGE.
    void OnDisplayChanged();

    void SetError(std::wstring message);

private:
    void ThreadMain();

    std::unique_ptr<Impl>  impl_;
    std::thread            thread_;
    std::atomic<bool>      running_{false};
    std::atomic<bool>      active_{false};
    std::atomic<uint32_t>  version_{0};

    mutable std::mutex     stateMutex_;
    RenderState            pending_;

    mutable std::mutex     errorMutex_;
    std::wstring           lastError_;

    HWND                   notifyWindow_  = nullptr;
    UINT                   modeChangeMsg_ = 0;
};

} // namespace crtb
