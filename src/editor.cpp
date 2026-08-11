#include "editor.h"
#include "i18n.h"
#include "util.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace crtb {
namespace {

constexpr const wchar_t* kEditorClass = L"CRTBenderEditorWindow";

enum ControlId : int {
    CTL_ENABLED = 1001,
    CTL_PATTERN,
    CTL_GRID,
    CTL_MIRROR,
    CTL_FREEMOVE,
    CTL_GAIN,
    CTL_OVERSCAN,
    CTL_AUTOBLEED,
    CTL_BLEED,
    CTL_QUALITY,
    CTL_AUTOSTART,
    CTL_LANG,
    CTL_GITHUB,
    CTL_UNDO,
    CTL_RESETROW,
    CTL_RESETALL,
    CTL_SAVE,
    CTL_LBL_PATTERN,
    CTL_LBL_GRID,
    CTL_LBL_GAIN,
    CTL_LBL_OVERSCAN,
    CTL_LBL_BLEED,
    CTL_LBL_QUALITY,
    CTL_LBL_LANG,
    CTL_STATUS,
    CTL_HELP,
    CTL_CREDIT,
};

constexpr float kMaxOffset = 0.15f;   // normalized clamp per control point
constexpr size_t kMaxUndo  = 64;

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

HWND MakeChild(HWND parent, const wchar_t* cls, const wchar_t* text,
               DWORD style, int id, HINSTANCE inst) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                           0, 0, 10, 10, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
}

void SetText(HWND parent, int id, const wchar_t* text) {
    HWND h = GetDlgItem(parent, id);
    if (h) SetWindowTextW(h, text);
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

    const int dpi = static_cast<int>(GetDpiForSystem());
    const int w   = MulDiv(1020, dpi, 96);
    const int h   = MulDiv(700, dpi, 96);
    const int x   = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    const int y   = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

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
    SyncControlsFromModel();
    InvalidateRect(hwnd_, nullptr, TRUE);
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

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void EditorWindow::CreateControls(HWND hwnd) {
    MakeChild(hwnd, L"BUTTON", L"", BS_AUTOCHECKBOX, CTL_ENABLED, inst_);

    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_LBL_PATTERN, inst_);
    MakeChild(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_PATTERN, inst_);

    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_LBL_GRID, inst_);
    MakeChild(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_GRID, inst_);

    MakeChild(hwnd, L"BUTTON", L"", BS_AUTOCHECKBOX, CTL_MIRROR, inst_);
    MakeChild(hwnd, L"BUTTON", L"", BS_AUTOCHECKBOX, CTL_FREEMOVE, inst_);

    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_LBL_GAIN, inst_);
    HWND gain = MakeChild(hwnd, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, CTL_GAIN, inst_);
    SendMessageW(gain, TBM_SETRANGE, TRUE, MAKELPARAM(1, 24));

    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_LBL_OVERSCAN, inst_);
    HWND overscan = MakeChild(hwnd, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, CTL_OVERSCAN, inst_);
    SendMessageW(overscan, TBM_SETRANGE, TRUE, MAKELPARAM(100, 115));

    MakeChild(hwnd, L"BUTTON", L"", BS_AUTOCHECKBOX, CTL_AUTOBLEED, inst_);
    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_LBL_BLEED, inst_);
    HWND bleed = MakeChild(hwnd, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, CTL_BLEED, inst_);
    SendMessageW(bleed, TBM_SETRANGE, TRUE, MAKELPARAM(0, 48));

    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_LBL_QUALITY, inst_);
    MakeChild(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_QUALITY, inst_);

    MakeChild(hwnd, L"BUTTON", L"", BS_AUTOCHECKBOX, CTL_AUTOSTART, inst_);

    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_LBL_LANG, inst_);
    MakeChild(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, CTL_LANG, inst_);

    MakeChild(hwnd, L"BUTTON", L"", BS_PUSHBUTTON, CTL_UNDO, inst_);
    MakeChild(hwnd, L"BUTTON", L"", BS_PUSHBUTTON, CTL_RESETROW, inst_);
    MakeChild(hwnd, L"BUTTON", L"", BS_PUSHBUTTON, CTL_RESETALL, inst_);
    MakeChild(hwnd, L"BUTTON", L"", BS_PUSHBUTTON, CTL_SAVE, inst_);
    MakeChild(hwnd, L"BUTTON", L"", BS_PUSHBUTTON, CTL_GITHUB, inst_);

    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_STATUS, inst_);
    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_CREDIT, inst_);
    MakeChild(hwnd, L"STATIC", L"", SS_LEFT, CTL_HELP, inst_);

    RelabelControls();
}

// Applies every visible string. Called once at creation and again whenever the
// language changes, so switching language never needs the window recreated.
void EditorWindow::RelabelControls() {
    if (!hwnd_) return;
    suppressSync_ = true;

    SetWindowTextW(hwnd_, T(Str::EdWindowTitle));

    SetText(hwnd_, CTL_ENABLED,     T(Str::EdEnabled));
    SetText(hwnd_, CTL_LBL_PATTERN, T(Str::EdPatternLabel));
    SetText(hwnd_, CTL_LBL_GRID,    T(Str::EdGridLabel));
    SetText(hwnd_, CTL_MIRROR,      T(Str::EdMirror));
    SetText(hwnd_, CTL_FREEMOVE,    T(Str::EdFreeMove));
    SetText(hwnd_, CTL_AUTOBLEED,   T(Str::EdAutoBleed));
    SetText(hwnd_, CTL_LBL_QUALITY, T(Str::EdQualityLabel));
    SetText(hwnd_, CTL_AUTOSTART,   T(Str::EdAutostart));
    SetText(hwnd_, CTL_LBL_LANG,    T(Str::EdLanguageLabel));
    SetText(hwnd_, CTL_UNDO,        T(Str::EdUndo));
    SetText(hwnd_, CTL_RESETROW,    T(Str::EdResetRow));
    SetText(hwnd_, CTL_RESETALL,    T(Str::EdResetAll));
    SetText(hwnd_, CTL_SAVE,        T(Str::EdSave));
    SetText(hwnd_, CTL_GITHUB,      T(Str::EdProjectButton));
    SetText(hwnd_, CTL_HELP,        T(Str::EdHelp));

    wchar_t credit[128];
    swprintf(credit, std::size(credit), T(Str::EdCredit), kVersion, kAuthor);
    SetText(hwnd_, CTL_CREDIT, credit);

    auto fillCombo = [&](int id, const wchar_t* const* items, int count, int selection) {
        HWND h = GetDlgItem(hwnd_, id);
        if (!h) return;
        SendMessageW(h, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < count; ++i)
            SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items[i]));
        SendMessageW(h, CB_SETCURSEL, selection, 0);
    };

    const Settings& s = host_ ? host_->HostSettings() : Settings{};

    const wchar_t* patternItems[] = {
        T(Str::TrayPatternOff), T(Str::TrayPatternOverDesktop), T(Str::TrayPatternOnBlack)
    };
    fillCombo(CTL_PATTERN, patternItems, 3, s.patternMode);

    const wchar_t* qualityItems[] = {
        T(Str::EdQualityBilinear), T(Str::EdQualityBicubic), T(Str::EdQualitySharp)
    };
    fillCombo(CTL_QUALITY, qualityItems, 3, std::clamp(s.quality, 0, 2));

    const wchar_t* langItems[] = {
        LanguageDisplayName(Lang::English), LanguageDisplayName(Lang::Hungarian)
    };
    fillCombo(CTL_LANG, langItems, 2, s.language == Lang::Hungarian ? 1 : 0);

    // Grid sizes carry a translated "recommended" note on the default entry.
    std::wstring gridLabels[std::size(kGridSizes)];
    const wchar_t* gridPtrs[std::size(kGridSizes)];
    for (size_t i = 0; i < std::size(kGridSizes); ++i) {
        wchar_t buf[64];
        if (kGridSizes[i] == WarpMesh::kDefaultSize)
            swprintf(buf, std::size(buf), L"%d x %d  (%s)",
                     kGridSizes[i], kGridSizes[i], T(Str::EdGridRecommended));
        else
            swprintf(buf, std::size(buf), L"%d x %d", kGridSizes[i], kGridSizes[i]);
        gridLabels[i] = buf;
        gridPtrs[i]   = gridLabels[i].c_str();
    }
    fillCombo(CTL_GRID, gridPtrs, static_cast<int>(std::size(kGridSizes)),
              host_ ? GridIndexFor(host_->ActiveProfile().mesh.Size()) : 6);

    suppressSync_ = false;
}

void EditorWindow::LayoutControls() {
    if (!hwnd_) return;

    RECT client{};
    GetClientRect(hwnd_, &client);

    const int pad      = Scale(12);
    const int panelW   = Scale(300);
    const int rowH     = Scale(23);
    const int comboH   = Scale(200);   // dropdown height, not control height
    const int gapS     = Scale(5);
    const int gapM     = Scale(12);
    const int panelX   = client.right - panelW - pad;
    int       y        = pad;

    auto place = [&](int id, int height, int width = -1, int xOffset = 0) {
        HWND h = GetDlgItem(hwnd_, id);
        if (!h) return;
        MoveWindow(h, panelX + xOffset, y, width < 0 ? panelW : width, height, TRUE);
    };

    place(CTL_ENABLED, rowH);              y += rowH + gapM;

    place(CTL_LBL_PATTERN, Scale(18));     y += Scale(18) + gapS;
    place(CTL_PATTERN, comboH);            y += rowH + gapM;

    place(CTL_LBL_GRID, Scale(18));        y += Scale(18) + gapS;
    place(CTL_GRID, comboH);               y += rowH + gapM;

    place(CTL_MIRROR, rowH);               y += rowH + gapS;
    place(CTL_FREEMOVE, rowH);             y += rowH + gapM;

    place(CTL_LBL_GAIN, Scale(18));        y += Scale(18) + gapS;
    place(CTL_GAIN, Scale(28));            y += Scale(28) + gapM;

    place(CTL_LBL_OVERSCAN, Scale(18));    y += Scale(18) + gapS;
    place(CTL_OVERSCAN, Scale(28));        y += Scale(28) + gapM;

    place(CTL_AUTOBLEED, rowH);            y += rowH + gapS;
    place(CTL_LBL_BLEED, Scale(18));       y += Scale(18) + gapS;
    place(CTL_BLEED, Scale(28));           y += Scale(28) + gapM;

    place(CTL_LBL_QUALITY, Scale(18));     y += Scale(18) + gapS;
    place(CTL_QUALITY, comboH);            y += rowH + gapM;

    place(CTL_AUTOSTART, rowH);            y += rowH + gapM;

    place(CTL_LBL_LANG, Scale(18));        y += Scale(18) + gapS;
    place(CTL_LANG, comboH);               y += rowH + gapM;

    const int btnW = (panelW - gapS) / 2;
    place(CTL_UNDO,     rowH + Scale(4), btnW, 0);
    place(CTL_RESETROW, rowH + Scale(4), btnW, btnW + gapS);
    y += rowH + Scale(4) + gapS;
    place(CTL_RESETALL, rowH + Scale(4), btnW, 0);
    place(CTL_SAVE,     rowH + Scale(4), btnW, btnW + gapS);
    y += rowH + Scale(4) + gapS;
    place(CTL_GITHUB,   rowH + Scale(4), btnW, 0);
    y += rowH + Scale(4) + gapM;

    const int creditH = Scale(18);
    const int statusH = std::max(Scale(40),
                                 static_cast<int>(client.bottom) - pad - y - creditH - gapS);
    place(CTL_STATUS, statusH);            y += statusH + gapS;
    place(CTL_CREDIT, creditH);

    // The help strip runs under the canvas.
    HWND help = GetDlgItem(hwnd_, CTL_HELP);
    if (help)
        MoveWindow(help, pad, client.bottom - pad - Scale(18),
                   client.right - panelW - 3 * pad, Scale(18), TRUE);
}

// ---------------------------------------------------------------------------
// Model <-> controls
// ---------------------------------------------------------------------------

void EditorWindow::RefreshValueLabels() {
    if (!hwnd_ || !host_) return;
    const Settings& s = host_->HostSettings();
    const Profile&  p = host_->ActiveProfile();

    wchar_t buf[192];
    swprintf(buf, std::size(buf), T(Str::EdGainLabel), s.previewGain);
    SetText(hwnd_, CTL_LBL_GAIN, buf);
    swprintf(buf, std::size(buf), T(Str::EdOverscanLabel), p.overscan * 100.0f);
    SetText(hwnd_, CTL_LBL_OVERSCAN, buf);
    swprintf(buf, std::size(buf), T(Str::EdBleedLabel), p.edgeBleedPx);
    SetText(hwnd_, CTL_LBL_BLEED, buf);
}

void EditorWindow::SyncControlsFromModel() {
    if (!hwnd_ || !host_) return;
    suppressSync_ = true;

    const Settings& s = host_->HostSettings();
    const Profile&  p = host_->ActiveProfile();

    CheckDlgButton(hwnd_, CTL_ENABLED,   s.enabled   ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, CTL_MIRROR,    s.mirror    ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, CTL_FREEMOVE,  s.freeMove  ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, CTL_AUTOBLEED, p.autoBleed ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, CTL_AUTOSTART, host_->GetAutostart() ? BST_CHECKED : BST_UNCHECKED);

    SendDlgItemMessageW(hwnd_, CTL_PATTERN, CB_SETCURSEL, s.patternMode, 0);
    SendDlgItemMessageW(hwnd_, CTL_QUALITY, CB_SETCURSEL, std::clamp(s.quality, 0, 2), 0);
    SendDlgItemMessageW(hwnd_, CTL_LANG, CB_SETCURSEL,
                        s.language == Lang::Hungarian ? 1 : 0, 0);
    SendDlgItemMessageW(hwnd_, CTL_GRID, CB_SETCURSEL, GridIndexFor(p.mesh.Size()), 0);

    SendDlgItemMessageW(hwnd_, CTL_GAIN, TBM_SETPOS, TRUE, s.previewGain);
    SendDlgItemMessageW(hwnd_, CTL_OVERSCAN, TBM_SETPOS, TRUE,
                        static_cast<LPARAM>(std::lround(p.overscan * 100.0f)));
    SendDlgItemMessageW(hwnd_, CTL_BLEED, TBM_SETPOS, TRUE,
                        static_cast<LPARAM>(std::lround(p.edgeBleedPx)));
    EnableWindow(GetDlgItem(hwnd_, CTL_BLEED), !p.autoBleed);

    RefreshValueLabels();

    suppressSync_ = false;
    UpdateStatusText();
}

void EditorWindow::ApplyControlsToModel(int controlId) {
    if (!hwnd_ || !host_ || suppressSync_) return;

    Settings& s = host_->HostSettings();
    Profile&  p = host_->ActiveProfile();
    bool persist = true;

    switch (controlId) {
    case CTL_ENABLED:
        s.enabled = IsDlgButtonChecked(hwnd_, CTL_ENABLED) == BST_CHECKED;
        break;
    case CTL_MIRROR:
        s.mirror = IsDlgButtonChecked(hwnd_, CTL_MIRROR) == BST_CHECKED;
        break;
    case CTL_FREEMOVE:
        s.freeMove = IsDlgButtonChecked(hwnd_, CTL_FREEMOVE) == BST_CHECKED;
        break;
    case CTL_QUALITY: {
        const int index = static_cast<int>(
            SendDlgItemMessageW(hwnd_, CTL_QUALITY, CB_GETCURSEL, 0, 0));
        if (index >= 0) s.quality = std::clamp(index, 0, 2);
        break;
    }
    case CTL_LANG: {
        const int index = static_cast<int>(
            SendDlgItemMessageW(hwnd_, CTL_LANG, CB_GETCURSEL, 0, 0));
        const Lang next = index == 1 ? Lang::Hungarian : Lang::English;
        if (next != s.language) {
            s.language = next;
            SetLanguage(next);
            RelabelControls();      // no need to recreate the window
        }
        break;
    }
    case CTL_AUTOSTART:
        host_->SetAutostart(IsDlgButtonChecked(hwnd_, CTL_AUTOSTART) == BST_CHECKED);
        break;
    case CTL_AUTOBLEED:
        p.autoBleed = IsDlgButtonChecked(hwnd_, CTL_AUTOBLEED) == BST_CHECKED;
        EnableWindow(GetDlgItem(hwnd_, CTL_BLEED), !p.autoBleed);
        break;
    case CTL_PATTERN:
        s.patternMode = static_cast<int>(
            SendDlgItemMessageW(hwnd_, CTL_PATTERN, CB_GETCURSEL, 0, 0));
        if (s.patternMode < 0) s.patternMode = 0;
        break;
    case CTL_GRID: {
        const int index = static_cast<int>(
            SendDlgItemMessageW(hwnd_, CTL_GRID, CB_GETCURSEL, 0, 0));
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
            SendDlgItemMessageW(hwnd_, CTL_GAIN, TBM_GETPOS, 0, 0));
        persist = false;   // preview only, saved with the next real edit
        break;
    case CTL_OVERSCAN:
        p.overscan = static_cast<float>(
            SendDlgItemMessageW(hwnd_, CTL_OVERSCAN, TBM_GETPOS, 0, 0)) / 100.0f;
        break;
    case CTL_BLEED:
        p.edgeBleedPx = static_cast<float>(
            SendDlgItemMessageW(hwnd_, CTL_BLEED, TBM_GETPOS, 0, 0));
        break;
    default:
        return;
    }

    // Refresh the derived labels without re-entering this function.
    suppressSync_ = true;
    RefreshValueLabels();
    suppressSync_ = false;

    host_->OnEditorChanged(persist);
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void EditorWindow::UpdateStatusText() {
    if (!hwnd_ || !host_) return;

    const DisplayMode mode = host_->ActiveMode();
    const Profile&    p    = host_->ActiveProfile();

    std::wstring text;
    wchar_t line[512];

    const std::wstring modeText = Widen(host_->ActiveModeKey());
    swprintf(line, std::size(line), T(Str::StatusProfile),
             modeText.c_str(), p.mesh.Size(), p.mesh.Size());
    text += line;

    if (selRow_ >= 0 && selCol_ >= 0) {
        const Offset& o = p.mesh.At(selRow_, selCol_);
        swprintf(line, std::size(line), T(Str::StatusSelected),
                 selRow_, selCol_,
                 p.mesh.RowLocked(selRow_) ? T(Str::StatusRowLocked) : L"",
                 o.dy * static_cast<float>(mode.height),
                 o.dx * static_cast<float>(mode.width));
    } else {
        swprintf(line, std::size(line), L"%s", T(Str::StatusNoSelection));
    }
    text += line;

    const std::wstring err = host_->EngineStatus();
    if (!err.empty()) text += std::wstring(T(Str::StatusWarning)) + err + L"\r\n\r\n";

    swprintf(line, std::size(line), T(Str::StatusUndoSteps), static_cast<int>(undo_.size()));
    text += line;

    SetText(hwnd_, CTL_STATUS, text.c_str());
}

// ---------------------------------------------------------------------------
// Canvas
// ---------------------------------------------------------------------------

RECT EditorWindow::CanvasRect(const RECT& client) const {
    const int pad    = Scale(12);
    const int panelW = Scale(300);
    RECT r{ pad, pad, client.right - panelW - 2 * pad, client.bottom - pad - Scale(26) };
    if (r.right < r.left + 50) r.right = r.left + 50;
    if (r.bottom < r.top + 50) r.bottom = r.top + 50;
    return r;
}

RECT EditorWindow::ScreenAreaRect(const RECT& client) const {
    RECT canvas = CanvasRect(client);
    // Leave room on the left for the row lock glyphs.
    const int inset = Scale(34);
    canvas.left   += inset;
    canvas.right  -= Scale(16);
    canvas.top    += Scale(16);
    canvas.bottom -= Scale(16);

    const DisplayMode mode = host_ ? host_->ActiveMode() : DisplayMode{};
    const float aspect = (mode.Valid())
        ? static_cast<float>(mode.width) / static_cast<float>(mode.height)
        : 4.0f / 3.0f;

    const int availW = canvas.right - canvas.left;
    const int availH = canvas.bottom - canvas.top;
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

void EditorWindow::PointToCanvas(int row, int col, const RECT& area, int& x, int& y) const {
    const Profile& p    = host_->ActiveProfile();
    const int      n    = p.mesh.Size();
    const float    gain = static_cast<float>(host_->HostSettings().previewGain);
    const Offset&  o    = p.mesh.At(row, col);

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

    const Profile& p    = host_->ActiveProfile();
    const WarpMesh& mesh = p.mesh;
    const int      n    = mesh.Size();
    const float    gain = static_cast<float>(host_->HostSettings().previewGain);
    const float    aw   = static_cast<float>(area.right - area.left);
    const float    ah   = static_cast<float>(area.bottom - area.top);

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

    // Deformed lattice: sample the spline so the real curvature is visible.
    HPEN curvePen = CreatePen(PS_SOLID, Scale(2), RGB(86, 208, 130));
    oldPen = SelectObject(dc, curvePen);

    // One sampler for the whole repaint: WarpMesh::Eval rebuilds every spline
    // per call, which at 21x21 turned a redraw into tens of milliseconds.
    const WarpSampler sampler(mesh);

    const int steps = CurveSteps(n);
    std::vector<POINT> pts(static_cast<size_t>(steps) + 1);
    for (int r = 0; r < n; ++r) {
        const float v = static_cast<float>(r) / static_cast<float>(n - 1);
        for (int s = 0; s <= steps; ++s) {
            const float u = static_cast<float>(s) / static_cast<float>(steps);
            const Offset o = sampler.At(u, v);
            pts[s].x = area.left + static_cast<LONG>(std::lround((u + o.dx * gain) * aw));
            pts[s].y = area.top  + static_cast<LONG>(std::lround((v + o.dy * gain) * ah));
        }
        Polyline(dc, pts.data(), static_cast<int>(pts.size()));
    }
    for (int c = 0; c < n; ++c) {
        const float u = static_cast<float>(c) / static_cast<float>(n - 1);
        for (int s = 0; s <= steps; ++s) {
            const float v = static_cast<float>(s) / static_cast<float>(steps);
            const Offset o = sampler.At(u, v);
            pts[s].x = area.left + static_cast<LONG>(std::lround((u + o.dx * gain) * aw));
            pts[s].y = area.top  + static_cast<LONG>(std::lround((v + o.dy * gain) * ah));
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

            HBRUSH b = CreateSolidBrush(fill);
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
    const int ah = area.bottom - area.top;
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
    const DisplayMode mode = host_->ActiveMode();
    if (!mode.Valid()) return;

    PushUndo();
    MoveControlPoint(selRow_, selCol_,
                     dxPixels / static_cast<float>(mode.width),
                     dyPixels / static_cast<float>(mode.height), true);
    host_->OnEditorChanged(false);
    UpdateStatusText();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------

LRESULT EditorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        hwnd_ = hwnd;
        dpi_  = static_cast<int>(GetDpiForWindow(hwnd));
        font_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        CreateControls(hwnd);
        EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
            SendMessageW(child, WM_SETFONT, param, TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(font_));
        LayoutControls();
        SyncControlsFromModel();
        return 0;
    }

    case WM_SIZE:
        LayoutControls();
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_DPICHANGED: {
        dpi_ = HIWORD(wp);
        if (font_) DeleteObject(font_);
        font_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        EnumChildWindows(hwnd, [](HWND child, LPARAM param) -> BOOL {
            SendMessageW(child, WM_SETFONT, param, TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(font_));
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
        // Only the panel side needs the system background; the canvas paints
        // itself completely in WM_PAINT.
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

        if (id == CTL_GITHUB && code == BN_CLICKED) {
            ShellExecuteW(hwnd, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == CTL_UNDO && code == BN_CLICKED) { Undo(); return 0; }
        if (id == CTL_RESETROW && code == BN_CLICKED) {
            if (host_ && selRow_ >= 0) {
                PushUndo();
                host_->ActiveProfile().mesh.ResetRow(selRow_);
                host_->OnEditorChanged(true);
                UpdateStatusText();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (id == CTL_RESETALL && code == BN_CLICKED) {
            if (host_) {
                PushUndo();
                host_->ActiveProfile().mesh.Reset();
                host_->OnEditorChanged(true);
                UpdateStatusText();
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (id == CTL_SAVE && code == BN_CLICKED) {
            if (host_) host_->OnEditorChanged(true);
            return 0;
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
        return 0;
    }

    case WM_CLOSE:
        if (host_) host_->OnEditorChanged(true);
        DestroyWindow(hwnd);
        return 0;

    case WM_NCDESTROY:
        if (font_) { DeleteObject(font_); font_ = nullptr; }
        hwnd_ = nullptr;
        dragging_ = false;
        undo_.clear();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace crtb
