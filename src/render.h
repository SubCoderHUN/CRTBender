// CRTBender - the capture + warp + present pipeline.
//
// Everything display related runs on its own thread and owns its own window.
// That matters: the editor and the tray live on the main thread, and modal
// loops there (dragging a window by its title bar, opening a menu) would
// otherwise stall the screen.
//
// The Win10 build uses DXGI Desktop Duplication and D3D11. The XP build keeps
// the same public engine interface but supplies a GDI BitBlt + D3D9 backend.
#pragma once

#include "config.h"
#include "geometry.h"
#include "warpmesh.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <memory>
#ifndef CRTB_XP
#include <atomic>
#include <mutex>
#include <thread>
#endif
#include <string>

namespace crtb {

// Everything the render thread needs, snapshotted under a lock on update.
struct RenderState {
    WarpMesh          mesh;
    GeometryParams    geometry;
    ConvergenceParams convergence;

    bool  enabled        = true;
    // Set by the auto-bypass watcher: the correction is wanted, but showing it
    // right now would do more harm than good (protected video would come out
    // black, an exclusive fullscreen app owns the output).
    bool  bypass         = false;
    float aspect         = 4.0f / 3.0f;
    float overscan       = 1.0f;
    float edgeBleed      = 0.0f;   // normalized units
    int   patternMode    = 0;      // 0 off, 1 over desktop, 2 on black
    TestPattern patternType = TestPattern::GeometryGrid;
    int   patternCells   = 8;
    float patternOpacity = 0.85f;
    int   tessellation   = 96;
    int   quality        = 2;      // 0 bilinear, 1 bicubic, 2 adaptive sharp
    float sharpness      = 0.40f;  // adaptive detail recovery in quality mode 2
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
    // targetDevice is a GDI display name such as \\.\DISPLAY1; empty means
    // the primary monitor. One engine drives one monitor.
    bool Start(HWND notifyWindow, UINT modeChangeMsg, std::wstring targetDevice);
    void Stop();

    // Thread-safe; applied on the next render iteration.
    void Update(const RenderState& state);

    // True while the overlay is up and presenting frames.
#ifdef CRTB_XP
    bool Active() const {
        return InterlockedCompareExchange(const_cast<volatile LONG*>(&active_), 0, 0) != 0;
    }
#else
    bool Active() const { return active_.load(std::memory_order_relaxed); }
#endif

    // Last initialization / runtime failure, empty when healthy.
    std::wstring LastError() const;

    // Called from the overlay window procedure on WM_DISPLAYCHANGE.
    void OnDisplayChanged();

    const std::wstring& TargetDevice() const { return targetDevice_; }

    void SetError(std::wstring message);

private:
    void ThreadMain();

    std::unique_ptr<Impl>  impl_;
#ifdef CRTB_XP
    static DWORD WINAPI ThreadThunk(LPVOID self);
    HANDLE                 thread_  = nullptr;
    volatile LONG          running_ = 0;
    volatile LONG          active_  = 0;
    volatile LONG          version_ = 0;

    mutable CRITICAL_SECTION stateMutex_;
    RenderState              pending_;

    mutable CRITICAL_SECTION errorMutex_;
    std::wstring             lastError_;
#else
    std::thread            thread_;
    std::atomic<bool>      running_{false};
    std::atomic<bool>      active_{false};
    std::atomic<uint32_t>  version_{0};

    mutable std::mutex     stateMutex_;
    RenderState            pending_;

    mutable std::mutex     errorMutex_;
    std::wstring           lastError_;
#endif

    HWND                   notifyWindow_  = nullptr;
    UINT                   modeChangeMsg_ = 0;
    std::wstring           targetDevice_;
};

} // namespace crtb
