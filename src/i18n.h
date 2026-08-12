// CRTBender - user interface strings.
//
// Every string the user can see goes through T(). Log lines deliberately stay
// English: they are diagnostics meant to be pasted into a bug report.
//
// This file is UTF-8. Hungarian text is written with its proper accents, so the
// build must keep the source encoding intact (/utf-8 on MSVC,
// -finput-charset=UTF-8 on GCC - both are set in CMakeLists.txt).
#pragma once

namespace crtb {

enum class Lang {
    English,
    Hungarian,
};

enum class Str {
    // Tray menu
    TrayOpenEditor,
    TrayCorrectionEnabled,
    TrayTestPattern,
    TrayPatternOff,
    TrayPatternOverDesktop,
    TrayPatternOnBlack,
    TrayAutostart,
    TrayShowConfig,
    TrayOpenLog,
    TrayLanguage,
    TrayProjectPage,
    TrayAbout,
    TrayExit,
    TrayTipEnabled,
    TrayTipDisabled,

    // Editor window
    EdWindowTitle,
    EdEnabled,
    EdPatternLabel,
    EdGridLabel,
    EdGridRecommended,
    EdMirror,
    EdFreeMove,
    EdGainLabel,
    EdOverscanLabel,
    EdOverscanHint,
    EdAutoBleed,
    EdBleedLabel,
    EdQualityLabel,
    EdQualityBilinear,
    EdQualityBicubic,
    EdQualitySharp,
    EdAutostart,
    EdLanguageLabel,
    EdUndo,
    EdResetRow,
    EdResetAll,
    EdSave,
    EdProjectButton,
    EdScreenTop,
    EdScreenBottom,
    EdHelp,
    EdCredit,

    // Editor status panel
    StatusProfile,
    StatusSelected,
    StatusRowLocked,
    StatusNoSelection,
    StatusWarning,
    StatusUndoSteps,

    // Dialogs
    MsgAutostartFailed,
    MsgAboutTitle,
    MsgAboutBody,

    // Engine errors, shown in the tray balloon and the status panel
    ErrOverlayCreate,
    ErrPipelineInit,
    ErrDuplicationUnavailable,
    ErrCaptureBlank,

    // Comments written into the config file
    CfgHeader,
    CfgPatternMode,
    CfgMirror,
    CfgFreeMove,
    CfgQuality,
    CfgPreviewGain,
    CfgPresentMode,
    CfgLanguage,
    CfgProfilePoints,

    Count,
};

void        SetLanguage(Lang lang);
Lang        GetLanguage();
const wchar_t* T(Str id);
// Explicit-language lookup. Used by the string table tests.
const wchar_t* T(Lang lang, Str id);

// Narrow (UTF-8) variant, for the config file writer.
const char* T8(Str id);

// Stable tokens for the config file.
const wchar_t* LanguageTag(Lang lang);      // L"en" / L"hu"
Lang           LanguageFromTag(const char* tag);
const wchar_t* LanguageDisplayName(Lang lang);   // always in its own language

constexpr const wchar_t* kProjectUrl = L"https://github.com/SubCoderHUN/CRTBender";
constexpr const wchar_t* kAuthor     = L"SubCoderHUN";
constexpr const wchar_t* kVersion    = L"1.1.0";

} // namespace crtb
