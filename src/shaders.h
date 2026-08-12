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
    float  gFilterMode;       // 0 = bilinear, 1 = bicubic, 2 = Lanczos-3 + anti-ringing
    float  gConvergence;      // 0 = one sample per pixel, 1 = red/green/blue separately
    float2 gPad;
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

// Lanczos-3 window. This is the sharpest of the three: for the sub-pixel shifts
// the correction actually applies, it holds on to high frequencies that bicubic
// rounds off, so corrected areas stay about as crisp as untouched ones.
float LanczosWeight(float x)
{
    x = abs(x);
    if (x < 1e-4)  return 1.0;
    if (x >= 3.0)  return 0.0;
    const float pi = 3.14159265358979;
    float px = pi * x;
    return (3.0 * sin(px) * sin(px / 3.0)) / (px * px);
}

// 36 tap Lanczos-3 with anti-ringing.
//
// A sharp kernel overshoots at hard edges, which around text looks like a halo.
// Clamping the result to the range of the four nearest samples removes exactly
// that overshoot and nothing else: a value between two neighbouring pixels can
// never legitimately fall outside their range.
float3 SampleLanczos(float2 uv)
{
    float2 coord = uv * gResolution - 0.5;
    float2 base  = floor(coord);
    float2 f     = coord - base;

    float wx[6];
    float wy[6];
    [unroll] for (int k = 0; k < 6; ++k)
    {
        float d = float(k) - 2.0;
        wx[k] = LanczosWeight(f.x - d);
        wy[k] = LanczosWeight(f.y - d);
    }

    float3 acc  = float3(0.0, 0.0, 0.0);
    float  wsum = 0.0;
    float3 lo   = float3( 1e6,  1e6,  1e6);
    float3 hi   = float3(-1e6, -1e6, -1e6);

    [unroll] for (int j = 0; j < 6; ++j)
    {
        [unroll] for (int i = 0; i < 6; ++i)
        {
            float2 offset = float2(float(i) - 2.0, float(j) - 2.0);
            float2 t      = (base + offset + 0.5) / gResolution;
            float3 s      = gTex.SampleLevel(gSmpPoint, saturate(t), 0.0).rgb;
            float  w      = wx[i] * wy[j];

            acc  += s * w;
            wsum += w;

            // The 2x2 block straddling the sample point defines the legal range.
            if (i >= 2 && i <= 3 && j >= 2 && j <= 3)
            {
                lo = min(lo, s);
                hi = max(hi, s);
            }
        }
    }

    float3 result = acc / max(wsum, 1e-5);
    return clamp(result, lo, hi);
}

float3 SampleDesktop(float2 uv)
{
    if (gFilterMode > 1.5) return SampleLanczos(uv);
    if (gFilterMode > 0.5) return saturate(SampleBicubic(uv));   // clamp the kernel's overshoot
    return gTex.Sample(gSmpLinear, uv).rgb;
}

float4 PSMain(VSOut input) : SV_Target
{
    float3 c;
    if (gConvergence > 0.5)
    {
        // Software convergence: green stays put and is the reference, red and
        // blue are fetched from where their beams actually land. Three times the
        // sampling work, so it only runs when there is something to correct.
        c.r = SampleDesktop(input.uv + input.conv.xy).r;
        c.g = SampleDesktop(input.uv).g;
        c.b = SampleDesktop(input.uv + input.conv.zw).b;
    }
    else
    {
        c = SampleDesktop(input.uv);
    }

    if (gPatternOpacity > 0.001)
    {
        c = lerp(c, float3(0.0, 0.0, 0.0), gPatternSolid);

        // Crosshatch. Distances are measured in source pixels so the lines keep
        // a constant apparent thickness whatever the resolution.
        float2 gp = input.uv * gCells;
        float2 fr = frac(gp);
        float2 dl = min(fr, 1.0 - fr) / gCells * gResolution;
        float  d  = min(dl.x, dl.y);
        float  a  = 1.0 - smoothstep(gLineHalfW - 0.75, gLineHalfW + 0.75, d);

        // Centre cross, drawn a touch heavier as an alignment anchor.
        float2 dc = abs(input.uv - 0.5) * gResolution;
        a = max(a, 1.0 - smoothstep(gLineHalfW + 0.25, gLineHalfW + 1.75, min(dc.x, dc.y)));

        c = lerp(c, gGridColor.rgb, saturate(a) * gPatternOpacity);

        // Screen border - the reference for judging edge straightness.
        float2 de = min(input.uv, 1.0 - input.uv) * gResolution;
        float  ab = 1.0 - smoothstep(gBorderW - 0.75, gBorderW + 0.75, min(de.x, de.y));
        c = lerp(c, gBorderColor.rgb, saturate(ab) * gPatternOpacity);
    }

    return float4(c, 1.0);
}
)HLSL";

} // namespace crtb
