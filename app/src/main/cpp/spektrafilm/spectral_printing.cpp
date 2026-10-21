// Pointwise printing/scanning port of Spektrafilm 0.3.4 (3bb2c2d).
// See LICENSE.spektrafilm and docs/spektrafilm-luts.md for provenance.
#include "spectral_printing.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace spektrafilm {
namespace {
const std::vector<double>& sized(const DataSet& d, const std::string& name, size_t count) {
    const auto& v = d.at(name);
    if (v.size() != count) throw std::invalid_argument("Invalid spectral tensor: " + name);
    return v;
}
double spectralDensity(const std::vector<double>& dyes, const std::vector<double>& base,
                       size_t wavelength, const Vec3& cmy) {
    double d = base[wavelength];
    for (int c = 0; c < 3; ++c) d += cmy[c] * dyes[wavelength * 3 + c];
    return d;
}
}

double proPhotoDecode(double v) {
    return v < 0.03125 ? v / 16.0 : std::pow(v, 1.8);
}
double proPhotoEncode(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return v < 0.001953125 ? v * 16.0 : std::pow(v, 1.0 / 1.8);
}

Scanner::Scanner(const DataSet& shared, const DataSet& medium, const OutputGamut& gamut)
    : base_(medium.at("spectral.baseDensity")), gamut_(gamut) {
    const size_t n = base_.size();
    if (n == 0 || n > 1024) throw std::invalid_argument("Invalid scan wavelengths");
    dyes_ = sized(medium, "spectral.channelDensity", n * 3);
    valid_ = sized(medium, "spectral.valid", n);
    illuminant_ = sized(medium, "scan.illuminant", n);
    observer_ = sized(shared, "scan.observer", n * 3);
    matrix_ = sized(medium, "scan.xyzToProPhoto", 9);
    for (size_t w = 0; w < n; ++w) normalization_ += illuminant_[w] * observer_[w * 3 + 1];
    if (!(normalization_ > 0)) throw std::invalid_argument("Invalid scanner illuminant");
}

Vec3 Scanner::scan(const Vec3& density) const {
    Vec3 xyz{}, rgb{};
    for (size_t w = 0; w < base_.size(); ++w) {
        if (valid_[w] == 0) continue;
        const double light = std::pow(10.0, -spectralDensity(dyes_, base_, w, density)) * illuminant_[w];
        for (int c = 0; c < 3; ++c) xyz[c] += light * observer_[w * 3 + c];
    }
    for (double& v : xyz) {
        // Match the upstream cmy_to_log_xyz -> pow(10, log_xyz) tap contract.
        v = std::pow(10.0, std::log10(std::max(v / normalization_, 0.0) + 1e-10));
    }
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) rgb[r] += matrix_[r * 3 + c] * xyz[c];
    rgb = gamut_.compress(rgb);
    for (double& v : rgb) {
        if (!std::isfinite(v)) throw std::runtime_error("Non-finite spectral scanner result");
        v = proPhotoEncode(v);
    }
    return rgb;
}

Printing::Printing(const DataSet& shared, const DataSet& film, const DataSet& paper,
                   const std::string& paperName, const Vec3& midgrayDensity)
    : filmBase_(film.at("spectral.baseDensity")), axis_(paper.at("print.logExposure")) {
    const size_t n = filmBase_.size();
    if (n == 0 || n > 1024 || axis_.size() < 2 || axis_.size() > 65536)
        throw std::invalid_argument("Invalid print model dimensions");
    filmDyes_ = sized(film, "spectral.channelDensity", n * 3);
    valid_ = sized(film, "spectral.valid", n);
    const auto& filters = sized(shared, "print.dichroicFilters", n * 3);
    const auto& light = sized(shared, "print.lightSource", n);
    const auto& sensitivity = sized(paper, "print.sensitivity", n * 3);
    const auto& cc = sized(film, "print.neutral." + paperName, 3);
    exposure_ = sized(paper, "print.exposure", 1)[0];
    const bool normalize = sized(paper, "print.normalizeExposure", 1)[0] != 0;
    if (!(exposure_ > 0)) throw std::invalid_argument("Invalid print exposure");
    Vec3 transmittance{};
    for (int c = 0; c < 3; ++c) transmittance[c] = std::pow(10.0, -cc[c] / 100.0);
    weights_.resize(n * 3);
    Vec3 rawMidgray{};
    for (size_t w = 0; w < n; ++w) {
        if (valid_[w] == 0) continue;
        double source = light[w];
        for (int c = 0; c < 3; ++c)
            source *= 1.0 - (1.0 - filters[w * 3 + c]) * (1.0 - transmittance[c]);
        const double transmission = std::pow(10.0, -spectralDensity(filmDyes_, filmBase_, w, midgrayDensity));
        for (int c = 0; c < 3; ++c) {
            weights_[w * 3 + c] = source * sensitivity[w * 3 + c];
            rawMidgray[c] += transmission * weights_[w * 3 + c];
        }
    }
    // Default camera EV=0: compensated and uncompensated reference exposures
    // coincide, preserving all normalize/compensate branches of this preset.
    if (normalize) {
        double logMean = 0;
        for (double v : rawMidgray) logMean += std::log(std::max(v, 1e-10)) / 3.0;
        normalization_ = std::exp(-logMean);
    }
    const auto& centers = paper.at("print.centers");
    if (centers.empty() || centers.size() % 3 != 0 || centers.size() > 96)
        throw std::invalid_argument("Invalid print density layers");
    const size_t layers = centers.size() / 3;
    const auto& sigmas = sized(paper, "print.sigmas", centers.size());
    const auto& amplitudes = sized(paper, "print.amplitudes", centers.size());
    if (!std::is_sorted(axis_.begin(), axis_.end())) throw std::invalid_argument("Unsorted print curve");
    curves_.resize(axis_.size() * 3);
    // LUT mode disables creative s023 morphing: evaluate the fitted normal
    // CDF layers on the stock exposure axis, then interpolate as upstream.
    if (std::any_of(sigmas.begin(), sigmas.end(), [](double v) { return !(v > 0); }))
        throw std::invalid_argument("Invalid print layer sigma");
    for (size_t i = 0; i < axis_.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            double density = 0;
            for (size_t layer = 0; layer < layers; ++layer) {
                const size_t k = c * layers + layer;
                const double sigma = sigmas[k];
                const double z = (axis_[i] - centers[k]) / sigma;
                density += amplitudes[k] * 0.5 * std::erfc(-z / std::sqrt(2.0));
            }
            curves_[i * 3 + c] = density;
        }
    }
}

Vec3 Printing::expose(const Vec3& filmDensity) const {
    Vec3 raw{};
    for (size_t w = 0; w < filmBase_.size(); ++w) {
        if (valid_[w] == 0) continue;
        const double transmission = std::pow(10.0, -spectralDensity(filmDyes_, filmBase_, w, filmDensity));
        for (int c = 0; c < 3; ++c) raw[c] += transmission * weights_[w * 3 + c];
    }
    for (double& v : raw) {
        const double logRaw = std::log10(std::max(v * normalization_, 0.0) + 1e-10);
        v = std::log10(std::max(std::pow(10.0, logRaw) * exposure_, 0.0) + 1e-10);
    }
    return raw;
}

Vec3 Printing::develop(const Vec3& logExposure) const {
    Vec3 result{};
    for (int c = 0; c < 3; ++c) {
        const double x = logExposure[c];
        if (x <= axis_.front()) result[c] = curves_[c];
        else if (x >= axis_.back()) result[c] = curves_[(axis_.size() - 1) * 3 + c];
        else {
            const size_t upper = std::upper_bound(axis_.begin(), axis_.end(), x) - axis_.begin();
            const size_t lower = upper - 1;
            const double width = axis_[upper] - axis_[lower];
            const double t = width != 0 ? (x - axis_[lower]) / width : 0.0;
            result[c] = curves_[lower * 3 + c] + t * (curves_[upper * 3 + c] - curves_[lower * 3 + c]);
        }
    }
    return result;
}
}
