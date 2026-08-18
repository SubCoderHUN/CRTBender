// Checks on config-file value parsing.
//
// This exists because of a real bug. The writer emits inline comments:
//
//     language        = hu      # en or hu
//     mirror          = 1       # edit left/right symmetrically
//     present_mode    = bitblt  # bitblt or flip
//
// and the reader handed the whole remainder to the value parser. Numbers
// survived by accident, but every string and boolean setting compared the full
// text against "hu" / "1" / "flip", never
// matched, and silently fell back to its default. Language, mirror, free_move,
// auto_bypass and present_mode all refused to persist.
//
//   g++ -std=c++17 -I../src -o config_tests test_config.cpp && ./config_tests
#include "cfgvalue.h"
#include "config.h"

#include <cstdio>
#include <string>

using namespace crtb;

namespace {

int g_failures = 0;

void CheckStr(const std::string& got, const std::string& want, const char* what) {
    const bool ok = got == want;
    if (!ok) std::printf("  FAIL: %s  (\"%s\" vs \"%s\")\n", what, got.c_str(), want.c_str());
    else     std::printf("  ok  : %s\n", what);
    if (!ok) ++g_failures;
}

void CheckBool(bool got, bool want, const char* what) {
    const bool ok = got == want;
    if (!ok) std::printf("  FAIL: %s  (%d vs %d)\n", what, got, want);
    else     std::printf("  ok  : %s\n", what);
    if (!ok) ++g_failures;
}

void CheckInt(int got, int want, const char* what) {
    const bool ok = got == want;
    if (!ok) std::printf("  FAIL: %s  (%d vs %d)\n", what, got, want);
    else     std::printf("  ok  : %s\n", what);
    if (!ok) ++g_failures;
}

} // namespace

int main() {
    std::printf("== inline comments are stripped ==\n");
    CheckStr(ConfigValue("hu      # en or hu"), "hu", "a hash comment goes");
    CheckStr(ConfigValue("bitblt  ; bitblt or flip"), "bitblt", "a semicolon comment goes too");
    CheckStr(ConfigValue("  auto  "), "auto", "surrounding whitespace goes");
    CheckStr(ConfigValue("en"), "en", "a bare value is untouched");
    CheckStr(ConfigValue("# nothing but a comment"), "", "a comment-only value is empty");
    CheckStr(ConfigValue(""), "", "an empty value stays empty");
    CheckStr(ConfigValue("   \t  "), "", "whitespace only is empty");

    std::printf("\n== booleans survive a trailing comment ==\n");
    // This is the exact shape the writer produces.
    CheckBool(ParseConfigBool("1       # edit left/right symmetrically", false), true,
              "1 with a comment reads as true");
    CheckBool(ParseConfigBool("0       # allow horizontal movement", true), false,
              "0 with a comment reads as false");
    CheckBool(ParseConfigBool("true", false), true, "true");
    CheckBool(ParseConfigBool("off", true), false, "off");
    CheckBool(ParseConfigBool("nonsense", true), true, "garbage keeps the fallback");
    CheckBool(ParseConfigBool("", false), false, "empty keeps the fallback");

    std::printf("\n== numbers ==\n");
    CheckInt(ParseConfigInt("15      # grid size", 0), 15, "int with a comment");
    CheckInt(ParseConfigInt("  -3 ", 0), -3, "negative int");
    CheckInt(ParseConfigInt("oops", 7), 7, "garbage keeps the fallback");
    CheckInt(ParseConfigInt("12x", 7), 7, "trailing garbage keeps the fallback");
    CheckInt(ParseConfigInt("", 7), 7, "empty keeps the fallback");

    std::printf("\n== test pattern indices ==\n");
    CheckInt(static_cast<int>(TestPatternFromIndex(0)),
             static_cast<int>(TestPattern::GeometryGrid), "first pattern is the geometry grid");
    CheckInt(static_cast<int>(TestPatternFromIndex(5)),
             static_cast<int>(TestPattern::Overscan), "last pattern is the overscan guide");
    CheckInt(static_cast<int>(TestPatternFromIndex(99)),
             static_cast<int>(TestPattern::Overscan), "large indices clamp to the last pattern");

    {
        const float got = ParseConfigFloat("+0.004250  # topbow", 0.0f);
        const bool ok = got > 0.00424f && got < 0.00426f;
        std::printf("  %s: float with a comment (%.6f)\n", ok ? "ok  " : "FAIL", got);
        if (!ok) ++g_failures;
    }
    {
        const float got = ParseConfigFloat("bad", 1.5f);
        const bool ok = got == 1.5f;
        std::printf("  %s: garbage float keeps the fallback\n", ok ? "ok  " : "FAIL");
        if (!ok) ++g_failures;
    }
    {
        const float got = ParseConfigFloat("1e100", 1.5f);
        const bool ok = got == 1.5f;
        std::printf("  %s: out-of-range float keeps the fallback\n", ok ? "ok  " : "FAIL");
        if (!ok) ++g_failures;
    }

    std::printf("\n== the settings that actually broke ==\n");
    // Written form -> what the reader must conclude.
    CheckStr(ConfigValue("hu      # en vagy hu"), "hu", "language survives (Hungarian)");
    CheckStr(ConfigValue("en      # en or hu"), "en", "language survives (English)");
    CheckStr(ConfigValue("flip    # bitblt or flip"), "flip", "present_mode survives");
    CheckBool(ParseConfigBool("1       # step aside for protected video", false), true,
              "auto_bypass survives");
    CheckStr(ConfigValue("auto    # derived from the largest offset"), "auto",
             "the bleed keyword survives");

    std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
