#include "warpmesh.h"

#include <algorithm>
#include <cmath>

namespace crtb {
namespace {

inline int ClampIdx(int i, int n) { return i < 0 ? 0 : (i >= n ? n - 1 : i); }

} // namespace

// ---------------------------------------------------------------------------
// MonotoneSpline
// ---------------------------------------------------------------------------
//
// Fritsch-Carlson monotone cubic Hermite interpolation.
//
// The obvious choice here is Catmull-Rom, but it overshoots: correcting the top
// edge by 6 px left a ~0.45 px bend of the *opposite* sign a third of the way
// down the screen - right in the part of the picture that was already fine.
// Fritsch-Carlson limits the tangents so the curve still passes exactly through
// every control point but never rings past the surrounding values. A control
// point you have not touched therefore stays exactly where it was.

void MonotoneSpline::Build(const float* values, int count) {
    n_ = std::clamp(count, 0, WarpMesh::kMaxSize);
    if (n_ <= 0) return;

    for (int i = 0; i < n_; ++i) y_[i] = values[i];
    if (n_ == 1) { m_[0] = 0.0f; return; }

    float secant[WarpMesh::kMaxSize];
    for (int i = 0; i < n_ - 1; ++i) secant[i] = y_[i + 1] - y_[i];

    m_[0] = secant[0];
    for (int i = 1; i < n_ - 1; ++i) m_[i] = 0.5f * (secant[i - 1] + secant[i]);
    m_[n_ - 1] = secant[n_ - 2];

    for (int i = 0; i < n_ - 1; ++i) {
        if (secant[i] == 0.0f) {
            // Flat segment: pin both ends so the curve cannot bulge out of it.
            m_[i]     = 0.0f;
            m_[i + 1] = 0.0f;
            continue;
        }
        const float a = m_[i] / secant[i];
        const float b = m_[i + 1] / secant[i];
        if (a < 0.0f) m_[i] = 0.0f;
        if (b < 0.0f) m_[i + 1] = 0.0f;

        const float magnitude = a * a + b * b;
        if (magnitude > 9.0f) {
            const float scale = 3.0f / std::sqrt(magnitude);
            m_[i]     = scale * a * secant[i];
            m_[i + 1] = scale * b * secant[i];
        }
    }
}

float MonotoneSpline::At(float t) const {
    if (n_ <= 0) return 0.0f;
    if (n_ == 1) return y_[0];

    const float x = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(n_ - 1);
    const int   k = std::clamp(static_cast<int>(x), 0, n_ - 2);   // x >= 0, so truncation is floor
    const float s = x - static_cast<float>(k);

    const float s2 = s * s;
    const float s3 = s2 * s;
    return ( 2.0f * s3 - 3.0f * s2 + 1.0f) * y_[k]
         + (        s3 - 2.0f * s2 + s   ) * m_[k]
         + (-2.0f * s3 + 3.0f * s2       ) * y_[k + 1]
         + (        s3 -        s2       ) * m_[k + 1];
}

// ---------------------------------------------------------------------------
// WarpSampler
// ---------------------------------------------------------------------------

WarpSampler::WarpSampler(const WarpMesh& mesh) : n_(mesh.Size()) {
    float buf[WarpMesh::kMaxSize];
    for (int r = 0; r < n_; ++r) {
        for (int c = 0; c < n_; ++c) buf[c] = mesh.At(r, c).dx;
        rowDx_[r].Build(buf, n_);
        for (int c = 0; c < n_; ++c) buf[c] = mesh.At(r, c).dy;
        rowDy_[r].Build(buf, n_);
    }
}

Offset WarpSampler::RowAt(int row, float u) const {
    if (row < 0 || row >= n_) return Offset{};
    return Offset{ rowDx_[row].At(u), rowDy_[row].At(u) };
}

Offset WarpSampler::At(float u, float v) const {
    // Collapse every row horizontally at u, then interpolate those n results
    // vertically at v. Separable, and both passes are monotone, so the result
    // never leaves the range spanned by the surrounding control points.
    float colDx[WarpMesh::kMaxSize];
    float colDy[WarpMesh::kMaxSize];
    for (int r = 0; r < n_; ++r) {
        colDx[r] = rowDx_[r].At(u);
        colDy[r] = rowDy_[r].At(u);
    }

    MonotoneSpline vertical;
    Offset out;
    vertical.Build(colDx, n_);
    out.dx = vertical.At(v);
    vertical.Build(colDy, n_);
    out.dy = vertical.At(v);
    return out;
}

// ---------------------------------------------------------------------------
// WarpMesh
// ---------------------------------------------------------------------------

void WarpMesh::Resize(int n) {
    n = std::clamp(n, kMinSize, kMaxSize);
    if (!pts_.empty() && n == n_) return;

    if (pts_.empty()) {
        n_ = n;
        pts_.assign(static_cast<size_t>(n_) * n_, Offset{});
        locked_.assign(static_cast<size_t>(n_), 0);
        return;
    }

    // Resample the existing spline onto the new lattice so the shape survives.
    const WarpSampler sampler(*this);
    std::vector<Offset> next(static_cast<size_t>(n) * n);
    for (int r = 0; r < n; ++r) {
        const float v = static_cast<float>(r) / static_cast<float>(n - 1);
        for (int c = 0; c < n; ++c) {
            const float u = static_cast<float>(c) / static_cast<float>(n - 1);
            next[static_cast<size_t>(r) * n + c] = sampler.At(u, v);
        }
    }

    // Carry the row locks over by nearest position.
    std::vector<char> nextLocked(static_cast<size_t>(n), 0);
    for (int r = 0; r < n; ++r) {
        const float v = static_cast<float>(r) / static_cast<float>(n - 1);
        const int src = ClampIdx(static_cast<int>(std::lround(v * (n_ - 1))), n_);
        nextLocked[static_cast<size_t>(r)] = locked_[static_cast<size_t>(src)];
    }

    n_ = n;
    pts_.swap(next);
    locked_.swap(nextLocked);
}

void WarpMesh::Reset() {
    std::fill(pts_.begin(), pts_.end(), Offset{});
}

void WarpMesh::ResetRow(int row) {
    if (row < 0 || row >= n_) return;
    for (int c = 0; c < n_; ++c) At(row, c) = Offset{};
}

Offset WarpMesh::Eval(float u, float v) const {
    // Convenience path for one-off samples. Taking many samples this way is
    // wasteful - build a WarpSampler once instead.
    return WarpSampler(*this).At(u, v);
}

void WarpMesh::MaxMagnitude(float& maxDx, float& maxDy) const {
    maxDx = maxDy = 0.0f;
    for (const Offset& o : pts_) {
        maxDx = std::max(maxDx, std::fabs(o.dx));
        maxDy = std::max(maxDy, std::fabs(o.dy));
    }
}

bool WarpMesh::AnyOffset() const {
    for (const Offset& o : pts_)
        if (std::fabs(o.dx) > 1e-6f || std::fabs(o.dy) > 1e-6f) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Grid generation
// ---------------------------------------------------------------------------

void BuildWarpGrid(const WarpMesh& mesh, int tess, float overscan, float edgeBleed,
                   std::vector<WarpVertex>& outVerts) {
    tess = std::clamp(tess, 8, 512);
    const int stride = WarpGridStride(tess);
    const int n      = mesh.Size();

    outVerts.resize(static_cast<size_t>(stride) * stride);

    // Axis sample: index 0 is the outer bleed ring, 1..tess+1 the regular
    // interior grid, tess+2 the far bleed ring. Bleed vertices keep the clamped
    // texcoord of the edge they sit next to.
    std::vector<float> axisPos(static_cast<size_t>(stride));
    std::vector<float> axisTex(static_cast<size_t>(stride));
    for (int i = 0; i < stride; ++i) {
        if (i == 0)                { axisPos[i] = -edgeBleed;       axisTex[i] = 0.0f; }
        else if (i == stride - 1)  { axisPos[i] = 1.0f + edgeBleed; axisTex[i] = 1.0f; }
        else {
            const float t = static_cast<float>(i - 1) / static_cast<float>(tess);
            axisPos[i] = t;
            axisTex[i] = t;
        }
    }

    // Build the n horizontal splines once, then walk column by column: for a
    // fixed u the vertical spline is the same for every row, so it is built
    // once per column rather than once per vertex. That turns the whole rebuild
    // from O(stride^2 * n^2) into O(stride * (stride + n)).
    const WarpSampler sampler(mesh);

    float colDx[WarpMesh::kMaxSize];
    float colDy[WarpMesh::kMaxSize];
    MonotoneSpline verticalDx;
    MonotoneSpline verticalDy;

    for (int c = 0; c < stride; ++c) {
        const float tu = axisTex[c];
        const float px = axisPos[c];

        for (int r = 0; r < n; ++r) {
            const Offset o = sampler.RowAt(r, tu);
            colDx[r] = o.dx;
            colDy[r] = o.dy;
        }
        verticalDx.Build(colDx, n);
        verticalDy.Build(colDy, n);

        for (int r = 0; r < stride; ++r) {
            const float tv = axisTex[r];

            float x = px         + verticalDx.At(tv);
            float y = axisPos[r] + verticalDy.At(tv);

            // Zoom about the screen centre.
            x = 0.5f + (x - 0.5f) * overscan;
            y = 0.5f + (y - 0.5f) * overscan;

            WarpVertex& out = outVerts[static_cast<size_t>(r) * stride + c];
            out.x = x * 2.0f - 1.0f;        // [0,1] -> NDC
            out.y = 1.0f - y * 2.0f;        // v grows downwards, NDC y upwards
            out.u = tu;
            out.v = tv;
        }
    }
}

void BuildWarpIndices(int tess, std::vector<unsigned int>& outIndices) {
    tess = std::clamp(tess, 8, 512);
    const int stride = WarpGridStride(tess);
    const int quads  = stride - 1;

    outIndices.clear();
    outIndices.reserve(static_cast<size_t>(quads) * quads * 6);
    for (int r = 0; r < quads; ++r) {
        for (int c = 0; c < quads; ++c) {
            const unsigned int i0 = static_cast<unsigned int>(r * stride + c);
            const unsigned int i1 = i0 + 1;
            const unsigned int i2 = i0 + static_cast<unsigned int>(stride);
            const unsigned int i3 = i2 + 1;
            outIndices.push_back(i0); outIndices.push_back(i1); outIndices.push_back(i2);
            outIndices.push_back(i2); outIndices.push_back(i1); outIndices.push_back(i3);
        }
    }
}

} // namespace crtb
