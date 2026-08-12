// Checks on the parametric geometry and convergence layers.
//
// These fields are analytic, so they are exactly the kind of thing that can be
// subtly wrong on screen and impossible to spot by eye. Everything here builds
// and runs off-Windows:
//
//   g++ -std=c++17 -I../src -o geometry_tests test_geometry.cpp
//       ../src/geometry.cpp ../src/warpmesh.cpp && ./geometry_tests
#include "geometry.h"
#include "warpmesh.h"

#include <cmath>
#include <cstdio>

using namespace crtb;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

void CheckNear(float a, float b, float tol, const char* what) {
    const bool ok = std::fabs(a - b) <= tol;
    if (!ok) std::printf("  FAIL: %s  (%.6f vs %.6f)\n", what, a, b);
    else     std::printf("  ok  : %s\n", what);
    if (!ok) ++g_failures;
}

constexpr float kAspect = 1600.0f / 1200.0f;   // the 4:3 tube this was written for
constexpr float kWidth  = 1600.0f;
constexpr float kHeight = 1200.0f;

} // namespace

int main() {
    std::printf("== a blank parametric layer moves nothing ==\n");
    {
        GeometryParams g;
        Check(!g.Any(), "Any() is false by default");
        bool allZero = true;
        for (int i = 0; i <= 10; ++i) {
            for (int j = 0; j <= 10; ++j) {
                const Offset o = g.At(j / 10.0f, i / 10.0f, kAspect);
                if (o.dx != 0.0f || o.dy != 0.0f) allZero = false;
            }
        }
        Check(allZero, "the field is exactly zero everywhere");
    }

    std::printf("\n== position and size behave as named ==\n");
    {
        GeometryParams g;
        g.hPosition = 0.01f;
        Check(g.Any(), "Any() notices a set parameter");
        CheckNear(g.At(0.0f, 0.0f, kAspect).dx, 0.01f, 1e-6f, "shift is uniform at one corner");
        CheckNear(g.At(1.0f, 1.0f, kAspect).dx, 0.01f, 1e-6f, "and the same at the other");

        GeometryParams size;
        size.hSize = 0.01f;
        CheckNear(size.At(0.0f, 0.5f, kAspect).dx, -0.01f, 1e-6f, "width pushes the left edge out");
        CheckNear(size.At(1.0f, 0.5f, kAspect).dx, +0.01f, 1e-6f, "and the right edge the other way");
        CheckNear(size.At(0.5f, 0.5f, kAspect).dx,  0.0f,  1e-6f, "the centre does not move");
    }

    std::printf("\n== rotation is a real rotation in pixels ==\n");
    {
        // dx is a fraction of width and dy a fraction of height, so without an
        // aspect correction a "rotation" would come out as a shear on any
        // non-square screen. In pixels it has to be dxPx = -a*t*H, dyPx = a*s*W.
        GeometryParams g;
        g.rotation = 0.01f;

        const float s = 1.0f, t = -1.0f;              // top-right corner
        const Offset o = g.At(1.0f, 0.0f, kAspect);
        CheckNear(o.dx * kWidth,  -g.rotation * t * kHeight, 0.01f, "horizontal term is -a*t*H");
        CheckNear(o.dy * kHeight,  g.rotation * s * kWidth,  0.01f, "vertical term is a*s*W");
        CheckNear(g.At(0.5f, 0.5f, kAspect).dx, 0.0f, 1e-6f, "the centre stays put (x)");
        CheckNear(g.At(0.5f, 0.5f, kAspect).dy, 0.0f, 1e-6f, "the centre stays put (y)");

        // Opposite corners must move in opposite directions.
        const Offset tl = g.At(0.0f, 0.0f, kAspect);
        const Offset br = g.At(1.0f, 1.0f, kAspect);
        Check(tl.dx * br.dx < 0.0f && tl.dy * br.dy < 0.0f,
              "opposite corners swing opposite ways");
    }

    std::printf("\n== the edge bows stay on their own half ==\n");
    {
        // This is the property the whole feature rests on: correcting a bowed
        // top edge must not disturb a bottom half that is already straight.
        GeometryParams g;
        g.topBow = 0.005f;

        CheckNear(g.At(0.5f, 0.0f, kAspect).dy, 0.005f, 1e-6f, "full strength at the top edge");

        float worstBelow = 0.0f;
        for (int i = 0; i <= 200; ++i) {
            const float v = 0.5f + 0.5f * static_cast<float>(i) / 200.0f;
            for (int j = 0; j <= 20; ++j) {
                const float u = static_cast<float>(j) / 20.0f;
                worstBelow = std::max(worstBelow, std::fabs(g.At(u, v, kAspect).dy));
            }
        }
        std::printf("    worst displacement at or below mid-screen: %.8f\n", worstBelow);
        Check(worstBelow == 0.0f, "exactly zero from the middle downwards");

        // And it must fade, not step: no jump big enough to show as a crease.
        float maxStep = 0.0f;
        float prev = g.At(0.5f, 0.0f, kAspect).dy;
        for (int i = 1; i <= 2000; ++i) {
            const float cur = g.At(0.5f, static_cast<float>(i) / 2000.0f, kAspect).dy;
            maxStep = std::max(maxStep, std::fabs(cur - prev));
            prev = cur;
        }
        std::printf("    largest step between adjacent samples: %.3e\n", maxStep);
        Check(maxStep < 1e-4f, "the fade is smooth");

        GeometryParams bottom;
        bottom.bottomBow = 0.005f;
        CheckNear(bottom.At(0.5f, 1.0f, kAspect).dy, 0.005f, 1e-6f, "bottom bow reaches its edge");
        CheckNear(bottom.At(0.5f, 0.25f, kAspect).dy, 0.0f, 1e-6f, "and leaves the top alone");

        // The bows also taper towards the left and right edges, which is what
        // makes them a bow rather than a shift of the whole edge.
        Check(std::fabs(g.At(0.0f, 0.0f, kAspect).dy) < 1e-6f, "the bow vanishes at the corners");
    }

    std::printf("\n== pincushion bows the sides ==\n");
    {
        GeometryParams g;
        g.pincushion = 0.01f;
        CheckNear(g.At(0.0f, 0.5f, kAspect).dx, -0.01f, 1e-6f, "left edge bows at mid-height");
        CheckNear(g.At(1.0f, 0.5f, kAspect).dx, +0.01f, 1e-6f, "right edge bows the other way");
        CheckNear(g.At(0.0f, 0.0f, kAspect).dx,  0.0f,  1e-6f, "corners are untouched");
        CheckNear(g.At(0.0f, 1.0f, kAspect).dx,  0.0f,  1e-6f, "both of them");
    }

    std::printf("\n== convergence offsets ==\n");
    {
        ConvergenceParams c;
        Check(!c.Any(), "Any() is false by default");

        c.rH     = 0.001f;
        c.bVEdge = 0.002f;
        Check(c.Any(), "Any() notices a set parameter");

        Offset red, blue;
        c.At(0.5f, 0.5f, red, blue);
        CheckNear(red.dx, 0.001f, 1e-6f, "red keeps its uniform offset at the centre");
        CheckNear(blue.dy, 0.0f, 1e-6f, "blue's edge term is zero at the centre");

        c.At(0.5f, 1.0f, red, blue);
        CheckNear(blue.dy, 0.002f, 1e-6f, "blue's edge term is full at the bottom");
        c.At(0.5f, 0.0f, red, blue);
        CheckNear(blue.dy, -0.002f, 1e-6f, "and inverted at the top");
    }

    std::printf("\n== the layers add without disturbing each other ==\n");
    {
        // A parametric slider and a hand-tuned point must sum, and neither may
        // silently rewrite the other.
        WarpMesh mesh;
        mesh.At(0, mesh.Size() / 2).dy = 0.004f;

        GeometryParams geo;
        geo.vPosition = 0.002f;

        constexpr int tess = 32;
        std::vector<WarpVertex> withMeshOnly, withBoth;

        WarpBuildParams params;
        params.mesh   = &mesh;
        params.tess   = tess;
        params.aspect = kAspect;
        BuildWarpGrid(params, withMeshOnly);

        params.geometry = &geo;
        BuildWarpGrid(params, withBoth);

        const int stride = WarpGridStride(tess);
        float worstDelta = 0.0f;
        for (int i = 0; i < stride * stride; ++i) {
            // vPosition shifts everything down by a constant, and NDC y points
            // the other way, so every vertex must drop by exactly 2*a.
            const float delta = withBoth[i].y - withMeshOnly[i].y;
            worstDelta = std::max(worstDelta, std::fabs(delta - (-2.0f * geo.vPosition)));
        }
        std::printf("    worst deviation from a pure uniform shift: %.3e\n", worstDelta);
        Check(worstDelta < 1e-6f, "the parametric layer adds a clean offset on top");
    }

    std::printf("\n== convergence reaches the vertex buffer ==\n");
    {
        WarpMesh mesh;
        ConvergenceParams conv;
        conv.rH = 0.0015f;
        conv.bV = -0.0020f;

        constexpr int tess = 16;
        WarpBuildParams params;
        params.mesh        = &mesh;
        params.convergence = &conv;
        params.tess        = tess;
        params.aspect      = kAspect;

        std::vector<WarpVertex> verts;
        BuildWarpGrid(params, verts);

        const int stride = WarpGridStride(tess);
        const WarpVertex& centre = verts[(stride / 2) * stride + stride / 2];
        CheckNear(centre.rdx, 0.0015f, 1e-6f, "red offset lands in the vertex");
        CheckNear(centre.bdy, -0.0020f, 1e-6f, "blue offset lands in the vertex");
        CheckNear(centre.rdy, 0.0f, 1e-6f, "unset components stay zero");

        // Without a convergence block the attributes must be zeroed, or stale
        // values would linger from a previous build.
        params.convergence = nullptr;
        BuildWarpGrid(params, verts);
        bool cleared = true;
        for (const WarpVertex& v : verts)
            if (v.rdx || v.rdy || v.bdx || v.bdy) cleared = false;
        Check(cleared, "no convergence means the attributes are cleared");
    }

    std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
    return g_failures ? 1 : 0;
}
