// CRTBender - parsing of single config-file values.
//
// Deliberately free of any Windows dependency so it can be unit tested, because
// this is where a whole class of silent bugs lived: the writer emits inline
// comments ("language = hu    # en or hu") and the reader used to hand the whole
// remainder to the value parser. Numeric values survived that by accident -
// stoi stops at the first non-digit - but every string and boolean setting fell
// back to its default, so language, mirror, free_move, auto_bypass and
// present_mode silently refused to persist.
#pragma once

#include <string>

namespace crtb {

inline std::string TrimValue(const std::string& s) {
    const size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Strips an inline comment and surrounding whitespace. '#' and ';' both start a
// comment, matching what the writer emits and what a hand-editor would expect.
inline std::string ConfigValue(const std::string& raw) {
    const size_t comment = raw.find_first_of("#;");
    return TrimValue(comment == std::string::npos ? raw : raw.substr(0, comment));
}

inline bool ParseConfigBool(const std::string& raw, bool fallback) {
    const std::string v = ConfigValue(raw);
    if (v == "1" || v == "true" || v == "yes" || v == "on")  return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return fallback;
}

inline int ParseConfigInt(const std::string& raw, int fallback) {
    try { return std::stoi(ConfigValue(raw)); } catch (...) { return fallback; }
}

inline float ParseConfigFloat(const std::string& raw, float fallback) {
    try { return std::stof(ConfigValue(raw)); } catch (...) { return fallback; }
}

} // namespace crtb
