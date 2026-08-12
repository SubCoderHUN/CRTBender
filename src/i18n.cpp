#include "i18n.h"

#include <cstring>

namespace crtb {
namespace {

Lang g_lang = Lang::English;   // default, per the project's own default

const wchar_t* const kEnglish[static_cast<int>(Str::Count)] = {
    // Tray menu
    L"Open calibration...\tCtrl+Alt+E",
    L"Correction enabled\tCtrl+Alt+B",
    L"Test pattern  (Esc to exit)",
    L"Off",
    L"Over the desktop",
    L"On a black background",
    L"Start with Windows",
    L"Show settings file",
    L"Open log",
    L"Language",
    L"Project page (GitHub)",
    L"About CRTBender...",
    L"Exit",
    L"CRTBender - correction on\n%ls",
    L"CRTBender - correction off\n%ls",

    // Editor window
    L"CRTBender - geometry calibration",
    L"Correction enabled  (Ctrl+Alt+B)",
    L"Test pattern  (Ctrl+Alt+G, exit: Esc)",
    L"Grid size",
    L"recommended",
    L"Mirror left / right",
    L"Allow horizontal movement too",
    L"Editor magnification:  %d x  (preview only)",
    L"Overscan (zoom):  %.0f %%",
    L"Above 100 %% the whole picture is rescaled, which softens all of it. "
    L"Leave it at 100 %% unless you get black edges.",
    L"Automatic edge fill",
    L"Edge fill:  %.0f px",
    L"Resampling quality",
    L"Bilinear (fastest, softest)",
    L"Bicubic (balanced)",
    L"Sharp - Lanczos + anti-ringing",
    L"Start with Windows",
    L"Language",
    L"Undo",
    L"Reset row",
    L"Reset all",
    L"Save",
    L"GitHub",
    L"top of the screen",
    L"bottom of the screen",
    L"Drag the points  |  Arrows: fine tuning (Shift = 1 px, Ctrl = 0.05 px)  |  "
    L"Double-click a point to reset it  |  Ctrl+Z: undo  |  Padlock: lock a row  |  "
    L"Esc: test pattern off",
    L"CRTBender %ls  -  by %ls",

    // Editor status panel
    L"Active profile:  %ls\r\nGrid:  %d x %d\r\n\r\n",
    L"Selected point:  row %d / column %d%ls\r\n"
    L"   vertical:    %+.2f px\r\n"
    L"   horizontal:  %+.2f px\r\n\r\n",
    L"   [ROW LOCKED]",
    L"No point selected.\r\nClick a grid point to select it.\r\n\r\n",
    L"WARNING:\r\n",
    L"Undo steps: %d",

    // Dialogs
    L"Could not change the start-with-Windows setting. See the log file for details.",
    L"About CRTBender",
    L"CRTBender %ls\r\n"
    L"Software geometry correction for CRT monitors.\r\n\r\n"
    L"Author: %ls\r\n"
    L"%ls\r\n\r\n"
    L"Open the project page in your browser?",

    // Engine errors
    L"The overlay window could not be created, or the system does not support "
    L"the WDA_EXCLUDEFROMCAPTURE exclusion (Windows 10 2004 or newer is required). "
    L"The correction will not start, as a safety measure.",
    L"The graphics pipeline could not be initialized. See the log file for details.",
    L"Screen capture (Desktop Duplication) is currently unavailable. Retrying.",
    L"Screen capture returns an empty, black image, so the correction is not being "
    L"displayed (this keeps your desktop visible). See the log file for details.",

    // Config file comments
    L"CRTBender configuration\n"
    L"This file can be edited by hand; the program reads it on startup.\n"
    L"Offsets are normalized: a fraction of the screen width / height.\n"
    L"Positive dy pushes the picture down, positive dx pushes it right.",
    L"0=off, 1=over the desktop, 2=on a black background",
    L"edit left/right symmetrically",
    L"allow horizontal movement",
    L"0=bilinear, 1=bicubic, 2=sharp (Lanczos + anti-ringing)",
    L"magnifies the editor preview only",
    L"bitblt or flip; bitblt is what keeps the overlay click-through",
    L"en or hu",
    L"control point offsets (dx,dy), row 0 is the top of the screen",
};

const wchar_t* const kHungarian[static_cast<int>(Str::Count)] = {
    // Tray menu
    L"Kalibráció megnyitása...\tCtrl+Alt+E",
    L"Korrekció bekapcsolva\tCtrl+Alt+B",
    L"Tesztminta  (Esc = kilépés)",
    L"Kikapcsolva",
    L"Az asztal fölött",
    L"Fekete háttéren",
    L"Indulás a Windowsszal",
    L"Beállításfájl megmutatása",
    L"Napló megnyitása",
    L"Nyelv",
    L"Projekt oldala (GitHub)",
    L"A CRTBender névjegye...",
    L"Kilépés",
    L"CRTBender - korrekció bekapcsolva\n%ls",
    L"CRTBender - korrekció kikapcsolva\n%ls",

    // Editor window
    L"CRTBender - geometria kalibrálás",
    L"Korrekció bekapcsolva  (Ctrl+Alt+B)",
    L"Tesztminta  (Ctrl+Alt+G, kilépés: Esc)",
    L"Rács mérete",
    L"ajánlott",
    L"Bal / jobb tükrözés",
    L"Vízszintes mozgatás is",
    L"Szerkesztő nagyítás:  %d x  (csak az előnézet)",
    L"Overscan (nagyítás):  %.0f %%",
    L"100 %% fölött az egész kép átméreteződik, ami mindenhol lágyítja. "
    L"Hagyd 100 %%-on, hacsak nem jelenik meg fekete csík a széleken.",
    L"Automatikus szélkitöltés",
    L"Szélkitöltés:  %.0f px",
    L"Újramintavételezés minősége",
    L"Bilineáris (leggyorsabb, leglágyabb)",
    L"Bikubikus (kiegyensúlyozott)",
    L"Éles - Lanczos + gyűrűzésgátlás",
    L"Indulás a Windowsszal",
    L"Nyelv",
    L"Visszavonás",
    L"Sor nullázása",
    L"Mind nullázása",
    L"Mentés",
    L"GitHub",
    L"a képernyő teteje",
    L"a képernyő alja",
    L"Húzd a pontokat  |  Nyilak: finomhangolás (Shift = 1 px, Ctrl = 0,05 px)  |  "
    L"Dupla kattintás: a pont nullázása  |  Ctrl+Z: visszavonás  |  Lakat: sor zárolása  |  "
    L"Esc: tesztminta ki",
    L"CRTBender %ls  -  készítette: %ls",

    // Editor status panel
    L"Aktív profil:  %ls\r\nRács:  %d x %d\r\n\r\n",
    L"Kijelölt pont:  %d. sor / %d. oszlop%ls\r\n"
    L"   függőleges:  %+.2f px\r\n"
    L"   vízszintes:  %+.2f px\r\n\r\n",
    L"   [A SOR ZÁROLVA]",
    L"Nincs kijelölt pont.\r\nKattints egy rácspontra a kijelöléséhez.\r\n\r\n",
    L"FIGYELEM:\r\n",
    L"Visszavonható lépések: %d",

    // Dialogs
    L"Az automatikus indulás beállítása nem sikerült. Részletek a naplófájlban.",
    L"A CRTBender névjegye",
    L"CRTBender %ls\r\n"
    L"Szoftveres geometria-korrekció CRT monitorokhoz.\r\n\r\n"
    L"Készítette: %ls\r\n"
    L"%ls\r\n\r\n"
    L"Megnyitod a projekt oldalát a böngészőben?",

    // Engine errors
    L"Az átfedő ablakot nem sikerült létrehozni, vagy a rendszer nem támogatja a "
    L"WDA_EXCLUDEFROMCAPTURE kizárást (Windows 10 2004 vagy újabb kell). "
    L"A korrekció biztonsági okból nem indul el.",
    L"A grafikus csővezeték inicializálása nem sikerült. Részletek a naplófájlban.",
    L"A képernyő rögzítése (Desktop Duplication) jelenleg nem érhető el. Újrapróbálkozás folyamatban.",
    L"A képernyő rögzítése üres, fekete képet ad vissza, ezért a korrekció nem "
    L"jelenik meg (így nem takarja le az asztalt). Részletek a naplófájlban.",

    // Config file comments
    L"CRTBender konfiguráció\n"
    L"Ez a fájl kézzel is szerkeszthető, a program indításkor beolvassa.\n"
    L"Az eltolások normalizáltak: a képernyő szélességének / magasságának törtrésze.\n"
    L"Pozitív dy lefelé tolja a képet, pozitív dx jobbra.",
    L"0=ki, 1=az asztal fölött, 2=fekete háttéren",
    L"bal/jobb szimmetrikus szerkesztés",
    L"vízszintes mozgatás engedélyezése",
    L"0=bilineáris, 1=bikubikus, 2=éles (Lanczos + gyűrűzésgátlás)",
    L"csak a szerkesztő előnézetét nagyítja",
    L"bitblt vagy flip; a bitblt tartja kattintás-átengedőnek az átfedést",
    L"en vagy hu",
    L"kontrollpont-eltolások (dx,dy), a 0. sor a képernyő teteje",
};

// UTF-8 mirrors for the config writer, rebuilt whenever the language changes.
char g_narrow[static_cast<int>(Str::Count)][640];
bool g_narrowReady = false;

void ToUtf8(const wchar_t* src, char* dst, int cap) {
    int out = 0;
    for (const wchar_t* p = src; *p && out < cap - 4; ++p) {
        const unsigned int cp = static_cast<unsigned int>(*p);
        if (cp < 0x80) {
            dst[out++] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            dst[out++] = static_cast<char>(0xC0 | (cp >> 6));
            dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            dst[out++] = static_cast<char>(0xE0 | (cp >> 12));
            dst[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    dst[out] = '\0';
}

} // namespace

void SetLanguage(Lang lang) {
    if (g_lang == lang) return;
    g_lang = lang;
    g_narrowReady = false;
}

Lang GetLanguage() { return g_lang; }

const wchar_t* T(Str id) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= static_cast<int>(Str::Count)) return L"";
    return g_lang == Lang::Hungarian ? kHungarian[index] : kEnglish[index];
}

const wchar_t* T(Lang lang, Str id) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= static_cast<int>(Str::Count)) return L"";
    return lang == Lang::Hungarian ? kHungarian[index] : kEnglish[index];
}

const char* T8(Str id) {
    const int index = static_cast<int>(id);
    if (index < 0 || index >= static_cast<int>(Str::Count)) return "";

    if (!g_narrowReady) {
        for (int i = 0; i < static_cast<int>(Str::Count); ++i) {
            const wchar_t* s = g_lang == Lang::Hungarian ? kHungarian[i] : kEnglish[i];
            ToUtf8(s, g_narrow[i], static_cast<int>(sizeof(g_narrow[i])));
        }
        g_narrowReady = true;
    }
    return g_narrow[index];
}

const wchar_t* LanguageTag(Lang lang) {
    return lang == Lang::Hungarian ? L"hu" : L"en";
}

Lang LanguageFromTag(const char* tag) {
    if (tag && (std::strcmp(tag, "hu") == 0 || std::strcmp(tag, "HU") == 0))
        return Lang::Hungarian;
    return Lang::English;
}

const wchar_t* LanguageDisplayName(Lang lang) {
    // Each language names itself, so the entry is recognizable whichever
    // language the UI is currently in.
    return lang == Lang::Hungarian ? L"Magyar" : L"English";
}

} // namespace crtb
