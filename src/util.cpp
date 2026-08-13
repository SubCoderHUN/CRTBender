#include "util.h"

#include <shlobj.h>
#include <cstdio>

namespace crtb {

namespace {

class CriticalSection {
public:
    CriticalSection() { InitializeCriticalSection(&value_); }
    ~CriticalSection() { DeleteCriticalSection(&value_); }
    void Lock() { EnterCriticalSection(&value_); }
    void Unlock() { LeaveCriticalSection(&value_); }

private:
    CRITICAL_SECTION value_{};
};

class ScopedLock {
public:
    explicit ScopedLock(CriticalSection& lock) : lock_(lock) { lock_.Lock(); }
    ~ScopedLock() { lock_.Unlock(); }

private:
    CriticalSection& lock_;
};

} // namespace

static std::wstring g_logPath;
static CriticalSection g_logMutex;

std::wstring ExePath() {
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return L"";
    return buf;
}

std::wstring ExeDir() {
    std::wstring p = ExePath();
    size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : p.substr(0, slash);
}

std::wstring AppDataDir() {
    wchar_t roaming[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr,
                                SHGFP_TYPE_CURRENT, roaming)))
        return L"";
    std::wstring dir = roaming;
    dir += L"\\CRTBender";
    CreateDirectoryW(dir.c_str(), nullptr);   // ignore ERROR_ALREADY_EXISTS
    return dir;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

UINT SystemDpi() {
    using Fn = UINT (WINAPI*)();
    static Fn getDpi = LoadSystemFunction<Fn>(L"user32.dll", "GetDpiForSystem");
    if (getDpi) return getDpi();

    HDC dc = GetDC(nullptr);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi > 0 ? static_cast<UINT>(dpi) : 96u;
}

UINT WindowDpi(HWND hwnd) {
    using Fn = UINT (WINAPI*)(HWND);
    static Fn getDpi = LoadSystemFunction<Fn>(L"user32.dll", "GetDpiForWindow");
    return getDpi ? getDpi(hwnd) : SystemDpi();
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

void LogInit() {
    std::wstring dir = AppDataDir();
    if (dir.empty()) return;
    g_logPath = dir + L"\\crtbender.log";

    // Keep the log from growing without bound across sessions.
    HANDLE h = CreateFileW(g_logPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size{};
        GetFileSizeEx(h, &size);
        CloseHandle(h);
        if (size.QuadPart > 512 * 1024)
            DeleteFileW(g_logPath.c_str());
    }
    LogLine(L"---- CRTBender started ----");
}

void LogLine(const std::wstring& msg) {
    if (g_logPath.empty()) return;
    ScopedLock lock(g_logMutex);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t stamp[64];
    swprintf(stamp, std::size(stamp), L"%04u-%02u-%02u %02u:%02u:%02u.%03u  ",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::wstring line = std::wstring(stamp) + msg + L"\r\n";
    std::string utf8 = Narrow(line);

    HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(h);
}

void LogHr(const wchar_t* what, HRESULT hr) {
    wchar_t buf[256];
    swprintf(buf, std::size(buf), L"%ls failed: hr=0x%08lX", what, static_cast<unsigned long>(hr));
    LogLine(buf);
}

} // namespace crtb
