// CRTBender - the geometry correction model.
//
// A WarpMesh is an N x N lattice of control points laid over the screen in
// normalized coordinates: u = 0 at the left edge, 1 at the right; v = 0 at the
// top edge, 1 at the bottom. Row 0 is therefore the top row of the screen.
//
// Every control point carries an offset (dx, dy) expressed as a fraction of the
// screen width / height. Positive dy pushes image content *down*, positive dx
// pushes it *right*. The lattice spans the entire picture, so edges, corners and
// the middle are all shaped the same way: to cancel a CRT that bows the top edge
// upwards you give the upper-middle points a positive dy, and to pull in a
// flaring left edge you give that column a positive dx.
//
// Offsets between control points are interpolated with monotone cubic Hermite
// splines (Fritsch-Carlson), applied separably in u and then in v.
//
// Two alternatives were rejected. Bilinear interpolation is C0 only and leaves
// visible creases along the lattice lines. Catmull-Rom is smooth but overshoots:
// pushing the top edge down by 6 px produced a ~0.45 px bend of the opposite
// sign a third of the way down the screen, in the part of the picture that was
// already correct. Monotone cubic still passes exactly through every control
// point - drag a handle by 6 px and the image moves 6 px - while a control
// point you have not touched stays exactly where it was.
#pragma once

#include <vector>

namespace crtb {

struct Offset {
    float dx = 0.0f;
    float dy = 0.0f;
};

// Forward declaration: the spline needs WarpMesh::kMaxSize for its buffers.
class WarpMesh;

class WarpMesh {
public:
    static constexpr int kMinSize = 3;
    static constexpr int kMaxSize = 21;

    // 15x15 by default: dense enough to shape the whole picture - edges,
    // corners and middle - rather than just nudging one band of it.
    static constexpr int kDefaultSize = 15;

    WarpMesh() { Resize(kDefaultSize); }

    int  Size() const { return n_; }

    // Changes the lattice resolution. The current deformation is preserved by
    // resampling the old spline at the new control point positions, so you can
    // start coarse and refine without losing your work.
    void Resize(int n);

    Offset&       At(int row, int col)       { return pts_[Index(row, col)]; }
    const Offset& At(int row, int col) const { return pts_[Index(row, col)]; }

    bool RowLocked(int row) const { return row >= 0 && row < n_ ? locked_[row] : false; }
    void SetRowLocked(int row, bool locked) { if (row >= 0 && row < n_) locked_[row] = locked; }

    // Zeroes every offset. Row locks are kept.
    void Reset();
    // Zeroes one row.
    void ResetRow(int row);

    // Evaluates the interpolated offset at (u, v), both in [0, 1].
    Offset Eval(float u, float v) const;

    // Largest |dx| and |dy| over the lattice - used to size the edge bleed so
    // the warped image still covers the whole screen.
    void MaxMagnitude(float& maxDx, float& maxDy) const;

    bool AnyOffset() const;

private:
    int Index(int row, int col) const { return row * n_ + col; }

    int                 n_ = 5;
    std::vector<Offset> pts_;
    std::vector<char>   locked_;   // char rather than bool: no bitset proxy
};

// A monotone cubic Hermite curve over a uniform lattice, with the tangents
// already limited and cached.
//
// Caching matters a lot. Computing the tangents inside every sample makes a
// single Eval() cost O(n^2), and the GPU mesh is rebuilt on every mouse move
// while a control point is being dragged: at 21x21 that measured 118 ms per
// rebuild, i.e. about 8 fps. Building once and sampling in O(1) brings it back
// under a millisecond.
class MonotoneSpline {
public:
    void  Build(const float* values, int count);
    float At(float t) const;   // t in [0, 1]

private:
    int   n_ = 0;
    float y_[WarpMesh::kMaxSize] = {};
    float m_[WarpMesh::kMaxSize] = {};
};

// Samples a mesh many times cheaply: the per-row splines are built once up
// front, so each At() costs O(n) instead of O(n^2). Use this instead of
// WarpMesh::Eval whenever you are about to take more than a handful of samples.
class WarpSampler {
public:
    explicit WarpSampler(const WarpMesh& mesh);
    Offset At(float u, float v) const;

    // Just the horizontal pass: the offset of lattice row `row` at u, in O(1).
    // Grid generation walks column by column and needs exactly this.
    Offset RowAt(int row, float u) const;

private:
    int            n_ = 0;
    MonotoneSpline rowDx_[WarpMesh::kMaxSize];
    MonotoneSpline rowDy_[WarpMesh::kMaxSize];
};

struct GeometryParams;
struct ConvergenceParams;

// One tessellated vertex handed to the GPU.
//
// The convergence offsets ride along as vertex attributes rather than as a
// lookup texture: the rasterizer already interpolates across this grid, so the
// field comes for free and the pixel shader just adds them to its texcoord.
struct WarpVertex {
    float x, y;        // NDC position
    float u, v;        // source texcoord (green / reference channel)
    float rdx, rdy;    // red sampling offset, in texcoord units
    float bdx, bdy;    // blue sampling offset
};

// Everything the grid builder needs. The final displacement at any point is
// geometry(u,v) + mesh(u,v): the parametric layer handles the broad shape, the
// lattice the rest, and neither destroys the other.
struct WarpBuildParams {
    const WarpMesh*          mesh        = nullptr;   // required
    const GeometryParams*    geometry    = nullptr;   // optional
    const ConvergenceParams* convergence = nullptr;   // optional

    // Interior subdivisions per axis (the grid gets tess+1 interior points,
    // plus one bleed ring on each side).
    int   tess      = 96;
    // Uniform zoom about the screen centre, 1.0 = none. Above 1.0 magnifies
    // slightly so displaced content never runs short at the edges, at the cost
    // of cropping a little of the desktop - and of resampling all of it.
    float overscan  = 1.0f;
    // How far (in normalized units) the outer ring is pushed beyond the screen.
    // The ring keeps its clamped texcoords, so it smears the outermost
    // row/column of pixels outwards and fills whatever sliver the displacement
    // would otherwise leave black.
    float edgeBleed = 0.0f;
    // Width / height, so that parametric rotation stays a true rotation.
    float aspect    = 4.0f / 3.0f;
};

void BuildWarpGrid(const WarpBuildParams& params, std::vector<WarpVertex>& outVerts);

// Index buffer for the grid produced by BuildWarpGrid. Depends only on tess.
void BuildWarpIndices(int tess, std::vector<unsigned int>& outIndices);

// Number of vertices per axis for a given tessellation (interior + bleed ring).
inline int WarpGridStride(int tess) { return tess + 3; }

} // namespace crtb
