// CRTBender - the calibration window.
//
// Layout: the lattice canvas and a status readout on the left, a tabbed panel on
// the right. The panel is tabbed rather than one long column because the program
// now carries four separate groups of controls - lattice, parametric geometry,
// convergence and program settings - and stacking sixty widgets in one strip
// would be unusable.
//
// The canvas draws the *combined* field: parametric layer plus lattice, which is
// what actually reaches the screen. Dragging a handle edits the lattice term
// only, so a slider moved afterwards does not fight the hand-tuned points.
//
// Real corrections are tiny - a few pixels out of a thousand - so the preview
// applies a display-only magnification, and readouts are in screen pixels
// because that is the number you are really reasoning about.
#pragma once

#include "config.h"
#include "display.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace crtb {

// Implemented by the application object; the editor never touches the engines or
// the config file directly.
class EditorHost {
public:
    virtual ~EditorHost() = default;

    virtual Settings& HostSettings() = 0;

    virtual const std::vector<MonitorInfo>& Monitors() const = 0;
    virtual int                SelectedMonitor() const = 0;
    virtual void               SelectMonitor(int index) = 0;
    virtual const MonitorInfo& ActiveMonitor() const = 0;
    virtual Profile&           ActiveProfile() = 0;

    // Called after any edit. Pushes the new state to the render threads and,
    // when persist is set, writes the config file.
    virtual void OnEditorChanged(bool persist) = 0;

    // Autostart is registry state rather than config state, so it goes through
    // the host too.
    virtual bool GetAutostart() const = 0;
    virtual void SetAutostart(bool enabled) = 0;

    // One line of engine health for the status area, empty when fine.
    virtual std::wstring EngineStatus() const = 0;
};

class EditorWindow {
public:
    // Creates the window if needed, then brings it to the front.
    void Open(HINSTANCE inst, EditorHost* host);
    void Close();
    bool IsOpen() const { return hwnd_ != nullptr; }
    HWND Handle() const { return hwnd_; }

    // Re-reads everything from the host, e.g. after a monitor or mode change.
    void Refresh();

private:
    enum Page {
        kPageGrid = 0,
        kPageGeometry,
        kPageConvergence,
        kPageImage,
        kPageProgram,
        kPageCount,
    };

    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    // The tab pages live in a child window of the tab control so that a page
    // taller than the panel can scroll and, crucially, be clipped: as plain
    // siblings the overflowing controls would spill over the canvas.
    static LRESULT CALLBACK PageHostProcThunk(HWND, UINT, WPARAM, LPARAM);
    void OnPageScroll(WPARAM request);
    void ScrollPageBy(int lines);
    // Lays out the active page inside the page host. Returns the content height.
    // measureOnly computes that height without moving anything, so the scroll
    // range can be clamped before the controls are placed for real.
    int  LayoutPage(bool measureOnly);
    void UpdatePageScroll(int contentHeight);

    // Page controls belong to the page host, everything else to the window
    // itself; this finds either.
    HWND Ctl(int id) const;
    // Where page controls live and where they are laid out. Normally the page
    // host; if that could not be created they fall back to the window itself,
    // laid out inside the tab's display area without scrolling. Either way the
    // panel is never blank.
    HWND PageParent() const { return pageHost_ ? pageHost_ : hwnd_; }
    RECT PageArea() const;
    // Double-clicking a slider returns it to its default.
    static LRESULT CALLBACK SliderProcThunk(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void ResetSliderToDefault(int id);

    void CreateControls(HWND hwnd);
    // Applies every visible string; re-run when the language changes.
    void RelabelControls();
    void RefreshValueLabels();
    void LayoutControls();
    void SyncControlsFromModel();
    void ApplyControlsToModel(int controlId);
    void UpdateStatusText();

    void PaintCanvas(HDC dc, const RECT& client);
    RECT CanvasRect(const RECT& client) const;
    RECT ScreenAreaRect(const RECT& client) const;

    // Combined displacement at a lattice node: parametric layer + lattice.
    Offset CombinedAt(int row, int col) const;
    void PointToCanvas(int row, int col, const RECT& area, int& x, int& y) const;
    int  HitTestPoint(int mx, int my, const RECT& area) const;   // index, -1 = none
    int  HitTestRowLock(int mx, int my, const RECT& area) const; // row, -1 = none

    // Handle and glyph sizes track the lattice density, so a 21x21 grid in a
    // small window stays clickable without the handles running into each other.
    float LatticeSpacing(const RECT& area) const;
    int   HandleRadius(const RECT& area) const;
    int   PickRadius(const RECT& area) const;
    int   LockGlyphSize(const RECT& area) const;

    void PushUndo();
    void Undo();
    void NudgeSelection(float dxPixels, float dyPixels);
    void MoveControlPoint(int row, int col, float dxNorm, float dyNorm, bool additive);

    float ScreenAspect() const;
    int   Scale(int value) const { return MulDiv(value, dpi_, 96); }

    HWND        hwnd_     = nullptr;
    HWND        tabs_     = nullptr;
    HWND        pageHost_ = nullptr;
    int         scrollPos_ = 0;
    RECT        pageRect_{};      // fallback page area in editor client coords
    HINSTANCE   inst_ = nullptr;
    EditorHost* host_ = nullptr;
    HFONT       font_ = nullptr;
    int         dpi_  = 96;
    int         page_ = kPageGrid;

    int  selRow_ = -1;
    int  selCol_ = -1;
    bool dragging_ = false;
    POINT dragOrigin_{};
    Offset dragStart_{};

    std::vector<WarpMesh> undo_;
    bool suppressSync_ = false;
};

} // namespace crtb
