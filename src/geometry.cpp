#include "geometry.h"

#include <algorithm>
#include <cmath>

namespace crtb {
namespace {

// Fades in over the outer half of the screen and is flat zero from the middle
// inwards. Squared so the transition is C1 - a linear ramp would leave a visible
// crease across the middle of the picture.
inline float EdgeRamp(float x) {
    const float clamped = std::clamp(x, 0.0f, 1.0f);
    return clamped * clamped;
}

inline bool NonZero(float v) { return std::fabs(v) > 1e-7f; }

} // namespace

bool GeometryParams::Any() const {
    return NonZero(hPosition) || NonZero(vPosition) ||
           NonZero(hSize)     || NonZero(vSize)     ||
           NonZero(rotation)  || NonZero(parallelogram) ||
           NonZero(trapezoid) || NonZero(pincushion)    ||
           NonZero(pinBalance)|| NonZero(hLinearity)    ||
           NonZero(vLinearity)|| NonZero(topBow)        ||
           NonZero(bottomBow);
}

Offset GeometryParams::At(float u, float v, float aspect) const {
    const float s = 2.0f * std::clamp(u, 0.0f, 1.0f) - 1.0f;
    const float t = 2.0f * std::clamp(v, 0.0f, 1.0f) - 1.0f;
    if (aspect <= 0.0f) aspect = 4.0f / 3.0f;

    Offset o;

    o.dx += hPosition;
    o.dy += vPosition;

    o.dx += hSize * s;
    o.dy += vSize * t;

    // Written so that in *pixels* this is a rigid rotation: dx*W = -a*t*H and
    // dy*H = a*s*W. Without the aspect terms a "rotation" would shear the
    // picture on any non-square screen.
    o.dx += -rotation * t / aspect;
    o.dy +=  rotation * s * aspect;

    o.dx += parallelogram * t;
    o.dx += trapezoid * s * t;

    const float bow = 1.0f - t * t;              // 1 at mid-height, 0 at top/bottom
    o.dx += pincushion * s * bow;
    o.dx += pinBalance * s * bow * t;

    o.dx += hLinearity * (1.0f - s * s);
    o.dy += vLinearity * bow;

    // Localized edge bows: full strength at the edge, gone by mid-screen, so
    // correcting the top cannot disturb a bottom half that is already straight.
    const float span = 1.0f - s * s;
    o.dy += topBow    * span * EdgeRamp(-t);
    o.dy += bottomBow * span * EdgeRamp(t);

    return o;
}

void GeometryParams::MaxMagnitude(float aspect, float& maxDx, float& maxDy) const {
    maxDx = maxDy = 0.0f;
    if (!Any()) return;

    constexpr int kSteps = 16;
    for (int i = 0; i <= kSteps; ++i) {
        const float v = static_cast<float>(i) / kSteps;
        for (int j = 0; j <= kSteps; ++j) {
            const float u = static_cast<float>(j) / kSteps;
            const Offset o = At(u, v, aspect);
            maxDx = std::max(maxDx, std::fabs(o.dx));
            maxDy = std::max(maxDy, std::fabs(o.dy));
        }
    }
}

// ---------------------------------------------------------------------------

bool ConvergenceParams::Any() const {
    return NonZero(rH) || NonZero(rV) || NonZero(rHEdge) || NonZero(rVEdge) ||
           NonZero(bH) || NonZero(bV) || NonZero(bHEdge) || NonZero(bVEdge);
}

void ConvergenceParams::At(float u, float v, Offset& outRed, Offset& outBlue) const {
    const float s = 2.0f * std::clamp(u, 0.0f, 1.0f) - 1.0f;
    const float t = 2.0f * std::clamp(v, 0.0f, 1.0f) - 1.0f;

    outRed.dx  = rH + rHEdge * s;
    outRed.dy  = rV + rVEdge * t;
    outBlue.dx = bH + bHEdge * s;
    outBlue.dy = bV + bVEdge * t;
}

void ConvergenceParams::MaxMagnitude(float& maxDx, float& maxDy) const {
    maxDx = std::max(std::fabs(rH) + std::fabs(rHEdge), std::fabs(bH) + std::fabs(bHEdge));
    maxDy = std::max(std::fabs(rV) + std::fabs(rVEdge), std::fabs(bV) + std::fabs(bVEdge));
}

} // namespace crtb
