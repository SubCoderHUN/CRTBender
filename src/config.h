// CRTBender - settings persistence.
//
// Everything lives in a single human-readable .cfg file. It is written next to
// the executable when a crtbender.cfg is already there (portable install),
// otherwise in %APPDATA%\CRTBender\crtbender.cfg.
//
// CRT geometry is timing dependent: 1024x768@85 and 1280x960@75 bow differently
// on the same tube. Corrections are therefore stored per display mode, keyed by
// "<width>x<height>@<refresh>", and the matching profile is selected
// automatically whenever the mode changes.
#pragma once

#include "i18n.h"
#include "warpmesh.h"

#include <map>
#include <string>

namespace crtb {

struct Settings {
    bool enabled       = true;   // master on/off for the correction
    bool autostart     = false;  // mirrored into the HKCU Run key
    int  patternMode   = 0;      // 0 = off, 1 = over desktop, 2 = on black
    bool mirror        = true;   // edit left/right symmetrically
    bool freeMove      = false;  // allow horizontal control point movement
    bool hotkeys       = true;   // register the global calibration hotkeys
    Lang language      = Lang::English;
    int  quality       = 2;      // 0 = bilinear, 1 = bicubic, 2 = Lanczos + anti-ringing
    bool flipPresent   = false;  // present_mode: false = bitblt, true = flip
    int  previewGain   = 8;      // editor-only magnification of the deformation
    int  tessellation  = 96;     // interior subdivisions per axis
    int  patternCells  = 8;      // test pattern crosshatch cells per axis
    int  patternOpacityPct = 85;
};

struct Profile {
    WarpMesh mesh;
    float    overscan      = 1.0f;   // 1.0 .. 1.15, uniform zoom
    float    edgeBleedPx   = 0.0f;   // manual bleed width, used when !autoBleed
    bool     autoBleed     = true;   // derive bleed from the largest offset
};

// Describes the mode the primary display is running in right now.
struct DisplayMode {
    int width   = 0;
    int height  = 0;
    int refresh = 0;
    std::string Key() const;               // "1600x1200@85"
    bool Valid() const { return width > 0 && height > 0; }
};

DisplayMode QueryPrimaryDisplayMode();

class Config {
public:
    // Resolves the config path and loads it if present. Missing file is not an
    // error - defaults are used and the file appears on the first Save().
    void Load();
    bool Save() const;

    const std::wstring& Path() const { return path_; }

    Settings&       GetSettings()       { return settings_; }
    const Settings& GetSettings() const { return settings_; }

    // Returns the profile for a mode key, creating a default one if needed.
    Profile& ProfileFor(const std::string& modeKey);
    bool     HasProfile(const std::string& modeKey) const;

    const std::map<std::string, Profile>& Profiles() const { return profiles_; }

private:
    void ResolvePath();

    std::wstring                   path_;
    Settings                       settings_;
    std::map<std::string, Profile> profiles_;
};

// Effective bleed in normalized units for a profile at a given mode. Auto mode
// sizes it from the largest control point offset plus a small margin, so the
// warped image always covers the screen without a black sliver.
float EffectiveEdgeBleed(const Profile& profile, const DisplayMode& mode);

} // namespace crtb
