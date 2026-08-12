// Host-side checks for the platform-independent warp math.
//
// warpmesh.cpp has no Windows dependencies, so this builds and runs anywhere:
//
//   g++ -std=c++17 -I../src -o test_warp test_warp.cpp ../src/warpmesh.cpp && ./test_warp
//
// or, from a configured build tree, `cmake --build . --target warp_tests`.
#include "warpmesh.h"
#include "geometry.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace crtb;

static int g_failures = 0;

static void Check(bool ok, const char* what) {
    if (!ok) { std::printf("  FAIL: %s\n", what); ++g_failures; }
    else     { std::printf("  ok  : %s\n", what); }
}

static void CheckNear(float a, float b, float tol, const char* what) {
    const bool ok = std::fabs(a - b) <= tol;
    if (!ok) std::printf("  FAIL: %s  (%.8f vs %.8f, tol %.8f)\n", what, a, b, tol);
    else     std::printf("  ok  : %s\n", what);
    if (!ok) ++g_failures;
}

int main() {
    std::printf("== the spline interpolates its control points ==\n");
    {
        WarpMesh m;
        m.Resize(5);
        m.At(0, 2).dy = 0.010f;
        m.At(0, 1).dy = 0.006f;
        m.At(1, 2).dy = 0.004f;
        m.At(3, 4).dx = -0.003f;

        const int n = m.Size();
        bool allMatch = true;
        float worst = 0.0f;
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) {
                const float u = static_cast<float>(c) / (n - 1);
                const float v = static_cast<float>(r) / (n - 1);
                const Offset e = m.Eval(u, v);
                worst = std::max(worst, std::fabs(e.dy - m.At(r, c).dy));
                worst = std::max(worst, std::fabs(e.dx - m.At(r, c).dx));
                if (std::fabs(e.dy - m.At(r, c).dy) > 1e-5f) allMatch = false;
                if (std::fabs(e.dx - m.At(r, c).dx) > 1e-5f) allMatch = false;
            }
        }
        std::printf("  worst deviation at control points: %.3e\n", worst);
        Check(allMatch, "Eval() reproduces every control point exactly");
    }

    std::printf("\n== the field is continuous (no creases at lattice lines) ==\n");
    {
        WarpMesh m;
        m.Resize(5);
        m.At(0, 2).dy = 0.012f;
        m.At(1, 1).dy = -0.005f;

        float maxJump = 0.0f;
        float prev = m.Eval(0.0f, 0.25f).dy;
        for (int i = 1; i <= 4000; ++i) {
            const float u = static_cast<float>(i) / 4000.0f;
            const float cur = m.Eval(u, 0.25f).dy;
            maxJump = std::max(maxJump, std::fabs(cur - prev));
            prev = cur;
        }
        std::printf("  max step between adjacent samples: %.3e\n", maxJump);
        Check(maxJump < 1e-4f, "no discontinuity crossing the lattice lines");
    }

    std::printf("\n== identity mesh maps 1:1 (lossless when disabled) ==\n");
    {
        WarpMesh identity;
        std::vector<WarpVertex> verts;
        const int tess = 32;
        WarpBuildParams params;
        params.mesh = &identity;
        params.tess = tess;
        BuildWarpGrid(params, verts);

        const int stride = WarpGridStride(tess);
        Check(static_cast<int>(verts.size()) == stride * stride, "vertex count matches the stride");

        bool exact = true;
        for (int r = 1; r < stride - 1; ++r) {
            for (int c = 1; c < stride - 1; ++c) {
                const WarpVertex& v = verts[r * stride + c];
                const float u = static_cast<float>(c - 1) / tess;
                const float w = static_cast<float>(r - 1) / tess;
                if (std::fabs(v.x - (u * 2.0f - 1.0f)) > 1e-6f) exact = false;
                if (std::fabs(v.y - (1.0f - w * 2.0f)) > 1e-6f) exact = false;
                if (std::fabs(v.u - u) > 1e-6f) exact = false;
                if (std::fabs(v.v - w) > 1e-6f) exact = false;
            }
        }
        Check(exact, "interior vertices land exactly on their own texels");

        // Corners of the interior grid must be the screen corners.
        const WarpVertex& tl = verts[1 * stride + 1];
        CheckNear(tl.x, -1.0f, 1e-6f, "top-left interior vertex is NDC (-1, +1) in x");
        CheckNear(tl.y, +1.0f, 1e-6f, "top-left interior vertex is NDC (-1, +1) in y");
    }

    std::printf("\n== bleed ring keeps the screen covered under displacement ==\n");
    {
        // Worst realistic case: push the whole top row down hard.
        WarpMesh m;
        m.Resize(5);
        for (int c = 0; c < m.Size(); ++c) m.At(0, c).dy = 0.02f;

        float maxDx = 0.0f, maxDy = 0.0f;
        m.MaxMagnitude(maxDx, maxDy);
        const float bleed = std::max(maxDx, maxDy) + 0.002f;   // same rule as EffectiveEdgeBleed

        std::vector<WarpVertex> verts;
        const int tess = 64;
        WarpBuildParams params;
        params.mesh      = &m;
        params.tess      = tess;
        params.edgeBleed = bleed;
        BuildWarpGrid(params, verts);
        const int stride = WarpGridStride(tess);

        // Every column of the outer ring must sit above the top of the screen
        // (NDC y >= 1), otherwise a black band appears.
        bool covered = true;
        float worstTop = std::numeric_limits<float>::max();
        for (int c = 0; c < stride; ++c) {
            const WarpVertex& v = verts[0 * stride + c];
            worstTop = std::min(worstTop, v.y);
            if (v.y < 1.0f) covered = false;
        }
        std::printf("  lowest top-ring vertex: NDC y = %.6f (needs >= 1.0)\n", worstTop);
        Check(covered, "top bleed ring still covers the screen edge");

        float worstBottom = -std::numeric_limits<float>::max();
        for (int c = 0; c < stride; ++c) {
            const WarpVertex& v = verts[(stride - 1) * stride + c];
            worstBottom = std::max(worstBottom, v.y);
        }
        std::printf("  highest bottom-ring vertex: NDC y = %.6f (needs <= -1.0)\n", worstBottom);
        Check(worstBottom <= -1.0f, "bottom bleed ring still covers the screen edge");

        // Bleed vertices must keep clamped texcoords, or they would sample
        // outside the desktop and stretch content instead of smearing it.
        bool clamped = true;
        for (int c = 0; c < stride; ++c) {
            if (verts[c].v != 0.0f) clamped = false;
            if (verts[(stride - 1) * stride + c].v != 1.0f) clamped = false;
        }
        Check(clamped, "bleed ring texcoords are clamped to the edge");
    }

    std::printf("\n== index buffer is well formed ==\n");
    {
        const int tess = 16;
        std::vector<unsigned int> indices;
        BuildWarpIndices(tess, indices);
        const int stride = WarpGridStride(tess);
        const unsigned int vertexCount = static_cast<unsigned int>(stride) * stride;

        Check(indices.size() == static_cast<size_t>(stride - 1) * (stride - 1) * 6,
              "two triangles per quad");
        unsigned int maxIndex = 0;
        for (unsigned int i : indices) maxIndex = std::max(maxIndex, i);
        std::printf("  max index %u, vertex count %u\n", maxIndex, vertexCount);
        Check(maxIndex < vertexCount, "no index runs past the vertex buffer");
    }

    std::printf("\n== resizing the lattice preserves the shape ==\n");
    {
        WarpMesh m;
        m.Resize(5);
        m.At(0, 2).dy = 0.010f;
        m.At(0, 1).dy = 0.005f;
        m.At(0, 3).dy = 0.005f;

        // Sample the field before and after a 5x5 -> 9x9 resize.
        float before[11][11];
        for (int i = 0; i <= 10; ++i)
            for (int j = 0; j <= 10; ++j)
                before[i][j] = m.Eval(j / 10.0f, i / 10.0f).dy;

        m.Resize(9);
        Check(m.Size() == 9, "grid resized to 9x9");

        float worst = 0.0f;
        for (int i = 0; i <= 10; ++i)
            for (int j = 0; j <= 10; ++j)
                worst = std::max(worst, std::fabs(before[i][j] - m.Eval(j / 10.0f, i / 10.0f).dy));
        std::printf("  worst field change after resize: %.3e (0.010 peak amplitude)\n", worst);
        Check(worst < 0.002f, "deformation survives a lattice resize");
    }

    std::printf("\n== a realistic top-bow correction ==\n");
    {
        // 1600x1200 on a 5x5 lattice: cancel a top edge that bows ~6 px up.
        const float screenH = 1200.0f;
        WarpMesh m;
        m.Resize(5);
        m.At(0, 1).dy = 4.0f / screenH;
        m.At(0, 2).dy = 6.0f / screenH;
        m.At(0, 3).dy = 4.0f / screenH;

        const float centreTopPx = m.Eval(0.5f, 0.0f).dy * screenH;
        CheckNear(centreTopPx, 6.0f, 0.01f, "top centre pushed down by 6 px");

        // v = 0.25 is control row 1 itself on a 5x5 lattice, so sample between
        // the rows to see the falloff.
        const float eighthPx = m.Eval(0.5f, 0.125f).dy * screenH;
        std::printf("  an eighth down the screen: %.2f px\n", eighthPx);
        Check(eighthPx > 0.0f && eighthPx < 6.0f, "correction fades towards the middle");

        const float halfPx = m.Eval(0.5f, 0.5f).dy * screenH;
        std::printf("  screen middle: %.2f px\n", halfPx);
        CheckNear(halfPx, 0.0f, 0.01f, "the untouched lower half stays put");

        // Catmull-Rom can overshoot. Anything meaningful below the corrected
        // band would bend the good half of the picture the wrong way.
        float worstUndershoot = 0.0f;
        float worstAt = 0.0f;
        for (int i = 0; i <= 2000; ++i) {
            const float v = 0.25f + 0.75f * static_cast<float>(i) / 2000.0f;
            const float px = m.Eval(0.5f, v).dy * screenH;
            if (px < worstUndershoot) { worstUndershoot = px; worstAt = v; }
        }
        std::printf("  worst ringing below the corrected band: %.4f px at v=%.3f\n",
                    worstUndershoot, worstAt);
        Check(worstUndershoot > -0.30f, "ringing below the corrected band stays under 0.3 px");

        // And the profile must be monotone enough not to look wavy in the band.
        float prev = 6.0f;
        bool monotone = true;
        for (int i = 1; i <= 1000; ++i) {
            const float v = 0.25f * static_cast<float>(i) / 1000.0f;
            const float px = m.Eval(0.5f, v).dy * screenH;
            if (px > prev + 1e-3f) monotone = false;
            prev = px;
        }
        Check(monotone, "the correction decays monotonically from the top edge");

        const float bottomPx = m.Eval(0.5f, 1.0f).dy * screenH;
        CheckNear(bottomPx, 0.0f, 0.01f, "the bottom edge stays put");
    }

    std::printf("\n== the default lattice is 15x15 ==\n");
    {
        WarpMesh m;
        std::printf("  default size: %dx%d\n", m.Size(), m.Size());
        Check(m.Size() == WarpMesh::kDefaultSize, "default grid is kDefaultSize");
        Check(WarpMesh::kDefaultSize == 15, "kDefaultSize is 15");
        Check(WarpMesh::kMaxSize >= 21, "lattice can go up to at least 21x21");

        // The stack buffers inside Eval() are sized by kMaxSize; a lattice at
        // the limit must still evaluate correctly rather than overrun them.
        WarpMesh big;
        big.Resize(WarpMesh::kMaxSize);
        Check(big.Size() == WarpMesh::kMaxSize, "resize to kMaxSize works");
        big.At(WarpMesh::kMaxSize - 1, WarpMesh::kMaxSize - 1).dy = 0.01f;
        const Offset corner = big.Eval(1.0f, 1.0f);
        CheckNear(corner.dy, 0.01f, 1e-6f, "far corner of a 21x21 lattice reads back exactly");
    }

    std::printf("\n== the whole screen is reachable, not just the top ==\n");
    {
        // Every quadrant plus the centre gets its own displacement; each must
        // come back exactly, and none may leak into the others.
        WarpMesh m;                      // 15x15
        const int n = m.Size();
        const int mid = n / 2;

        m.At(0, 0).dy         =  0.004f;   // top-left
        m.At(0, n - 1).dy     = -0.004f;   // top-right
        m.At(n - 1, 0).dy     = -0.003f;   // bottom-left
        m.At(n - 1, n - 1).dy =  0.003f;   // bottom-right
        m.At(mid, mid).dy     =  0.006f;   // dead centre
        m.At(mid, 0).dy       =  0.002f;   // left edge, halfway down
        m.At(n - 1, mid).dy   = -0.005f;   // bottom edge, middle

        CheckNear(m.Eval(0.0f, 0.0f).dy,  0.004f, 1e-6f, "top-left corner");
        CheckNear(m.Eval(1.0f, 0.0f).dy, -0.004f, 1e-6f, "top-right corner");
        CheckNear(m.Eval(0.0f, 1.0f).dy, -0.003f, 1e-6f, "bottom-left corner");
        CheckNear(m.Eval(1.0f, 1.0f).dy,  0.003f, 1e-6f, "bottom-right corner");
        CheckNear(m.Eval(0.5f, 0.5f).dy,  0.006f, 1e-6f, "screen centre");
        CheckNear(m.Eval(0.0f, 0.5f).dy,  0.002f, 1e-6f, "left edge, halfway down");
        CheckNear(m.Eval(0.5f, 1.0f).dy, -0.005f, 1e-6f, "bottom edge, middle");

        // An untouched control point stays put even with neighbours pulling.
        const float u = 3.0f / (n - 1);
        const float v = 3.0f / (n - 1);
        CheckNear(m.Eval(u, v).dy, 0.0f, 1e-6f, "an untouched point keeps its zero");

        // And the field never exceeds the range its control points span.
        float lo = 0.0f, hi = 0.0f;
        for (int i = 0; i <= 300; ++i) {
            for (int j = 0; j <= 300; ++j) {
                const float d = m.Eval(j / 300.0f, i / 300.0f).dy;
                lo = std::min(lo, d);
                hi = std::max(hi, d);
            }
        }
        std::printf("  field range %.5f .. %.5f (controls span -0.00500 .. 0.00600)\n", lo, hi);
        Check(lo >= -0.005f - 1e-5f && hi <= 0.006f + 1e-5f,
              "no overshoot anywhere across the whole screen");
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
