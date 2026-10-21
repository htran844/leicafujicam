// Port of Sources/FotufilmCore/DiffusionFilter.swift.
#include "DiffusionFilter.h"

#include <cmath>

#include "FilmStock.h"
#include "Simd.h"
#include "SpectralGrid.h"

namespace fotufilm {

bool DiffusionHalo::isIdentity() const {
    if (!(scatteredShare > 0)) return true;
    for (float s : sigmasPixels) {
        if (s > 0) return false;
    }
    return true;
}

DiffusionFilter::DiffusionFilter(std::string id_, std::string name_, float interactedFraction_, float albedo_,
                                 float particleRadiusUM_, float sizeSpread_)
    : id(std::move(id_)),
      name(std::move(name_)),
      interactedFraction(clamp(interactedFraction_, 0.0f, 1.0f)),
      albedo(clamp(albedo_, 0.0f, 1.0f)),
      particleRadiusUM(swiftMax(particleRadiusUM_, 0.1f)),
      sizeSpread(swiftMax(sizeSpread_, 0.01f)) {}

std::vector<float> DiffusionFilter::recordWavelengths(const FilmStock& stock) {
    std::vector<std::vector<float>> sensitivities = stock.spectralProfile.layerSensitivity;
    for (const DonorCaptureLayer& donor : stock.donorLayers) sensitivities.push_back(donor.sensitivity);
    std::vector<float> result;
    for (const std::vector<float>& sensitivity : sensitivities) {
        float weighted = 0, total = 0;
        for (int i = 0; i < SpectralGrid::count; ++i) {
            float w = sensitivity[i] * SpectralGrid::d65()[i];
            weighted += w * SpectralGrid::wavelengths()[i];
            total += w;
        }
        result.push_back(total > 0 ? weighted / total : 550.0f);
    }
    return result;
}

float DiffusionFilter::scatteredEnergyWithin(float radiusPixels, float wavelengthNM, float medianRadiusUM,
                                             float spread, float focalLengthMM, float pixelPitchMM) {
    if (!(radiusPixels > 0)) return 0;
    float theta = radiusPixels * pixelPitchMM / swiftMax(focalLengthMM, 1e-3f);
    float total = 0, weight = 0;
    for (int step = -3; step <= 3; ++step) {
        float z = static_cast<float>(step) * spread;
        float radiusUM = medianRadiusUM * std::exp(z);
        float w = std::exp(-0.5f * (z / spread) * (z / spread));
        weight += w;
        total += w * airyEnergyWithin(2 * kFloatPi * radiusUM * 1000 * theta / wavelengthNM);
    }
    return weight > 0 ? clamp(total / weight, 0.0f, 1.0f) : 0.0f;
}

float DiffusionFilter::airyEnergyWithin(float u) {
    if (!(u > 0)) return 0;
    float j0 = besselJ0(u), j1 = besselJ1(u);
    return clamp(1 - j0 * j0 - j1 * j1, 0.0f, 1.0f);
}

DiffusionHalo DiffusionFilter::halo(const FilmStock& stock, float focalLengthMM, float pixelPitchMM,
                                    float maximumSigmaPixels) const {
    std::vector<float> wavelengths = recordWavelengths(stock);
    if (!(scatteredShare() > 0 && pixelPitchMM > 0 && focalLengthMM > 0 && maximumSigmaPixels > 0)) {
        return DiffusionHalo{{0, 0, 0}, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, directShare(), 0, 0};
    }
    auto energy = [&](float radius, float wavelength) {
        return scatteredEnergyWithin(radius, wavelength, particleRadiusUM, sizeSpread, focalLengthMM, pixelPitchMM);
    };
    auto quantile = [&](float fraction, float wavelength) {
        float low = 0, high = 1;
        while (energy(high, wavelength) < fraction && high < 1e6f) high *= 2;
        for (int i = 0; i < 40; ++i) {
            float mid = 0.5f * (low + high);
            if (energy(mid, wavelength) < fraction) low = mid; else high = mid;
        }
        return 0.5f * (low + high);
    };
    auto sigmaForQuantile = [&](float p, float wavelength) {
        return quantile(p, wavelength) / std::sqrt(-2 * std::log(1 - p));
    };
    auto meanSigma = [&](float p) {
        float sum = 0;
        for (float wavelength : wavelengths) sum = sum + sigmaForQuantile(p, wavelength);
        return sum / 3;
    };
    float tightest = clamp(meanSigma(coreQuantile), 0.5f, maximumSigmaPixels);
    float widest = clamp(meanSigma(tailQuantile), tightest, maximumSigmaPixels);
    std::vector<float> sigmas = {tightest, std::sqrt(tightest * widest), widest};
    float reach = sigmas[2] * 3;

    std::vector<std::vector<float>> weights;
    float truncated = 0;
    for (float wavelength : wavelengths) {
        std::vector<float> fitted =
            solveWeights(sigmas, reach, [&](float radius) { return energy(radius, wavelength); });
        float total = 0;
        for (float v : fitted) total = total + v;
        truncated += swiftMax(0.0f, 1 - total);
        if (total > 0) {
            std::vector<float> row;
            for (float v : fitted) row.push_back(v / total);
            weights.push_back(row);
        } else {
            weights.push_back({1, 0, 0});
        }
    }
    return DiffusionHalo{sigmas, weights, directShare(), scatteredShare(),
                         truncated / static_cast<float>(wavelengths.size())};
}

std::vector<float> DiffusionFilter::solveWeights(const std::vector<float>& sigmas, float reachPixels,
                                                 const std::function<float(float)>& profile) {
    const int samples = 48;
    float smallest = swiftMax(sigmas[0], 1e-3f) * 0.15f;
    float largest = swiftMax(reachPixels, smallest * 2);
    std::vector<float> radii;
    for (int i = 0; i < samples; ++i) {
        radii.push_back(smallest * std::pow(largest / smallest, static_cast<float>(i) / static_cast<float>(samples - 1)));
    }
    std::vector<float> target;
    for (float r : radii) target.push_back(profile(r));
    std::vector<std::vector<float>> basis;
    for (float sigma : sigmas) {
        std::vector<float> row;
        for (float r : radii) row.push_back(sigma > 0 ? 1 - std::exp(-(r * r) / (2 * sigma * sigma)) : 0.0f);
        basis.push_back(row);
    }
    std::vector<float> weights = {1.0f / 3, 1.0f / 3, 1.0f / 3};
    auto residualCost = [&](const std::vector<float>& w) {
        float total = 0;
        for (int i = 0; i < samples; ++i) {
            float modelled = w[0] * basis[0][i] + w[1] * basis[1][i] + w[2] * basis[2][i];
            float d = modelled - target[i];
            total += d * d;
        }
        return total;
    };
    float step = 0.25f;
    float best = residualCost(weights);
    for (int iteration = 0; iteration < 400; ++iteration) {
        bool improved = false;
        for (int k = 0; k < 3; ++k) {
            for (float direction : {step, -step}) {
                std::vector<float> trial = weights;
                trial[k] = swiftMax(trial[k] + direction, 0.0f);
                float sum = 0;
                for (float v : trial) sum = sum + v;
                if (sum > 1) {
                    for (int j = 0; j < 3; ++j) trial[j] /= sum;
                }
                float cost = residualCost(trial);
                if (cost < best) {
                    best = cost;
                    weights = trial;
                    improved = true;
                }
            }
        }
        if (!improved) {
            step *= 0.5f;
            if (step < 1e-5f) break;
        }
    }
    return weights;
}

const char* DiffusionFilter::rawValue(Grade grade) {
    switch (grade) {
        case Grade::eighth: return "1/8";
        case Grade::quarter: return "1/4";
        case Grade::half: return "1/2";
        case Grade::one: return "1";
        case Grade::two: return "2";
    }
    return "";
}

float DiffusionFilter::gradeInteractedFraction(Grade grade) {
    switch (grade) {
        case Grade::eighth: return 0.05f;
        case Grade::quarter: return 0.10f;
        case Grade::half: return 0.19f;
        case Grade::one: return 0.32f;
        case Grade::two: return 0.50f;
    }
    return 0;
}

const char* DiffusionFilter::rawValue(Family family) {
    switch (family) {
        case Family::proMist: return "promist";
        case Family::blackProMist: return "blackpromist";
        case Family::glimmerglass: return "glimmerglass";
        case Family::blackGlimmerglass: return "blackglimmerglass";
        case Family::fog: return "fog";
        case Family::blackFog: return "blackfog";
    }
    return "";
}

const char* DiffusionFilter::label(Family family) {
    switch (family) {
        case Family::proMist: return "Pro-Mist";
        case Family::blackProMist: return "Black Pro-Mist";
        case Family::glimmerglass: return "Glimmerglass";
        case Family::blackGlimmerglass: return "Black Glimmerglass";
        case Family::fog: return "Fog";
        case Family::blackFog: return "Black Fog";
    }
    return "";
}

float DiffusionFilter::familyParticleRadiusUM(Family family) {
    switch (family) {
        case Family::proMist:
        case Family::blackProMist: return 12;
        case Family::glimmerglass:
        case Family::blackGlimmerglass: return 35;
        case Family::fog:
        case Family::blackFog: return 5;
    }
    return 12;
}

float DiffusionFilter::familyAlbedo(Family family) {
    switch (family) {
        case Family::proMist: return 0.95f;
        case Family::blackProMist: return 0.30f;
        case Family::glimmerglass: return 0.92f;
        case Family::blackGlimmerglass: return 0.35f;
        case Family::fog: return 0.97f;
        case Family::blackFog: return 0.35f;
    }
    return 0.95f;
}

DiffusionFilter DiffusionFilter::preset(Family family, Grade grade) {
    return DiffusionFilter(std::string(rawValue(family)) + "-" + rawValue(grade),
                           std::string(label(family)) + " " + rawValue(grade), gradeInteractedFraction(grade),
                           familyAlbedo(family), familyParticleRadiusUM(family));
}

float besselJ0(float x) {
    float ax = std::fabs(x);
    if (ax < 3) {
        float y = (x / 3) * (x / 3);
        return 1 - y * (2.2499997f - y * (1.2656208f - y * (0.3163866f - y * (0.0444479f - y * (0.0039444f - y * 0.00021f)))));
    }
    float z = 3 / ax;
    float amplitude = 0.79788456f - z * (0.00000077f + z * (0.00552740f + z * (0.00009512f - z * (0.00137237f - z * (0.00072805f - z * 0.00014476f)))));
    float phase = ax - 0.78539816f - z * (0.04166397f + z * (0.00003954f - z * (0.00262573f - z * (0.00054125f + z * (0.00029333f - z * 0.00013558f)))));
    return amplitude * std::cos(phase) / std::sqrt(ax);
}

float besselJ1(float x) {
    float ax = std::fabs(x);
    if (ax < 3) {
        float y = (x / 3) * (x / 3);
        float value = x * (0.5f - y * (0.56249985f - y * (0.21093573f - y * (0.03954289f - y * (0.00443319f - y * (0.00031761f - y * 0.00001109f))))));
        return value;
    }
    float z = 3 / ax;
    float amplitude = 0.79788456f + z * (0.00000156f + z * (0.01659667f + z * (0.00017105f - z * (0.00249511f - z * (0.00113653f - z * 0.00020033f)))));
    float phase = ax - 2.35619449f + z * (0.12499612f + z * (0.00005650f - z * (0.00637879f - z * (0.00074348f + z * (0.00079824f - z * 0.00029166f)))));
    float value = amplitude * std::cos(phase) / std::sqrt(ax);
    return x < 0 ? -value : value;
}

}  // namespace fotufilm
