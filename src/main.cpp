// CRTBender - entry point, tray icon and application wiring.
//
// The app is a tray resident. It owns the config, drives the render thread and
// opens the calibration window on demand.
#include "autostart.h"
#include "config.h"
#include "display.h"
#include "editor.h"
#include "i18n.h"
#include "render.h"
#include "util.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace crtb {
namespace {

constexpr const wchar_t* kAppWindowClass = L"CRTBenderAppWindow";
constexpr const wchar_t* kMutexName      = L"Local\\CRTBender.SingleInstance";
constexpr UINT kTrayIconId = 1;

enum : UINT {
    WM_TRAYICON     = WM_APP + 1,
    WM_MODE_CHANGED = WM_APP + 2,
    WM_SHOW_EDITOR  = WM_APP + 3,
};

enum : int {
    IDM_EDITOR = 100,
    IDM_ENABLED,
    IDM_PATTERN_OFF,
    IDM_PATTERN_OVER,
    IDM_PATTERN_SOLID,
    IDM_AUTOSTART,
    IDM_OPEN_CONFIG,
    IDM_OPEN_LOG,
    IDM_LANG_EN,
    IDM_LANG_HU,
    IDM_PROJECT,
    IDM_ABOUT,
    IDM_EXIT,
};

enum : int {
    HOTKEY_TOGGLE  = 1,
    HOTKEY_PATTERN = 2,
    HOTKEY_EDITOR  = 3,
    HOTKEY_ESCAPE  = 4,
};

constexpr UINT kTimerPoll   = 1;   // monitor set / mode changes, engine health
constexpr UINT kTimerBypass = 2;   // protected-content watcher, needs to be quick

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

bool HasCaptureAffinity(HWND hwnd) {
    using Fn = BOOL (WINAPI*)(HWND, DWORD*);
    static Fn getAffinity = LoadSystemFunction<Fn>(
        L"user32.dll", "GetWindowDisplayAffinity");
    if (!getAffinity) return false;   // XP: the API does not exist

    DWORD affinity = WDA_NONE;
    return getAffinity(hwnd, &affinity) && affinity != WDA_NONE;
}

} // namespace

// ---------------------------------------------------------------------------

class App : public EditorHost {
public:
    int Run(HINSTANCE inst, bool openEditor);

    // EditorHost
    Settings& HostSettings() override { return config_.GetSettings(); }

    const std::vector<MonitorInfo>& Monitors() const override { return monitors_; }
    int  SelectedMonitor() const override { return selected_; }
    void SelectMonitor(int index) override;
    const MonitorInfo& ActiveMonitor() const override;
    Profile& ActiveProfile() override { return config_.ProfileFor(ActiveMonitor().ProfileKey()); }

    void         OnEditorChanged(bool persist) override;
    bool         GetAutostart() const override { return IsAutostartEnabled(); }
    void         SetAutostart(bool enabled) override;
    std::wstring EngineStatus() const override;

private:
    static LRESULT CALLBACK WndProcThunk(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND, UINT, WPARAM, LPARAM);

    void PushStates();
    void RebuildMonitors(bool force);
    void UpdateBypass();
    void AddTrayIcon();
    void RemoveTrayIcon();
    void UpdateTrayTooltip();
    void ShowTrayMenu();
    void RegisterHotkeys();
    // Esc is only claimed while the test pattern is up. It covers the screen and
    // is the one thing you urgently want to get out of, but stealing Esc from
    // every application permanently would be unacceptable.
    void UpdateEscapeHotkey();
    void SetLanguageSetting(Lang lang);
    void ShowAbout();
    void ShowBalloon(const wchar_t* title, const wchar_t* text);

    HINSTANCE    inst_ = nullptr;
    HWND         hwnd_ = nullptr;
    Config       config_;
    EditorWindow editor_;

    // One engine per monitor, in the same order as monitors_.
    std::vector<MonitorInfo>                 monitors_;
    std::vector<std::unique_ptr<WarpEngine>> engines_;
    int          selected_ = 0;

    // Auto-bypass state, driven by the fast timer.
    bool         bypassActive_ = false;
    bool         bypassProtected_ = false;

    UINT         taskbarCreatedMsg_ = 0;
    bool         warnedAboutError_  = false;
    bool         trayAdded_         = false;
    bool         escapeHotkeyOwned_ = false;
};

// ---------------------------------------------------------------------------

const MonitorInfo& App::ActiveMonitor() const {
    static const MonitorInfo kFallback;
    if (monitors_.empty()) return kFallback;
    const int index = std::clamp(selected_, 0, static_cast<int>(monitors_.size()) - 1);
    return monitors_[static_cast<size_t>(index)];
}

void App::SelectMonitor(int index) {
    if (monitors_.empty()) return;
    selected_ = std::clamp(index, 0, static_cast<int>(monitors_.size()) - 1);
    UpdateTrayTooltip();
}

std::wstring App::EngineStatus() const {
    // Report the selected monitor's engine first, then any other complaint, so
    // a problem on a second screen is never silently swallowed.
    const size_t index = static_cast<size_t>(
        std::clamp(selected_, 0, static_cast<int>(engines_.size()) - 1));
    if (index < engines_.size()) {
        const std::wstring mine = engines_[index]->LastError();
        if (!mine.empty()) return mine;
    }
    for (const auto& engine : engines_) {
        const std::wstring other = engine->LastError();
        if (!other.empty()) return other;
    }
    return {};
}

void App::PushStates() {
    const Settings& s = config_.GetSettings();

    for (size_t i = 0; i < engines_.size() && i < monitors_.size(); ++i) {
        const MonitorInfo& monitor = monitors_[i];
        Profile& profile = config_.ProfileFor(monitor.ProfileKey());

        RenderState state;
        state.mesh           = profile.mesh;
        state.geometry       = profile.geometry;
        state.convergence    = profile.convergence;
        state.enabled        = s.enabled;
        state.bypass         = bypassActive_;
        state.overscan       = profile.overscan;
        state.edgeBleed      = EffectiveEdgeBleed(profile, monitor.width, monitor.height);
        state.patternMode    = s.patternMode;
        state.patternCells   = s.patternCells;
        state.patternOpacity = static_cast<float>(s.patternOpacityPct) / 100.0f;
        state.quality        = s.quality;
        state.flipModel      = s.flipPresent;
        state.aspect         = monitor.height > 0
            ? static_cast<float>(monitor.width) / static_cast<float>(monitor.height)
            : 4.0f / 3.0f;

        // The rasterizer interpolates linearly between tessellation vertices, so
        // a dense lattice needs a dense grid to stay faithful to the spline. Keep
        // at least ten subdivisions inside every lattice cell; the configured
        // value is a floor, not a cap.
        const int perCell = (profile.mesh.Size() - 1) * 10;
        state.tessellation = std::clamp(std::max(s.tessellation, perCell), 16, 256);

        engines_[i]->Update(state);
    }
}

void App::OnEditorChanged(bool persist) {
    SetLanguage(config_.GetSettings().language);
    PushStates();
    UpdateTrayTooltip();
    UpdateEscapeHotkey();
    if (persist) config_.Save();
}

void App::UpdateEscapeHotkey() {
    const Settings& s = config_.GetSettings();
    const bool want = s.hotkeys && s.patternMode != 0;
    if (want == escapeHotkeyOwned_) return;

    if (want) {
        escapeHotkeyOwned_ = RegisterHotKey(hwnd_, HOTKEY_ESCAPE, 0, VK_ESCAPE) != FALSE;
        if (!escapeHotkeyOwned_) LogLine(L"Esc hotkey could not be registered");
    } else {
        UnregisterHotKey(hwnd_, HOTKEY_ESCAPE);
        escapeHotkeyOwned_ = false;
    }
}

void App::SetAutostart(bool enabled) {
    if (SetAutostartEnabled(enabled)) {
        config_.GetSettings().autostart = enabled;
        config_.Save();
    } else {
        MessageBoxW(editor_.IsOpen() ? editor_.Handle() : hwnd_,
                    T(Str::MsgAutostartFailed), L"CRTBender", MB_OK | MB_ICONWARNING);
    }
}

void App::RebuildMonitors(bool force) {
    std::vector<MonitorInfo> current = EnumerateMonitors();
    if (current.empty()) return;

    // Only tear engines down when the set of monitors or their modes actually
    // changed; this runs on a timer.
    bool changed = force || current.size() != monitors_.size();
    for (size_t i = 0; !changed && i < current.size(); ++i) {
        changed = current[i].ProfileKey() != monitors_[i].ProfileKey() ||
                  current[i].deviceName  != monitors_[i].deviceName;
    }
    if (!changed) return;

    LogLine(L"Monitor configuration changed, restarting engines");
    for (const MonitorInfo& mon : current) {
        wchar_t line[256];
        swprintf(line, std::size(line), L"  %ls (%ls) %dx%d@%d%ls",
                 mon.deviceName.c_str(), Widen(mon.monitorKey).c_str(),
                 mon.width, mon.height, mon.refresh, mon.primary ? L" [primary]" : L"");
        LogLine(line);
    }

    for (auto& engine : engines_) engine->Stop();
    engines_.clear();

    monitors_ = std::move(current);
    selected_ = std::clamp(selected_, 0, static_cast<int>(monitors_.size()) - 1);

    for (const MonitorInfo& monitor : monitors_) {
        // Profiles written before 1.2 had no monitor part in the key and only
        // ever described the primary screen.
        if (monitor.primary)
            config_.AdoptLegacyProfile(monitor.ModeKey(), monitor.ProfileKey());
        config_.ProfileFor(monitor.ProfileKey());

        auto engine = std::make_unique<WarpEngine>();
        engine->Start(hwnd_, WM_MODE_CHANGED, monitor.deviceName);
        engines_.push_back(std::move(engine));
    }

    PushStates();
    UpdateTrayTooltip();
    if (editor_.IsOpen()) editor_.Refresh();
}

void App::UpdateBypass() {
    const Settings& s = config_.GetSettings();

    bool wantBypass = false;
    if (s.autoBypass) {
        // A window that excludes itself from capture - a DRM video player, or
        // anything else using SetWindowDisplayAffinity - would come through the
        // capture as solid black. Rather than blanking the user's screen, step
        // aside for as long as it is in the foreground.
        if (HWND foreground = GetForegroundWindow()) {
            wantBypass = HasCaptureAffinity(foreground);
        }
    }

    if (wantBypass == bypassActive_) return;

    bypassActive_    = wantBypass;
    bypassProtected_ = wantBypass;
    LogLine(wantBypass ? L"Auto-bypass on: protected content in the foreground"
                       : L"Auto-bypass off");
    PushStates();
    UpdateTrayTooltip();
    if (editor_.IsOpen()) editor_.Refresh();
}

// ---------------------------------------------------------------------------
// Tray
// ---------------------------------------------------------------------------

void App::AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd_;
    nid.uID              = kTrayIconId;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
#ifndef CRTB_XP
    nid.uFlags          |= NIF_SHOWTIP;
#endif
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = static_cast<HICON>(LoadImageW(inst_, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXSMICON),
                                                         GetSystemMetrics(SM_CYSMICON), 0));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    CopyTo(nid.szTip, L"CRTBender");

    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
#ifdef CRTB_XP
    nid.uVersion = NOTIFYICON_VERSION;
#else
    nid.uVersion = NOTIFYICON_VERSION_4;
#endif
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    UpdateTrayTooltip();
}

void App::RemoveTrayIcon() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayAdded_ = false;
}

void App::UpdateTrayTooltip() {
    if (!trayAdded_) return;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd_;
    nid.uID    = kTrayIconId;
    nid.uFlags = NIF_TIP;
#ifndef CRTB_XP
    nid.uFlags |= NIF_SHOWTIP;
#endif

    const Settings& s = config_.GetSettings();
    const std::wstring modeText = Widen(ActiveMonitor().ModeKey());
    wchar_t tip[128];
    swprintf(tip, std::size(tip),
             s.enabled ? T(Str::TrayTipEnabled) : T(Str::TrayTipDisabled),
             modeText.c_str());
    CopyTo(nid.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ShowBalloon(const wchar_t* title, const wchar_t* text) {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize   = sizeof(nid);
    nid.hWnd     = hwnd_;
    nid.uID      = kTrayIconId;
    nid.uFlags   = NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING;
    CopyTo(nid.szInfoTitle, title);
    CopyTo(nid.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ShowTrayMenu() {
    const Settings& s = config_.GetSettings();

    HMENU pattern = CreatePopupMenu();
    AppendMenuW(pattern, MF_STRING | (s.patternMode == 0 ? MF_CHECKED : 0),
                IDM_PATTERN_OFF, T(Str::TrayPatternOff));
    AppendMenuW(pattern, MF_STRING | (s.patternMode == 1 ? MF_CHECKED : 0),
                IDM_PATTERN_OVER, T(Str::TrayPatternOverDesktop));
    AppendMenuW(pattern, MF_STRING | (s.patternMode == 2 ? MF_CHECKED : 0),
                IDM_PATTERN_SOLID, T(Str::TrayPatternOnBlack));

    HMENU language = CreatePopupMenu();
    AppendMenuW(language, MF_STRING | (s.language == Lang::English ? MF_CHECKED : 0),
                IDM_LANG_EN, LanguageDisplayName(Lang::English));
    AppendMenuW(language, MF_STRING | (s.language == Lang::Hungarian ? MF_CHECKED : 0),
                IDM_LANG_HU, LanguageDisplayName(Lang::Hungarian));

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_EDITOR, T(Str::TrayOpenEditor));
    SetMenuDefaultItem(menu, IDM_EDITOR, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (s.enabled ? MF_CHECKED : 0), IDM_ENABLED,
                T(Str::TrayCorrectionEnabled));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(pattern), T(Str::TrayTestPattern));
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(language), T(Str::TrayLanguage));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), IDM_AUTOSTART,
                T(Str::TrayAutostart));
    AppendMenuW(menu, MF_STRING, IDM_OPEN_CONFIG, T(Str::TrayShowConfig));
    AppendMenuW(menu, MF_STRING, IDM_OPEN_LOG, T(Str::TrayOpenLog));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_PROJECT, T(Str::TrayProjectPage));
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, T(Str::TrayAbout));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, T(Str::TrayExit));

    POINT pt{};
    GetCursorPos(&pt);
    // Required so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

void App::SetLanguageSetting(Lang lang) {
    Settings& s = config_.GetSettings();
    if (s.language == lang) return;
    s.language = lang;
    SetLanguage(lang);
    config_.Save();
    UpdateTrayTooltip();
    if (editor_.IsOpen()) editor_.Refresh();
}

void App::ShowAbout() {
    wchar_t body[512];
    swprintf(body, std::size(body), T(Str::MsgAboutBody), kVersion, kAuthor, kProjectUrl);

    const int answer = MessageBoxW(editor_.IsOpen() ? editor_.Handle() : hwnd_,
                                   body, T(Str::MsgAboutTitle),
                                   MB_YESNO | MB_ICONINFORMATION);
    if (answer == IDYES)
        ShellExecuteW(nullptr, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
}

void App::RegisterHotkeys() {
    if (!config_.GetSettings().hotkeys) return;
    // Ctrl+Alt+B toggles the correction - the A/B compare you need while
    // calibrating. Ctrl+Alt+G cycles the test pattern, Ctrl+Alt+E opens the UI.
    if (!RegisterHotKey(hwnd_, HOTKEY_TOGGLE, MOD_CONTROL | MOD_ALT, 'B'))
        LogLine(L"Hotkey Ctrl+Alt+B could not be registered");
    if (!RegisterHotKey(hwnd_, HOTKEY_PATTERN, MOD_CONTROL | MOD_ALT, 'G'))
        LogLine(L"Hotkey Ctrl+Alt+G could not be registered");
    if (!RegisterHotKey(hwnd_, HOTKEY_EDITOR, MOD_CONTROL | MOD_ALT, 'E'))
        LogLine(L"Hotkey Ctrl+Alt+E could not be registered");
}

// ---------------------------------------------------------------------------

LRESULT CALLBACK App::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_ != 0) {
        trayAdded_ = false;
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_TRAYICON:
        switch (LOWORD(lp)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONUP:
            editor_.Open(inst_, this);
            return 0;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            ShowTrayMenu();
            return 0;
        default:
            return 0;
        }

    case WM_COMMAND: {
        Settings& s = config_.GetSettings();
        switch (LOWORD(wp)) {
        case IDM_EDITOR:
            editor_.Open(inst_, this);
            return 0;
        case IDM_ENABLED:
            s.enabled = !s.enabled;
            OnEditorChanged(true);
            if (editor_.IsOpen()) editor_.Refresh();
            return 0;
        case IDM_PATTERN_OFF:
        case IDM_PATTERN_OVER:
        case IDM_PATTERN_SOLID:
            s.patternMode = LOWORD(wp) - IDM_PATTERN_OFF;
            OnEditorChanged(true);
            if (editor_.IsOpen()) editor_.Refresh();
            return 0;
        case IDM_AUTOSTART:
            SetAutostart(!IsAutostartEnabled());
            if (editor_.IsOpen()) editor_.Refresh();
            return 0;
        case IDM_OPEN_CONFIG: {
            config_.Save();   // make sure the file exists before revealing it
            const std::wstring arg = L"/select,\"" + config_.Path() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case IDM_OPEN_LOG: {
            const std::wstring log = AppDataDir() + L"\\crtbender.log";
            ShellExecuteW(nullptr, L"open", L"notepad.exe", log.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case IDM_LANG_EN:
            SetLanguageSetting(Lang::English);
            return 0;
        case IDM_LANG_HU:
            SetLanguageSetting(Lang::Hungarian);
            return 0;
        case IDM_PROJECT:
            ShellExecuteW(nullptr, L"open", kProjectUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case IDM_ABOUT:
            ShowAbout();
            return 0;
        case IDM_EXIT:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        default:
            break;
        }
        return 0;
    }

    case WM_HOTKEY: {
        Settings& s = config_.GetSettings();
        switch (static_cast<int>(wp)) {
        case HOTKEY_TOGGLE:
            s.enabled = !s.enabled;
            OnEditorChanged(true);
            if (editor_.IsOpen()) editor_.Refresh();
            return 0;
        case HOTKEY_PATTERN:
            s.patternMode = (s.patternMode + 1) % 3;
            OnEditorChanged(true);
            if (editor_.IsOpen()) editor_.Refresh();
            return 0;
        case HOTKEY_EDITOR:
            editor_.Open(inst_, this);
            return 0;
        case HOTKEY_ESCAPE:
            s.patternMode = 0;
            OnEditorChanged(true);
            if (editor_.IsOpen()) editor_.Refresh();
            return 0;
        default:
            return 0;
        }
    }

    case WM_SHOW_EDITOR:
        editor_.Open(inst_, this);
        return 0;

    case WM_MODE_CHANGED:
        RebuildMonitors(false);
        return 0;

    case WM_TIMER:
        if (wp == kTimerBypass) {
            UpdateBypass();
            return 0;
        }
        if (wp == kTimerPoll) {
            // Safety net: catches monitor and mode changes even when every
            // overlay happens to be down.
            RebuildMonitors(false);

            const std::wstring error = EngineStatus();
            if (!error.empty() && !warnedAboutError_) {
                warnedAboutError_ = true;
                ShowBalloon(L"CRTBender", error.c_str());
            } else if (error.empty()) {
                warnedAboutError_ = false;
            }
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------

int App::Run(HINSTANCE inst, bool openEditor) {
    inst_ = inst;

    config_.Load();
    SetLanguage(config_.GetSettings().language);   // before any UI text is built
    // The registry is the source of truth for autostart; mirror it into the
    // config so the file reflects reality.
    config_.GetSettings().autostart = IsAutostartEnabled();

    LogLine(std::wstring(L"CRTBender ") + kVersion + L" by " + kAuthor + L" - " + kProjectUrl);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &App::WndProcThunk;
    wc.hInstance     = inst;
    wc.lpszClassName = kAppWindowClass;
    RegisterClassExW(&wc);

    // A normal (hidden) top-level window rather than HWND_MESSAGE: message-only
    // windows do not receive the TaskbarCreated broadcast.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kAppWindowClass, L"CRTBender",
                            WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, this);
    if (!hwnd_) {
        LogLine(L"App window creation failed");
        return 1;
    }

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    RebuildMonitors(true);
    AddTrayIcon();
    RegisterHotkeys();
    UpdateEscapeHotkey();   // the config may already have the pattern switched on

    PushStates();

    SetTimer(hwnd_, kTimerPoll, 2000, nullptr);
    // Faster, because a black video window is noticed immediately.
    SetTimer(hwnd_, kTimerBypass, 400, nullptr);

    if (openEditor) editor_.Open(inst_, this);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd_, kTimerPoll);
    KillTimer(hwnd_, kTimerBypass);
    UnregisterHotKey(hwnd_, HOTKEY_TOGGLE);
    UnregisterHotKey(hwnd_, HOTKEY_PATTERN);
    UnregisterHotKey(hwnd_, HOTKEY_EDITOR);
    UnregisterHotKey(hwnd_, HOTKEY_ESCAPE);
    for (auto& engine : engines_) engine->Stop();
    engines_.clear();
    config_.Save();
    RemoveTrayIcon();

    LogLine(L"---- CRTBender exited ----");
    return static_cast<int>(msg.wParam);
}

} // namespace crtb

// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    using namespace crtb;

    // Single instance: a second launch just raises the calibration window.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kAppWindowClass, nullptr);
        if (existing) PostMessageW(existing, WM_SHOW_EDITOR, 0, 0);
        CloseHandle(mutex);
        return 0;
    }

    LogInit();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);

    const std::wstring args = cmdLine ? cmdLine : L"";
    const bool silent = args.find(L"--silent") != std::wstring::npos;
    const bool forceEditor = args.find(L"--editor") != std::wstring::npos;

    // First run (no config file yet) opens the editor, otherwise an invisible
    // tray app would be baffling.
    const std::wstring probe = AppDataDir() + L"\\crtbender.cfg";
    const bool firstRun = GetFileAttributesW(probe.c_str()) == INVALID_FILE_ATTRIBUTES &&
                          GetFileAttributesW((ExeDir() + L"\\crtbender.cfg").c_str()) ==
                              INVALID_FILE_ATTRIBUTES;

    App app;
    const int rc = app.Run(inst, forceEditor || (firstRun && !silent));

    if (mutex) CloseHandle(mutex);
    return rc;
}
