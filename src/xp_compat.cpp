// Runtime shims needed by the current MinGW-w64 POSIX threading library when
// targeting XP. GetTickCount64 arrived in Vista, but winpthreads only needs a
// monotonic millisecond counter; extending XP's 32-bit GetTickCount is enough.
#include <windows.h>

#if defined(CRTB_XP) && defined(__GNUC__)

#include <cstdint>

extern "C" ULONGLONG WINAPI CrtbGetTickCount64() {
    static volatile LONG lastTick = 0;
    static volatile LONG highPart = 0;

    const DWORD now = GetTickCount();
    for (;;) {
        const LONG previous = lastTick;
        if (InterlockedCompareExchange(&lastTick, static_cast<LONG>(now), previous) != previous)
            continue;

        const DWORD before = static_cast<DWORD>(previous);
        if (now < before && before - now > 0x80000000u)
            InterlockedIncrement(&highPart);
        break;
    }

    const DWORD high = static_cast<DWORD>(
        InterlockedCompareExchange(&highPart, 0, 0));
    return (static_cast<ULONGLONG>(high) << 32) | now;
}

extern "C" {
using GetTickCount64Proc = ULONGLONG (WINAPI*)();
GetTickCount64Proc crtbGetTickCount64Import
    __asm__("__imp__GetTickCount64@0") = &CrtbGetTickCount64;
}

#endif
