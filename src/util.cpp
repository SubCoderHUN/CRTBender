#include "util.h"

#include <shlobj.h>
#include <mutex>
#include <cstdio>

namespace crtb {

static std::wstring g_logPath;
static std::mutex   g_logMutex;

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
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)))
        return L"";
    std::wstring dir = roaming;
    CoTaskMemFree(roaming);
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
    std::lock_guard<std::mutex> lock(g_logMutex);

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
