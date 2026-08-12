// CRTBender - parametric geometry and convergence.
//
// The 15x15 lattice can express any shape, but dragging 225 handles to remove a
// plain rotation or a symmetric pincushion is busywork. These are the same
// controls a monitor's own service menu offers, expressed as analytic
// displacement fields evaluated per vertex.
//
// They are deliberately kept *separate* from the manual lattice rather than
// baked into it: the final displacement is parametric(u,v) + lattice(u,v), so
// you can still nudge a slider after hand-tuning individual points without
// destroying that work.
//
// Everything here is in normalized units, exactly like WarpMesh offsets: dx is a
// fraction of the screen width, dy a fraction of its height. Positive dy pushes
// the picture down, positive dx pushes it right.
#pragma once

#include "warpmesh.h"

namespace crtb {

// Centred coordinates used throughout: s = 2u-1 and t = 2v-1, both -1..1, so the
// screen centre is the origin and every edge sits at +-1.
struct GeometryParams {
    float hPosition   = 0.0f;   // dx = a
    float vPosition   = 0.0f;   // dy = a
    float hSize       = 0.0f;   // dx = a*s          - widen / narrow
    float vSize       = 0.0f;   // dy = a*t          - stretch / squash
    float rotation    = 0.0f;   // rigid rotation about the centre
    float parallelogram = 0.0f; // dx = a*t          - top and bottom slide apart
    float trapezoid   = 0.0f;   // dx = a*s*t        - top wider than the bottom
    float pincushion  = 0.0f;   // dx = a*s*(1-t^2)  - the side edges bow
    float pinBalance  = 0.0f;   // the same bow, but unequal top vs bottom
    float hLinearity  = 0.0f;   // dx = a*(1-s^2)    - one half stretched
    float vLinearity  = 0.0f;   // dy = a*(1-t^2)
    float topBow      = 0.0f;   // the top edge bows, fading out by mid-screen
    float bottomBow   = 0.0f;   // the same for the bottom edge

    bool Any() const;
    void Reset() { *this = GeometryParams{}; }

    // aspect is width/height, needed so that "rotation" is a true rotation in
    // pixels rather than in normalized units.
    Offset At(float u, float v, float aspect) const;

    // Largest displacement magnitude over the screen, sampled on a coarse grid.
    // Used to size the edge bleed together with the lattice.
    void MaxMagnitude(float aspect, float& maxDx, float& maxDy) const;
};

// Software convergence correction.
//
// On an ageing tube the three beams no longer land on the same spot, which shows
// up as coloured fringes on high-contrast edges - usually small in the centre
// and growing towards the edges. Green is the reference; red and blue are
// shifted to meet it.
//
// A centre offset plus a linear edge term covers the dominant real-world error
// without turning convergence into a second 225-point calibration job.
struct ConvergenceParams {
    float rH = 0.0f, rV = 0.0f;           // red, uniform offset
    float rHEdge = 0.0f, rVEdge = 0.0f;   // red, growing towards the edges
    float bH = 0.0f, bV = 0.0f;           // blue, uniform offset
    float bHEdge = 0.0f, bVEdge = 0.0f;   // blue, growing towards the edges

    bool Any() const;
    void Reset() { *this = ConvergenceParams{}; }

    // Where to sample red and blue from, relative to green. The sign convention
    // matches the sliders: a positive rH means "the red beam lands to the right",
    // so red is sampled from further right to compensate.
    void At(float u, float v, Offset& outRed, Offset& outBlue) const;

    void MaxMagnitude(float& maxDx, float& maxDy) const;
};

} // namespace crtb
