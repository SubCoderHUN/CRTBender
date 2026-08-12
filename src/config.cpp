#include "config.h"
#include "cfgvalue.h"
#include "i18n.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace crtb {
namespace {

constexpr const char* kFileName    = "crtbender.cfg";
constexpr const wchar_t* kFileNameW = L"crtbender.cfg";

bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 8 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = out.empty() ? TRUE
                                : ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(read);

    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);
    return true;
}

bool WriteWholeFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const char bom[3] = { '\xEF', '\xBB', '\xBF' };
    DWORD written = 0;
    BOOL ok = WriteFile(h, bom, 3, &written, nullptr);
    if (ok && !data.empty())
        ok = WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(h);
    return ok != FALSE;
}

// Thin aliases; the real work (including stripping inline comments) lives in
// cfgvalue.h so it can be unit tested without Windows.
bool  ParseBool (const std::string& v, bool fallback)  { return ParseConfigBool(v, fallback); }
int   ParseInt  (const std::string& v, int fallback)   { return ParseConfigInt(v, fallback); }
float ParseFloat(const std::string& v, float fallback) { return ParseConfigFloat(v, fallback); }

std::string Fmt(const char* format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    return buf;
}

// Named tables so a parameter is declared once and both read and written.
struct GeometryField { const char* name; float GeometryParams::* member; };
constexpr GeometryField kGeometryFields[] = {
    { "hposition",     &GeometryParams::hPosition },
    { "vposition",     &GeometryParams::vPosition },
    { "hsize",         &GeometryParams::hSize },
    { "vsize",         &GeometryParams::vSize },
    { "rotation",      &GeometryParams::rotation },
    { "parallelogram", &GeometryParams::parallelogram },
    { "trapezoid",     &GeometryParams::trapezoid },
    { "pincushion",    &GeometryParams::pincushion },
    { "pinbalance",    &GeometryParams::pinBalance },
    { "hlinearity",    &GeometryParams::hLinearity },
    { "vlinearity",    &GeometryParams::vLinearity },
    { "topbow",        &GeometryParams::topBow },
    { "bottombow",     &GeometryParams::bottomBow },
};

struct ConvergenceField { const char* name; float ConvergenceParams::* member; };
constexpr ConvergenceField kConvergenceFields[] = {
    { "rh",     &ConvergenceParams::rH },
    { "rv",     &ConvergenceParams::rV },
    { "rhedge", &ConvergenceParams::rHEdge },
    { "rvedge", &ConvergenceParams::rVEdge },
    { "bh",     &ConvergenceParams::bH },
    { "bv",     &ConvergenceParams::bV },
    { "bhedge", &ConvergenceParams::bHEdge },
    { "bvedge", &ConvergenceParams::bVEdge },
};

} // namespace

bool Profile::Touched() const {
    // Compared against the defaults rather than against fixed literals, so that
    // changing a default cannot silently make every profile look "touched".
    static const Profile kDefault;
    if (mesh.AnyOffset() || geometry.Any() || convergence.Any()) return true;
    if (std::fabs(overscan - kDefault.overscan) > 1e-4f) return true;
    if (autoBleed != kDefault.autoBleed) return true;
    if (std::fabs(edgeBleedPx - kDefault.edgeBleedPx) > 1e-4f) return true;
    // Row locks count as work: locking the bands that already look right is the
    // first calibration step, and losing it would be infuriating.
    for (int r = 0; r < mesh.Size(); ++r)
        if (mesh.RowLocked(r)) return true;
    return false;
}

void Config::ResolvePath() {
    // Portable install wins: if a config sits next to the exe, use that one.
    const std::wstring local = ExeDir() + L"\\" + kFileNameW;
    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
        path_ = local;
        return;
    }
    const std::wstring dir = AppDataDir();
    path_ = dir.empty() ? local : dir + L"\\" + kFileNameW;
}

bool Config::HasProfile(const std::string& key) const {
    return profiles_.find(key) != profiles_.end();
}

Profile& Config::ProfileFor(const std::string& key) {
    auto it = profiles_.find(key);
    if (it != profiles_.end()) return it->second;
    return profiles_.emplace(key, Profile{}).first->second;
}

bool Config::AdoptLegacyProfile(const std::string& modeKey, const std::string& profileKey) {
    if (modeKey == profileKey) return false;

    auto legacy = profiles_.find(modeKey);
    if (legacy == profiles_.end() || !legacy->second.Touched()) return false;

    auto existing = profiles_.find(profileKey);
    if (existing != profiles_.end() && existing->second.Touched()) return false;

    profiles_[profileKey] = legacy->second;
    profiles_.erase(legacy);
    LogLine(L"Migrated legacy profile " + Widen(modeKey) + L" to " + Widen(profileKey));
    return true;
}

void Config::Load() {
    ResolvePath();

    std::string text;
    if (!ReadWholeFile(path_, text)) {
        LogLine(L"No config found, using defaults: " + path_);
        return;
    }

    std::istringstream in(text);
    std::string line;
    std::string section;
    Profile*    profile = nullptr;

    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = Trim(line.substr(1, line.size() - 2));
            profile = nullptr;
            if (section.rfind("profile:", 0) == 0) {
                const std::string key = Trim(section.substr(8));
                if (!key.empty()) profile = &ProfileFor(key);
            }
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string val = Trim(line.substr(eq + 1));

        if (section == "general") {
            if      (key == "enabled")         settings_.enabled       = ParseBool(val, settings_.enabled);
            else if (key == "autostart")       settings_.autostart     = ParseBool(val, settings_.autostart);
            else if (key == "pattern_mode")    settings_.patternMode   = std::clamp(ParseInt(val, 0), 0, 2);
            else if (key == "mirror")          settings_.mirror        = ParseBool(val, settings_.mirror);
            else if (key == "free_move")       settings_.freeMove      = ParseBool(val, settings_.freeMove);
            else if (key == "hotkeys")         settings_.hotkeys       = ParseBool(val, settings_.hotkeys);
            else if (key == "auto_bypass")     settings_.autoBypass    = ParseBool(val, settings_.autoBypass);
            // "bicubic" is the pre-1.1 spelling; keep reading it so an existing
            // config does not silently lose the setting.
            else if (key == "bicubic")         settings_.quality       = ParseBool(val, true) ? 1 : 0;
            else if (key == "quality")         settings_.quality       = std::clamp(ParseInt(val, 2), 0, 2);
            else if (key == "language")        settings_.language      = LanguageFromTag(ConfigValue(val).c_str());
            else if (key == "present_mode")    settings_.flipPresent   = (ConfigValue(val) == "flip");
            else if (key == "preview_gain")    settings_.previewGain   = std::clamp(ParseInt(val, 8), 1, 24);
            else if (key == "tessellation")    settings_.tessellation  = std::clamp(ParseInt(val, 96), 16, 256);
            else if (key == "pattern_cells")   settings_.patternCells  = std::clamp(ParseInt(val, 8), 2, 64);
            else if (key == "pattern_opacity") settings_.patternOpacityPct = std::clamp(ParseInt(val, 85), 5, 100);
            continue;
        }

        if (!profile) continue;

        if (key.rfind("geo.", 0) == 0) {
            const std::string name = key.substr(4);
            for (const GeometryField& field : kGeometryFields) {
                if (name == field.name) {
                    profile->geometry.*(field.member) =
                        std::clamp(ParseFloat(val, 0.0f), -0.5f, 0.5f);
                    break;
                }
            }
        } else if (key.rfind("conv.", 0) == 0) {
            const std::string name = key.substr(5);
            for (const ConvergenceField& field : kConvergenceFields) {
                if (name == field.name) {
                    profile->convergence.*(field.member) =
                        std::clamp(ParseFloat(val, 0.0f), -0.05f, 0.05f);
                    break;
                }
            }
        } else if (key == "overscan") {
            profile->overscan = std::clamp(ParseFloat(val, 1.0f), 1.0f, 1.25f);
        } else if (key == "bleed") {
            if (ConfigValue(val) == "auto") {
                profile->autoBleed = true;
            } else {
                profile->autoBleed   = false;
                profile->edgeBleedPx = std::clamp(ParseFloat(val, 0.0f), 0.0f, 128.0f);
            }
        } else if (key == "grid") {
            profile->mesh.Resize(ParseInt(val, WarpMesh::kDefaultSize));
        } else if (key == "locked") {
            std::istringstream ls(ConfigValue(val));
            std::string tok;
            while (std::getline(ls, tok, ',')) {
                const std::string t = Trim(tok);
                if (!t.empty()) profile->mesh.SetRowLocked(ParseInt(t, -1), true);
            }
        } else if (key.rfind("row.", 0) == 0) {
            const int row = ParseInt(key.substr(4), -1);
            if (row < 0 || row >= profile->mesh.Size()) continue;
            std::istringstream vs(val);
            std::string tok;
            int col = 0;
            while (vs >> tok && col < profile->mesh.Size()) {
                const size_t comma = tok.find(',');
                if (comma == std::string::npos) { ++col; continue; }
                Offset& o = profile->mesh.At(row, col);
                o.dx = ParseFloat(tok.substr(0, comma), 0.0f);
                o.dy = ParseFloat(tok.substr(comma + 1), 0.0f);
                ++col;
            }
        }
    }

    LogLine(L"Config loaded: " + path_);
}

bool Config::Save() const {
    if (path_.empty()) return false;

    std::string out;
    // Header comment, in whichever language the UI is set to.
    out += "# ";
    for (const char* ch = T8(Str::CfgHeader); *ch; ++ch) {
        out += *ch;
        if (*ch == '\n') out += "# ";
    }
    out += "\n\n";

    char langTag[8] = {};
    for (int i = 0; i < 4 && LanguageTag(settings_.language)[i]; ++i)
        langTag[i] = static_cast<char>(LanguageTag(settings_.language)[i]);

    out += "[general]\n";
    out += Fmt("language        = %-6s  # %s\n", langTag, T8(Str::CfgLanguage));
    out += Fmt("enabled         = %d\n", settings_.enabled ? 1 : 0);
    out += Fmt("autostart       = %d\n", settings_.autostart ? 1 : 0);
    out += Fmt("pattern_mode    = %d       # %s\n", settings_.patternMode, T8(Str::CfgPatternMode));
    out += Fmt("pattern_cells   = %d\n", settings_.patternCells);
    out += Fmt("pattern_opacity = %d\n", settings_.patternOpacityPct);
    out += Fmt("mirror          = %d       # %s\n", settings_.mirror ? 1 : 0, T8(Str::CfgMirror));
    out += Fmt("free_move       = %d       # %s\n", settings_.freeMove ? 1 : 0, T8(Str::CfgFreeMove));
    out += Fmt("hotkeys         = %d\n", settings_.hotkeys ? 1 : 0);
    out += Fmt("auto_bypass     = %d       # %s\n", settings_.autoBypass ? 1 : 0,
               T8(Str::CfgAutoBypass));
    out += Fmt("quality         = %d       # %s\n", settings_.quality, T8(Str::CfgQuality));
    out += Fmt("present_mode    = %-6s  # %s\n",
               settings_.flipPresent ? "flip" : "bitblt", T8(Str::CfgPresentMode));
    out += Fmt("preview_gain    = %d       # %s\n", settings_.previewGain, T8(Str::CfgPreviewGain));
    out += Fmt("tessellation    = %d\n", settings_.tessellation);

    for (const auto& [key, profile] : profiles_) {
        const WarpMesh& mesh = profile.mesh;

        // Skip untouched auto-created profiles so the file stays readable.
        if (!profile.Touched()) continue;

        out += Fmt("\n[profile:%s]\n", key.c_str());
        out += Fmt("grid     = %d\n", mesh.Size());
        out += Fmt("overscan = %.4f\n", profile.overscan);
        if (profile.autoBleed) out += "bleed    = auto\n";
        else                   out += Fmt("bleed    = %.2f\n", profile.edgeBleedPx);

        std::string locked;
        for (int r = 0; r < mesh.Size(); ++r) {
            if (!mesh.RowLocked(r)) continue;
            if (!locked.empty()) locked += ",";
            locked += std::to_string(r);
        }
        if (!locked.empty()) out += Fmt("locked   = %s\n", locked.c_str());

        if (profile.geometry.Any()) {
            out += Fmt("# %s\n", T8(Str::CfgGeometry));
            for (const GeometryField& field : kGeometryFields) {
                const float value = profile.geometry.*(field.member);
                if (std::fabs(value) > 1e-7f)
                    out += Fmt("geo.%-14s = %+.6f\n", field.name, value);
            }
        }

        if (profile.convergence.Any()) {
            out += Fmt("# %s\n", T8(Str::CfgConvergence));
            for (const ConvergenceField& field : kConvergenceFields) {
                const float value = profile.convergence.*(field.member);
                if (std::fabs(value) > 1e-7f)
                    out += Fmt("conv.%-13s = %+.6f\n", field.name, value);
            }
        }

        if (!mesh.AnyOffset()) continue;

        out += Fmt("# %s\n", T8(Str::CfgProfilePoints));
        for (int r = 0; r < mesh.Size(); ++r) {
            out += Fmt("row.%d    =", r);
            for (int c = 0; c < mesh.Size(); ++c) {
                const Offset& o = mesh.At(r, c);
                out += Fmt(" %+.5f,%+.5f", o.dx, o.dy);
            }
            out += "\n";
        }
    }

    // Normalize to CRLF so Notepad shows the file correctly.
    std::string crlf;
    crlf.reserve(out.size() + out.size() / 8);
    for (char ch : out) {
        if (ch == '\n') crlf += '\r';
        crlf += ch;
    }

    if (!WriteWholeFile(path_, crlf)) {
        LogLine(L"Config save FAILED: " + path_);
        return false;
    }
    return true;
}

float EffectiveEdgeBleed(const Profile& profile, int screenWidth, int screenHeight) {
    const float height = screenHeight > 0 ? static_cast<float>(screenHeight) : 1080.0f;
    if (!profile.autoBleed) return profile.edgeBleedPx / height;

    const float aspect = (screenWidth > 0 && screenHeight > 0)
        ? static_cast<float>(screenWidth) / static_cast<float>(screenHeight)
        : 4.0f / 3.0f;

    float meshDx = 0.0f, meshDy = 0.0f;
    profile.mesh.MaxMagnitude(meshDx, meshDy);

    float geoDx = 0.0f, geoDy = 0.0f;
    profile.geometry.MaxMagnitude(aspect, geoDx, geoDy);

    float convDx = 0.0f, convDy = 0.0f;
    profile.convergence.MaxMagnitude(convDx, convDy);

    // Worst case the layers stack, so add rather than take the maximum.
    const float needed = std::max(meshDx + geoDx + convDx, meshDy + geoDy + convDy);

    // Overscan already pulls the image outwards by (overscan-1)/2 per side.
    const float fromZoom = std::max(0.0f, (profile.overscan - 1.0f) * 0.5f);
    return std::max(0.0f, needed - fromZoom) + 0.002f;
}

} // namespace crtb
