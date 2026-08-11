// CRTBender - the calibration window.
//
// The canvas draws the control lattice with its interpolated curves so you can
// see the actual deformation, not just the handles. Real corrections are tiny -
// a few pixels out of a thousand - so the preview applies a display-only
// magnification, and the readout is in screen pixels because that is the number
// you are really reasoning about.
#pragma once

#include "config.h"

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

// Implemented by the application object; the editor never touches the engine or
// the config file directly.
class EditorHost {
public:
    virtual ~EditorHost() = default;

    virtual Settings&    HostSettings()   = 0;
    virtual Profile&     ActiveProfile()  = 0;
    virtual DisplayMode  ActiveMode() const = 0;
    virtual std::string  ActiveModeKey() const = 0;

    // Called after any edit. Pushes the new state to the render thread and,
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

    // Re-reads everything from the host, e.g. after a display mode change.
    void Refresh();

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

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

    void PointToCanvas(int row, int col, const RECT& area, int& x, int& y) const;
    // Handle and glyph sizes track the lattice density, so a 21x21 grid in a
    // small window stays clickable without the handles running into each other.
    float LatticeSpacing(const RECT& area) const;
    int   HandleRadius(const RECT& area) const;
    int   PickRadius(const RECT& area) const;
    int   LockGlyphSize(const RECT& area) const;
    int  HitTestPoint(int mx, int my, const RECT& area) const;   // index, -1 = none
    int  HitTestRowLock(int mx, int my, const RECT& area) const; // row, -1 = none

    void PushUndo();
    void Undo();
    void NudgeSelection(float dxPixels, float dyPixels);
    void MoveControlPoint(int row, int col, float dxNorm, float dyNorm, bool additive);

    int  Scale(int value) const { return MulDiv(value, dpi_, 96); }

    HWND        hwnd_ = nullptr;
    HINSTANCE   inst_ = nullptr;
    EditorHost* host_ = nullptr;
    HFONT       font_ = nullptr;
    int         dpi_  = 96;

    int  selRow_ = -1;
    int  selCol_ = -1;
    bool dragging_ = false;
    POINT dragOrigin_{};
    Offset dragStart_{};

    std::vector<WarpMesh> undo_;
    bool suppressSync_ = false;
};

} // namespace crtb
