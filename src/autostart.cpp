#include "autostart.h"
#include "util.h"

namespace crtb {
namespace {

constexpr const wchar_t* kRunKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kValueName = L"CRTBender";

std::wstring CommandLine() {
    // --silent keeps the editor closed on a boot-time launch.
    return L"\"" + ExePath() + L"\" --silent";
}

} // namespace

bool IsAutostartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    wchar_t buf[1024] = {};
    DWORD   size = sizeof(buf);
    DWORD   type = 0;
    const LONG rc = RegQueryValueExW(key, kValueName, nullptr, &type,
                                     reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;

    // Treat a stale path (exe moved) as "not enabled" so the UI offers to fix it.
    return _wcsicmp(buf, CommandLine().c_str()) == 0;
}

bool SetAutostartEnabled(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        LogLine(L"Autostart: cannot open Run key");
        return false;
    }

    LONG rc;
    if (enabled) {
        const std::wstring cmd = CommandLine();
        rc = RegSetValueExW(key, kValueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(cmd.c_str()),
                            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kValueName);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;   // already gone
    }
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS) {
        LogLine(L"Autostart: registry write failed");
        return false;
    }
    LogLine(enabled ? L"Autostart enabled" : L"Autostart disabled");
    return true;
}

} // namespace crtb
