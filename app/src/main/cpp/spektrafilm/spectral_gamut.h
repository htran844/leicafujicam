#pragma once

#include "spectral_data.h"

namespace spektrafilm {

// Spektrafilm 0.3.4's default output gamut compressor, fixed to linear
// ProPhoto RGB. The shared 64 x 720 chroma boundary is generated from the
// CAM16 inverse on first use; it is not a pre-baked colour transform.
class OutputGamut {
public:
    OutputGamut();
    Vec3 compress(const Vec3& linearProPhoto) const;
};

} // namespace spektrafilm
