// CRTBender - HLSL for the warp pass, compiled at runtime with D3DCompile so
// the build needs no fxc step.
//
// The geometry is a densely tessellated grid whose vertex *positions* carry the
// correction while the texcoords stay on the regular lattice. That makes the
// rasterizer do the interpolation for us and keeps the shader trivial.
#pragma once

namespace crtb {

inline const char* kWarpShaderHlsl = R"HLSL(

cbuffer Params : register(b0)
{
    float2 gResolution;       // source desktop size in pixels
    float2 gCells;            // test pattern crosshatch cells per axis
    float  gLineHalfW;        // pattern line half width, pixels
    float  gBorderW;          // pattern border width, pixels
    float  gPatternOpacity;   // 0 = pattern off
    float  gPatternSolid;     // 1 = draw the pattern on black instead of the desktop
    float4 gGridColor;
    float4 gBorderColor;
    float  gFilterMode;       // 0 = bilinear, 1 = bicubic, 2 = adaptive sharp
    float  gConvergence;      // 0 = one sample per pixel, 1 = red/green/blue separately
    float  gPatternType;      // see TestPattern in config.h
    float  gSharpness;        // contrast-adaptive sharpening, 0..1
    float2 gOutputResolution; // overlay viewport size in pixels
    float2 gOutputPad;
};

Texture2D    gTex        : register(t0);
SamplerState gSmpLinear  : register(s0);
SamplerState gSmpPoint   : register(s1);

struct VSIn  {
    float2 pos  : POSITION;
    float2 uv   : TEXCOORD0;
    float4 conv : TEXCOORD1;   // xy = red offset, zw = blue offset
};
struct VSOut {
    float4 pos  : SV_Position;
    float2 uv   : TEXCOORD0;
    float4 conv : TEXCOORD1;
};

VSOut VSMain(VSIn input)
{
    VSOut o;
    o.pos  = float4(input.pos, 0.0, 1.0);
    o.uv   = input.uv;
    o.conv = input.conv;
    return o;
}

// Catmull-Rom kernel (Mitchell-Netravali with B=0, C=0.5).
float CRWeight(float x)
{
    x = abs(x);
    float x2 = x * x;
    float x3 = x2 * x;
    if (x < 1.0) return (1.5 * x3 - 2.5 * x2 + 1.0);
    if (x < 2.0) return (-0.5 * x3 + 2.5 * x2 - 4.0 * x + 2.0);
    return 0.0;
}

// 16 tap bicubic. The whole point of this program is resampling the desktop,
// so it is worth spending a few extra fetches to keep text legible.
float3 SampleBicubic(float2 uv)
{
    float2 coord = uv * gResolution - 0.5;
    float2 base  = floor(coord);
    float2 f     = coord - base;

    float3 acc  = float3(0.0, 0.0, 0.0);
    float  wsum = 0.0;

    [unroll] for (int j = -1; j <= 2; ++j)
    {
        float wy = CRWeight(f.y - float(j));
        [unroll] for (int i = -1; i <= 2; ++i)
        {
            float  wx = CRWeight(f.x - float(i));
            float  w  = wx * wy;
            float2 t  = (base + float2(float(i), float(j)) + 0.5) / gResolution;
            acc  += gTex.SampleLevel(gSmpPoint, saturate(t), 0.0).rgb * w;
            wsum += w;
        }
    }
    return acc / max(wsum, 1e-5);
}

// Bicubic reconstruction followed by a small contrast-adaptive detail pass.
// The cross-shaped neighbourhood limits the result to colours that already
// exist nearby, which keeps text crisp without adding bright or dark halos.
float3 SampleAdaptiveSharp(float2 uv, float amount)
{
    float3 bicubic = saturate(SampleBicubic(uv));
    [branch] if (amount < 0.001) return bicubic;

    float2 texel = 1.0 / gResolution;
    float3 centre = gTex.SampleLevel(gSmpLinear, uv, 0.0).rgb;
    float3 north  = gTex.SampleLevel(gSmpLinear, saturate(uv - float2(0.0, texel.y)), 0.0).rgb;
    float3 south  = gTex.SampleLevel(gSmpLinear, saturate(uv + float2(0.0, texel.y)), 0.0).rgb;
    float3 west   = gTex.SampleLevel(gSmpLinear, saturate(uv - float2(texel.x, 0.0)), 0.0).rgb;
    float3 east   = gTex.SampleLevel(gSmpLinear, saturate(uv + float2(texel.x, 0.0)), 0.0).rgb;

    float3 lo = min(centre, min(min(north, south), min(west, east)));
    float3 hi = max(centre, max(max(north, south), max(west, east)));
    float3 base = clamp(bicubic, lo, hi);
    float3 blur = (north + south + west + east) * 0.25;

    float3 span = hi - lo;
    float contrast = max(span.r, max(span.g, span.b));
    float adaptive = lerp(0.35, 1.0, saturate(contrast * 4.0));
    float3 sharpened = base + (base - blur) * amount * adaptive;
    return clamp(sharpened, lo, hi);
}

float3 SampleDesktop(float2 uv, float warpMask)
{
    if (gFilterMode > 1.5)
    {
        // Identity-mapped pixels stay on the exact one-tap path. Only pixels
        // that are actually resampled pay for bicubic reconstruction.
        [branch] if (warpMask < 0.001)
            return gTex.Sample(gSmpLinear, uv).rgb;
        return SampleAdaptiveSharp(uv, gSharpness * warpMask);
    }
    if (gFilterMode > 0.5) return saturate(SampleBicubic(uv));   // clamp the kernel's overshoot
    return gTex.Sample(gSmpLinear, uv).rgb;
}

float3 SampleSource(float2 uv, float warpMask)
{
    uv = saturate(uv);
    float3 c = SampleDesktop(uv, warpMask);

    if (gPatternOpacity > 0.001)
    {
        c = lerp(c, float3(0.0, 0.0, 0.0), gPatternSolid);

        if (gPatternType < 0.5)
        {
            // Geometry grid. Distances are measured in source pixels, so lines
            // keep the same apparent thickness at every resolution.
            float2 gp = uv * gCells;
            float2 fr = frac(gp);
            float2 dl = min(fr, 1.0 - fr) / gCells * gResolution;
            float  d  = min(dl.x, dl.y);
            float  a  = 1.0 - smoothstep(gLineHalfW - 0.75, gLineHalfW + 0.75, d);

            float2 dc = abs(uv - 0.5) * gResolution;
            a = max(a, 1.0 - smoothstep(
                gLineHalfW + 0.25, gLineHalfW + 1.75, min(dc.x, dc.y)));
            c = lerp(c, gGridColor.rgb, saturate(a) * gPatternOpacity);

            float2 de = min(uv, 1.0 - uv) * gResolution;
            float ab = 1.0 - smoothstep(
                gBorderW - 0.75, gBorderW + 0.75, min(de.x, de.y));
            c = lerp(c, gBorderColor.rgb, saturate(ab) * gPatternOpacity);
        }
        else if (gPatternType < 1.5)
        {
            // Eight familiar broadcast-style colour bars.
            float x = saturate(uv.x);
            float3 bar;
            if      (x < 0.125) bar = float3(1.0, 1.0, 1.0);
            else if (x < 0.250) bar = float3(1.0, 1.0, 0.0);
            else if (x < 0.375) bar = float3(0.0, 1.0, 1.0);
            else if (x < 0.500) bar = float3(0.0, 1.0, 0.0);
            else if (x < 0.625) bar = float3(1.0, 0.0, 1.0);
            else if (x < 0.750) bar = float3(1.0, 0.0, 0.0);
            else if (x < 0.875) bar = float3(0.0, 0.0, 1.0);
            else                bar = float3(0.0, 0.0, 0.0);
            c = lerp(c, bar, gPatternOpacity);
        }
        else if (gPatternType < 2.5)
        {
            // Smooth ramp above, sixteen discrete steps below.
            float grey = uv.x;
            if (uv.y > 0.75)
                grey = min(floor(saturate(uv.x) * 16.0), 15.0) / 15.0;
            c = lerp(c, grey.xxx, gPatternOpacity);
        }
        else if (gPatternType < 3.5)
        {
            // A white-on-black grid makes even a one-pixel RGB split obvious.
            float2 cells = gCells * 2.0;
            float2 fr = frac(uv * cells);
            float2 dl = min(fr, 1.0 - fr) / cells * gResolution;
            float lineCoverage = 1.0 - smoothstep(0.25, 1.25, min(dl.x, dl.y));
            c = lerp(c, lineCoverage.xxx, gPatternOpacity);
        }
        else if (gPatternType < 4.5)
        {
            // Single-pixel checkerboard for focus and bandwidth checks.
            float2 pixel = floor(uv * gResolution);
            float checker = fmod(pixel.x + pixel.y, 2.0);
            c = lerp(c, checker.xxx, gPatternOpacity);
        }
        else
        {
            // Nested 5% and 10% safe-area guides plus the centre cross.
            float2 p = uv;
            float2 dOuter = min(p, 1.0 - p) * gResolution;
            float outer = 1.0 - smoothstep(0.25, 1.25, min(dOuter.x, dOuter.y));

            float2 dFive = abs(min(p, 1.0 - p) - 0.05) * gResolution;
            float five = 1.0 - smoothstep(0.25, 1.25, min(dFive.x, dFive.y));
            float2 dTen = abs(min(p, 1.0 - p) - 0.10) * gResolution;
            float ten = 1.0 - smoothstep(0.25, 1.25, min(dTen.x, dTen.y));
            float2 dCentre = abs(p - 0.5) * gResolution;
            float centre = 1.0 - smoothstep(0.25, 1.25, min(dCentre.x, dCentre.y));

            float3 guide = float3(0.0, 0.0, 0.0);
            guide = lerp(guide, float3(1.0, 0.2, 0.2), outer);
            guide = lerp(guide, float3(0.2, 1.0, 0.2), five);
            guide = lerp(guide, float3(0.2, 0.5, 1.0), ten);
            guide = lerp(guide, float3(1.0, 1.0, 1.0), centre);
            c = lerp(c, guide, gPatternOpacity);
        }
    }

    return c;
}

float4 PSMain(VSOut input) : SV_Target
{
    float2 outputUv = input.pos.xy / max(gOutputResolution, 1.0);
    float2 greenDeltaPx = abs((input.uv - outputUv) * gOutputResolution);
    float greenShiftPx = max(greenDeltaPx.x, greenDeltaPx.y);
    float greenMask = smoothstep(0.05, 0.75, greenShiftPx);

    float3 c;
    if (gConvergence > 0.5)
    {
        // Green is the reference. Patterns are part of the sampled source, so
        // convergence controls move their RGB channels exactly like real image
        // content on both rendering backends.
        float2 redDeltaPx =
            abs((input.uv + input.conv.xy - outputUv) * gOutputResolution);
        float2 blueDeltaPx =
            abs((input.uv + input.conv.zw - outputUv) * gOutputResolution);
        float redMask = smoothstep(0.05, 0.75, max(redDeltaPx.x, redDeltaPx.y));
        float blueMask = smoothstep(0.05, 0.75, max(blueDeltaPx.x, blueDeltaPx.y));

        c.r = SampleSource(input.uv + input.conv.xy, redMask).r;
        c.g = SampleSource(input.uv, greenMask).g;
        c.b = SampleSource(input.uv + input.conv.zw, blueMask).b;
    }
    else
    {
        c = SampleSource(input.uv, greenMask);
    }
    return float4(c, 1.0);
}
)HLSL";

} // namespace crtb
