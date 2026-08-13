#include "i18n.h"
#include "render.h"
#include "util.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace crtb {
namespace {

constexpr const wchar_t* kOverlayClass = L"CRTBenderOverlayWindowXP";
constexpr UINT  kTimerTopmost = 1;
constexpr DWORD kFrameTimeMs  = 16;   // target about 60 FPS; Present may pace it lower
constexpr DWORD kRetryTimeMs  = 1000;

struct XpVertex {
    float x, y, z, rhw;
    float u, v;
};

constexpr DWORD kXpFvf = D3DFVF_XYZRHW | D3DFVF_TEX1;

class ScopedCriticalSection {
public:
    explicit ScopedCriticalSection(CRITICAL_SECTION& value) : value_(value) {
        EnterCriticalSection(&value_);
    }
    ~ScopedCriticalSection() { LeaveCriticalSection(&value_); }

private:
    CRITICAL_SECTION& value_;
};

unsigned char BlendChannel(unsigned char from, unsigned char to, int alpha) {
    return static_cast<unsigned char>(
        (static_cast<int>(from) * (255 - alpha) + static_cast<int>(to) * alpha + 127) / 255);
}

int NextPowerOfTwo(int value) {
    int result = 1;
    while (result < value && result < (1 << 30)) result <<= 1;
    return result;
}

} // namespace

struct WarpEngine::Impl {
    WarpEngine* owner = nullptr;
    HWND        hwnd  = nullptr;

    ComPtr<IDirect3D9>       d3d;
    ComPtr<IDirect3DDevice9> device;
    ComPtr<IDirect3DTexture9> texture;
    ComPtr<IDirect3DSurface9> renderTarget;
    ComPtr<IDirect3DSurface9> readback;

    HDC     screenDc      = nullptr;
    HDC     captureDc     = nullptr;
    HBITMAP captureBitmap = nullptr;
    HGDIOBJ oldBitmap     = nullptr;
    void*   captureBits   = nullptr;

    RECT deskRect{};
    int  width  = 0;
    int  height = 0;
    int  textureWidth  = 0;
    int  textureHeight = 0;
    UINT adapter = D3DADAPTER_DEFAULT;

    bool needReinit      = true;
    bool visible         = false;
    bool textureReady    = false;
    bool gridDirty       = true;
    bool convergencePass = true;
    bool loggedCaptureFailure = false;
    bool captureWasBlank = false;
    bool loggedPresentFailure = false;

    RenderState state;

    std::vector<XpVertex> vertices;
    std::vector<XpVertex> redVertices;
    std::vector<XpVertex> blueVertices;
    std::vector<WORD>     indices;

    bool CreateOverlayWindow(HINSTANCE inst);
    bool InitGraphics();
    void ShutdownGraphics();
    bool InitCapture();
    bool CaptureFrame();
    bool CaptureIsBlank() const;
    void ApplyPattern();
    void BlendPixel(int x, int y, unsigned char r, unsigned char g,
                    unsigned char b, int alpha);
    void DrawHorizontal(int y, int thickness, unsigned char r, unsigned char g,
                        unsigned char b, int alpha);
    void DrawVertical(int x, int thickness, unsigned char r, unsigned char g,
                      unsigned char b, int alpha);
    void UploadGrid();
    bool Draw();
    void SetVisible(bool show);
};

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* impl = reinterpret_cast<WarpEngine::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_ERASEBKGND:
        return 1;

    case WM_DISPLAYCHANGE:
        if (impl) {
            LogLine(L"XP display mode changed, reinitializing GDI/D3D9 pipeline");
            impl->needReinit = true;
            if (impl->owner) impl->owner->OnDisplayChanged();
        }
        return 0;

    case WM_TIMER:
        if (wp == kTimerTopmost && impl && impl->visible) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, kTimerTopmost);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool WarpEngine::Impl::CreateOverlayWindow(HINSTANCE inst) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = inst;
    wc.hCursor       = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kOverlayClass;
    RegisterClassExW(&wc);

    hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kOverlayClass, L"CRTBender XP Overlay", WS_POPUP,
        0, 0, 16, 16, nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        LogLine(L"XP overlay window creation failed");
        return false;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_LAYERED) || !(exStyle & WS_EX_TRANSPARENT)) {
        LogLine(L"XP overlay is not safely click-through");
        DestroyWindow(hwnd);
        hwnd = nullptr;
        return false;
    }

    SetTimer(hwnd, kTimerTopmost, 2000, nullptr);
    return true;
}

void WarpEngine::Impl::ShutdownGraphics() {
    SetVisible(false);
    readback.Reset();
    renderTarget.Reset();
    texture.Reset();
    device.Reset();
    d3d.Reset();

    if (captureDc && oldBitmap) SelectObject(captureDc, oldBitmap);
    if (captureBitmap) DeleteObject(captureBitmap);
    if (captureDc) DeleteDC(captureDc);
    if (screenDc) ReleaseDC(nullptr, screenDc);

    screenDc      = nullptr;
    captureDc     = nullptr;
    captureBitmap = nullptr;
    oldBitmap     = nullptr;
    captureBits   = nullptr;

    width = height = 0;
    textureWidth = textureHeight = 0;
    textureReady = false;
    gridDirty    = true;
    captureWasBlank = false;
    loggedPresentFailure = false;
    vertices.clear();
    redVertices.clear();
    blueVertices.clear();
    indices.clear();
}

bool WarpEngine::Impl::InitCapture() {
    screenDc = GetDC(nullptr);
    if (!screenDc) {
        LogLine(L"XP GetDC(NULL) failed");
        return false;
    }

    captureDc = CreateCompatibleDC(screenDc);
    if (!captureDc) {
        LogLine(L"XP CreateCompatibleDC failed");
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = width;
    bmi.bmiHeader.biHeight      = -height;   // top-down, same orientation as the texture
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    captureBitmap = CreateDIBSection(captureDc, &bmi, DIB_RGB_COLORS,
                                     &captureBits, nullptr, 0);
    if (!captureBitmap || !captureBits) {
        LogLine(L"XP CreateDIBSection failed");
        return false;
    }
    oldBitmap = SelectObject(captureDc, captureBitmap);
    return oldBitmap != nullptr && oldBitmap != HGDI_ERROR;
}

bool WarpEngine::Impl::InitGraphics() {
    ShutdownGraphics();

    *d3d.Put() = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        LogLine(L"Direct3DCreate9 failed");
        return false;
    }

    const std::wstring wanted = owner ? owner->TargetDevice() : std::wstring();
    bool found = false;
    for (UINT i = 0; i < d3d->GetAdapterCount(); ++i) {
        const HMONITOR monitor = d3d->GetAdapterMonitor(i);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!monitor || !GetMonitorInfoW(monitor, &info)) continue;

        const bool matches = wanted.empty()
            ? (info.dwFlags & MONITORINFOF_PRIMARY) != 0
            : wanted == info.szDevice;
        if (!matches) continue;

        adapter  = i;
        deskRect = info.rcMonitor;
        found    = true;
        break;
    }
    if (!found) {
        if (!wanted.empty()) {
            LogLine(L"XP D3D9 adapter not found for " + wanted);
            return false;
        }
        adapter = D3DADAPTER_DEFAULT;
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        const HMONITOR monitor = d3d->GetAdapterMonitor(adapter);
        if (!monitor || !GetMonitorInfoW(monitor, &info)) {
            LogLine(L"XP target monitor was not found");
            return false;
        }
        deskRect = info.rcMonitor;
    }

    width  = deskRect.right - deskRect.left;
    height = deskRect.bottom - deskRect.top;
    if (width <= 0 || height <= 0) {
        LogLine(L"XP target monitor has an empty rectangle");
        return false;
    }

    SetWindowPos(hwnd, HWND_TOPMOST, deskRect.left, deskRect.top, width, height,
                 SWP_NOACTIVATE | SWP_NOREDRAW);

    D3DDISPLAYMODE displayMode{};
    if (FAILED(d3d->GetAdapterDisplayMode(adapter, &displayMode))) {
        LogLine(L"XP D3D9 could not read the adapter display mode");
        return false;
    }

    D3DCAPS9 caps{};
    if (FAILED(d3d->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &caps))) {
        LogLine(L"XP D3D9 could not read device capabilities");
        return false;
    }
    convergencePass = (caps.PrimitiveMiscCaps & D3DPMISCCAPS_COLORWRITEENABLE) != 0;
    if (!convergencePass)
        LogLine(L"XP D3D9 device lacks per-channel writes; convergence is disabled");

    textureWidth  = width;
    textureHeight = height;
    const bool strictPowerOfTwo =
        (caps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0 &&
        (caps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) == 0;
    if (strictPowerOfTwo) {
        textureWidth  = NextPowerOfTwo(textureWidth);
        textureHeight = NextPowerOfTwo(textureHeight);
    }
    if (caps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
        textureWidth = textureHeight = std::max(textureWidth, textureHeight);
    if (textureWidth > static_cast<int>(caps.MaxTextureWidth) ||
        textureHeight > static_cast<int>(caps.MaxTextureHeight)) {
        LogLine(L"XP desktop is larger than this D3D9 device's maximum texture size");
        return false;
    }

    if (FAILED(d3d->CheckDeviceFormat(adapter, D3DDEVTYPE_HAL, displayMode.Format, 0,
                                      D3DRTYPE_TEXTURE, D3DFMT_X8R8G8B8)) ||
        FAILED(d3d->CheckDeviceFormat(adapter, D3DDEVTYPE_HAL, displayMode.Format,
                                      D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE,
                                      D3DFMT_X8R8G8B8))) {
        LogLine(L"XP D3D9 device does not support the required 32-bit texture format");
        return false;
    }

    D3DPRESENT_PARAMETERS pp{};
    pp.BackBufferWidth            = static_cast<UINT>(width);
    pp.BackBufferHeight           = static_cast<UINT>(height);
    pp.BackBufferFormat           = displayMode.Format;
    pp.BackBufferCount            = 1;
    pp.MultiSampleType            = D3DMULTISAMPLE_NONE;
    pp.SwapEffect                 = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow              = hwnd;
    pp.Windowed                   = TRUE;
    pp.EnableAutoDepthStencil     = FALSE;
    pp.PresentationInterval       = D3DPRESENT_INTERVAL_ONE;

    DWORD behavior = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    HRESULT hr = d3d->CreateDevice(adapter, D3DDEVTYPE_HAL, hwnd, behavior, &pp, device.Put());
    if (FAILED(hr)) {
        behavior = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        hr = d3d->CreateDevice(adapter, D3DDEVTYPE_HAL, hwnd, behavior, &pp, device.Put());
    }
    if (FAILED(hr)) {
        LogHr(L"XP Direct3D9 CreateDevice", hr);
        return false;
    }

    hr = device->CreateTexture(static_cast<UINT>(textureWidth),
                               static_cast<UINT>(textureHeight), 1, 0,
                               D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, texture.Put(), nullptr);
    if (FAILED(hr)) {
        LogHr(L"XP CreateTexture", hr);
        return false;
    }
    hr = device->CreateRenderTarget(static_cast<UINT>(width), static_cast<UINT>(height),
                                    D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                    renderTarget.Put(), nullptr);
    if (FAILED(hr)) {
        LogHr(L"XP CreateRenderTarget", hr);
        return false;
    }
    hr = device->CreateOffscreenPlainSurface(
        static_cast<UINT>(width), static_cast<UINT>(height), D3DFMT_X8R8G8B8,
        D3DPOOL_SYSTEMMEM, readback.Put(), nullptr);
    if (FAILED(hr)) {
        LogHr(L"XP CreateOffscreenPlainSurface", hr);
        return false;
    }
    if (!InitCapture()) return false;

    gridDirty = true;
    wchar_t line[192];
    swprintf(line, std::size(line),
             L"XP pipeline ready: GDI BitBlt + D3D9, %dx%d (texture %dx%d) adapter %u",
             width, height, textureWidth, textureHeight, adapter);
    LogLine(line);
    return true;
}

void WarpEngine::Impl::BlendPixel(int x, int y, unsigned char r, unsigned char g,
                                  unsigned char b, int alpha) {
    if (!captureBits || x < 0 || y < 0 || x >= width || y >= height) return;
    auto* px = static_cast<unsigned char*>(captureBits) +
               (static_cast<size_t>(y) * width + x) * 4;
    px[0] = BlendChannel(px[0], b, alpha);
    px[1] = BlendChannel(px[1], g, alpha);
    px[2] = BlendChannel(px[2], r, alpha);
    px[3] = 255;
}

void WarpEngine::Impl::DrawHorizontal(int y, int thickness, unsigned char r, unsigned char g,
                                      unsigned char b, int alpha) {
    const int first = y - thickness / 2;
    for (int dy = 0; dy < thickness; ++dy)
        for (int x = 0; x < width; ++x) BlendPixel(x, first + dy, r, g, b, alpha);
}

void WarpEngine::Impl::DrawVertical(int x, int thickness, unsigned char r, unsigned char g,
                                    unsigned char b, int alpha) {
    const int first = x - thickness / 2;
    for (int dx = 0; dx < thickness; ++dx)
        for (int y = 0; y < height; ++y) BlendPixel(first + dx, y, r, g, b, alpha);
}

void WarpEngine::Impl::ApplyPattern() {
    if (!captureBits || state.patternMode == 0) return;

    if (state.patternMode == 2)
        std::memset(captureBits, 0, static_cast<size_t>(width) * height * 4);

    const int alpha = std::clamp(
        static_cast<int>(std::lround(state.patternOpacity * 255.0f)), 0, 255);
    const int cellsX = std::clamp(state.patternCells, 2, 64);
    const int cellsY = std::clamp(static_cast<int>(std::lround(
        static_cast<double>(cellsX) * height / std::max(1, width))), 2, 64);

    for (int i = 0; i <= cellsX; ++i) {
        const int x = static_cast<int>(std::lround(
            static_cast<double>(i) * (width - 1) / cellsX));
        DrawVertical(x, 2, 89, 255, 140, alpha);
    }
    for (int i = 0; i <= cellsY; ++i) {
        const int y = static_cast<int>(std::lround(
            static_cast<double>(i) * (height - 1) / cellsY));
        DrawHorizontal(y, 2, 89, 255, 140, alpha);
    }

    DrawVertical(width / 2, 3, 89, 255, 140, alpha);
    DrawHorizontal(height / 2, 3, 89, 255, 140, alpha);
    DrawVertical(0, 3, 255, 89, 89, alpha);
    DrawVertical(width - 1, 3, 255, 89, 89, alpha);
    DrawHorizontal(0, 3, 255, 89, 89, alpha);
    DrawHorizontal(height - 1, 3, 255, 89, 89, alpha);
}

bool WarpEngine::Impl::CaptureFrame() {
    if (!screenDc || !captureDc || !captureBits || !texture) return false;

    // CAPTUREBLT is deliberately absent: on XP a normal SRCCOPY omits layered
    // windows, so the overlay does not capture itself and recurse.
    if (!BitBlt(captureDc, 0, 0, width, height, screenDc,
                deskRect.left, deskRect.top, SRCCOPY)) {
        if (!loggedCaptureFailure) LogLine(L"XP GDI BitBlt failed; overlay stays hidden");
        loggedCaptureFailure = true;
        captureWasBlank = false;
        needReinit = true;
        return false;
    }
    if (!GdiFlush()) {
        if (!loggedCaptureFailure) LogLine(L"XP GdiFlush failed; rebuilding capture resources");
        loggedCaptureFailure = true;
        captureWasBlank = false;
        needReinit = true;
        return false;
    }
    loggedCaptureFailure = false;

    if (state.patternMode == 0 && CaptureIsBlank()) {
        if (!captureWasBlank)
            LogLine(L"XP GDI capture is blank; overlay stays hidden");
        captureWasBlank = true;
        return false;
    }
    if (captureWasBlank) LogLine(L"XP GDI capture recovered");
    captureWasBlank = false;

    ApplyPattern();

    D3DLOCKED_RECT locked{};
    const HRESULT hr = texture->LockRect(0, &locked, nullptr, 0);
    if (FAILED(hr)) {
        LogHr(L"XP texture LockRect", hr);
        needReinit = true;
        return false;
    }

    const auto* src = static_cast<const unsigned char*>(captureBits);
    auto* dst = static_cast<unsigned char*>(locked.pBits);
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    const size_t textureRowBytes = static_cast<size_t>(textureWidth) * 4;
    for (int y = 0; y < height; ++y) {
        unsigned char* dstRow = dst + static_cast<size_t>(y) * locked.Pitch;
        std::memcpy(dstRow, src + static_cast<size_t>(y) * rowBytes, rowBytes);
        for (int x = width; x < textureWidth; ++x)
            std::memcpy(dstRow + static_cast<size_t>(x) * 4,
                        dstRow + static_cast<size_t>(width - 1) * 4, 4);
    }
    const unsigned char* lastRow =
        dst + static_cast<size_t>(height - 1) * locked.Pitch;
    for (int y = height; y < textureHeight; ++y)
        std::memcpy(dst + static_cast<size_t>(y) * locked.Pitch,
                    lastRow, textureRowBytes);

    const HRESULT unlockHr = texture->UnlockRect(0);
    if (FAILED(unlockHr)) {
        LogHr(L"XP texture UnlockRect", unlockHr);
        needReinit = true;
        return false;
    }
    textureReady = true;
    return true;
}

bool WarpEngine::Impl::CaptureIsBlank() const {
    if (!captureBits || width < 16 || height < 16) return true;

    constexpr int patch = 8;
    const int origins[3][2] = {
        { width / 4,     height / 4     },
        { width / 2,     height / 2     },
        { width * 3 / 4, height * 3 / 4 },
    };

    const auto* pixels = static_cast<const unsigned char*>(captureBits);
    for (const auto& origin : origins) {
        for (int y = 0; y < patch; ++y) {
            for (int x = 0; x < patch; ++x) {
                const int px = std::min(width - 1, origin[0] + x);
                const int py = std::min(height - 1, origin[1] + y);
                const unsigned char* value =
                    pixels + (static_cast<size_t>(py) * width + px) * 4;
                if (value[0] || value[1] || value[2]) return false;
            }
        }
    }
    return true;
}

void WarpEngine::Impl::UploadGrid() {
    if (!gridDirty || width <= 0 || height <= 0) return;

    static const WarpMesh          kIdentityMesh;
    static const GeometryParams    kNoGeometry;
    static const ConvergenceParams kNoConvergence;

    const bool passthrough = !state.enabled;
    WarpBuildParams params;
    params.mesh        = passthrough ? &kIdentityMesh   : &state.mesh;
    params.geometry    = passthrough ? &kNoGeometry     : &state.geometry;
    params.convergence = passthrough ? &kNoConvergence  : &state.convergence;
    params.tess        = std::clamp(state.tessellation, 16, 128);
    params.overscan    = passthrough ? 1.0f : state.overscan;
    params.edgeBleed   = passthrough ? 0.0f : state.edgeBleed;
    params.aspect      = state.aspect;

    std::vector<WarpVertex> source;
    std::vector<unsigned int> sourceIndices;
    BuildWarpGrid(params, source);
    BuildWarpIndices(params.tess, sourceIndices);

    vertices.resize(source.size());
    const bool convergence = convergencePass && !passthrough && state.convergence.Any();
    if (convergence) {
        redVertices.resize(source.size());
        blueVertices.resize(source.size());
    } else {
        redVertices.clear();
        blueVertices.clear();
    }

    // XYZRHW positions already carry D3D9's -0.5 pixel-centre correction, so
    // the texture coordinates retain their full 0..captured-size span.
    const float uSpan = static_cast<float>(width) /
                        static_cast<float>(textureWidth);
    const float vSpan = static_cast<float>(height) /
                        static_cast<float>(textureHeight);
    const float convergenceU = static_cast<float>(width) /
                               static_cast<float>(textureWidth);
    const float convergenceV = static_cast<float>(height) /
                               static_cast<float>(textureHeight);

    for (size_t i = 0; i < source.size(); ++i) {
        const WarpVertex& in = source[i];
        XpVertex base{
            (in.x + 1.0f) * 0.5f * width - 0.5f,
            (1.0f - in.y) * 0.5f * height - 0.5f,
            0.0f, 1.0f, in.u * uSpan, in.v * vSpan
        };
        vertices[i] = base;
        if (convergence) {
            redVertices[i]  = base;
            blueVertices[i] = base;
            redVertices[i].u  += in.rdx * convergenceU;
            redVertices[i].v  += in.rdy * convergenceV;
            blueVertices[i].u += in.bdx * convergenceU;
            blueVertices[i].v += in.bdy * convergenceV;
        }
    }

    indices.resize(sourceIndices.size());
    for (size_t i = 0; i < sourceIndices.size(); ++i)
        indices[i] = static_cast<WORD>(sourceIndices[i]);

    gridDirty = false;
}

bool WarpEngine::Impl::Draw() {
    if (!device || !texture || !renderTarget || !readback || !textureReady ||
        vertices.empty() || indices.empty())
        return false;

    HRESULT hr = device->TestCooperativeLevel();
    if (hr == D3DERR_DEVICELOST || hr == D3DERR_DEVICENOTRESET) {
        needReinit = true;
        return false;
    }
    if (FAILED(hr)) {
        LogHr(L"XP TestCooperativeLevel", hr);
        needReinit = true;
        return false;
    }

    hr = device->SetRenderTarget(0, renderTarget.Get());
    if (FAILED(hr)) {
        LogHr(L"XP SetRenderTarget", hr);
        needReinit = true;
        return false;
    }

    device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
    hr = device->BeginScene();
    if (FAILED(hr)) {
        needReinit = true;
        return false;
    }

    device->SetFVF(kXpFvf);
    device->SetTexture(0, texture.Get());
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_ARGB(255, 255, 255, 255));
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);

    const UINT vertexCount = static_cast<UINT>(vertices.size());
    const UINT primitiveCount = static_cast<UINT>(indices.size() / 3);
    auto drawPass = [&](const std::vector<XpVertex>& pass) {
        return device->DrawIndexedPrimitiveUP(
            D3DPT_TRIANGLELIST, 0, vertexCount, primitiveCount,
            indices.data(), D3DFMT_INDEX16, pass.data(), sizeof(XpVertex));
    };

    if (!redVertices.empty()) {
        device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_GREEN);
        hr = drawPass(vertices);
        if (SUCCEEDED(hr)) {
            device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_RED);
            hr = drawPass(redVertices);
        }
        if (SUCCEEDED(hr)) {
            device->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_BLUE);
            hr = drawPass(blueVertices);
        }
        device->SetRenderState(D3DRS_COLORWRITEENABLE,
            D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN |
            D3DCOLORWRITEENABLE_BLUE | D3DCOLORWRITEENABLE_ALPHA);
    } else {
        hr = drawPass(vertices);
    }

    const HRESULT endHr = device->EndScene();
    if (FAILED(hr) || FAILED(endHr)) {
        LogHr(L"XP DrawIndexedPrimitiveUP", FAILED(hr) ? hr : endHr);
        needReinit = true;
        return false;
    }

    hr = device->GetRenderTargetData(renderTarget.Get(), readback.Get());
    if (FAILED(hr)) {
        if (hr != D3DERR_DEVICELOST) LogHr(L"XP GetRenderTargetData", hr);
        needReinit = true;
        return false;
    }

    D3DLOCKED_RECT rendered{};
    hr = readback->LockRect(&rendered, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr)) {
        LogHr(L"XP readback LockRect", hr);
        needReinit = true;
        return false;
    }

    auto* dst = static_cast<unsigned char*>(captureBits);
    const auto* src = static_cast<const unsigned char*>(rendered.pBits);
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    for (int y = 0; y < height; ++y) {
        unsigned char* dstRow = dst + static_cast<size_t>(y) * rowBytes;
        std::memcpy(dstRow, src + static_cast<size_t>(y) * rendered.Pitch, rowBytes);
        for (int x = 0; x < width; ++x) dstRow[x * 4 + 3] = 255;
    }
    readback->UnlockRect();

    POINT destination{ deskRect.left, deskRect.top };
    POINT source{ 0, 0 };
    SIZE  size{ width, height };
    BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (!UpdateLayeredWindow(hwnd, screenDc, &destination, &size, captureDc,
                             &source, 0, &blend, ULW_ALPHA)) {
        if (!loggedPresentFailure) LogLine(L"XP UpdateLayeredWindow failed");
        loggedPresentFailure = true;
        needReinit = true;
        return false;
    }
    loggedPresentFailure = false;
    return true;
}

void WarpEngine::Impl::SetVisible(bool show) {
    if (!hwnd || visible == show) return;
    visible = show;
    if (show) {
        ShowWindow(hwnd, SW_SHOWNA);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        ShowWindow(hwnd, SW_HIDE);
    }
}

WarpEngine::WarpEngine() : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    InitializeCriticalSection(&stateMutex_);
    InitializeCriticalSection(&errorMutex_);
}

WarpEngine::~WarpEngine() {
    Stop();
    DeleteCriticalSection(&errorMutex_);
    DeleteCriticalSection(&stateMutex_);
}

void WarpEngine::SetError(std::wstring message) {
    ScopedCriticalSection lock(errorMutex_);
    lastError_ = std::move(message);
}

std::wstring WarpEngine::LastError() const {
    ScopedCriticalSection lock(errorMutex_);
    return lastError_;
}

void WarpEngine::OnDisplayChanged() {
    if (notifyWindow_ && modeChangeMsg_)
        PostMessageW(notifyWindow_, modeChangeMsg_, 0, 0);
}

bool WarpEngine::Start(HWND notifyWindow, UINT modeChangeMsg, std::wstring targetDevice) {
    if (InterlockedCompareExchange(&running_, 1, 0) != 0) return true;
    notifyWindow_  = notifyWindow;
    modeChangeMsg_ = modeChangeMsg;
    targetDevice_  = std::move(targetDevice);
    thread_ = CreateThread(nullptr, 0, &WarpEngine::ThreadThunk, this, 0, nullptr);
    if (!thread_) {
        InterlockedExchange(&running_, 0);
        SetError(T(Str::ErrPipelineInit));
        return false;
    }
    return true;
}

void WarpEngine::Stop() {
    InterlockedExchange(&running_, 0);
    if (impl_ && impl_->hwnd) PostMessageW(impl_->hwnd, WM_NULL, 0, 0);
    if (thread_) {
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
}

void WarpEngine::Update(const RenderState& state) {
    {
        ScopedCriticalSection lock(stateMutex_);
        pending_ = state;
    }
    InterlockedIncrement(&version_);
    if (impl_ && impl_->hwnd) PostMessageW(impl_->hwnd, WM_NULL, 0, 0);
}

DWORD WINAPI WarpEngine::ThreadThunk(LPVOID self) {
    static_cast<WarpEngine*>(self)->ThreadMain();
    return 0;
}

void WarpEngine::ThreadMain() {
    const HINSTANCE inst = GetModuleHandleW(nullptr);
    if (!impl_->CreateOverlayWindow(inst)) {
        SetError(T(Str::ErrOverlayCreate));
        InterlockedExchange(&running_, 0);
        InterlockedExchange(&active_, 0);
        return;
    }

    LONG applied = 0;
    DWORD nextRetryTick = 0;
    DWORD lastFrameTick = 0;

    while (InterlockedCompareExchange(&running_, 0, 0) != 0) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { InterlockedExchange(&running_, 0); break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (InterlockedCompareExchange(&running_, 0, 0) == 0) break;

        const LONG version = InterlockedCompareExchange(&version_, 0, 0);
        if (version != applied) {
            ScopedCriticalSection lock(stateMutex_);
            impl_->state = pending_;
            applied = version;
            impl_->gridDirty = true;
        }

        const bool want = (impl_->state.enabled || impl_->state.patternMode != 0) &&
                          !impl_->state.bypass;
        if (!want) {
            impl_->SetVisible(false);
            InterlockedExchange(&active_, 0);
            MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        const DWORD now = GetTickCount();
        if (impl_->needReinit) {
            if (now < nextRetryTick) {
                MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                continue;
            }
            impl_->SetVisible(false);
            if (!impl_->InitGraphics()) {
                impl_->ShutdownGraphics();
                nextRetryTick = GetTickCount() + kRetryTimeMs;
                SetError(T(Str::ErrPipelineInit));
                InterlockedExchange(&active_, 0);
                continue;
            }
            impl_->needReinit = false;
            SetError(L"");
        }

        const DWORD tick = GetTickCount();
        const DWORD elapsed = tick - lastFrameTick;
        if (lastFrameTick != 0 && elapsed < kFrameTimeMs) {
            MsgWaitForMultipleObjectsEx(
                0, nullptr, kFrameTimeMs - elapsed, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }
        lastFrameTick = tick;

        if (!impl_->CaptureFrame()) {
            impl_->SetVisible(false);
            InterlockedExchange(&active_, 0);
            SetError(T(impl_->captureWasBlank ? Str::ErrCaptureBlank
                                              : Str::ErrDuplicationUnavailable));
            MsgWaitForMultipleObjectsEx(0, nullptr, 250, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        impl_->UploadGrid();
        if (!impl_->Draw()) {
            impl_->SetVisible(false);
            InterlockedExchange(&active_, 0);
            continue;
        }

        impl_->SetVisible(true);
        InterlockedExchange(&active_, 1);
        SetError(L"");
    }

    impl_->SetVisible(false);
    impl_->ShutdownGraphics();
    if (impl_->hwnd) {
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
    InterlockedExchange(&active_, 0);
    LogLine(L"XP render thread stopped");
}

} // namespace crtb
