// CRTBender - settings persistence.
//
// Everything lives in a single human-readable .cfg file. It is written next to
// the executable when a crtbender.cfg is already there (portable install),
// otherwise in %APPDATA%\CRTBender\crtbender.cfg.
//
// CRT geometry depends on both the tube and the timing: two monitors bow
// differently, and the same tube bows differently at 1600x1200@85 than at
// 1280x960@75. Corrections are therefore stored per monitor *and* per mode,
// keyed by "<monitor>|<width>x<height>@<refresh>", and the matching profile is
// selected automatically.
#pragma once

#include "geometry.h"
#include "i18n.h"
#include "warpmesh.h"

#include <map>
#include <string>

namespace crtb {

enum class TestPattern {
    GeometryGrid = 0,
    ColourBars,
    Greyscale,
    Convergence,
    Sharpness,
    Overscan,
    Count,
};

constexpr int kTestPatternCount = static_cast<int>(TestPattern::Count);

inline TestPattern TestPatternFromIndex(int index) {
    if (index < 0) index = 0;
    if (index >= kTestPatternCount) index = kTestPatternCount - 1;
    return static_cast<TestPattern>(index);
}

struct Settings {
    bool enabled       = true;   // master on/off for the correction
    bool autostart     = false;  // mirrored into the HKCU Run key
    int  patternMode   = 0;      // 0 = off, 1 = over desktop, 2 = on black
    TestPattern patternType = TestPattern::GeometryGrid;
    bool mirror        = true;   // edit left/right symmetrically
    bool freeMove      = false;  // allow horizontal control point movement
    bool hotkeys       = true;   // register the global calibration hotkeys
    bool autoBypass    = true;   // step aside for protected video and exclusive fullscreen
    Lang language      = Lang::English;
    int  quality       = 2;      // 0 = bilinear, 1 = bicubic, 2 = adaptive sharp
    int  sharpnessPct  = 40;     // adaptive sharpening used by quality mode 2
    bool flipPresent   = false;  // present_mode: false = bitblt, true = flip
    int  previewGain   = 8;      // editor-only magnification of the deformation
    int  tessellation  = 96;     // interior subdivisions per axis
    int  patternCells  = 8;      // test pattern crosshatch cells per axis
    int  patternOpacityPct = 85;
};

struct Profile {
    // Broad shape: rotation, pincushion, trapezoid and friends.
    GeometryParams    geometry;
    // Per-point corrections, applied on top of the parametric layer.
    WarpMesh          mesh;
    // Red and blue beam alignment relative to green.
    ConvergenceParams convergence;

    float overscan    = 1.0f;    // 1.0 .. 1.15, uniform zoom
    float edgeBleedPx = 0.0f;    // manual bleed width, used when !autoBleed
    bool  autoBleed   = false;   // off by default: the smear is more visible on a
                                 // CRT than the sliver of black it hides

    // Whether this profile holds anything worth writing to disk.
    bool Touched() const;
};

class Config {
public:
    // Resolves the config path and loads it if present. A missing file is not an
    // error - defaults are used and the file appears on the first Save().
    void Load();
    bool Save() const;

    const std::wstring& Path() const { return path_; }

    Settings&       GetSettings()       { return settings_; }
    const Settings& GetSettings() const { return settings_; }

    // Returns the profile for a key, creating a default one if needed.
    Profile& ProfileFor(const std::string& key);
    bool     HasProfile(const std::string& key) const;

    // Pre-1.2 configs keyed profiles by display mode alone, with no monitor
    // part, and only ever described the primary monitor. Adopts such a profile
    // under the new key when nothing better exists. Returns true if it did.
    bool AdoptLegacyProfile(const std::string& modeKey, const std::string& profileKey);

    const std::map<std::string, Profile>& Profiles() const { return profiles_; }

private:
    void ResolvePath();

    std::wstring                   path_;
    Settings                       settings_;
    std::map<std::string, Profile> profiles_;
};

// Effective bleed in normalized units. Auto mode sizes it from the largest
// displacement the whole profile can produce - parametric layer, lattice and
// convergence together - plus a small margin, so the warped image always covers
// the screen without a black sliver at the edge.
float EffectiveEdgeBleed(const Profile& profile, int screenWidth, int screenHeight);

} // namespace crtb
