// Checks on the translated string tables.
//
// These exist because of a real bug: every "%s" in the tables was silently
// truncating its argument to one character. In a *wide* printf, MSVCRT (and the
// C standard) read %s as char*, while MSVC's own CRT reads it as wchar_t*. Since
// L"SubCoderHUN" starts with 'S' followed by a zero byte, "Created by: %s"
// printed "Created by: S". Every string argument in this program is wide, so
// "%ls" is the only spelling that is correct on both toolchains.
//
// Build:
//   g++ -std=c++17 -I../src -o i18n_tests test_i18n.cpp ../src/i18n.cpp && ./i18n_tests
#include "i18n.h"

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

using namespace crtb;

namespace {

int g_failures = 0;

void Fail(const char* what, int id, const std::string& detail) {
    std::printf("  FAIL [%3d] %s: %s\n", id, what, detail.c_str());
    ++g_failures;
}

std::string Narrow(const wchar_t* s) {
    std::string out;
    for (; *s; ++s) out += (*s < 128) ? static_cast<char>(*s) : '?';
    return out;
}

// Pulls the conversion specifications out of a format string, normalized to
// their length modifier + conversion character (e.g. "ls", "d", ".2f").
std::vector<std::string> Specifiers(const wchar_t* fmt, std::string& error) {
    std::vector<std::string> specs;
    for (const wchar_t* p = fmt; *p; ++p) {
        if (*p != L'%') continue;
        ++p;
        if (*p == L'%') continue;               // escaped percent

        std::string spec;
        while (*p == L'-' || *p == L'+' || *p == L' ' || *p == L'#' || *p == L'0') ++p;
        while (*p >= L'0' && *p <= L'9') ++p;   // width
        if (*p == L'.') { spec += '.'; ++p; while (*p >= L'0' && *p <= L'9') { spec += static_cast<char>(*p); ++p; } }
        while (*p == L'l' || *p == L'h') { spec += static_cast<char>(*p); ++p; }

        if (*p == L'\0') { error = "format ends mid-specifier"; return specs; }
        spec += static_cast<char>(*p);
        specs.push_back(spec);
    }
    return specs;
}

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) { if (!out.empty()) out += " "; out += "%" + s; }
    return out.empty() ? "(none)" : out;
}

} // namespace

int main() {
    const int count = static_cast<int>(Str::Count);
    std::printf("Checking %d strings x 2 languages\n\n", count);

    std::printf("== every entry is present ==\n");
    for (int i = 0; i < count; ++i) {
        const Str id = static_cast<Str>(i);
        if (T(Lang::English, id)[0] == L'\0')   Fail("English entry is empty", i, "");
        if (T(Lang::Hungarian, id)[0] == L'\0') Fail("Hungarian entry is empty", i, "");
    }
    std::printf("  %d entries populated in both languages\n", count);

    std::printf("\n== no bare %%s (it means char* in a wide printf) ==\n");
    int stringArgs = 0;
    for (int i = 0; i < count; ++i) {
        for (Lang lang : { Lang::English, Lang::Hungarian }) {
            std::string error;
            for (const std::string& spec : Specifiers(T(lang, static_cast<Str>(i)), error)) {
                if (spec == "s")
                    Fail("bare %s would truncate a wide argument", i, Narrow(T(lang, static_cast<Str>(i))));
                else if (spec == "ls")
                    ++stringArgs;
            }
            if (!error.empty()) Fail("malformed format", i, error);
        }
    }
    std::printf("  %d wide string arguments, all spelled %%ls\n", stringArgs);

    std::printf("\n== the two languages agree on their format arguments ==\n");
    for (int i = 0; i < count; ++i) {
        std::string errEn, errHu;
        const auto en = Specifiers(T(Lang::English, static_cast<Str>(i)), errEn);
        const auto hu = Specifiers(T(Lang::Hungarian, static_cast<Str>(i)), errHu);
        if (en != hu) {
            Fail("argument lists differ, so one language would read the wrong stack", i,
                 "en: " + Join(en) + "   hu: " + Join(hu));
        }
    }
    std::printf("  all %d entries take the same arguments in both languages\n", count);

    std::printf("\n== language tags round-trip ==\n");
    for (Lang lang : { Lang::English, Lang::Hungarian }) {
        SetLanguage(lang);
        if (GetLanguage() != lang) Fail("SetLanguage/GetLanguage mismatch", -1, "");
        char tag[8] = {};
        for (int i = 0; i < 4 && LanguageTag(lang)[i]; ++i) tag[i] = static_cast<char>(LanguageTag(lang)[i]);
        if (LanguageFromTag(tag) != lang) Fail("LanguageFromTag round-trip failed", -1, tag);
        if (T8(Str::CfgLanguage)[0] == '\0') Fail("UTF-8 mirror is empty", -1, tag);
    }
    std::printf("  en/hu tags and the UTF-8 mirror behave\n");

    // End-to-end: glibc's swprintf follows the same rule as MSVCRT (%s is
    // char*, %ls is wchar_t*), so formatting here reproduces exactly what
    // Windows would print. This is the check that would have caught the bug.
    std::printf("\n== formatted output keeps whole strings ==\n");
    {
        wchar_t buf[512];

        for (Lang lang : { Lang::English, Lang::Hungarian }) {
            std::swprintf(buf, 512, T(lang, Str::EdCredit), kVersion, kAuthor);
            const std::wstring got = buf;
            if (got.find(kAuthor) == std::wstring::npos)
                Fail("credit line lost the author name", -1, Narrow(buf));
            if (got.find(kVersion) == std::wstring::npos)
                Fail("credit line lost the version", -1, Narrow(buf));
            std::printf("  credit: \"%s\"\n", Narrow(buf).c_str());
        }

        // The grid combo label, the other place the user saw truncation.
        for (Lang lang : { Lang::English, Lang::Hungarian }) {
            const wchar_t* note = T(lang, Str::EdGridRecommended);
            std::swprintf(buf, 512, L"%d x %d  (%ls)", 15, 15, note);
            if (std::wstring(buf).find(note) == std::wstring::npos)
                Fail("grid label lost the recommended note", -1, Narrow(buf));
            std::printf("  grid:   \"%s\"\n", Narrow(buf).c_str());
        }

        // Tray tooltip and About body carry wide arguments too.
        std::swprintf(buf, 512, T(Lang::Hungarian, Str::TrayTipEnabled), L"1600x1200@85");
        if (std::wstring(buf).find(L"1600x1200@85") == std::wstring::npos)
            Fail("tray tooltip lost the display mode", -1, Narrow(buf));

        std::swprintf(buf, 512, T(Lang::Hungarian, Str::MsgAboutBody),
                      kVersion, kAuthor, kProjectUrl);
        if (std::wstring(buf).find(kProjectUrl) == std::wstring::npos)
            Fail("about box lost the project URL", -1, Narrow(buf));
        std::printf("  about box keeps the version, author and URL\n");
    }

    // The default has to be English, and an unknown tag must fall back to it.
    if (LanguageFromTag("de") != Lang::English) Fail("unknown tag should fall back to English", -1, "de");
    if (LanguageFromTag(nullptr) != Lang::English) Fail("null tag should fall back to English", -1, "null");

    std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
