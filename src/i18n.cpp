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
#ifdef CRTB_XP
    L"The Windows XP overlay could not be created. The correction will not start.",
#else
    L"The overlay window could not be created, or the system does not support "
    L"the WDA_EXCLUDEFROMCAPTURE exclusion (Windows 10 2004 or newer is required). "
    L"The correction will not start, as a safety measure.",
#endif
    L"The graphics pipeline could not be initialized. See the log file for details.",
#ifdef CRTB_XP
    L"GDI screen capture is currently unavailable. Retrying.",
    L"GDI screen capture returned an empty image, so the correction stays hidden.",
#else
    L"Screen capture (Desktop Duplication) is currently unavailable. Retrying.",
    L"Screen capture returns an empty, black image, so the correction is not being "
    L"displayed (this keeps your desktop visible). See the log file for details.",
#endif

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

    // Tabs and monitor selection
    L"Grid",
    L"Basic geometry",
    L"Convergence",
    L"Picture quality",
    L"Program",
    L"Monitor",

    // Parametric geometry
    L"Horizontal position",
    L"Vertical position",
    L"Width",
    L"Height",
    L"Rotation",
    L"Parallelogram",
    L"Trapezoid",
    L"Pincushion",
    L"Pincushion balance",
    L"Horizontal linearity",
    L"Vertical linearity",
    L"Top edge bow",
    L"Bottom edge bow",
    L"Reset basic geometry",
    L"These are the controls a monitor's own service menu offers. Start here: "
    L"one slider usually removes what would otherwise take a dozen grid points. "
    L"The grid is applied on top and stays untouched.",

    // Convergence
    L"Red - horizontal",
    L"Red - vertical",
    L"Red - horizontal at the edges",
    L"Red - vertical at the edges",
    L"Blue - horizontal",
    L"Blue - vertical",
    L"Blue - horizontal at the edges",
    L"Blue - vertical at the edges",
    L"Reset convergence",
    L"Corrects coloured fringes on sharp edges. Green is the reference and does "
    L"not move. Set the middle of the screen with the first two sliders, then use "
    L"the edge sliders for what is left.",

    // Auto bypass
    L"Step aside automatically for protected video and fullscreen games",
    L"Paused: protected content is in the foreground, so the capture would be "
    L"black. The correction returns on its own.",
    L"Paused: an exclusive fullscreen application is running. The correction "
    L"returns on its own.",

    // Misc
    L"Monitor:  %ls\r\n",
    L"%+.2f px",
    L"%+.3f deg",
    L"basic geometry, in fractions of the screen size",
    L"convergence: where red and blue are sampled from, relative to green",
    L"step aside for protected video and exclusive fullscreen",
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
#ifdef CRTB_XP
    L"A Windows XP átfedő ablakot nem sikerült létrehozni. A korrekció nem indul el.",
#else
    L"Az átfedő ablakot nem sikerült létrehozni, vagy a rendszer nem támogatja a "
    L"WDA_EXCLUDEFROMCAPTURE kizárást (Windows 10 2004 vagy újabb kell). "
    L"A korrekció biztonsági okból nem indul el.",
#endif
    L"A grafikus csővezeték inicializálása nem sikerült. Részletek a naplófájlban.",
#ifdef CRTB_XP
    L"A GDI képernyőrögzítés jelenleg nem érhető el. Újrapróbálkozás folyamatban.",
    L"A GDI képernyőrögzítés üres képet adott, ezért a korrekció rejtve marad.",
#else
    L"A képernyő rögzítése (Desktop Duplication) jelenleg nem érhető el. Újrapróbálkozás folyamatban.",
    L"A képernyő rögzítése üres, fekete képet ad vissza, ezért a korrekció nem "
    L"jelenik meg (így nem takarja le az asztalt). Részletek a naplófájlban.",
#endif

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

    // Tabs and monitor selection
    L"Rács",
    L"Alapgeometria",
    L"Konvergencia",
    L"Képminőség",
    L"Program",
    L"Monitor",

    // Parametric geometry
    L"Vízszintes pozíció",
    L"Függőleges pozíció",
    L"Szélesség",
    L"Magasság",
    L"Elforgatás",
    L"Paralelogramma",
    L"Trapéz",
    L"Párnatorzítás",
    L"Párnatorzítás egyensúlya",
    L"Vízszintes linearitás",
    L"Függőleges linearitás",
    L"Felső él íve",
    L"Alsó él íve",
    L"Alapgeometria nullázása",
    L"Ezek ugyanazok a szabályzók, amiket a monitor szervizmenüje kínál. Érdemes "
    L"innen indulni: egy csúszka általában elintézi azt, amihez különben egy tucat "
    L"rácspontot kellene húzogatni. A rács ezek tetejére kerül, és érintetlen marad.",

    // Convergence
    L"Vörös - vízszintes",
    L"Vörös - függőleges",
    L"Vörös - vízszintes a széleken",
    L"Vörös - függőleges a széleken",
    L"Kék - vízszintes",
    L"Kék - függőleges",
    L"Kék - vízszintes a széleken",
    L"Kék - függőleges a széleken",
    L"Konvergencia nullázása",
    L"A kontúrokon megjelenő színes szegélyt javítja. A zöld a viszonyítási alap, "
    L"az nem mozdul. Előbb a képernyő közepét állítsd be az első két csúszkával, "
    L"utána a maradékot a szélekre valókkal.",

    // Auto bypass
    L"Automatikus szünet védett videónál és teljes képernyős játéknál",
    L"Szünetel: védett tartalom van előtérben, a rögzítés fekete képet adna. "
    L"A korrekció magától visszatér.",
    L"Szünetel: exkluzív teljes képernyős alkalmazás fut. A korrekció magától "
    L"visszatér.",

    // Misc
    L"Monitor:  %ls\r\n",
    L"%+.2f px",
    L"%+.3f fok",
    L"alapgeometria, a képernyőméret törtrészében",
    L"konvergencia: honnan mintavételezzük a vöröset és a kéket a zöldhöz képest",
    L"félreállás védett videónál és exkluzív teljes képernyőnél",
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
