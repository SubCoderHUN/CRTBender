#include "editor.h"
#include "i18n.h"
#include "util.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace crtb {
namespace {

constexpr const wchar_t* kEditorClass   = L"CRTBenderEditorWindow";
constexpr const wchar_t* kPageHostClass = L"CRTBenderPageHost";
constexpr int kScrollLine = 24;   // logical pixels per wheel notch step

enum ControlId : int {
    // Always visible, above the tabs
    CTL_LBL_MONITOR = 1001,
    CTL_MONITOR,
    CTL_ENABLED,
    CTL_LBL_PATTERN,
    CTL_PATTERN,
    CTL_TABS,

    // Grid page
    CTL_LBL_GRID,
    CTL_GRID,
    CTL_MIRROR,
    CTL_FREEMOVE,
    CTL_LBL_GAIN,
    CTL_GAIN,
    CTL_UNDO,
    CTL_RESETROW,
    CTL_RESETALL,

    // Geometry page
    CTL_GEO_HINT,
    CTL_GEO_RESET,

    // Convergence page
    CTL_CONV_HINT,
    CTL_CONV_RESET,

    // Image page
    CTL_LBL_QUALITY,
    CTL_QUALITY,
    CTL_LBL_OVERSCAN,
    CTL_OVERSCAN,
    CTL_AUTOBLEED,
    CTL_LBL_BLEED,
    CTL_BLEED,

    // Program page
    CTL_LBL_LANG,
    CTL_LANG,
    CTL_AUTOSTART,
    CTL_AUTOBYPASS,
    CTL_SAVE,
    CTL_GITHUB,
    CTL_CREDIT,

    // Outside the tabs
    CTL_STATUS,
    CTL_HELP,
};

// Slider ids are allocated in blocks, so a control maps back to its table entry
// by subtraction.
constexpr int kGeoSliderBase  = 1600;
constexpr int kGeoLabelBase   = 1700;
constexpr int kConvSliderBase = 1800;
constexpr int kConvLabelBase  = 1900;

// Trackbars run -kSliderRange..+kSliderRange, mapped onto each parameter's own
// range. 500 steps over a few pixels is finer than the eye can judge.
constexpr int kSliderRange = 500;

struct GeoSlider {
    Str   label;
    float GeometryParams::* member;
    float range;        // normalized value at full deflection
    bool  horizontal;   // reads out against screen width rather than height
    bool  degrees;      // reads out as an angle
};

constexpr GeoSlider kGeoSliders[] = {
    { Str::GeoHPosition,     &GeometryParams::hPosition,     0.05f, true,  false },
    { Str::GeoVPosition,     &GeometryParams::vPosition,     0.05f, false, false },
    { Str::GeoHSize,         &GeometryParams::hSize,         0.05f, true,  false },
    { Str::GeoVSize,         &GeometryParams::vSize,         0.05f, false, false },
    { Str::GeoRotation,      &GeometryParams::rotation,      0.02f, false, true  },
    { Str::GeoTrapezoid,     &GeometryParams::trapezoid,     0.03f, true,  false },
    { Str::GeoParallelogram, &GeometryParams::parallelogram, 0.03f, true,  false },
    { Str::GeoPincushion,    &GeometryParams::pincushion,    0.05f, true,  false },
    { Str::GeoPinBalance,    &GeometryParams::pinBalance,    0.03f, true,  false },
    { Str::GeoHLinearity,    &GeometryParams::hLinearity,    0.05f, true,  false },
    { Str::GeoVLinearity,    &GeometryParams::vLinearity,    0.05f, false, false },
    { Str::GeoTopBow,        &GeometryParams::topBow,        0.05f, false, false },
    { Str::GeoBottomBow,     &GeometryParams::bottomBow,     0.05f, false, false },
};

struct ConvSlider {
    Str   label;
    float ConvergenceParams::* member;
    float range;
    bool  horizontal;
};

constexpr ConvSlider kConvSliders[] = {
    { Str::ConvRedH,      &ConvergenceParams::rH,     0.004f, true  },
    { Str::ConvRedV,      &ConvergenceParams::rV,     0.004f, false },
    { Str::ConvRedHEdge,  &ConvergenceParams::rHEdge, 0.004f, true  },
    { Str::ConvRedVEdge,  &ConvergenceParams::rVEdge, 0.004f, false },
    { Str::ConvBlueH,     &ConvergenceParams::bH,     0.004f, true  },
    { Str::ConvBlueV,     &ConvergenceParams::bV,     0.004f, false },
    { Str::ConvBlueHEdge, &ConvergenceParams::bHEdge, 0.004f, true  },
    { Str::ConvBlueVEdge, &ConvergenceParams::bVEdge, 0.004f, false },
};

constexpr int kGeoCount  = static_cast<int>(std::size(kGeoSliders));
constexpr int kConvCount = static_cast<int>(std::size(kConvSliders));

// Offered lattice resolutions. 15x15 is the default: dense enough to shape the
// whole picture - edges, corners and middle - not just one band of it.
constexpr int kGridSizes[] = { 3, 5, 7, 9, 11, 13, 15, 17, 21 };

int GridIndexFor(int size) {
    for (size_t i = 0; i < std::size(kGridSizes); ++i)
        if (kGridSizes[i] == size) return static_cast<int>(i);
    return 6;   // 15 x 15
}

// Enough polyline samples that the drawn curve is smooth between control
// points, whatever the lattice resolution.
int CurveSteps(int gridSize) { return std::max(96, (gridSize - 1) * 10); }

constexpr float kMaxOffset = 0.15f;   // normalized clamp per control point
constexpr size_t kMaxUndo  = 64;

constexpr UINT_PTR kSliderSubclassId = 1;

HWND MakeChild(HWND parent, const wchar_t* cls, DWORD style, int id, HINSTANCE inst) {
    return CreateWindowExW(0, cls, L"", WS_CHILD | style,
                           0, 0, 10, 10, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
}

void SetTextOn(HWND control, const wchar_t* text) {
    if (control) SetWindowTextW(control, text);
}

BOOL CALLBACK SetChildFont(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, font, TRUE);
    return TRUE;
}

// Every control that belongs to a tab page, so the layout pass can hide the ones
// that are not on the current page.
template <typename Fn>
void ForEachPageControl(Fn fn) {
    static constexpr int kIds[] = {
        CTL_LBL_GRID, CTL_GRID, CTL_MIRROR, CTL_FREEMOVE, CTL_LBL_GAIN, CTL_GAIN,
        CTL_UNDO, CTL_RESETROW, CTL_RESETALL,
        CTL_GEO_HINT, CTL_GEO_RESET,
        CTL_CONV_HINT, CTL_CONV_RESET,
        CTL_LBL_QUALITY, CTL_QUALITY, CTL_LBL_OVERSCAN, CTL_OVERSCAN,
        CTL_AUTOBLEED, CTL_LBL_BLEED, CTL_BLEED,
        CTL_LBL_LANG, CTL_LANG, CTL_AUTOSTART, CTL_AUTOBYPASS,
        CTL_SAVE, CTL_GITHUB, CTL_CREDIT,
    };
    for (int id : kIds) fn(id);
    for (int i = 0; i < kGeoCount; ++i)  { fn(kGeoSliderBase + i);  fn(kGeoLabelBase + i); }
    for (int i = 0; i < kConvCount; ++i) { fn(kConvSliderBase + i); fn(kConvLabelBase + i); }
}

} // namespace

// ---------------------------------------------------------------------------

void EditorWindow::Open(HINSTANCE inst, EditorHost* host) {
    inst_ = inst;
    host_ = host;

    if (hwnd_) {
        Refresh();
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS;   // without this WM_LBUTTONDBLCLK never arrives
    wc.lpfnWndProc   = &EditorWindow::WndProcThunk;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kEditorClass;
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = wc.hIcon;
    RegisterClassExW(&wc);

    const int dpi = static_cast<int>(SystemDpi());
    const int w   = MulDiv(1180, dpi, 96);
    const int h   = MulDiv(820, dpi, 96);
    const int x   = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - w) / 2);
    const int y   = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - h) / 2);

    hwnd_ = CreateWindowExW(0, kEditorClass, T(Str::EdWindowTitle),
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            x, y, w, h, nullptr, nullptr, inst, this);
    if (!hwnd_) {
        LogLine(L"Editor window creation failed");
        return;
    }

    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void EditorWindow::Close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

void EditorWindow::Refresh() {
    if (!hwnd_) return;
    selRow_ = selCol_ = -1;
    undo_.clear();
    RelabelControls();          // monitor list may have changed
    SyncControlsFromModel();
    LayoutControls();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

float EditorWindow::ScreenAspect() const {
    if (!host_) return 4.0f / 3.0f;
    const MonitorInfo& monitor = host_->ActiveMonitor();
    if (monitor.width <= 0 || monitor.height <= 0) return 4.0f / 3.0f;
    return static_cast<float>(monitor.width) / static_cast<float>(monitor.height);
}

LRESULT CALLBACK EditorWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditorWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<EditorWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK EditorWindow::SliderProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                               UINT_PTR, DWORD_PTR ref) {
    // A trackbar normally treats a double click as two clicks, each nudging the
    // thumb. Returning to the default is far more useful, and matches what
    // double-clicking a control point already does on the canvas.
    if (msg == WM_LBUTTONDBLCLK) {
        if (auto* self = reinterpret_cast<EditorWindow*>(ref)) {
            self->ResetSliderToDefault(static_cast<int>(GetDlgCtrlID(hwnd)));
            return 0;
        }
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, &EditorWindow::SliderProcThunk,
                                                  kSliderSubclassId);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK EditorWindow::PageHostProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EditorWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<EditorWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<EditorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    // The controls live here, but every decision is the editor's, so their
    // notifications are handed straight up to it.
    case WM_COMMAND:
    case WM_HSCROLL: {
        HWND editor = GetAncestor(hwnd, GA_ROOT);
        return editor ? SendMessageW(editor, msg, wp, lp) : 0;
    }

    case WM_VSCROLL:
        if (self) self->OnPageScroll(wp);
        return 0;

    case WM_MOUSEWHEEL:
        if (self) {
            const int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            self->ScrollPageBy(-notches * self->Scale(kScrollLine) * 3);
        }
        return 0;

    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_BTNFACE));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));

    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wp), &client, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Control creation and labelling
// ---------------------------------------------------------------------------

RECT EditorWindow::PageArea() const {
    if (pageHost_) {
        RECT host{};
        GetClientRect(pageHost_, &host);
        return host;
    }
    return pageRect_;
}

HWND EditorWindow::Ctl(int id) const {
    if (pageHost_) {
        if (HWND h = GetDlgItem(pageHost_, id)) return h;
    }
    return hwnd_ ? GetDlgItem(hwnd_, id) : nullptr;
}

void EditorWindow::CreateControls(HWND hwnd) {
    // Always visible
    MakeChild(hwnd, L"STATIC", SS_LEFT, CTL_LBL_MONITOR, inst_);
    MakeChild(hwnd, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_MONITOR, inst_);
    MakeChild(hwnd, L"BUTTON", BS_AUTOCHECKBOX, CTL_ENABLED, inst_);
    MakeChild(hwnd, L"STATIC", SS_LEFT, CTL_LBL_PATTERN, inst_);
    MakeChild(hwnd, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_PATTERN, inst_);

    tabs_ = MakeChild(hwnd, WC_TABCONTROLW,
                      TCS_TABS | TCS_FIXEDWIDTH | WS_CLIPCHILDREN, CTL_TABS, inst_);
    for (int i = 0; i < kPageCount; ++i) {
        TCITEMW item{};
        item.mask    = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(L"");
        SendMessageW(tabs_, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
    }

    // Everything from here on lives in the scrolling page host, so a page taller
    // than the panel is clipped and scrolled instead of spilling over the canvas.
    WNDCLASSEXW pageClass{};
    pageClass.cbSize        = sizeof(pageClass);
    pageClass.lpfnWndProc   = &EditorWindow::PageHostProcThunk;
    pageClass.hInstance     = inst_;
    pageClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    pageClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    pageClass.lpszClassName = kPageHostClass;
    RegisterClassExW(&pageClass);

    // The page host is a child of the tab control. Making it an overlapping
    // sibling of the tab lets sibling clipping remove its entire visible region
    // on some Windows/theme combinations.
    HWND hostParent = tabs_ ? tabs_ : hwnd;
    pageHost_ = CreateWindowExW(0, kPageHostClass, L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN,
                                0, 0, 10, 10, hostParent, nullptr, inst_, this);
    if (!pageHost_) {
        // Not fatal: the pages then live directly in the window and simply do
        // not scroll. Worth knowing about, though.
        LogLine(L"Page host could not be created; tab pages will not scroll");
    }
    HWND page = PageParent();

    // Grid page
    MakeChild(page, L"STATIC", SS_LEFT, CTL_LBL_GRID, inst_);
    MakeChild(page, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_GRID, inst_);
    MakeChild(page, L"BUTTON", BS_AUTOCHECKBOX, CTL_MIRROR, inst_);
    MakeChild(page, L"BUTTON", BS_AUTOCHECKBOX, CTL_FREEMOVE, inst_);
    MakeChild(page, L"STATIC", SS_LEFT, CTL_LBL_GAIN, inst_);
    HWND gain = MakeChild(page, TRACKBAR_CLASSW, TBS_HORZ | TBS_NOTICKS, CTL_GAIN, inst_);
    SendMessageW(gain, TBM_SETRANGE, TRUE, MAKELPARAM(1, 24));
    SetWindowSubclass(gain, &EditorWindow::SliderProcThunk, kSliderSubclassId,
                      reinterpret_cast<DWORD_PTR>(this));
    MakeChild(page, L"BUTTON", BS_PUSHBUTTON, CTL_UNDO, inst_);
    MakeChild(page, L"BUTTON", BS_PUSHBUTTON, CTL_RESETROW, inst_);
    MakeChild(page, L"BUTTON", BS_PUSHBUTTON, CTL_RESETALL, inst_);

    // Geometry page
    MakeChild(page, L"STATIC", SS_LEFT, CTL_GEO_HINT, inst_);
    for (int i = 0; i < kGeoCount; ++i) {
        MakeChild(page, L"STATIC", SS_LEFT, kGeoLabelBase + i, inst_);
        HWND slider = MakeChild(page, TRACKBAR_CLASSW, TBS_HORZ | TBS_NOTICKS,
                                kGeoSliderBase + i, inst_);
        SendMessageW(slider, TBM_SETRANGEMIN, FALSE, -kSliderRange);
        SendMessageW(slider, TBM_SETRANGEMAX, TRUE,   kSliderRange);
        SetWindowSubclass(slider, &EditorWindow::SliderProcThunk, kSliderSubclassId,
                          reinterpret_cast<DWORD_PTR>(this));
    }
    MakeChild(page, L"BUTTON", BS_PUSHBUTTON, CTL_GEO_RESET, inst_);

    // Convergence page
    MakeChild(page, L"STATIC", SS_LEFT, CTL_CONV_HINT, inst_);
    for (int i = 0; i < kConvCount; ++i) {
        MakeChild(page, L"STATIC", SS_LEFT, kConvLabelBase + i, inst_);
        HWND slider = MakeChild(page, TRACKBAR_CLASSW, TBS_HORZ | TBS_NOTICKS,
                                kConvSliderBase + i, inst_);
        SendMessageW(slider, TBM_SETRANGEMIN, FALSE, -kSliderRange);
        SendMessageW(slider, TBM_SETRANGEMAX, TRUE,   kSliderRange);
        SetWindowSubclass(slider, &EditorWindow::SliderProcThunk, kSliderSubclassId,
                          reinterpret_cast<DWORD_PTR>(this));
    }
    MakeChild(page, L"BUTTON", BS_PUSHBUTTON, CTL_CONV_RESET, inst_);

    // Image page
    MakeChild(page, L"STATIC", SS_LEFT, CTL_LBL_QUALITY, inst_);
    MakeChild(page, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_QUALITY, inst_);
    MakeChild(page, L"STATIC", SS_LEFT, CTL_LBL_OVERSCAN, inst_);
    HWND overscan = MakeChild(page, TRACKBAR_CLASSW, TBS_HORZ | TBS_NOTICKS, CTL_OVERSCAN, inst_);
    SendMessageW(overscan, TBM_SETRANGE, TRUE, MAKELPARAM(100, 115));
    SetWindowSubclass(overscan, &EditorWindow::SliderProcThunk, kSliderSubclassId,
                      reinterpret_cast<DWORD_PTR>(this));
    MakeChild(page, L"BUTTON", BS_AUTOCHECKBOX, CTL_AUTOBLEED, inst_);
    MakeChild(page, L"STATIC", SS_LEFT, CTL_LBL_BLEED, inst_);
    HWND bleed = MakeChild(page, TRACKBAR_CLASSW, TBS_HORZ | TBS_NOTICKS, CTL_BLEED, inst_);
    SendMessageW(bleed, TBM_SETRANGE, TRUE, MAKELPARAM(0, 48));
    SetWindowSubclass(bleed, &EditorWindow::SliderProcThunk, kSliderSubclassId,
                      reinterpret_cast<DWORD_PTR>(this));

    // Program page
    MakeChild(page, L"STATIC", SS_LEFT, CTL_LBL_LANG, inst_);
    MakeChild(page, L"COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_LANG, inst_);
    MakeChild(page, L"BUTTON", BS_AUTOCHECKBOX, CTL_AUTOSTART, inst_);
    MakeChild(page, L"BUTTON", BS_AUTOCHECKBOX | BS_MULTILINE, CTL_AUTOBYPASS, inst_);
    MakeChild(page, L"BUTTON", BS_PUSHBUTTON, CTL_SAVE, inst_);
    MakeChild(page, L"BUTTON", BS_PUSHBUTTON, CTL_GITHUB, inst_);
    MakeChild(page, L"STATIC", SS_LEFT, CTL_CREDIT, inst_);

    // Outside the tabs
    MakeChild(hwnd, L"STATIC", SS_LEFT, CTL_STATUS, inst_);
    MakeChild(hwnd, L"STATIC", SS_LEFT, CTL_HELP, inst_);

#ifdef CRTB_XP
    // The XP backend is bilinear-only and cannot inspect protected-window
    // affinity, so keep unsupported settings visible but clearly unavailable.
    EnableWindow(Ctl(CTL_QUALITY), FALSE);
    EnableWindow(Ctl(CTL_AUTOBYPASS), FALSE);
#endif

    RelabelControls();
}

void EditorWindow::RelabelControls() {
    if (!hwnd_) return;
    suppressSync_ = true;

    SetWindowTextW(hwnd_, T(Str::EdWindowTitle));

    SetTextOn(Ctl(CTL_LBL_MONITOR), T(Str::EdMonitorLabel));
    SetTextOn(Ctl(CTL_ENABLED),     T(Str::EdEnabled));
    SetTextOn(Ctl(CTL_LBL_PATTERN), T(Str::EdPatternLabel));

    SetTextOn(Ctl(CTL_LBL_GRID),    T(Str::EdGridLabel));
    SetTextOn(Ctl(CTL_MIRROR),      T(Str::EdMirror));
    SetTextOn(Ctl(CTL_FREEMOVE),    T(Str::EdFreeMove));
    SetTextOn(Ctl(CTL_UNDO),        T(Str::EdUndo));
    SetTextOn(Ctl(CTL_RESETROW),    T(Str::EdResetRow));
    SetTextOn(Ctl(CTL_RESETALL),    T(Str::EdResetAll));

    SetTextOn(Ctl(CTL_GEO_HINT),    T(Str::GeoHint));
    SetTextOn(Ctl(CTL_GEO_RESET),   T(Str::GeoReset));
    SetTextOn(Ctl(CTL_CONV_HINT),   T(Str::ConvHint));
    SetTextOn(Ctl(CTL_CONV_RESET),  T(Str::ConvReset));

    SetTextOn(Ctl(CTL_LBL_QUALITY), T(Str::EdQualityLabel));
    SetTextOn(Ctl(CTL_AUTOBLEED),   T(Str::EdAutoBleed));

    SetTextOn(Ctl(CTL_LBL_LANG),    T(Str::EdLanguageLabel));
    SetTextOn(Ctl(CTL_AUTOSTART),   T(Str::EdAutostart));
    SetTextOn(Ctl(CTL_AUTOBYPASS),  T(Str::EdAutoBypass));
    SetTextOn(Ctl(CTL_SAVE),        T(Str::EdSave));
    SetTextOn(Ctl(CTL_GITHUB),      T(Str::EdProjectButton));
    SetTextOn(Ctl(CTL_HELP),        T(Str::EdHelp));

    wchar_t buf[256];
    swprintf(buf, std::size(buf), T(Str::EdCredit), kVersion, kAuthor);
    SetTextOn(Ctl(CTL_CREDIT), buf);

    // Tab captions
    if (tabs_) {
        static const Str kTabNames[kPageCount] = {
            Str::TabGrid, Str::TabGeometry, Str::TabConvergence,
            Str::TabImage, Str::TabProgram,
        };
        for (int i = 0; i < kPageCount; ++i) {
            TCITEMW item{};
            item.mask    = TCIF_TEXT;
            item.pszText = const_cast<wchar_t*>(T(kTabNames[i]));
            SendMessageW(tabs_, TCM_SETITEMW, i, reinterpret_cast<LPARAM>(&item));
        }
    }

    auto fillCombo = [&](int id, const wchar_t* const* items, int count, int selection) {
        HWND h = Ctl(id);
        if (!h) return;
        SendMessageW(h, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < count; ++i)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
        SendMessageW(h, CB_SETCURSEL, selection, 0);
    };

    if (host_) {
        const Settings& s = host_->HostSettings();

        const wchar_t* patternItems[] = {
            T(Str::TrayPatternOff), T(Str::TrayPatternOverDesktop), T(Str::TrayPatternOnBlack)
        };
        fillCombo(CTL_PATTERN, patternItems, 3, std::clamp(s.patternMode, 0, 2));

        const wchar_t* qualityItems[] = {
            T(Str::EdQualityBilinear), T(Str::EdQualityBicubic), T(Str::EdQualitySharp)
        };
#ifdef CRTB_XP
        fillCombo(CTL_QUALITY, qualityItems, 3, 0);
#else
        fillCombo(CTL_QUALITY, qualityItems, 3, std::clamp(s.quality, 0, 2));
#endif

        const wchar_t* langItems[] = {
            LanguageDisplayName(Lang::English), LanguageDisplayName(Lang::Hungarian)
        };
        fillCombo(CTL_LANG, langItems, 2, s.language == Lang::Hungarian ? 1 : 0);

        // Monitors, with the mode spelled out so two identical tubes can be told
        // apart.
        HWND combo = Ctl(CTL_MONITOR);
        if (combo) {
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            for (const MonitorInfo& monitor : host_->Monitors()) {
                wchar_t entry[256];
                swprintf(entry, std::size(entry), L"%ls  (%d x %d @ %d Hz)",
                         monitor.friendlyName.c_str(),
                         monitor.width, monitor.height, monitor.refresh);
                SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry));
            }
            SendMessageW(combo, CB_SETCURSEL, host_->SelectedMonitor(), 0);
        }

        // Grid sizes carry a translated "recommended" note on the default entry.
        std::wstring gridLabels[std::size(kGridSizes)];
        const wchar_t* gridPtrs[std::size(kGridSizes)];
        for (size_t i = 0; i < std::size(kGridSizes); ++i) {
            wchar_t buffer[64];
            if (kGridSizes[i] == WarpMesh::kDefaultSize)
                swprintf(buffer, std::size(buffer), L"%d x %d  (%ls)",
                         kGridSizes[i], kGridSizes[i], T(Str::EdGridRecommended));
            else
                swprintf(buffer, std::size(buffer), L"%d x %d", kGridSizes[i], kGridSizes[i]);
            gridLabels[i] = buffer;
            gridPtrs[i]   = gridLabels[i].c_str();
        }
        fillCombo(CTL_GRID, gridPtrs, static_cast<int>(std::size(kGridSizes)),
                  GridIndexFor(host_->ActiveProfile().mesh.Size()));
    }

    suppressSync_ = false;
    RefreshValueLabels();
}

void EditorWindow::RefreshValueLabels() {
    if (!hwnd_ || !host_) return;

    const Settings&    s       = host_->HostSettings();
    const Profile&     p       = host_->ActiveProfile();
    const MonitorInfo& monitor = host_->ActiveMonitor();

    const float widthPx  = monitor.width  > 0 ? static_cast<float>(monitor.width)  : 1600.0f;
    const float heightPx = monitor.height > 0 ? static_cast<float>(monitor.height) : 1200.0f;

    wchar_t buf[320];
    wchar_t value[64];

    swprintf(buf, std::size(buf), T(Str::EdGainLabel), s.previewGain);
    SetTextOn(Ctl(CTL_LBL_GAIN), buf);
    swprintf(buf, std::size(buf), T(Str::EdOverscanLabel), p.overscan * 100.0f);
    SetTextOn(Ctl(CTL_LBL_OVERSCAN), buf);
    swprintf(buf, std::size(buf), T(Str::EdBleedLabel), p.edgeBleedPx);
    SetTextOn(Ctl(CTL_LBL_BLEED), buf);

    for (int i = 0; i < kGeoCount; ++i) {
        const GeoSlider& spec = kGeoSliders[i];
        const float normalized = p.geometry.*(spec.member);
        if (spec.degrees) {
            swprintf(value, std::size(value), T(Str::ValueDegrees),
                     normalized * 180.0f / 3.14159265f);
        } else {
            swprintf(value, std::size(value), T(Str::ValuePixels),
                     normalized * (spec.horizontal ? widthPx : heightPx));
        }
        swprintf(buf, std::size(buf), L"%ls:  %ls", T(spec.label), value);
        SetTextOn(Ctl(kGeoLabelBase + i), buf);
    }

    for (int i = 0; i < kConvCount; ++i) {
        const ConvSlider& spec = kConvSliders[i];
        const float normalized = p.convergence.*(spec.member);
        swprintf(value, std::size(value), T(Str::ValuePixels),
                 normalized * (spec.horizontal ? widthPx : heightPx));
        swprintf(buf, std::size(buf), L"%ls:  %ls", T(spec.label), value);
        SetTextOn(Ctl(kConvLabelBase + i), buf);
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void EditorWindow::LayoutControls() {
    if (!hwnd_) return;

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int pad    = Scale(12);
    const int panelW = Scale(340);
    const int rowH   = Scale(23);
    const int comboH = Scale(220);   // dropdown height, not control height
    const int labelH = Scale(17);
    const int gapS   = Scale(4);
    const int gapM   = Scale(11);
    const int panelX = static_cast<int>(client.right) - panelW - pad;

    int y = pad;
    auto place = [&](int id, int height) {
        HWND h = hwnd_ ? GetDlgItem(hwnd_, id) : nullptr;
        if (!h) return;
        MoveWindow(h, panelX, y, panelW, height, TRUE);
        ShowWindow(h, SW_SHOW);
    };

    // --- always visible, above the tabs
    place(CTL_LBL_MONITOR, labelH);   y += labelH + gapS;
    place(CTL_MONITOR, comboH);       y += rowH + gapM;
    place(CTL_ENABLED, rowH);         y += rowH + gapM;
    place(CTL_LBL_PATTERN, labelH);   y += labelH + gapS;
    place(CTL_PATTERN, comboH);       y += rowH + gapM;

    const int tabsBottom = std::max(y + Scale(80), static_cast<int>(client.bottom) - pad);
    if (tabs_) {
        MoveWindow(tabs_, panelX, y, panelW, tabsBottom - y, TRUE);
        ShowWindow(tabs_, SW_SHOW);
    }

    // TCM_ADJUSTRECT expects tab-client coordinates. The old code passed editor
    // coordinates and then overlapped two sibling windows, which is what made
    // the page disappear. Keep the host inside the tab in its native coordinate
    // system; only map the rectangle for the no-host fallback.
    RECT display{};
    if (tabs_) {
        GetClientRect(tabs_, &display);
        SendMessageW(tabs_, TCM_ADJUSTRECT, FALSE, reinterpret_cast<LPARAM>(&display));
    } else {
        display = RECT{ panelX, y, panelX + panelW, tabsBottom };
    }

    if (pageHost_) {
        MoveWindow(pageHost_, display.left, display.top,
                   std::max(Scale(40), static_cast<int>(display.right - display.left)),
                   std::max(Scale(40), static_cast<int>(display.bottom - display.top)), TRUE);
        ShowWindow(pageHost_, SW_SHOW);
    }

    pageRect_ = display;
    if (tabs_ && !pageHost_) {
        MapWindowPoints(tabs_, hwnd_, reinterpret_cast<POINT*>(&pageRect_), 2);
    }

    // Measure first so the scroll position can be clamped before anything is
    // moved; otherwise a short page would keep a stale offset.
    const int contentHeight = LayoutPage(true);
    UpdatePageScroll(contentHeight);
    LayoutPage(false);

    // --- left column: canvas, status readout, help strip
    const RECT canvas = CanvasRect(client);
    HWND status = GetDlgItem(hwnd_, CTL_STATUS);
    if (status) {
        MoveWindow(status, pad, canvas.bottom + gapM,
                   canvas.right - canvas.left, Scale(100), TRUE);
        ShowWindow(status, SW_SHOW);
    }
    HWND help = GetDlgItem(hwnd_, CTL_HELP);
    if (help) {
        MoveWindow(help, pad, static_cast<int>(client.bottom) - pad - Scale(30),
                   static_cast<int>(client.right) - panelW - 3 * pad, Scale(30), TRUE);
        ShowWindow(help, SW_SHOW);
    }
}

int EditorWindow::LayoutPage(bool measureOnly) {
    HWND parent = PageParent();
    if (!parent) return 0;

    const RECT host = PageArea();
    // Without the scrolling host the controls sit directly in the window, so
    // they need the page area's own offset.
    const int originX = pageHost_ ? 0 : static_cast<int>(host.left);
    const int originY = pageHost_ ? 0 : static_cast<int>(host.top);
    const int hostW   = static_cast<int>(host.right - host.left);

    const int pad    = Scale(8);
    const int rowH   = Scale(23);
    const int comboH = Scale(220);
    const int labelH = Scale(17);
    const int barH   = Scale(24);
    const int gapS   = Scale(4);
    const int gapM   = Scale(11);

    const int pageW = std::max(Scale(40), hostW - 2 * pad);
    const int btnW  = (pageW - gapS) / 2;

    if (!measureOnly) {
        ForEachPageControl([&](int id) {
            if (HWND h = GetDlgItem(parent, id)) ShowWindow(h, SW_HIDE);
        });
    }

    // y is measured from the top of the content, independent of the scroll; the
    // scroll offset is applied only when actually moving a control.
    int y = pad;
    auto put = [&](int id, int height, int width = -1, int xOffset = 0) {
        if (measureOnly) return;
        HWND h = GetDlgItem(parent, id);
        if (!h) return;
        MoveWindow(h, originX + pad + xOffset, originY + y - scrollPos_,
                   width < 0 ? pageW : width, height, TRUE);
        ShowWindow(h, SW_SHOW);
    };

    switch (page_) {
    case kPageGrid:
        put(CTL_LBL_GRID, labelH);   y += labelH + gapS;
        put(CTL_GRID, comboH);       y += rowH + gapM;
        put(CTL_MIRROR, rowH);       y += rowH + gapS;
        put(CTL_FREEMOVE, rowH);     y += rowH + gapM;
        put(CTL_LBL_GAIN, labelH);   y += labelH + gapS;
        put(CTL_GAIN, barH);         y += barH + gapM;
        put(CTL_UNDO,     rowH + Scale(4), btnW, 0);
        put(CTL_RESETROW, rowH + Scale(4), btnW, btnW + gapS);
        y += rowH + Scale(4) + gapS;
        put(CTL_RESETALL, rowH + Scale(4), btnW, 0);
        y += rowH + Scale(4);
        break;

    case kPageGeometry:
        put(CTL_GEO_HINT, Scale(58));  y += Scale(58) + gapM;
        for (int i = 0; i < kGeoCount; ++i) {
            put(kGeoLabelBase + i, labelH);  y += labelH;
            put(kGeoSliderBase + i, barH);   y += barH + gapS;
        }
        y += gapS;
        put(CTL_GEO_RESET, rowH + Scale(4));
        y += rowH + Scale(4);
        break;

    case kPageConvergence:
        put(CTL_CONV_HINT, Scale(72)); y += Scale(72) + gapM;
        for (int i = 0; i < kConvCount; ++i) {
            put(kConvLabelBase + i, labelH);  y += labelH;
            put(kConvSliderBase + i, barH);   y += barH + gapS;
        }
        y += gapS;
        put(CTL_CONV_RESET, rowH + Scale(4));
        y += rowH + Scale(4);
        break;

    case kPageImage:
        put(CTL_LBL_QUALITY, labelH);   y += labelH + gapS;
        put(CTL_QUALITY, comboH);       y += rowH + gapM;
        put(CTL_LBL_OVERSCAN, labelH);  y += labelH + gapS;
        put(CTL_OVERSCAN, barH);        y += barH + gapM;
        put(CTL_AUTOBLEED, rowH);       y += rowH + gapS;
        put(CTL_LBL_BLEED, labelH);     y += labelH + gapS;
        put(CTL_BLEED, barH);           y += barH;
        break;

    case kPageProgram:
        put(CTL_LBL_LANG, labelH);     y += labelH + gapS;
        put(CTL_LANG, comboH);         y += rowH + gapM;
        put(CTL_AUTOSTART, rowH);      y += rowH + gapS;
        put(CTL_AUTOBYPASS, rowH * 2); y += rowH * 2 + gapM;
        put(CTL_SAVE,   rowH + Scale(4), btnW, 0);
        put(CTL_GITHUB, rowH + Scale(4), btnW, btnW + gapS);
        y += rowH + Scale(4) + gapM;
        put(CTL_CREDIT, labelH);
        y += labelH;
        break;

    default:
        break;
    }

    return y + pad;
}

void EditorWindow::UpdatePageScroll(int contentHeight) {
    if (!pageHost_) { scrollPos_ = 0; return; }   // no host, no scrolling

    const RECT host = PageArea();
    const int pageHeight = std::max(1, static_cast<int>(host.bottom - host.top));
    const int maxScroll  = std::max(0, contentHeight - pageHeight);

    scrollPos_ = std::clamp(scrollPos_, 0, maxScroll);

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin   = 0;
    si.nMax   = std::max(0, contentHeight - 1);
    si.nPage  = static_cast<UINT>(pageHeight);
    si.nPos   = scrollPos_;
    SetScrollInfo(pageHost_, SB_VERT, &si, TRUE);
}

void EditorWindow::ScrollPageBy(int pixels) {
    if (!pageHost_) return;
    const int before = scrollPos_;
    scrollPos_ += pixels;

    const int contentHeight = LayoutPage(true);
    UpdatePageScroll(contentHeight);      // clamps scrollPos_
    if (scrollPos_ == before) return;

    LayoutPage(false);
    InvalidateRect(pageHost_, nullptr, TRUE);
}

void EditorWindow::OnPageScroll(WPARAM request) {
    if (!pageHost_) return;

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_ALL;
    GetScrollInfo(pageHost_, SB_VERT, &si);

    int target = scrollPos_;
    switch (LOWORD(request)) {
    case SB_TOP:           target = 0; break;
    case SB_BOTTOM:        target = si.nMax; break;
    case SB_LINEUP:        target -= Scale(kScrollLine); break;
    case SB_LINEDOWN:      target += Scale(kScrollLine); break;
    case SB_PAGEUP:        target -= static_cast<int>(si.nPage); break;
    case SB_PAGEDOWN:      target += static_cast<int>(si.nPage); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: target = si.nTrackPos; break;
    default: return;
    }

    ScrollPageBy(target - scrollPos_);
}

// ---------------------------------------------------------------------------
// Model <-> controls
// ---------------------------------------------------------------------------

void EditorWindow::SyncControlsFromModel() {
    if (!hwnd_ || !host_) return;
    suppressSync_ = true;

    const Settings& s = host_->HostSettings();
    const Profile&  p = host_->ActiveProfile();

    SendMessageW(Ctl(CTL_ENABLED), BM_SETCHECK, s.enabled    ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Ctl(CTL_MIRROR), BM_SETCHECK, s.mirror     ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Ctl(CTL_FREEMOVE), BM_SETCHECK, s.freeMove   ? BST_CHECKED : BST_UNCHECKED, 0);
#ifdef CRTB_XP
    SendMessageW(Ctl(CTL_AUTOBYPASS), BM_SETCHECK, BST_UNCHECKED, 0);
#else
    SendMessageW(Ctl(CTL_AUTOBYPASS), BM_SETCHECK, s.autoBypass ? BST_CHECKED : BST_UNCHECKED, 0);
#endif
    SendMessageW(Ctl(CTL_AUTOBLEED), BM_SETCHECK, p.autoBleed  ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Ctl(CTL_AUTOSTART), BM_SETCHECK, host_->GetAutostart() ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageW(Ctl(CTL_MONITOR), CB_SETCURSEL, host_->SelectedMonitor(), 0);
    SendMessageW(Ctl(CTL_PATTERN), CB_SETCURSEL, std::clamp(s.patternMode, 0, 2), 0);
#ifdef CRTB_XP
    SendMessageW(Ctl(CTL_QUALITY), CB_SETCURSEL, 0, 0);
#else
    SendMessageW(Ctl(CTL_QUALITY), CB_SETCURSEL, std::clamp(s.quality, 0, 2), 0);
#endif
    SendMessageW(Ctl(CTL_LANG), CB_SETCURSEL,
                        s.language == Lang::Hungarian ? 1 : 0, 0);
    SendMessageW(Ctl(CTL_GRID), CB_SETCURSEL, GridIndexFor(p.mesh.Size()), 0);

    SendMessageW(Ctl(CTL_GAIN), TBM_SETPOS, TRUE, s.previewGain);
    SendMessageW(Ctl(CTL_OVERSCAN), TBM_SETPOS, TRUE,
                        static_cast<LPARAM>(std::lround(p.overscan * 100.0f)));
    SendMessageW(Ctl(CTL_BLEED), TBM_SETPOS, TRUE,
                        static_cast<LPARAM>(std::lround(p.edgeBleedPx)));
    EnableWindow(Ctl(CTL_BLEED), !p.autoBleed);

    for (int i = 0; i < kGeoCount; ++i) {
        const GeoSlider& spec = kGeoSliders[i];
        const int pos = static_cast<int>(std::lround(
            p.geometry.*(spec.member) / spec.range * kSliderRange));
        SendMessageW(Ctl(kGeoSliderBase + i), TBM_SETPOS, TRUE,
                            std::clamp(pos, -kSliderRange, kSliderRange));
    }
    for (int i = 0; i < kConvCount; ++i) {
        const ConvSlider& spec = kConvSliders[i];
        const int pos = static_cast<int>(std::lround(
            p.convergence.*(spec.member) / spec.range * kSliderRange));
        SendMessageW(Ctl(kConvSliderBase + i), TBM_SETPOS, TRUE,
                            std::clamp(pos, -kSliderRange, kSliderRange));
    }

    suppressSync_ = false;
    RefreshValueLabels();
    UpdateStatusText();
}

void EditorWindow::ApplyControlsToModel(int controlId) {
    if (!hwnd_ || !host_ || suppressSync_) return;

    Settings& s = host_->HostSettings();
    Profile&  p = host_->ActiveProfile();
    bool persist  = true;
    bool relayout = false;

    // Parametric sliders first: they are the bulk of the controls.
    if (controlId >= kGeoSliderBase && controlId < kGeoSliderBase + kGeoCount) {
        const GeoSlider& spec = kGeoSliders[controlId - kGeoSliderBase];
        const int pos = static_cast<int>(
            SendMessageW(Ctl(controlId), TBM_GETPOS, 0, 0));
        p.geometry.*(spec.member) = static_cast<float>(pos) / kSliderRange * spec.range;
        persist = false;   // written when the thumb is released
    } else if (controlId >= kConvSliderBase && controlId < kConvSliderBase + kConvCount) {
        const ConvSlider& spec = kConvSliders[controlId - kConvSliderBase];
        const int pos = static_cast<int>(
            SendMessageW(Ctl(controlId), TBM_GETPOS, 0, 0));
        p.convergence.*(spec.member) = static_cast<float>(pos) / kSliderRange * spec.range;
        persist = false;
    } else {
        switch (controlId) {
        case CTL_ENABLED:
            s.enabled = SendMessageW(Ctl(CTL_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        case CTL_MIRROR:
            s.mirror = SendMessageW(Ctl(CTL_MIRROR), BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        case CTL_FREEMOVE:
            s.freeMove = SendMessageW(Ctl(CTL_FREEMOVE), BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        case CTL_AUTOBYPASS:
            s.autoBypass = SendMessageW(Ctl(CTL_AUTOBYPASS), BM_GETCHECK, 0, 0) == BST_CHECKED;
            break;
        case CTL_AUTOSTART:
            host_->SetAutostart(SendMessageW(Ctl(CTL_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case CTL_AUTOBLEED:
            p.autoBleed = SendMessageW(Ctl(CTL_AUTOBLEED), BM_GETCHECK, 0, 0) == BST_CHECKED;
            EnableWindow(Ctl(CTL_BLEED), !p.autoBleed);
            break;
        case CTL_MONITOR: {
            const int index = static_cast<int>(
                SendMessageW(Ctl(CTL_MONITOR), CB_GETCURSEL, 0, 0));
            if (index >= 0 && index != host_->SelectedMonitor()) {
                host_->SelectMonitor(index);
                selRow_ = selCol_ = -1;
                undo_.clear();
                SyncControlsFromModel();
                InvalidateRect(hwnd_, nullptr, TRUE);
            }
            return;
        }
        case CTL_PATTERN:
            s.patternMode = std::clamp(static_cast<int>(
                SendMessageW(Ctl(CTL_PATTERN), CB_GETCURSEL, 0, 0)), 0, 2);
            break;
        case CTL_QUALITY:
            s.quality = std::clamp(static_cast<int>(
                SendMessageW(Ctl(CTL_QUALITY), CB_GETCURSEL, 0, 0)), 0, 2);
            break;
        case CTL_LANG: {
            const int index = static_cast<int>(
                SendMessageW(Ctl(CTL_LANG), CB_GETCURSEL, 0, 0));
            const Lang next = index == 1 ? Lang::Hungarian : Lang::English;
            if (next != s.language) {
                s.language = next;
                SetLanguage(next);
                RelabelControls();      // no need to recreate the window
                relayout = true;
            }
            break;
        }
        case CTL_GRID: {
            const int index = static_cast<int>(
                SendMessageW(Ctl(CTL_GRID), CB_GETCURSEL, 0, 0));
            if (index >= 0 && index < static_cast<int>(std::size(kGridSizes)) &&
                kGridSizes[index] != p.mesh.Size()) {
                PushUndo();
                p.mesh.Resize(kGridSizes[index]);
                selRow_ = selCol_ = -1;
            }
            break;
        }
        case CTL_GAIN:
            s.previewGain = static_cast<int>(
                SendMessageW(Ctl(CTL_GAIN), TBM_GETPOS, 0, 0));
            persist = false;   // preview only, saved with the next real edit
            break;
        case CTL_OVERSCAN:
            p.overscan = static_cast<float>(
                SendMessageW(Ctl(CTL_OVERSCAN), TBM_GETPOS, 0, 0)) / 100.0f;
            persist = false;
            break;
        case CTL_BLEED:
            p.edgeBleedPx = static_cast<float>(
                SendMessageW(Ctl(CTL_BLEED), TBM_GETPOS, 0, 0));
            persist = false;
            break;
        default:
            return;
        }
    }

    suppressSync_ = true;
    RefreshValueLabels();
    suppressSync_ = false;

    host_->OnEditorChanged(persist);
    if (relayout) LayoutControls();
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorWindow::ResetSliderToDefault(int id) {
    if (!hwnd_ || !host_) return;

    // Defaults come from a freshly constructed profile / settings block, so a
    // changed default cannot drift away from what this restores.
    static const Profile  kDefaultProfile;
    static const Settings kDefaultSettings;

    Settings& s = host_->HostSettings();
    Profile&  p = host_->ActiveProfile();

    if (id >= kGeoSliderBase && id < kGeoSliderBase + kGeoCount) {
        const GeoSlider& spec = kGeoSliders[id - kGeoSliderBase];
        p.geometry.*(spec.member) = kDefaultProfile.geometry.*(spec.member);
    } else if (id >= kConvSliderBase && id < kConvSliderBase + kConvCount) {
        const ConvSlider& spec = kConvSliders[id - kConvSliderBase];
        p.convergence.*(spec.member) = kDefaultProfile.convergence.*(spec.member);
    } else if (id == CTL_GAIN) {
        s.previewGain = kDefaultSettings.previewGain;
    } else if (id == CTL_OVERSCAN) {
        p.overscan = kDefaultProfile.overscan;
    } else if (id == CTL_BLEED) {
        p.edgeBleedPx = kDefaultProfile.edgeBleedPx;
    } else {
        return;
    }

    host_->OnEditorChanged(true);
    SyncControlsFromModel();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorWindow::UpdateStatusText() {
    if (!hwnd_ || !host_) return;

    const MonitorInfo& monitor = host_->ActiveMonitor();
    const Profile&     p       = host_->ActiveProfile();

    std::wstring text;
    wchar_t line[512];

    swprintf(line, std::size(line), T(Str::StatusMonitor), monitor.friendlyName.c_str());
    text += line;

    const std::wstring profileKey = Widen(monitor.ProfileKey());
    swprintf(line, std::size(line), T(Str::StatusProfile),
             profileKey.c_str(), p.mesh.Size(), p.mesh.Size());
    text += line;

    if (selRow_ >= 0 && selCol_ >= 0) {
        const Offset& o = p.mesh.At(selRow_, selCol_);
        swprintf(line, std::size(line), T(Str::StatusSelected),
                 selRow_, selCol_,
                 p.mesh.RowLocked(selRow_) ? T(Str::StatusRowLocked) : L"",
                 o.dy * static_cast<float>(monitor.height),
                 o.dx * static_cast<float>(monitor.width));
    } else {
        swprintf(line, std::size(line), L"%ls", T(Str::StatusNoSelection));
    }
    text += line;

    if (p.overscan > 1.001f)
        text += std::wstring(T(Str::EdOverscanHint)) + L"\r\n\r\n";

    const std::wstring err = host_->EngineStatus();
    if (!err.empty()) text += std::wstring(T(Str::StatusWarning)) + err + L"\r\n\r\n";

    swprintf(line, std::size(line), T(Str::StatusUndoSteps), static_cast<int>(undo_.size()));
    text += line;

    SetTextOn(Ctl(CTL_STATUS), text.c_str());
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

RECT EditorWindow::CanvasRect(const RECT& client) const {
    const int pad    = Scale(12);
    const int panelW = Scale(340);
    RECT r{ pad, pad,
            static_cast<LONG>(client.right) - panelW - 2 * pad,
            static_cast<LONG>(client.bottom) - pad - Scale(152) };
    if (r.right < r.left + 50) r.right = r.left + 50;
    if (r.bottom < r.top + 50) r.bottom = r.top + 50;
    return r;
}

RECT EditorWindow::ScreenAreaRect(const RECT& client) const {
    RECT canvas = CanvasRect(client);
    // Leave room on the left for the row lock glyphs.
    canvas.left   += Scale(34);
    canvas.right  -= Scale(16);
    canvas.top    += Scale(16);
    canvas.bottom -= Scale(16);

    const float aspect = ScreenAspect();
    const int availW = static_cast<int>(canvas.right - canvas.left);
    const int availH = static_cast<int>(canvas.bottom - canvas.top);

    int w = availW;
    int h = static_cast<int>(std::lround(static_cast<float>(w) / aspect));
    if (h > availH) {
        h = availH;
        w = static_cast<int>(std::lround(static_cast<float>(h) * aspect));
    }

    RECT area;
    area.left   = canvas.left + (availW - w) / 2;
    area.top    = canvas.top + (availH - h) / 2;
    area.right  = area.left + w;
    area.bottom = area.top + h;
    return area;
}

Offset EditorWindow::CombinedAt(int row, int col) const {
    if (!host_) return {};
    const Profile& p = host_->ActiveProfile();
    const int n = p.mesh.Size();
    if (n < 2) return p.mesh.At(row, col);

    const float u = static_cast<float>(col) / static_cast<float>(n - 1);
    const float v = static_cast<float>(row) / static_cast<float>(n - 1);

    Offset o = p.mesh.At(row, col);
    const Offset g = p.geometry.At(u, v, ScreenAspect());
    o.dx += g.dx;
    o.dy += g.dy;
    return o;
}

void EditorWindow::PointToCanvas(int row, int col, const RECT& area, int& x, int& y) const {
    const Profile& p    = host_->ActiveProfile();
    const int      n    = p.mesh.Size();
    const float    gain = static_cast<float>(host_->HostSettings().previewGain);
    const Offset   o    = CombinedAt(row, col);

    const float u = static_cast<float>(col) / static_cast<float>(n - 1);
    const float v = static_cast<float>(row) / static_cast<float>(n - 1);
    const float w = static_cast<float>(area.right - area.left);
    const float h = static_cast<float>(area.bottom - area.top);

    x = area.left + static_cast<int>(std::lround((u + o.dx * gain) * w));
    y = area.top  + static_cast<int>(std::lround((v + o.dy * gain) * h));
}

float EditorWindow::LatticeSpacing(const RECT& area) const {
    const int n = host_ ? host_->ActiveProfile().mesh.Size() : 5;
    if (n <= 1) return 1.0f;
    const float w = static_cast<float>(area.right - area.left);
    const float h = static_cast<float>(area.bottom - area.top);
    return std::min(w, h) / static_cast<float>(n - 1);
}

int EditorWindow::HandleRadius(const RECT& area) const {
    return std::clamp(static_cast<int>(LatticeSpacing(area) / 6.0f), Scale(2), Scale(5));
}

int EditorWindow::PickRadius(const RECT& area) const {
    // Never more than half the spacing, or neighbouring points would compete.
    return std::clamp(static_cast<int>(LatticeSpacing(area) * 0.45f), Scale(4), Scale(11));
}

int EditorWindow::LockGlyphSize(const RECT& area) const {
    return std::clamp(static_cast<int>(LatticeSpacing(area) * 0.42f), Scale(7), Scale(14));
}

void EditorWindow::PaintCanvas(HDC dc, const RECT& client) {
    const RECT canvas = CanvasRect(client);
    const RECT area   = ScreenAreaRect(client);

    HBRUSH bgBrush = CreateSolidBrush(RGB(18, 20, 24));
    FillRect(dc, &canvas, bgBrush);
    DeleteObject(bgBrush);

    if (!host_) return;

    const Profile&  p      = host_->ActiveProfile();
    const WarpMesh& mesh   = p.mesh;
    const int       n      = mesh.Size();
    const float     gain   = static_cast<float>(host_->HostSettings().previewGain);
    const float     aw     = static_cast<float>(area.right - area.left);
    const float     ah     = static_cast<float>(area.bottom - area.top);
    const float     aspect = ScreenAspect();

    HBRUSH screenBrush = CreateSolidBrush(RGB(30, 34, 40));
    FillRect(dc, &area, screenBrush);
    DeleteObject(screenBrush);

    SetBkMode(dc, TRANSPARENT);

    // Undeformed reference lattice, so the deviation is readable at a glance.
    HPEN refPen = CreatePen(PS_DOT, 1, RGB(48, 53, 62));
    HGDIOBJ oldPen = SelectObject(dc, refPen);
    for (int i = 0; i < n; ++i) {
        const int gx = area.left + static_cast<int>(std::lround(
            static_cast<float>(i) / (n - 1) * aw));
        const int gy = area.top + static_cast<int>(std::lround(
            static_cast<float>(i) / (n - 1) * ah));
        MoveToEx(dc, area.left, gy, nullptr); LineTo(dc, area.right, gy);
        MoveToEx(dc, gx, area.top, nullptr);  LineTo(dc, gx, area.bottom);
    }
    SelectObject(dc, oldPen);
    DeleteObject(refPen);

    // Deformed lattice: the parametric layer and the hand-tuned points together,
    // because that sum is what actually reaches the screen.
    const WarpSampler sampler(mesh);
    auto sampleCombined = [&](float u, float v, float& outX, float& outY) {
        const Offset m = sampler.At(u, v);
        const Offset g = p.geometry.At(u, v, aspect);
        outX = area.left + (u + (m.dx + g.dx) * gain) * aw;
        outY = area.top  + (v + (m.dy + g.dy) * gain) * ah;
    };

    HPEN curvePen = CreatePen(PS_SOLID, Scale(2), RGB(86, 208, 130));
    oldPen = SelectObject(dc, curvePen);

    const int steps = CurveSteps(n);
    std::vector<POINT> pts(static_cast<size_t>(steps) + 1);
    for (int r = 0; r < n; ++r) {
        const float v = static_cast<float>(r) / static_cast<float>(n - 1);
        for (int st = 0; st <= steps; ++st) {
            float px, py;
            sampleCombined(static_cast<float>(st) / static_cast<float>(steps), v, px, py);
            pts[st].x = static_cast<LONG>(std::lround(px));
            pts[st].y = static_cast<LONG>(std::lround(py));
        }
        Polyline(dc, pts.data(), static_cast<int>(pts.size()));
    }
    for (int c = 0; c < n; ++c) {
        const float u = static_cast<float>(c) / static_cast<float>(n - 1);
        for (int st = 0; st <= steps; ++st) {
            float px, py;
            sampleCombined(u, static_cast<float>(st) / static_cast<float>(steps), px, py);
            pts[st].x = static_cast<LONG>(std::lround(px));
            pts[st].y = static_cast<LONG>(std::lround(py));
        }
        Polyline(dc, pts.data(), static_cast<int>(pts.size()));
    }
    SelectObject(dc, oldPen);
    DeleteObject(curvePen);

    // Screen outline.
    HPEN edgePen = CreatePen(PS_SOLID, 1, RGB(190, 90, 90));
    oldPen = SelectObject(dc, edgePen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, area.left, area.top, area.right, area.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(edgePen);

    // Row lock glyphs down the left margin.
    const int lockSize = LockGlyphSize(area);
    for (int r = 0; r < n; ++r) {
        const int cy = area.top + static_cast<int>(std::lround(
            static_cast<float>(r) / (n - 1) * ah));
        RECT box{ area.left - Scale(28), cy - lockSize / 2,
                  area.left - Scale(28) + lockSize, cy + lockSize / 2 };
        HBRUSH b = CreateSolidBrush(mesh.RowLocked(r) ? RGB(220, 160, 60) : RGB(52, 58, 68));
        FillRect(dc, &box, b);
        DeleteObject(b);
        FrameRect(dc, &box, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
    }

    // Control point handles.
    const int handle = HandleRadius(area);
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            int x, y;
            PointToCanvas(r, c, area, x, y);

            const bool selected = (r == selRow_ && c == selCol_);
            const bool locked   = mesh.RowLocked(r);
            COLORREF fill = selected ? RGB(255, 190, 60)
                                     : (locked ? RGB(120, 126, 136) : RGB(235, 238, 244));

            HBRUSH b   = CreateSolidBrush(fill);
            HPEN   pen = CreatePen(PS_SOLID, 1, RGB(20, 22, 26));
            HGDIOBJ ob = SelectObject(dc, b);
            HGDIOBJ op = SelectObject(dc, pen);
            const int rad = selected ? handle + Scale(2) : handle;
            Ellipse(dc, x - rad, y - rad, x + rad, y + rad);
            SelectObject(dc, ob);
            SelectObject(dc, op);
            DeleteObject(b);
            DeleteObject(pen);
        }
    }
    SelectObject(dc, oldBrush);

    // Orientation labels: which end of the canvas is the top of the screen.
    SetTextColor(dc, RGB(150, 158, 172));
    HGDIOBJ oldFont = SelectObject(dc, font_ ? font_ : GetStockObject(DEFAULT_GUI_FONT));
    RECT top{ area.left, canvas.top + Scale(1), area.right, area.top };
    DrawTextW(dc, T(Str::EdScreenTop), -1, &top, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    RECT bottom{ area.left, area.bottom, area.right, canvas.bottom - Scale(1) };
    DrawTextW(dc, T(Str::EdScreenBottom), -1, &bottom, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, oldFont);
}

int EditorWindow::HitTestPoint(int mx, int my, const RECT& area) const {
    if (!host_) return -1;
    const int n = host_->ActiveProfile().mesh.Size();
    const int radius = PickRadius(area);

    int best = -1;
    int bestDist = radius * radius + 1;
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            int x, y;
            PointToCanvas(r, c, area, x, y);
            const int dx = mx - x, dy = my - y;
            const int d  = dx * dx + dy * dy;
            if (d < bestDist) { bestDist = d; best = r * n + c; }
        }
    }
    return best;
}

int EditorWindow::HitTestRowLock(int mx, int my, const RECT& area) const {
    if (!host_) return -1;
    const int n  = host_->ActiveProfile().mesh.Size();
    const int ah = static_cast<int>(area.bottom - area.top);
    const int lockSize = LockGlyphSize(area);

    for (int r = 0; r < n; ++r) {
        const int cy = area.top + static_cast<int>(std::lround(
            static_cast<float>(r) / (n - 1) * static_cast<float>(ah)));
        RECT box{ area.left - Scale(28), cy - lockSize / 2,
                  area.left - Scale(28) + lockSize, cy + lockSize / 2 };
        POINT pt{ mx, my };
        if (PtInRect(&box, pt)) return r;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void EditorWindow::PushUndo() {
    if (!host_) return;
    undo_.push_back(host_->ActiveProfile().mesh);
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
}

void EditorWindow::Undo() {
    if (!host_ || undo_.empty()) return;
    host_->ActiveProfile().mesh = undo_.back();
    undo_.pop_back();
    selRow_ = selCol_ = -1;
    host_->OnEditorChanged(true);
    SyncControlsFromModel();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorWindow::MoveControlPoint(int row, int col, float dxNorm, float dyNorm, bool additive) {
    if (!host_) return;
    WarpMesh& mesh = host_->ActiveProfile().mesh;
    if (row < 0 || row >= mesh.Size() || col < 0 || col >= mesh.Size()) return;
    if (mesh.RowLocked(row)) return;

    const bool freeMove = host_->HostSettings().freeMove;
    Offset& o = mesh.At(row, col);
    if (additive) {
        o.dy += dyNorm;
        if (freeMove) o.dx += dxNorm;
    } else {
        o.dy = dyNorm;
        if (freeMove) o.dx = dxNorm;
    }
    o.dy = std::clamp(o.dy, -kMaxOffset, kMaxOffset);
    o.dx = std::clamp(o.dx, -kMaxOffset, kMaxOffset);

    // CRT geometry is almost always symmetric about the vertical axis, so
    // mirroring halves the work and keeps the two sides consistent.
    if (host_->HostSettings().mirror) {
        const int mirrorCol = mesh.Size() - 1 - col;
        if (mirrorCol != col) {
            Offset& m = mesh.At(row, mirrorCol);
            m.dy = o.dy;
            m.dx = -o.dx;
        }
    }
}

void EditorWindow::NudgeSelection(float dxPixels, float dyPixels) {
    if (!host_ || selRow_ < 0 || selCol_ < 0) return;
    const MonitorInfo& monitor = host_->ActiveMonitor();
    if (!monitor.Valid()) return;

    PushUndo();
    MoveControlPoint(selRow_, selCol_,
                     dxPixels / static_cast<float>(monitor.width),
                     dyPixels / static_cast<float>(monitor.height), true);
    host_->OnEditorChanged(false);
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------

LRESULT EditorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        hwnd_ = hwnd;
        dpi_  = static_cast<int>(WindowDpi(hwnd));
        font_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        CreateControls(hwnd);
        EnumChildWindows(hwnd, SetChildFont, reinterpret_cast<LPARAM>(font_));
        LayoutControls();
        SyncControlsFromModel();
        return 0;
    }

    case WM_SIZE:
        LayoutControls();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = MulDiv(920, dpi_, 96);
        mmi->ptMinTrackSize.y = MulDiv(700, dpi_, 96);
        return 0;
    }

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        EnumChildWindows(hwnd, SetChildFont, reinterpret_cast<LPARAM>(font_));
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutControls();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_BTNFACE));
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));

    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(hwnd, &client);
        FillRect(reinterpret_cast<HDC>(wp), &client, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT client{};
        GetClientRect(hwnd, &client);

        // Double buffer the canvas so dragging does not flicker.
        const RECT canvas = CanvasRect(client);
        const int  w = canvas.right - canvas.left;
        const int  h = canvas.bottom - canvas.top;
        HDC     mem  = CreateCompatibleDC(dc);
        HBITMAP bmp  = CreateCompatibleBitmap(dc, client.right, client.bottom);
        HGDIOBJ old  = SelectObject(mem, bmp);
        FillRect(mem, &client, GetSysColorBrush(COLOR_BTNFACE));
        PaintCanvas(mem, client);
        BitBlt(dc, canvas.left, canvas.top, w, h, mem, canvas.left, canvas.top, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (pageHost_) {
            RECT host{};
            GetWindowRect(pageHost_, &host);
            if (PtInRect(&host, pt)) {
                const int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
                ScrollPageBy(-notches * Scale(kScrollLine) * 3);
                return 0;
            }
        }
        return 0;
    }

    case WM_NOTIFY: {
        auto* header = reinterpret_cast<LPNMHDR>(lp);
        if (header && tabs_ && header->hwndFrom == tabs_ && header->code == TCN_SELCHANGE) {
            page_ = static_cast<int>(SendMessageW(tabs_, TCM_GETCURSEL, 0, 0));
            scrollPos_ = 0;          // a new page always starts at the top
            LayoutControls();
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        SetFocus(hwnd);
        RECT client{};
        GetClientRect(hwnd, &client);
        const RECT area = ScreenAreaRect(client);
        const int mx = GET_X_LPARAM(lp);
        const int my = GET_Y_LPARAM(lp);

        const int lockRow = HitTestRowLock(mx, my, area);
        if (lockRow >= 0 && host_) {
            WarpMesh& mesh = host_->ActiveProfile().mesh;
            mesh.SetRowLocked(lockRow, !mesh.RowLocked(lockRow));
            host_->OnEditorChanged(true);
            UpdateStatusText();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        const int hit = HitTestPoint(mx, my, area);
        if (hit >= 0 && host_) {
            const int n = host_->ActiveProfile().mesh.Size();
            selRow_ = hit / n;
            selCol_ = hit % n;
            if (!host_->ActiveProfile().mesh.RowLocked(selRow_)) {
                PushUndo();
                dragging_   = true;
                dragOrigin_ = POINT{ mx, my };
                dragStart_  = host_->ActiveProfile().mesh.At(selRow_, selCol_);
                SetCapture(hwnd);
            }
        } else {
            selRow_ = selCol_ = -1;
        }
        UpdateStatusText();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        // Reset just this one point back to zero.
        if (!host_) return 0;
        RECT client{};
        GetClientRect(hwnd, &client);
        const RECT area = ScreenAreaRect(client);

        const int hit = HitTestPoint(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), area);
        if (hit < 0) return 0;

        WarpMesh& mesh = host_->ActiveProfile().mesh;
        const int n   = mesh.Size();
        const int row = hit / n;
        const int col = hit % n;
        if (mesh.RowLocked(row)) return 0;

        // A drag started on the first click of the double click, so undo the
        // half-finished drag state before recording this edit.
        if (dragging_) {
            dragging_ = false;
            ReleaseCapture();
            if (!undo_.empty()) undo_.pop_back();
        }

        PushUndo();
        selRow_ = row;
        selCol_ = col;
        mesh.At(row, col) = Offset{};
        if (host_->HostSettings().mirror) {
            const int mirrorCol = n - 1 - col;
            if (mirrorCol != col) mesh.At(row, mirrorCol) = Offset{};
        }
        host_->OnEditorChanged(true);
        UpdateStatusText();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!dragging_ || !host_) return 0;
        RECT client{};
        GetClientRect(hwnd, &client);
        const RECT area = ScreenAreaRect(client);
        const float gain = static_cast<float>(host_->HostSettings().previewGain);
        const float aw = static_cast<float>(std::max(1L, area.right - area.left));
        const float ah = static_cast<float>(std::max(1L, area.bottom - area.top));

        const float dxNorm = static_cast<float>(GET_X_LPARAM(lp) - dragOrigin_.x) / (aw * gain);
        const float dyNorm = static_cast<float>(GET_Y_LPARAM(lp) - dragOrigin_.y) / (ah * gain);

        MoveControlPoint(selRow_, selCol_,
                         dragStart_.dx + dxNorm, dragStart_.dy + dyNorm, false);
        host_->OnEditorChanged(false);
        UpdateStatusText();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
        if (dragging_) {
            dragging_ = false;
            ReleaseCapture();
            if (host_) host_->OnEditorChanged(true);   // persist the finished drag
        }
        return 0;

    case WM_KEYDOWN: {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const float step = shift ? 1.0f : (ctrl ? 0.05f : 0.25f);

        switch (wp) {
        case VK_UP:    NudgeSelection(0.0f, -step); return 0;
        case VK_DOWN:  NudgeSelection(0.0f, +step); return 0;
        case VK_LEFT:  NudgeSelection(-step, 0.0f); return 0;
        case VK_RIGHT: NudgeSelection(+step, 0.0f); return 0;
        case 'Z':
            if (ctrl) { Undo(); return 0; }
            break;
        case 'S':
            if (ctrl && host_) { host_->OnEditorChanged(true); return 0; }
            break;
        case VK_ESCAPE:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        default:
            break;
        }
        return 0;
    }

    case WM_COMMAND: {
        const int id   = LOWORD(wp);
        const int code = HIWORD(wp);

        if (code == BN_CLICKED) {
            switch (id) {
            case CTL_GITHUB:
                ShellExecuteW(hwnd, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            case CTL_UNDO:
                Undo();
                return 0;
            case CTL_RESETROW:
                if (host_ && selRow_ >= 0) {
                    PushUndo();
                    host_->ActiveProfile().mesh.ResetRow(selRow_);
                    host_->OnEditorChanged(true);
                    UpdateStatusText();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case CTL_RESETALL:
                if (host_) {
                    PushUndo();
                    host_->ActiveProfile().mesh.Reset();
                    host_->OnEditorChanged(true);
                    UpdateStatusText();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case CTL_GEO_RESET:
                if (host_) {
                    host_->ActiveProfile().geometry.Reset();
                    host_->OnEditorChanged(true);
                    SyncControlsFromModel();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            case CTL_CONV_RESET:
                if (host_) {
                    host_->ActiveProfile().convergence.Reset();
                    host_->OnEditorChanged(true);
                    SyncControlsFromModel();
                }
                return 0;
            case CTL_SAVE:
                if (host_) host_->OnEditorChanged(true);
                return 0;
            default:
                break;
            }
        }

        if (code == BN_CLICKED || code == CBN_SELCHANGE) {
            ApplyControlsToModel(id);
            if (id == CTL_GRID) SyncControlsFromModel();
            return 0;
        }
        return 0;
    }

    case WM_HSCROLL: {
        HWND bar = reinterpret_cast<HWND>(lp);
        if (!bar) break;
        ApplyControlsToModel(static_cast<int>(GetDlgCtrlID(bar)));
        // Releasing the thumb is the natural point to write the file.
        if (LOWORD(wp) == TB_ENDTRACK && host_) host_->OnEditorChanged(true);
        return 0;
    }

    case WM_CLOSE:
        if (host_) host_->OnEditorChanged(true);
        DestroyWindow(hwnd);
        return 0;

    case WM_NCDESTROY:
        if (font_) { DeleteObject(font_); font_ = nullptr; }
        hwnd_     = nullptr;
        tabs_     = nullptr;
        pageHost_ = nullptr;
        dragging_ = false;
        undo_.clear();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace crtb
