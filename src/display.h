// CRTBender - monitor enumeration.
//
// Correction is per monitor and per display mode. Two CRTs bow differently, and
// the same tube bows differently at 1600x1200@85 than at 1280x960@75, so a
// profile is keyed by both.
#pragma once

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

struct MonitorInfo {
    // GDI device name, e.g. "\\.\DISPLAY1". This is what DXGI reports as
    // DXGI_OUTPUT_DESC::DeviceName, so it is how the render engine finds its
    // output. It is *not* stable across replugging, which is why it is not the
    // profile key.
    std::wstring deviceName;

    // Derived from the monitor's hardware id, so a profile survives a reboot,
    // a cable swap or a change of which monitor is primary.
    std::string  monitorKey;

    // What to show in the UI, e.g. "DELL P1130 (1600x1200 @ 85 Hz)".
    std::wstring friendlyName;

    RECT rect{};                 // desktop coordinates
    int  width   = 0;
    int  height  = 0;
    int  refresh = 0;
    bool primary = false;

    bool Valid() const { return width > 0 && height > 0; }

    // "<monitorKey>|<W>x<H>@<R>" - the profile key.
    std::string ProfileKey() const;

    // Mode part on its own. Pre-1.2 configs used this as the whole key, for the
    // primary monitor only; those profiles are migrated on first save.
    std::string ModeKey() const;
};

// Every active monitor, primary first. Empty only if enumeration fails outright.
std::vector<MonitorInfo> EnumerateMonitors();

} // namespace crtb
