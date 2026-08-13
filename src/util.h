// CRTBender - small shared helpers.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstring>
#include <string>

namespace crtb {

template <typename Fn>
Fn LoadSystemFunction(const wchar_t* module, const char* name) {
    const FARPROC raw = GetProcAddress(GetModuleHandleW(module), name);
    static_assert(sizeof(Fn) == sizeof(raw), "unexpected Windows function pointer size");
    Fn typed = nullptr;
    std::memcpy(&typed, &raw, sizeof(typed));
    return typed;
}

// Minimal COM smart pointer so the project builds with both MSVC and MinGW
// without pulling in WRL or ATL.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~ComPtr() { Reset(); }

    ComPtr& operator=(const ComPtr& o) {
        if (this != &o) { if (o.p_) o.p_->AddRef(); Reset(); p_ = o.p_; }
        return *this;
    }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { Reset(); p_ = o.p_; o.p_ = nullptr; }
        return *this;
    }

    void Reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T*   Get() const { return p_; }
    T**  Put() { Reset(); return &p_; }              // for out-params
    void** PutVoid() { Reset(); return reinterpret_cast<void**>(&p_); }
    T*   operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};

// %APPDATA%\CRTBender, created on demand. Empty string on failure.
std::wstring AppDataDir();
// Directory the running executable lives in (no trailing separator).
std::wstring ExeDir();
// Full path of the running executable.
std::wstring ExePath();

std::string  Narrow(const std::wstring& s);
std::wstring Widen(const std::string& s);

// Win10 exposes direct DPI helpers, XP does not. These wrappers load the modern
// APIs when available and otherwise fall back to the system device context.
UINT SystemDpi();
UINT WindowDpi(HWND hwnd);

// Rolling log at %APPDATA%\CRTBender\crtbender.log. Never throws.
void LogInit();
void LogLine(const std::wstring& msg);
void LogHr(const wchar_t* what, HRESULT hr);

// Trim ASCII whitespace from both ends.
std::string Trim(const std::string& s);

// Bounded copy into a fixed wide buffer. Always null terminates. Used for the
// NOTIFYICONDATA fields, where the *_s functions are not portable.
template <size_t N>
void CopyTo(wchar_t (&dst)[N], const wchar_t* src) {
    size_t i = 0;
    if (src) { for (; i + 1 < N && src[i]; ++i) dst[i] = src[i]; }
    dst[i] = L'\0';
}

} // namespace crtb
