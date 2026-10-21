// Port of Sources/FotufilmCore/PrinterMatching.swift.
#include "PrinterMatching.h"

#include <cmath>
#include <utility>

#include "ColorScience.h"
#include "FilmStock.h"
#include "PrintPaperTables.h"
#include "SpectralModel.h"

namespace fotufilm {
namespace PrinterMatching {

Match matching(const PrinterProfile& profile, Matching mode, Float3 referenceDensity, Float3 targetDensity,
               const FilmStock& stock, PrintPaper paper, Enlarger enlarger, float bleachBypass,
               std::optional<float> printViewingKelvin) {
    using Kind = MatchingError::Kind;
    if (!EnlargerOptics::illuminates(stock, paper)) throw MatchingError(Kind::unsupportedMedium);
    auto array = [](Float3 v) { return std::vector<float>{v.x, v.y, v.z}; };
    for (float v : {referenceDensity.x, referenceDensity.y, referenceDensity.z, targetDensity.x, targetDensity.y,
                    targetDensity.z}) {
        if (!(std::isfinite(v) && v >= 0)) throw MatchingError(Kind::invalidReference);
    }
    float callier = callierCoefficient(enlarger, stock, paper);
    float silverCallier = callier == 1 ? 1.0f : EnlargerOptics::silverCallierCoefficient;
    float bleach = SpectralRuntime::retainedSilverFraction(bleachBypass, stock);
    std::vector<float> dMin = {stock.curves[0].dMin, stock.curves[1].dMin, stock.curves[2].dMin};
    std::optional<SpectralRuntime::NeutralDensityBasis> basis;
    if (stock.isReversal) basis = SpectralRuntime::neutralDensityBasis(stock);
    std::vector<std::vector<float>> paperSensitivity = sensitivity(paper);
    auto energy = [&](const std::vector<float>& density, const PrinterProfile& p) {
        std::vector<float> scaled = basis ? (*basis)(density) : density;
        for (float& d : scaled) d = d * callier;
        return SpectralRuntime::paperExposure(
                   scaled, stock, p.filteredSpectrum(), paperSensitivity,
                   silverCallier * SpectralRuntime::retainedSilverDensity(density, dMin, bleach), callier) *
               std::exp2(p.exposureEV);
    };
    Float3 calibration = energy({stock.developedDensity(0, 0), stock.developedDensity(1, 0), stock.developedDensity(2, 0)},
                                PrinterProfile::simulatedTungsten());
    Float3 targetEnergy = energy(array(targetDensity), PrinterProfile::simulatedTungsten());
    for (float v : {calibration.x, calibration.y, calibration.z, targetEnergy.x, targetEnergy.y, targetEnergy.z}) {
        if (!(std::isfinite(v) && v > 1e-20f)) throw MatchingError(Kind::invalidReference);
    }
    std::vector<CharacteristicCurve> curves = printCurves(paper, stock);
    std::vector<float> mids = printExposureMidpoints(paper, stock);
    SpectralRuntime::TableOptions tableOptions;
    tableOptions.bleachBypass = bleach;
    tableOptions.printViewingKelvin = printViewingKelvin;
    tableOptions.callier = callier;
    tableOptions.printer = PrinterProfile::simulatedTungsten();
    SpectralLUT output = *SpectralRuntime::tables(stock, paper, tableOptions).paperOutput;
    auto rgb = [&](Float3 e) {
        Float3 coordinate;
        for (int c = 0; c < 3; ++c) {
            float x = mids[c] + exposureDirection(paper) * std::log10(swiftMax(e[c], 1e-20f) / calibration[c]);
            coordinate[c] = (curves[c].density(x) - curves[c].dMin) / (curves[c].dMax() - curves[c].dMin);
        }
        Float3 value = output.sample(coordinate);
        // The renderer delivers a neutral print for a monochrome stock.
        return stock.isMonochrome ? Float3::repeating((value.x + value.y + value.z) / 3) : value;
    };
    Float3 targetRGB = rgb(targetEnergy);
    Float3 w = ColorScience::displayP3LuminanceWeights;
    auto luminance = [&](Float3 v) { return w.x * v.x + w.y * v.y + w.z * v.z; };
    PrinterProfile p = profile.normalized();
    auto response = [&](const PrinterProfile& q) { return energy(array(referenceDensity), q); };
    auto residual = [&](const PrinterProfile& q) {
        Float3 e = response(q);
        return Float3(std::log(swiftMax(e[0], 1e-20f) / targetEnergy[0]),
                      std::log(swiftMax(e[1], 1e-20f) / targetEnergy[1]),
                      std::log(swiftMax(e[2], 1e-20f) / targetEnergy[2]));
    };
    auto worst = [](Float3 v) { return swiftMax(std::fabs(v.x), swiftMax(std::fabs(v.y), std::fabs(v.z))); };
    auto finish = [&]() {
        Float3 reference = rgb(response(p));
        bool limited = false;
        switch (mode) {
            case Matching::fixed: limited = false; break;
            case Matching::density: limited = std::fabs(luminance(reference) - luminance(targetRGB)) > 1e-5f; break;
            case Matching::densityAndColor: limited = worst(residual(p)) > 1e-4f; break;
        }
        return Match{p, reference, targetRGB, worst(reference - targetRGB), limited};
    };
    if (mode == Matching::fixed) return finish();
    if (mode == Matching::density) {
        float target = exposureDirection(paper) * luminance(targetRGB);
        auto level = [&](float ev) {
            PrinterProfile test = p;
            test.exposureEV = ev;
            return exposureDirection(paper) * luminance(rgb(response(test)));
        };
        if (std::fabs(level(p.exposureEV) - target) < 1e-7f) return finish();
        float lo = PrinterProfile::exposureRangeLower, hi = PrinterProfile::exposureRangeUpper;
        if (target >= level(lo)) {
            p.exposureEV = lo;
            return finish();
        }
        if (target <= level(hi)) {
            p.exposureEV = hi;
            return finish();
        }
        for (int i = 0; i < 32; ++i) {
            float ev = (lo + hi) / 2;
            if (level(ev) > target) lo = ev; else hi = ev;
        }
        p.exposureEV = (lo + hi) / 2;
        return finish();
    }
    auto shifted = [](const PrinterProfile& source, int axis, float amount) {
        PrinterProfile value = source;
        switch (axis) {
            case 0: value.exposureEV += amount; break;
            case 1: value.magenta += amount; break;
            default: value.yellow += amount; break;
        }
        return value.normalized();
    };
    auto control = [](const PrinterProfile& q, int axis) {
        return axis == 0 ? q.exposureEV : (axis == 1 ? q.magenta : q.yellow);
    };
    auto squared = [](Float3 v) { return v.x * v.x + v.y * v.y + v.z * v.z; };
    for (int iteration = 0; iteration < 35; ++iteration) {
        Float3 f = residual(p);
        if (worst(f) < 2e-6f) break;
        Float3 columns[3];
        for (int axis = 0; axis < 3; ++axis) {
            PrinterProfile lo = shifted(p, axis, -0.001f);
            PrinterProfile hi = shifted(p, axis, 0.001f);
            columns[axis] = (residual(hi) - residual(lo)) / (control(hi, axis) - control(lo, axis));
        }
        float a[3][4];
        for (int row = 0; row < 3; ++row) {
            a[row][0] = columns[0][row];
            a[row][1] = columns[1][row];
            a[row][2] = columns[2][row];
            a[row][3] = -f[row];
        }
        bool singular = false;
        for (int k = 0; k < 3; ++k) {
            // `(k..<3).max { abs(a[$0][k]) < abs(a[$1][k]) }`: the first largest wins.
            int pivot = k;
            for (int r = k + 1; r < 3; ++r) {
                if (std::fabs(a[pivot][k]) < std::fabs(a[r][k])) pivot = r;
            }
            if (std::fabs(a[pivot][k]) < 1e-8f) {
                singular = true;
                break;
            }
            for (int j = 0; j < 4; ++j) std::swap(a[k][j], a[pivot][j]);
            float divisor = a[k][k];
            for (int j = k; j < 4; ++j) a[k][j] /= divisor;
            for (int i = 0; i < 3; ++i) {
                if (i == k) continue;
                float factor = a[i][k];
                for (int j = k; j < 4; ++j) a[i][j] -= factor * a[k][j];
            }
        }
        if (singular) break;
        bool accepted = false;
        float step = 1;
        for (int attempt = 0; attempt < 11; ++attempt) {
            PrinterProfile candidate = PrinterProfile(p.lampKelvin, p.exposureEV + step * a[0][3],
                                                      p.magenta + step * a[1][3], p.yellow + step * a[2][3])
                                           .normalized();
            if (squared(residual(candidate)) < squared(f)) {
                p = candidate;
                accepted = true;
                break;
            }
            step /= 2;
        }
        if (!accepted) break;
    }
    return finish();
}

}  // namespace PrinterMatching
}  // namespace fotufilm
