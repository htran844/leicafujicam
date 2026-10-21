// Port of Sources/FotufilmCore/ExposureSampler.swift: the kernel's per-pixel exposure stage on the
// CPU (white balance, scene adjustments, spectral recovery), used by the veiling-glare meter. Its
// operation order is the kernel's; the glare mean is compared bit for bit.
#ifndef FOTUFILM_CORE_EXPOSURE_SAMPLER_H
#define FOTUFILM_CORE_EXPOSURE_SAMPLER_H

#include <cmath>

#include "ColorScience.h"
#include "Simd.h"
#include "SpectralModel.h"

namespace fotufilm {

struct Double3 {
    double x = 0, y = 0, z = 0;
    Double3() = default;
    Double3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Double3& operator+=(const Double3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    Double3 operator*(double s) const { return Double3(x * s, y * s, z * s); }
    Double3 operator/(double s) const { return Double3(x / s, y / s, z / s); }
};

struct ExposureSampler {
    float gain;
    Float3 balance;
    float highlights;
    float shadows;
    float saturation;
    float vibrance;
    /// True when all scene adjustments are identities and tone-grid sampling can be skipped.
    bool neutral;
    const float* values;
    int dimension;
    Float3 luma;
    int gridWidth;
    int gridHeight;
    float frameWidth;
    float frameHeight;
    const float* planeA;
    const float* planeB;

    /// Mirrors recover_exposure.
    Double3 layerExposure(Float3 scene) const {
        float y = luma.x * scene.x + luma.y * scene.y + luma.z * scene.z;
        if (!(y > 0)) return Double3();
        Float3 domain = ColorScience::linearRec2020ToExposureDomain(scene);
        Float3 projected = domain;
        if (domain.x < 0 || domain.y < 0 || domain.z < 0) {
            float s = 1;
            if (domain.x < 0) s = swiftMin(s, y / (y - domain.x));
            if (domain.y < 0) s = swiftMin(s, y / (y - domain.y));
            if (domain.z < 0) s = swiftMin(s, y / (y - domain.z));
            projected = Float3(y + s * (domain.x - y), y + s * (domain.y - y), y + s * (domain.z - y));
        }
        Float3 rgb(swiftMax(projected.x, 0.0f), swiftMax(projected.y, 0.0f), swiftMax(projected.z, 0.0f));
        float brightest = swiftMax(rgb.x, swiftMax(rgb.y, rgb.z));
        float radiance = swiftMax(1e-6f, brightest);
        Float3 chromaticity(rgb.x / radiance, rgb.y / radiance, rgb.z / radiance);
        Float3 sampled = SpectralLUT::sample(chromaticity, values, dimension);
        float scale = radiance * gain;
        Float3 layer(sampled.x * scale, sampled.y * scale, sampled.z * scale);
        return Double3(static_cast<double>(swiftMax(layer.x, 0.0f)), static_cast<double>(swiftMax(layer.y, 0.0f)),
                       static_cast<double>(swiftMax(layer.z, 0.0f)));
    }

    /// The regional tone base under pixel `x`, `y`, bilinear across the two grid planes.
    float toneBase(float stops, int x, int y) const {
        float gx = swiftMin(swiftMax((static_cast<float>(x) + 0.5f) * static_cast<float>(gridWidth) / frameWidth - 0.5f,
                                     0.0f),
                            static_cast<float>(gridWidth - 1));
        float gy = swiftMin(swiftMax((static_cast<float>(y) + 0.5f) * static_cast<float>(gridHeight) / frameHeight -
                                         0.5f,
                                     0.0f),
                            static_cast<float>(gridHeight - 1));
        int x0 = swiftMin(swiftMax(static_cast<int>(gx), 0), swiftMax(gridWidth - 2, 0));
        int y0 = swiftMin(swiftMax(static_cast<int>(gy), 0), swiftMax(gridHeight - 2, 0));
        int x1 = swiftMin(x0 + 1, gridWidth - 1);
        int y1 = swiftMin(y0 + 1, gridHeight - 1);
        float fx = swiftMin(swiftMax(gx - static_cast<float>(x0), 0.0f), 1.0f);
        float fy = swiftMin(swiftMax(gy - static_cast<float>(y0), 0.0f), 1.0f);
        auto bilinear = [&](const float* plane) {
            float top = (1 - fx) * plane[y0 * gridWidth + x0] + fx * plane[y0 * gridWidth + x1];
            float bottom = (1 - fx) * plane[y1 * gridWidth + x0] + fx * plane[y1 * gridWidth + x1];
            return (1 - fy) * top + fy * bottom;
        };
        return bilinear(planeA) * stops + bilinear(planeB);
    }

    /// The exposure the scene pixel at `x`, `y` put on the film.
    Double3 exposure(Float3 scene, int x, int y) const {
        if (neutral) return layerExposure(scene * balance);
        float r0 = scene.x * balance.x;
        float g0 = scene.y * balance.y;
        float b0 = scene.z * balance.z;
        float metered = (luma.x * r0 + luma.y * g0 + luma.z * b0) * gain;
        float stops = std::log(swiftMax(metered, 1e-6f)) * (1 / 0.6931472f);
        float keyed = toneBase(stops, x, y);
        float high = swiftMin(swiftMax(keyed * (1.0f / 6.0f), 0.0f), 1.0f);
        float low = swiftMin(swiftMax(-keyed * (1.0f / 6.0f), 0.0f), 1.0f);
        float highMask = high * high * (3 - 2 * high);
        float lowMask = low * low * (3 - 2 * low);
        float toneGain = std::exp2(3 * (highlights * highMask + shadows * lowMask));
        float r1 = r0 * toneGain, g1 = g0 * toneGain, b1 = b0 * toneGain;
        float luma1 = luma.x * r1 + luma.y * g1 + luma.z * b1;
        float peak = swiftMax(r1, swiftMax(g1, b1));
        float colourfulness = (peak - swiftMin(r1, swiftMin(g1, b1))) / swiftMax(peak, 1e-6f);
        float chroma = saturation * (1 + vibrance * (1 - colourfulness));
        Float3 adjusted(luma1 + chroma * (r1 - luma1), luma1 + chroma * (g1 - luma1), luma1 + chroma * (b1 - luma1));
        return layerExposure(adjusted);
    }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_EXPOSURE_SAMPLER_H
