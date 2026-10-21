#pragma once

#include "spectral_data.h"

namespace spektrafilm {

/** Deterministic upstream lut_mode filming, from linear ProPhoto RGB to CMY. */
class Filming {
public:
    Filming(const DataSet& shared, const DataSet& film);
    Vec3 develop(const Vec3& linearProPhoto) const;
    // Upstream enlarger balance uses unnormalised stock curves, without DIR,
    // and its default sRGB .184 reference rather than ProPhoto .184.
    Vec3 midgrayDensity() const;
    bool positive() const { return positive_; }
    const Vec3& curveMin() const { return curveMin_; }
    const Vec3& curveMax() const { return curveMax_; }

private:
    Vec3 expose(const Vec3& rgb, const std::array<double, 9>& matrix) const;
    Vec3 interpolate(const Vec3& logRaw, const std::vector<double>& curves) const;
    int tcSize_ = 0;
    bool positive_ = false;
    bool dirActive_ = false;
    Vec3 curveMin_{}, curveMax_{}, densityMax_{}, gamma_{};
    std::array<double, 9> proPhotoMatrix_{}, srgbMatrix_{}, dirMatrix_{};
    std::vector<double> tcLut_, logExposure_, originalCurves_, normalizedCurves_, beforeDirCurves_;
};

}  // namespace spektrafilm
