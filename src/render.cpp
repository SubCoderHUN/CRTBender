#include "i18n.h"
#include "render.h"
#include "shaders.h"
#include "util.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// Available since Windows 10 2004. Defined here so the project still builds
// against older SDK headers; the call is verified at runtime either way.
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace crtb {
namespace {

constexpr const wchar_t* kOverlayClass = L"CRTBenderOverlayWindow";
constexpr UINT kTimerTopmost = 1;

struct alignas(16) WarpConstants {
    float resX, resY, cellsX, cellsY;
    float lineHalfW, borderW, patternOpacity, patternSolid;
    float gridR, gridG, gridB, gridA;
    float borderR, borderG, borderB, borderA;
    float filterMode, pad0, pad1, pad2;
};
static_assert(sizeof(WarpConstants) % 16 == 0, "cbuffer must be 16-byte aligned");

template <class T, class U>
HRESULT QueryTo(U* src, ComPtr<T>& dst) {
    if (!src) return E_POINTER;
    return src->QueryInterface(__uuidof(T), dst.PutVoid());
}

bool CompileShader(const char* entry, const char* target, ComPtr<ID3DBlob>& blob) {
    ComPtr<ID3DBlob> errors;
    const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS;
    const HRESULT hr = D3DCompile(kWarpShaderHlsl, std::strlen(kWarpShaderHlsl), "warp.hlsl",
                                  nullptr, nullptr, entry, target, flags, 0,
                                  blob.Put(), errors.Put());
    if (FAILED(hr)) {
        if (errors && errors->GetBufferPointer()) {
            LogLine(L"Shader compile error: " +
                    Widen(std::string(static_cast<const char*>(errors->GetBufferPointer()),
                                      errors->GetBufferSize())));
        }
        LogHr(L"D3DCompile", hr);
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Render thread state
// ---------------------------------------------------------------------------

struct WarpEngine::Impl {
    WarpEngine* owner = nullptr;
    HWND        hwnd  = nullptr;

    ComPtr<ID3D11Device>             device;
    ComPtr<ID3D11DeviceContext>      ctx;
    ComPtr<IDXGISwapChain1>          swap;
    ComPtr<ID3D11RenderTargetView>   rtv;
    ComPtr<IDXGIOutput1>             output1;
    ComPtr<IDXGIOutputDuplication>   dupl;
    ComPtr<ID3D11Texture2D>          srcTex;
    ComPtr<ID3D11ShaderResourceView> srcSrv;
    ComPtr<ID3D11VertexShader>       vs;
    ComPtr<ID3D11PixelShader>        ps;
    ComPtr<ID3D11InputLayout>        layout;
    ComPtr<ID3D11Buffer>             vb, ib, cb;
    ComPtr<ID3D11SamplerState>       smpLinear, smpPoint;
    ComPtr<ID3D11RasterizerState>    raster;

    RECT deskRect{};
    int  width = 0, height = 0;

    int  vbTess     = -1;
    UINT indexCount = 0;

    bool needReinit   = true;
    bool visible      = false;
    bool haveFrame    = false;
    bool forceDraw    = true;
    bool gridDirty    = true;
    bool firstAcquire = true;

    UINT  srcWidth  = 0;
    UINT  srcHeight = 0;
    DWORD lastPresentTick = 0;

    // Until a frame with actual content shows up, the overlay stays hidden: a
    // full-screen black sheet over the desktop is far worse than no correction.
    bool  captureConfirmed  = false;
    DWORD lastCaptureCheck  = 0;
    bool  loggedBlankWarning = false;

    RenderState             state;
    std::vector<WarpVertex> verts;

    // Sizes srcTex from the frame DXGI actually hands us rather than from the
    // output rect. CopyResource fails silently when the two disagree, and a
    // silent failure here shows up as a fully black screen.
    bool EnsureSourceTexture(ID3D11Texture2D* frame);
    // Reads a few pixels back from the captured desktop. Answers the one
    // question a black screen leaves open: did we capture nothing, or capture
    // something and fail to draw it?
    bool CaptureIsBlank();

    bool CreateOverlayWindow(HINSTANCE inst);
    bool InitGraphics();
    void ShutdownGraphics();
    bool EnsureDuplication();
    bool EnsureGridBuffers();
    void UploadGrid();
    void Draw();
    void SetVisible(bool show);
};

// ---------------------------------------------------------------------------

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* impl = reinterpret_cast<WarpEngine::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_NCHITTEST:
        // Every click falls through to whatever is really underneath.
        return HTTRANSPARENT;

    case WM_ERASEBKGND:
        return 1;

    case WM_DISPLAYCHANGE:
        if (impl) {
            LogLine(L"Display mode changed, reinitializing pipeline");
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
    RegisterClassExW(&wc);   // harmless if it is already registered

    // WS_EX_LAYERED is not optional, and leaving it out is what made the whole
    // desktop unclickable in the first release.
    //
    // WS_EX_TRANSPARENT on its own only makes the window return HTTRANSPARENT
    // from hit testing, and the system then continues the hit test *within the
    // same thread* only. Nothing of ours sits underneath, so the click reached
    // no window at all: the desktop looked normal but swallowed every click.
    // WS_EX_LAYERED | WS_EX_TRANSPARENT together take the window out of hit
    // testing system wide, which is what an overlay actually needs.
    hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        kOverlayClass, L"CRTBender Overlay", WS_POPUP,
        0, 0, 16, 16, nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        LogLine(L"Overlay window creation failed");
        return false;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    // A layered window stays invisible until its attributes are set. Alpha 255:
    // the overlay replaces the desktop image rather than blending with it.
    if (!SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)) {
        LogLine(L"SetLayeredWindowAttributes failed");
        return false;
    }

    // Refuse to run unless both flags really stuck. Getting this wrong locks the
    // user out of their own mouse, so it is worth checking rather than assuming.
    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (!(exStyle & WS_EX_LAYERED) || !(exStyle & WS_EX_TRANSPARENT)) {
        LogLine(L"Overlay is not click-through (WS_EX_LAYERED|WS_EX_TRANSPARENT missing)");
        return false;
    }

    // Without this the overlay captures itself and the picture recurses.
    if (!SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)) {
        LogLine(L"SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed");
        return false;
    }
    DWORD affinity = 0;
    if (!GetWindowDisplayAffinity(hwnd, &affinity) || affinity != WDA_EXCLUDEFROMCAPTURE) {
        LogLine(L"Capture exclusion was not honoured by the system");
        return false;
    }

    SetTimer(hwnd, kTimerTopmost, 2000, nullptr);
    return true;
}

void WarpEngine::Impl::ShutdownGraphics() {
    if (dupl) dupl->ReleaseFrame();
    dupl.Reset();
    srcSrv.Reset();
    srcTex.Reset();
    rtv.Reset();
    swap.Reset();
    raster.Reset();
    smpPoint.Reset();
    smpLinear.Reset();
    cb.Reset();
    ib.Reset();
    vb.Reset();
    layout.Reset();
    ps.Reset();
    vs.Reset();
    output1.Reset();
    if (ctx) ctx->ClearState();
    ctx.Reset();
    device.Reset();

    vbTess       = -1;
    indexCount   = 0;
    haveFrame    = false;
    firstAcquire = true;
    srcWidth     = 0;
    srcHeight    = 0;

    captureConfirmed   = false;
    lastCaptureCheck   = 0;
    loggedBlankWarning = false;
}

bool WarpEngine::Impl::InitGraphics() {
    ShutdownGraphics();

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), factory.PutVoid());
    if (FAILED(hr)) { LogHr(L"CreateDXGIFactory1", hr); return false; }

    // Find the primary monitor's output and build the device on the adapter
    // that actually drives it: duplication only works same-adapter.
    POINT origin{ 0, 0 };
    HMONITOR primary = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);

    ComPtr<IDXGIAdapter1> chosenAdapter;
    DXGI_OUTPUT_DESC      outDesc{};
    bool found = false;

    for (UINT ai = 0; !found; ++ai) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(ai, adapter.Put()) == DXGI_ERROR_NOT_FOUND) break;

        for (UINT oi = 0;; ++oi) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(oi, output.Put()) == DXGI_ERROR_NOT_FOUND) break;

            DXGI_OUTPUT_DESC desc{};
            if (FAILED(output->GetDesc(&desc))) continue;
            if (desc.Monitor != primary) continue;
            if (FAILED(QueryTo(output.Get(), output1))) continue;

            chosenAdapter = adapter;
            outDesc       = desc;
            found         = true;
            break;
        }
    }
    if (!found) { LogLine(L"Primary DXGI output not found"); return false; }

    deskRect = outDesc.DesktopCoordinates;
    width    = deskRect.right - deskRect.left;
    height   = deskRect.bottom - deskRect.top;
    if (width <= 0 || height <= 0) { LogLine(L"Primary output has an empty rect"); return false; }

    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                         D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got{};
    hr = D3D11CreateDevice(chosenAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                           static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                           device.Put(), &got, ctx.Put());
    if (FAILED(hr)) { LogHr(L"D3D11CreateDevice", hr); return false; }

    // The overlay follows the physical desktop rect of the primary output.
    SetWindowPos(hwnd, HWND_TOPMOST, deskRect.left, deskRect.top, width, height,
                 SWP_NOACTIVATE | SWP_NOREDRAW);

    ComPtr<IDXGIFactory2> factory2;
    if (FAILED(QueryTo(factory.Get(), factory2))) {
        LogLine(L"IDXGIFactory2 unavailable");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width       = static_cast<UINT>(width);
    sc.Height      = static_cast<UINT>(height);
    sc.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc.SampleDesc  = { 1, 0 };
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = 2;
    sc.Scaling     = DXGI_SCALING_STRETCH;
    sc.AlphaMode   = DXGI_ALPHA_MODE_IGNORE;

    // The overlay has to be WS_EX_LAYERED to be click-through, and flip-model
    // swap chains are not supported on layered windows. The BitBlt model is,
    // and its extra full-screen copy is a fraction of a millisecond here.
    // present_mode=flip in the config forces the flip model back on for anyone
    // who wants to try it on a setup where it works.
    const bool wantFlip = state.flipModel;
    sc.SwapEffect = wantFlip ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;

    hr = factory2->CreateSwapChainForHwnd(device.Get(), hwnd, &sc, nullptr, nullptr, swap.Put());
    if (FAILED(hr) && wantFlip) {
        LogHr(L"CreateSwapChainForHwnd(flip)", hr);
        LogLine(L"Falling back to the BitBlt present model");
        sc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = factory2->CreateSwapChainForHwnd(device.Get(), hwnd, &sc, nullptr, nullptr, swap.Put());
    }
    if (FAILED(hr)) { LogHr(L"CreateSwapChainForHwnd", hr); return false; }
    LogLine(sc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD
                ? L"Present model: flip"
                : L"Present model: bitblt");
    factory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

    ComPtr<ID3D11Texture2D> backBuffer;
    hr = swap->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.PutVoid());
    if (FAILED(hr)) { LogHr(L"SwapChain::GetBuffer", hr); return false; }
    hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtv.Put());
    if (FAILED(hr)) { LogHr(L"CreateRenderTargetView", hr); return false; }

    // The private copy of the desktop is created on the first acquired frame,
    // sized from that frame - see EnsureSourceTexture.

    // Shaders. Model 4.0 keeps this working on anything that runs D3D11.
    ComPtr<ID3DBlob> vsBlob, psBlob;
    if (!CompileShader("VSMain", "vs_4_0", vsBlob)) return false;
    if (!CompileShader("PSMain", "ps_4_0", psBlob)) return false;

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                    nullptr, vs.Put());
    if (FAILED(hr)) { LogHr(L"CreateVertexShader", hr); return false; }
    hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                   nullptr, ps.Put());
    if (FAILED(hr)) { LogHr(L"CreatePixelShader", hr); return false; }

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = device->CreateInputLayout(elements, static_cast<UINT>(std::size(elements)),
                                   vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                   layout.Put());
    if (FAILED(hr)) { LogHr(L"CreateInputLayout", hr); return false; }

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth      = sizeof(WarpConstants);
    cbd.Usage          = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device->CreateBuffer(&cbd, nullptr, cb.Put());
    if (FAILED(hr)) { LogHr(L"CreateBuffer(constants)", hr); return false; }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD   = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&sd, smpLinear.Put());
    if (FAILED(hr)) { LogHr(L"CreateSamplerState(linear)", hr); return false; }

    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    hr = device->CreateSamplerState(&sd, smpPoint.Put());
    if (FAILED(hr)) { LogHr(L"CreateSamplerState(point)", hr); return false; }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;     // the warped grid can wind either way
    rd.DepthClipEnable = TRUE;
    hr = device->CreateRasterizerState(&rd, raster.Put());
    if (FAILED(hr)) { LogHr(L"CreateRasterizerState", hr); return false; }

    gridDirty = true;
    forceDraw = true;
    wchar_t msg[160];
    swprintf(msg, std::size(msg), L"Pipeline ready: %dx%d (feature level 0x%04X)",
             width, height, static_cast<unsigned>(got));
    LogLine(msg);
    return true;
}

bool WarpEngine::Impl::EnsureSourceTexture(ID3D11Texture2D* frame) {
    if (!frame || !device) return false;

    D3D11_TEXTURE2D_DESC fd{};
    frame->GetDesc(&fd);

    if (srcTex && srcSrv && fd.Width == srcWidth && fd.Height == srcHeight) return true;

    srcSrv.Reset();
    srcTex.Reset();

    // The duplication texture cannot be sampled directly (no shader-resource
    // bind flag), so we keep a private copy. It has to match the source exactly:
    // CopyResource is a no-op with a debug-layer warning when the descriptions
    // disagree, and on a release build that silently yields a black screen.
    D3D11_TEXTURE2D_DESC td{};
    td.Width      = fd.Width;
    td.Height     = fd.Height;
    td.MipLevels  = 1;
    td.ArraySize  = 1;
    td.Format     = fd.Format;
    td.SampleDesc = { 1, 0 };
    td.Usage      = D3D11_USAGE_DEFAULT;
    td.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&td, nullptr, srcTex.Put());
    if (FAILED(hr)) { LogHr(L"CreateTexture2D(source)", hr); return false; }
    hr = device->CreateShaderResourceView(srcTex.Get(), nullptr, srcSrv.Put());
    if (FAILED(hr)) { LogHr(L"CreateShaderResourceView", hr); srcTex.Reset(); return false; }

    srcWidth  = fd.Width;
    srcHeight = fd.Height;

    wchar_t msg[192];
    swprintf(msg, std::size(msg),
             L"Source texture %ux%u format=%u (overlay is %dx%d)",
             srcWidth, srcHeight, static_cast<unsigned>(fd.Format), width, height);
    LogLine(msg);
    return true;
}

bool WarpEngine::Impl::CaptureIsBlank() {
    if (!srcTex || !device || !ctx || srcWidth < 64 || srcHeight < 64) return false;

    D3D11_TEXTURE2D_DESC td{};
    srcTex->GetDesc(&td);

    constexpr UINT kPatch = 8;
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width          = kPatch;
    sd.Height         = kPatch;
    sd.MipLevels      = 1;
    sd.ArraySize      = 1;
    sd.Format         = td.Format;
    sd.SampleDesc     = { 1, 0 };
    sd.Usage          = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&sd, nullptr, staging.Put()))) return false;

    // Three widely separated patches, so one dark window cannot make a healthy
    // capture look dead.
    const UINT origins[3][2] = {
        { td.Width / 4,     td.Height / 4     },
        { td.Width / 2,     td.Height / 2     },
        { td.Width * 3 / 4, td.Height * 3 / 4 },
    };

    unsigned nonBlack = 0;
    for (const auto& origin : origins) {
        D3D11_BOX box{};
        box.left   = origin[0];
        box.top    = origin[1];
        box.right  = box.left + kPatch;
        box.bottom = box.top + kPatch;
        box.back   = 1;
        if (box.right > td.Width || box.bottom > td.Height) continue;

        ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, srcTex.Get(), 0, &box);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) continue;
        const auto* base = static_cast<const unsigned char*>(mapped.pData);
        for (UINT y = 0; y < kPatch; ++y) {
            for (UINT x = 0; x < kPatch; ++x) {
                const unsigned char* px = base + y * mapped.RowPitch + x * 4;
                if (px[0] || px[1] || px[2]) ++nonBlack;
            }
        }
        ctx->Unmap(staging.Get(), 0);
    }

    const bool blank = nonBlack == 0;
    if (!blank || !loggedBlankWarning) {
        wchar_t msg[224];
        swprintf(msg, std::size(msg),
                 L"Capture check: %u/%u non-black pixels across 3 patches of a %ux%u frame - %s",
                 nonBlack, 3u * kPatch * kPatch, srcWidth, srcHeight,
                 blank ? L"BLANK, the problem is upstream of the warp; overlay stays hidden"
                       : L"good");
        LogLine(msg);
        if (blank) loggedBlankWarning = true;
    }
    return blank;
}

bool WarpEngine::Impl::EnsureDuplication() {
    if (dupl) return true;
    if (!output1 || !device) return false;

    const HRESULT hr = output1->DuplicateOutput(device.Get(), dupl.Put());
    if (FAILED(hr)) {
        dupl.Reset();
        // DXGI_ERROR_NOT_CURRENTLY_AVAILABLE means another process already holds
        // the single allowed duplication, so we simply try again later.
        LogHr(L"DuplicateOutput", hr);
        return false;
    }
    firstAcquire = true;
    return true;
}

bool WarpEngine::Impl::EnsureGridBuffers() {
    const int tess = std::clamp(state.tessellation, 16, 256);
    if (vb && ib && vbTess == tess) return true;

    vb.Reset();
    ib.Reset();

    const int stride      = WarpGridStride(tess);
    const UINT vertexCount = static_cast<UINT>(stride) * static_cast<UINT>(stride);

    D3D11_BUFFER_DESC vbd{};
    vbd.ByteWidth      = vertexCount * sizeof(WarpVertex);
    vbd.Usage          = D3D11_USAGE_DYNAMIC;
    vbd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&vbd, nullptr, vb.Put()))) {
        LogLine(L"Vertex buffer creation failed");
        return false;
    }

    std::vector<unsigned int> indices;
    BuildWarpIndices(tess, indices);
    indexCount = static_cast<UINT>(indices.size());

    D3D11_BUFFER_DESC ibd{};
    ibd.ByteWidth = static_cast<UINT>(indices.size() * sizeof(unsigned int));
    ibd.Usage     = D3D11_USAGE_IMMUTABLE;
    ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = indices.data();
    if (FAILED(device->CreateBuffer(&ibd, &init, ib.Put()))) {
        LogLine(L"Index buffer creation failed");
        return false;
    }

    vbTess    = tess;
    gridDirty = true;
    return true;
}

void WarpEngine::Impl::UploadGrid() {
    if (!gridDirty || !vb) return;

    // A disabled correction still renders, as a strict pass-through: the mesh
    // is identity, so every vertex lands on its own texel and the bilinear tap
    // is exact. That keeps the A/B hotkey free of any re-scaling difference.
    static const WarpMesh kIdentity;
    const bool  bypass  = !state.enabled;
    const auto& mesh    = bypass ? kIdentity : state.mesh;
    const float zoom    = bypass ? 1.0f : state.overscan;
    const float bleed   = bypass ? 0.0f : state.edgeBleed;

    BuildWarpGrid(mesh, vbTess, zoom, bleed, verts);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    std::memcpy(mapped.pData, verts.data(), verts.size() * sizeof(WarpVertex));
    ctx->Unmap(vb.Get(), 0);

    gridDirty = false;
}

void WarpEngine::Impl::Draw() {
    if (!ctx || !rtv || !vb || !ib) return;

    WarpConstants c{};
    // Sampling maths works in source pixels; the source can legitimately differ
    // from the overlay size (rotation, odd modes).
    c.resX   = static_cast<float>(srcWidth  ? srcWidth  : static_cast<UINT>(width));
    c.resY   = static_cast<float>(srcHeight ? srcHeight : static_cast<UINT>(height));
    c.cellsX = static_cast<float>(std::max(2, state.patternCells));
    c.cellsY = c.cellsX;
    // Keep the crosshatch roughly square whatever the aspect ratio.
    if (height > 0)
        c.cellsY = std::max(2.0f, std::round(c.cellsX * static_cast<float>(height) /
                                             static_cast<float>(width)));
    c.lineHalfW      = 1.0f;
    c.borderW        = 2.0f;
    c.patternOpacity = state.patternMode == 0 ? 0.0f : state.patternOpacity;
    c.patternSolid   = state.patternMode == 2 ? 1.0f : 0.0f;
    c.gridR = 0.35f; c.gridG = 1.00f; c.gridB = 0.55f; c.gridA = 1.0f;
    c.borderR = 1.0f; c.borderG = 0.35f; c.borderB = 0.35f; c.borderA = 1.0f;
    c.filterMode = static_cast<float>(state.quality);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (SUCCEEDED(ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &c, sizeof(c));
        ctx->Unmap(cb.Get(), 0);
    }

    ID3D11RenderTargetView* targets[] = { rtv.Get() };
    ctx->OMSetRenderTargets(1, targets, nullptr);

    D3D11_VIEWPORT vp{};
    vp.Width    = static_cast<float>(width);
    vp.Height   = static_cast<float>(height);
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);
    ctx->RSSetState(raster.Get());

    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ctx->ClearRenderTargetView(rtv.Get(), clear);

    ID3D11Buffer* vbs[]      = { vb.Get() };
    const UINT    strides[]  = { sizeof(WarpVertex) };
    const UINT    offsets[]  = { 0 };
    ctx->IASetInputLayout(layout.Get());
    ctx->IASetVertexBuffers(0, 1, vbs, strides, offsets);
    ctx->IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->VSSetShader(vs.Get(), nullptr, 0);
    ctx->PSSetShader(ps.Get(), nullptr, 0);

    ID3D11Buffer* cbs[] = { cb.Get() };
    ctx->PSSetConstantBuffers(0, 1, cbs);

    ID3D11ShaderResourceView* srvs[] = { srcSrv.Get() };
    ctx->PSSetShaderResources(0, 1, srvs);

    ID3D11SamplerState* samplers[] = { smpLinear.Get(), smpPoint.Get() };
    ctx->PSSetSamplers(0, 2, samplers);

    ctx->DrawIndexed(indexCount, 0, 0);

    const HRESULT hr = swap->Present(1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        LogHr(L"Present", hr);
        needReinit = true;
    } else if (hr == DXGI_STATUS_OCCLUDED) {
        // Succeeded, but nothing reached the screen - typically because the
        // window was still hidden. Draw again rather than leaving whatever was
        // in the surface (which is black) sitting there.
        forceDraw = true;
    }
}

void WarpEngine::Impl::SetVisible(bool show) {
    if (!hwnd || visible == show) return;
    visible = show;
    if (show) {
        ShowWindow(hwnd, SW_SHOWNA);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // The frame we just presented went to a hidden window and may have been
        // dropped. Redraw straight away now that the window is really up.
        forceDraw       = true;
        lastPresentTick = 0;
    } else {
        ShowWindow(hwnd, SW_HIDE);
    }
}

// ---------------------------------------------------------------------------
// WarpEngine
// ---------------------------------------------------------------------------

WarpEngine::WarpEngine() : impl_(std::make_unique<Impl>()) { impl_->owner = this; }

WarpEngine::~WarpEngine() { Stop(); }

void WarpEngine::SetError(std::wstring message) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    lastError_ = std::move(message);
}

std::wstring WarpEngine::LastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return lastError_;
}

void WarpEngine::OnDisplayChanged() {
    if (notifyWindow_ && modeChangeMsg_)
        PostMessageW(notifyWindow_, modeChangeMsg_, 0, 0);
}

bool WarpEngine::Start(HWND notifyWindow, UINT modeChangeMsg) {
    if (running_.load()) return true;
    notifyWindow_  = notifyWindow;
    modeChangeMsg_ = modeChangeMsg;
    running_.store(true);
    thread_ = std::thread(&WarpEngine::ThreadMain, this);
    return true;
}

void WarpEngine::Stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) thread_.join();
        return;
    }
    // Nudge the thread out of its wait.
    if (impl_ && impl_->hwnd) PostMessageW(impl_->hwnd, WM_NULL, 0, 0);
    if (thread_.joinable()) thread_.join();
}

void WarpEngine::Update(const RenderState& state) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        pending_ = state;
    }
    version_.fetch_add(1, std::memory_order_release);
    if (impl_ && impl_->hwnd) PostMessageW(impl_->hwnd, WM_NULL, 0, 0);
}

void WarpEngine::ThreadMain() {
    HINSTANCE inst = GetModuleHandleW(nullptr);

    if (!impl_->CreateOverlayWindow(inst)) {
        SetError(T(Str::ErrOverlayCreate));
        running_.store(false);
        active_.store(false);
        return;
    }

    uint32_t applied       = 0;
    DWORD    nextRetryTick = 0;

    while (running_.load()) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running_.store(false); break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running_.load()) break;

        const uint32_t version = version_.load(std::memory_order_acquire);
        if (version != applied) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            impl_->state = pending_;
            applied      = version;
            impl_->gridDirty = true;
            impl_->forceDraw = true;
        }

        const bool want = impl_->state.enabled || impl_->state.patternMode != 0;
        if (!want) {
            if (impl_->visible) impl_->SetVisible(false);
            if (impl_->dupl) { impl_->dupl->ReleaseFrame(); impl_->dupl.Reset(); }
            impl_->haveFrame = false;
            active_.store(false);
            MsgWaitForMultipleObjectsEx(0, nullptr, 200, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        if (impl_->needReinit) {
            if (GetTickCount() < nextRetryTick) {
                MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                continue;
            }
            impl_->SetVisible(false);
            if (!impl_->InitGraphics()) {
                impl_->ShutdownGraphics();
                nextRetryTick = GetTickCount() + 2000;
                SetError(T(Str::ErrPipelineInit));
                active_.store(false);
                continue;
            }
            impl_->needReinit = false;
            SetError(L"");
        }

        if (!impl_->EnsureDuplication()) {
            // Typically another process holds the duplication, or an exclusive
            // fullscreen app owns the output. Back off and retry.
            impl_->SetVisible(false);
            active_.store(false);
            SetError(T(Str::ErrDuplicationUnavailable));
            MsgWaitForMultipleObjectsEx(0, nullptr, 500, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        if (!impl_->EnsureGridBuffers()) {
            impl_->needReinit = true;
            nextRetryTick     = GetTickCount() + 1000;
            continue;
        }

        bool newFrame = false;
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource>   resource;
        const HRESULT hr = impl_->dupl->AcquireNextFrame(8, &info, resource.Put());

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // Nothing changed on the desktop; nothing to redraw.
        } else if (FAILED(hr)) {
            LogHr(L"AcquireNextFrame", hr);
            impl_->dupl.Reset();
            if (hr != DXGI_ERROR_ACCESS_LOST) {
                impl_->needReinit = true;
                nextRetryTick     = GetTickCount() + 500;
            }
            continue;
        } else {
            // LastPresentTime stays zero for pointer-only updates. The cursor is
            // drawn by the hardware above the overlay, so those need no redraw -
            // except for the very first frame, which carries the initial image.
            if (info.LastPresentTime.QuadPart != 0 || impl_->firstAcquire) {
                ComPtr<ID3D11Texture2D> frame;
                if (SUCCEEDED(QueryTo(resource.Get(), frame)) &&
                    impl_->EnsureSourceTexture(frame.Get())) {
                    impl_->ctx->CopyResource(impl_->srcTex.Get(), frame.Get());
                    impl_->haveFrame    = true;
                    impl_->firstAcquire = false;
                    newFrame            = true;
                }
            }
            impl_->dupl->ReleaseFrame();
        }

        // Never cover the desktop with a black sheet. Reading pixels back stalls
        // the pipeline, so this only runs until one good frame has been seen,
        // and at most five times a second while the capture looks dead.
        if (impl_->haveFrame && !impl_->captureConfirmed) {
            const DWORD tick = GetTickCount();
            if (tick - impl_->lastCaptureCheck >= 200 || impl_->lastCaptureCheck == 0) {
                impl_->lastCaptureCheck = tick;
                if (impl_->CaptureIsBlank()) {
                    impl_->SetVisible(false);
                    active_.store(false);
                    SetError(T(Str::ErrCaptureBlank));
                    continue;
                }
                impl_->captureConfirmed = true;
                SetError(L"");
            } else {
                continue;   // wait for the next check window
            }
        }

        // A static desktop produces no new frames, so without a heartbeat a
        // single dropped present would leave the screen black indefinitely -
        // and because the overlay covers the desktop, nothing would ever change
        // to trigger a redraw. Four idle redraws a second costs nothing.
        const DWORD now = GetTickCount();
        if (impl_->visible && now - impl_->lastPresentTick > 250) impl_->forceDraw = true;

        if (impl_->haveFrame && (newFrame || impl_->forceDraw)) {
            impl_->UploadGrid();
            impl_->Draw();
            impl_->forceDraw      = false;
            impl_->lastPresentTick = GetTickCount();
            impl_->SetVisible(true);
            active_.store(true);
        }
    }

    impl_->SetVisible(false);
    impl_->ShutdownGraphics();
    if (impl_->hwnd) {
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
    active_.store(false);
    LogLine(L"Render thread stopped");
}

} // namespace crtb
