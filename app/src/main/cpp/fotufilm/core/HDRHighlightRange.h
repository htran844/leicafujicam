// Port of Sources/FotufilmCore/HDRHighlightRange.swift.
#ifndef FOTUFILM_CORE_HDR_HIGHLIGHT_RANGE_H
#define FOTUFILM_CORE_HDR_HIGHLIGHT_RANGE_H

namespace fotufilm {

/// How much of a source's recorded range above diffuse white reaches the film.
namespace HDRHighlightRange {
/// Where compression starts, in units of diffuse white.
constexpr float knee = 0.75f;
/// The headroom left when `range` (0...1) of the stops above white in `headroom` is kept.
float keptHeadroom(float headroom, float range);
/// The curvature c that lands the declared peak on the kept headroom; 0 changes nothing.
float curvature(float headroom, float range);
/// The gain a pixel whose brightest channel is `peak` takes.
float gain(float peak, float curvature);
}  // namespace HDRHighlightRange

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_HDR_HIGHLIGHT_RANGE_H
