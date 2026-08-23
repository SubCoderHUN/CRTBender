#include "display.h"
#include "util.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>

#ifndef EDD_GET_DEVICE_INTERFACE_NAME
#define EDD_GET_DEVICE_INTERFACE_NAME 0x00000001
#endif

namespace crtb {
namespace {

std::string Fmt(const char* format, int a, int b, int c) {
    char buf[64];
    snprintf(buf, sizeof(buf), format, a, b, c);
    return buf;
}

// "MONITOR\GSM4B85\{4d36e96e-...}\0001" -> "GSM4B85".
// Falls back to the GDI device name so a key always exists.
std::string HardwareIdOf(const std::wstring& deviceName) {
    DISPLAY_DEVICEW monitor{};
    monitor.cb = sizeof(monitor);
    if (EnumDisplayDevicesW(deviceName.c_str(), 0, &monitor, 0)) {
        const std::wstring id = monitor.DeviceID;
        const size_t first = id.find(L'\\');
        if (first != std::wstring::npos) {
            const size_t second = id.find(L'\\', first + 1);
            const std::wstring part = (second == std::wstring::npos)
                ? id.substr(first + 1)
                : id.substr(first + 1, second - first - 1);
            if (!part.empty()) return Narrow(part);
        }
    }

    // "\\.\DISPLAY2" -> "DISPLAY2"
    const size_t slash = deviceName.find_last_of(L'\\');
    return Narrow(slash == std::wstring::npos ? deviceName : deviceName.substr(slash + 1));
}

std::string InstanceKeyOf(const std::wstring& deviceName) {
    DISPLAY_DEVICEW monitor{};
    monitor.cb = sizeof(monitor);
    if (EnumDisplayDevicesW(deviceName.c_str(), 0, &monitor,
                            EDD_GET_DEVICE_INTERFACE_NAME) &&
        monitor.DeviceID[0]) {
        // DeviceID is now the documented monitor interface path, not the
        // legacy display-class registry suffix. Hashing keeps the config key
        // compact while retaining the full instance identity.
        std::uint64_t hash = 1469598103934665603ull;
        for (const wchar_t* ch = monitor.DeviceID; *ch; ++ch) {
            hash ^= static_cast<std::uint16_t>(*ch);
            hash *= 1099511628211ull;
        }
        char suffix[24];
        snprintf(suffix, sizeof(suffix), "%016llX",
                 static_cast<unsigned long long>(hash));
        return HardwareIdOf(deviceName) + "@" + suffix;
    }

    // Generic drivers may expose no instance id. The GDI output name is at
    // least unique within the current desktop topology.
    const size_t slash = deviceName.find_last_of(L'\\');
    return Narrow(slash == std::wstring::npos ? deviceName : deviceName.substr(slash + 1));
}

std::wstring FriendlyNameOf(const std::wstring& deviceName) {
    DISPLAY_DEVICEW monitor{};
    monitor.cb = sizeof(monitor);
    if (EnumDisplayDevicesW(deviceName.c_str(), 0, &monitor, 0) && monitor.DeviceString[0])
        return monitor.DeviceString;
    return deviceName;
}

BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM param) {
    auto* out = reinterpret_cast<std::vector<MonitorInfo>*>(param);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(handle, &info)) return TRUE;

    MonitorInfo mon;
    mon.deviceName = info.szDevice;
    mon.rect       = info.rcMonitor;
    mon.primary    = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    mon.width      = info.rcMonitor.right - info.rcMonitor.left;
    mon.height     = info.rcMonitor.bottom - info.rcMonitor.top;

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(mon.deviceName.c_str(), ENUM_CURRENT_SETTINGS, &mode)) {
        mon.refresh = static_cast<int>(mode.dmDisplayFrequency);
        // Trust the mode for the pixel size: the monitor rect is in virtual
        // desktop units, which are scaled when the app is not DPI aware. We are,
        // but the mode is still the more direct answer.
        if (mode.dmPelsWidth > 0)  mon.width  = static_cast<int>(mode.dmPelsWidth);
        if (mode.dmPelsHeight > 0) mon.height = static_cast<int>(mode.dmPelsHeight);
    }

    mon.monitorKey   = HardwareIdOf(mon.deviceName);
    mon.controlKey   = InstanceKeyOf(mon.deviceName);
    mon.friendlyName = FriendlyNameOf(mon.deviceName);

    out->push_back(std::move(mon));
    return TRUE;
}

} // namespace

std::string MonitorInfo::ModeKey() const {
    return Fmt("%dx%d@%d", width, height, refresh);
}

std::string MonitorInfo::ProfileKey() const {
    return monitorKey + "|" + ModeKey();
}

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
                        reinterpret_cast<LPARAM>(&monitors));

    // Two identical monitors report the same hardware id, which would make them
    // share a profile. Disambiguate by GDI device name, which is at least stable
    // for as long as the layout is.
    std::map<std::string, int> seen;
    std::map<std::string, int> seenControls;
    for (MonitorInfo& mon : monitors) {
        const int index = seen[mon.monitorKey]++;
        if (index > 0) {
            const size_t slash = mon.deviceName.find_last_of(L'\\');
            const std::wstring tail = slash == std::wstring::npos
                ? mon.deviceName : mon.deviceName.substr(slash + 1);
            mon.monitorKey += "#" + Narrow(tail);
        }

        const int controlIndex = seenControls[mon.controlKey]++;
        if (controlIndex > 0) {
            const size_t slash = mon.deviceName.find_last_of(L'\\');
            const std::wstring tail = slash == std::wstring::npos
                ? mon.deviceName : mon.deviceName.substr(slash + 1);
            mon.controlKey += "#" + Narrow(tail);
        }
    }

    // Primary first: it is what the editor selects by default.
    std::stable_sort(monitors.begin(), monitors.end(),
                     [](const MonitorInfo& a, const MonitorInfo& b) {
                         return a.primary && !b.primary;
                     });

    return monitors;
}

} // namespace crtb
