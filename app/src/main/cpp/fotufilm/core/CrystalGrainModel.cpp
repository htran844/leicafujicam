// Port of Sources/FotufilmCore/CrystalGrainModel.swift.
#include "CrystalGrainModel.h"

#include <cfloat>
#include <cmath>
#include <optional>

#include "FilmStock.h"
#include "Simd.h"

namespace fotufilm {

CrystalGrainPopulation::CrystalGrainPopulation(float radiusSpan_, std::vector<float> sublayerShares_,
                                               float coatingDensityScale_)
    : radiusSpan(radiusSpan_), sublayerShares(std::move(sublayerShares_)), coatingDensityScale(coatingDensityScale_) {
    preconditionCheck(valid(radiusSpan, sublayerShares, coatingDensityScale), "invalid crystal grain population");
}

bool CrystalGrainPopulation::valid(float span, const std::vector<float>& shares, float scale) {
    if (!(std::isfinite(span) && inClosedRange(span, 1.0f, 20.0f))) return false;
    if (shares.size() != static_cast<size_t>(kCrystalGrainBinCount)) return false;
    for (float s : shares) {
        if (!(std::isfinite(s) && s > 0 && s < 1)) return false;
    }
    float sum = 0;
    for (float s : shares) sum = sum + s;
    if (!(std::fabs(sum - 1) <= 1e-5f)) return false;
    return std::isfinite(scale) && inClosedRange(scale, 0.25f, 4.0f);
}

CrystalGrainPopulation CrystalGrainPopulation::decode(const JsonValue& value, const std::string& path) {
    const JsonValue& c = json::object(value, path);
    float span = json::requiredFloat(c, "radiusSpan", path);
    std::vector<float> shares = json::requiredFloatArray(c, "sublayerShares", path);
    float scale = json::requiredFloat(c, "coatingDensityScale", path);
    if (!valid(span, shares, scale)) {
        throw DecodingError("dataCorrupted at " + path +
                            ": crystalGrainPopulation requires radiusSpan in 1...20, four positive sublayerShares "
                            "summing to 1, and coatingDensityScale in 0.25...4");
    }
    return CrystalGrainPopulation(span, std::move(shares), scale);
}

namespace {

// `stride(from:through:by:)` over Double, each element mapped to Float.
std::vector<float> doubleStrideThrough(double start, double end, double step) {
    std::vector<float> values;
    for (int i = 0;; ++i) {
        double value = i == 0 ? start : std::fma(static_cast<double>(i), step, start);
        if (step > 0 ? value >= end : value <= end) {
            if (value == end) values.push_back(static_cast<float>(value));
            break;
        }
        values.push_back(static_cast<float>(value));
    }
    return values;
}

}  // namespace

const std::vector<float>& CrystalGrainModel::fitLogExposures() {
    static const std::vector<float> values = doubleStrideThrough(-4.0, 4.0, 0.05);
    return values;
}

const std::vector<float>& CrystalGrainModel::Exposure::classLogExposures() {
    static const std::vector<float> values = doubleStrideThrough(-3.0, 4.0, 0.2);
    return values;
}

const std::vector<float>& CrystalGrainModel::Development::developabilities() {
    static const std::vector<float> values = doubleStrideThrough(0.3, 1.8, 0.05);
    return values;
}

float CrystalGrainModel::Exposure::hitProbability(int hits, float logExposure, float classLogExposure) {
    double mean = static_cast<double>(std::pow(10.0f, logExposure - classLogExposure));
    double term = std::exp(-mean);
    if (hits > 0) {
        for (int n = 1; n <= hits; ++n) term *= mean / static_cast<double>(n);
    }
    return static_cast<float>(swiftMin(swiftMax(term, 0.0), 1.0));
}

float CrystalGrainModel::Exposure::hitFraction(float logExposure, float classLogExposure) {
    double mean = static_cast<double>(std::pow(10.0f, logExposure - classLogExposure));
    double term = 1.0, below = 1.0;
    for (int hits = 1; hits < hitThreshold; ++hits) {
        term *= mean / static_cast<double>(hits);
        below += term;
    }
    return static_cast<float>(swiftMin(swiftMax(1 - std::exp(-mean) * below, 0.0), 1.0));
}

float CrystalGrainModel::Exposure::latentFraction(float logExposure, float classLogExposure, float developability) {
    float threshold = hitFraction(logExposure, classLogExposure);
    if (developability <= 1) return threshold * swiftMax(developability, 0.0f);
    float shortOne = hitProbability(hitThreshold - 1, logExposure, classLogExposure);
    return swiftMin(threshold + (developability - 1) * shortOne, 1.0f);
}

std::vector<float> CrystalGrainModel::Exposure::latentFractions(float logExposure, float developability) const {
    std::vector<float> result;
    for (float e0 : classLogExposures()) result.push_back(latentFraction(logExposure, e0, developability));
    return result;
}

float CrystalGrainModel::Development::saturated(float forming, float gain) {
    return gain >= 1 ? forming : (1 - std::pow(gain, forming)) / (1 - gain);
}

float CrystalGrainModel::Development::dye(float demand, float pool) {
    return pool > 0 ? pool * (1 - std::exp(-demand / pool)) : demand;
}

std::vector<float> CrystalGrainModel::Development::formingFractions(const Exposure& exposure, float logExposure) const {
    std::vector<float> latent = exposure.latentFractions(logExposure, developability);
    std::vector<float> result;
    size_t count = std::min(latent.size(), fogFractions.size());
    for (size_t i = 0; i < count; ++i) {
        float developed = 1 - (1 - latent[i]) * (1 - fogFractions[i]);
        result.push_back(isReversal ? 1 - developed : developed);
    }
    return result;
}

std::vector<float> CrystalGrainModel::Development::binFractions(const Exposure& exposure, float logExposure) const {
    std::vector<float> fractions = formingFractions(exposure, logExposure);
    std::vector<float> result;
    for (const Bin& bin : bins) {
        float total = 0;
        for (size_t i = 0; i < std::min(bin.classes.size(), bin.shares.size()); ++i) {
            total = total + fractions[bin.classes[i]] * bin.shares[i];
        }
        result.push_back(total);
    }
    return result;
}

float CrystalGrainModel::Development::meanDensity(const Exposure& exposure, float logExposure) const {
    std::vector<float> fractions = binFractions(exposure, logExposure);
    float total = 0;
    for (size_t i = 0; i < bins.size(); ++i) total += dye(bins[i].demandAtFull * fractions[i], bins[i].pool);
    return total;
}

float CrystalGrainModel::Development::apertureVariance(const Exposure& exposure, float logExposure) const {
    std::vector<float> fractions = binFractions(exposure, logExposure);
    float apertureArea = kFloatPi * FilmStock::granularityApertureRadiusMM * FilmStock::granularityApertureRadiusMM;
    float total = 0;
    for (size_t i = 0; i < bins.size(); ++i) {
        const Bin& bin = bins[i];
        if (!(bin.cloudRadiusMM > 0 && bin.dyePerCloud > 0)) continue;
        float fraction = fractions[i];
        float demand = bin.demandAtFull * fraction;
        float poolGain = bin.pool > 0 ? std::exp(-demand / bin.pool) : 1.0f;
        float developed = bin.crystalsPerMM2 * fraction;
        float asked = developed * bin.dyePerCloud * bin.dyePerCloud * markSecondMoment();
        float response = FilmStock::granularityApertureResponse(bin.cloudRadiusMM / 2);
        total += poolGain * poolGain * asked * response * response / apertureArea;
    }
    return total;
}

namespace {

bool sameCurve(const CharacteristicCurve& a, const CharacteristicCurve& b) {
    for (float x : CrystalGrainModel::fitLogExposures()) {
        if (!(std::fabs(a.density(x) - b.density(x)) < 1e-4f)) return false;
    }
    return true;
}

}  // namespace

CrystalGrainModel::CrystalGrainModel(const FilmStock& stock, const FilmStock* reference, int layer, float grainScale)
    : readNetDensity(0), readSigma(0), dMin(0), range(0), curve(stock.curves[layer]) {
    const FilmStock& referenceStock = reference ? *reference : stock;
    const CrystalGrainPopulation& population = referenceStock.crystalGrainPopulation;
    const CharacteristicCurve& referenceCurve = referenceStock.curves[layer];
    const CharacteristicCurve& developedCurve = stock.curves[layer];
    curve = developedCurve;
    dMin = developedCurve.dMin;
    range = swiftMax(developedCurve.dMax() - developedCurve.dMin, 1e-3f);
    bool reversal = stock.isReversal;
    bool silver = stock.grainDensityLaw == GrainDensityLaw::silver;
    float gain = silver ? 1.0f : Development::poolGain;
    float kappa = gain >= 1 ? 1.0f : -std::log(gain) / (1 - gain);

    const std::vector<float>& classes = Exposure::classLogExposures();
    std::vector<float> targets;
    for (float x : fitLogExposures()) targets.push_back(referenceCurve.density(x) - referenceCurve.dMin);
    std::vector<double> targetsD(targets.begin(), targets.end());
    std::vector<std::vector<double>> basis;
    for (float x : fitLogExposures()) {
        std::vector<double> row;
        for (float e0 : classes) {
            float p = Exposure::hitFraction(x, e0);
            float forming = reversal ? 1 - p : p;
            float dye = Development::saturated(forming, gain);
            row.push_back(static_cast<double>(reversal ? 1 - dye : dye));
        }
        basis.push_back(row);
    }
    std::vector<float> weights = nonNegativeLeastSquares(basis, targetsD);
    float worst = 0;
    for (size_t r = 0; r < basis.size(); ++r) {
        double formed = 0;
        for (size_t i = 0; i < weights.size(); ++i) formed = formed + basis[r][i] * static_cast<double>(weights[i]);
        worst = swiftMax(worst, std::fabs(static_cast<float>(formed) - targets[r]));
    }

    float largestWeight = maxOr(weights, 0.0f);
    std::vector<int> active;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] > 1e-6f * largestWeight) active.push_back(static_cast<int>(i));
    }
    std::vector<float> logRadius;
    for (float e0 : classes) logRadius.push_back(-Exposure::sizeExponent * e0);
    if (!active.empty()) {
        float numerator = 0, denominator = 0;
        for (int i : active) numerator = numerator + weights[i] * logRadius[i];
        for (int i : active) denominator = denominator + weights[i];
        float mean = numerator / denominator;
        float half = std::log10(population.radiusSpan) / 2;
        for (float& r : logRadius) r = swiftMin(swiftMax(r, mean - half), mean + half);
    }

    std::vector<Development::Bin> bins;
    if (!active.empty()) {
        float activeWeight = 0;
        for (int k : active) activeWeight = activeWeight + weights[k];
        std::vector<float> cuts;
        float boundary = 0;
        for (size_t i = 0; i + 1 < population.sublayerShares.size(); ++i) {
            boundary += population.sublayerShares[i];
            cuts.push_back(boundary);
        }
        std::vector<std::vector<int>> members(binCount);
        float cumulative = 0;
        for (int k : active) {
            cumulative += weights[k];
            float fraction = cumulative / activeWeight - 1e-6f;
            int bin = binCount - 1;
            for (size_t c = 0; c < cuts.size(); ++c) {
                if (fraction < cuts[c]) {
                    bin = static_cast<int>(c);
                    break;
                }
            }
            bin = std::min(bin, binCount - 1);
            members[bin].push_back(k);
        }
        for (const std::vector<int>& ks : members) {
            if (ks.empty()) continue;
            float weight = 0, logSum = 0;
            for (int k : ks) weight = weight + weights[k];
            for (int k : ks) logSum = logSum + weights[k] * logRadius[k];
            float logMean = logSum / weight;
            std::vector<float> shares;
            for (int k : ks) shares.push_back(weights[k] / weight);
            bins.push_back(Development::Bin{std::pow(10.0f, logMean), 0, 0, 0, weight * kappa, 1, ks, shares});
        }
    }
    if (!bins.empty()) {
        std::vector<std::vector<double>> binBasis;
        for (float x : fitLogExposures()) {
            std::vector<float> fractions;
            for (float e0 : classes) fractions.push_back(Exposure::hitFraction(x, e0));
            std::vector<double> row;
            for (const Development::Bin& bin : bins) {
                float developed = 0;
                for (size_t i = 0; i < bin.classes.size(); ++i) {
                    developed = developed + fractions[bin.classes[i]] * bin.shares[i];
                }
                float forming = reversal ? 1 - developed : developed;
                float dye = Development::saturated(forming, gain);
                row.push_back(static_cast<double>(reversal ? 1 - dye : dye));
            }
            binBasis.push_back(row);
        }
        std::vector<float> refitted = nonNegativeLeastSquares(binBasis, targetsD);
        for (size_t i = 0; i < bins.size(); ++i) {
            float before = bins[i].demandAtFull / kappa;
            float scale = before > 0 ? refitted[i] / before : 0.0f;
            bins[i].demandAtFull = refitted[i] * kappa;
            for (int k : bins[i].classes) weights[k] *= scale;
        }
        worst = 0;
        for (size_t r = 0; r < binBasis.size(); ++r) {
            double formed = 0;
            for (size_t i = 0; i < refitted.size(); ++i) {
                formed = formed + binBasis[r][i] * static_cast<double>(refitted[i]);
            }
            worst = swiftMax(worst, std::fabs(static_cast<float>(formed) - targets[r]));
        }
    }
    while (static_cast<int>(bins.size()) < binCount) {
        bins.push_back(Development::Bin{0, 0, 0, 0, 0, 1, {}, {}});
    }

    float anchor = referenceStock.grainSizeMM * referenceStock.grainLayerSizeRatio[layer] * grainScale;
    float fastest = bins[0].cloudRadiusMM;
    for (Development::Bin& bin : bins) {
        if (!(bin.cloudRadiusMM > 0)) continue;
        float relative = bin.cloudRadiusMM / fastest;
        bin.cloudRadiusMM = anchor * relative;
        bin.dyePerCloud = std::pow(relative, Development::dyeExponent);
        bin.pool = silver ? 0.0f : bin.demandAtFull / -std::log(gain);
        bin.crystalsPerMM2 = bin.demandAtFull / bin.dyePerCloud;
    }
    exposure = Exposure{weights, logRadius};
    development = Development{bins, reversal, silver, fogFractions(weights, logRadius, stock.grainFogDensity), 1,
                              worst};

    readNetDensity = referenceStock.granularityAnchorDensity(layer);
    readSigma = referenceStock.grainStrength * referenceStock.grainLayerWeights[layer] * grainScale;
    CrystalGrainModel referenceModel = *this;
    referenceModel.curve = referenceCurve;
    referenceModel.dMin = referenceCurve.dMin;
    referenceModel.range = swiftMax(referenceCurve.dMax() - referenceCurve.dMin, 1e-3f);
    float unitVariance = referenceModel.apertureVariance(referenceModel.logExposure(readNetDensity));
    float perCloud = unitVariance > 0 ? readSigma * readSigma / unitVariance : 0.0f;
    for (Development::Bin& bin : development.bins) {
        if (!(bin.cloudRadiusMM > 0)) continue;
        bin.dyePerCloud *= perCloud;
        bin.crystalsPerMM2 = bin.dyePerCloud > 0 ? bin.demandAtFull / bin.dyePerCloud : 0.0f;
    }

    if (!sameCurve(referenceCurve, developedCurve)) {
        refitDevelopment(developedCurve);
        float measured = stock.grainStrength * stock.grainLayerWeights[layer] * grainScale;
        if (std::fabs(measured - readSigma) > 1e-6f) {
            float at = stock.granularityAnchorDensity(layer);
            float modelled = sigma(at);
            if (modelled > 0) {
                float scale = measured * measured / (modelled * modelled);
                for (Development::Bin& bin : development.bins) {
                    if (!(bin.cloudRadiusMM > 0)) continue;
                    bin.dyePerCloud *= scale;
                    bin.crystalsPerMM2 /= scale;
                }
            }
            readNetDensity = at;
            readSigma = measured;
        }
    }
    if (population.coatingDensityScale != 1) {
        for (Development::Bin& bin : development.bins) {
            bin.crystalsPerMM2 *= population.coatingDensityScale;
            bin.dyePerCloud /= population.coatingDensityScale;
        }
        readSigma /= std::sqrt(population.coatingDensityScale);
    }
}

std::vector<float> CrystalGrainModel::fogFractions(const std::vector<float>& weights,
                                                   const std::vector<float>& logRadius, float density) {
    std::vector<float> coated;
    for (size_t i = 0; i < std::min(weights.size(), logRadius.size()); ++i) {
        if (weights[i] > 0) coated.push_back(logRadius[i]);
    }
    float largest = maxOr(coated, 0.0f);
    std::vector<float> surface;
    for (float r : logRadius) surface.push_back(std::pow(10.0f, 2 * (r - largest)));
    auto formed = [&](float scale) {
        float total = 0;
        for (size_t i = 0; i < std::min(weights.size(), surface.size()); ++i) {
            total = total + weights[i] * swiftMin(scale * surface[i], 0.5f);
        }
        return total;
    };
    if (!(density > 0 && formed(FLT_MAX) > 0)) return std::vector<float>(weights.size(), 0.0f);
    float low = 0, high = 1;
    while (formed(high) < density && high < 1e9f) high *= 2;
    for (int i = 0; i < 60; ++i) {
        float mid = (low + high) / 2;
        if (formed(mid) < density) low = mid; else high = mid;
    }
    std::vector<float> result;
    for (float s : surface) result.push_back(swiftMin(high * s, 0.5f));
    return result;
}

void CrystalGrainModel::refitDevelopment(const CharacteristicCurve& developed) {
    std::vector<float> targets;
    for (float x : fitLogExposures()) targets.push_back(developed.density(x) - developed.dMin);
    const Development reference = development;
    std::vector<int> coated;
    for (size_t b = 0; b < reference.bins.size(); ++b) {
        if (reference.bins[b].demandAtFull > 0) coated.push_back(static_cast<int>(b));
    }
    struct Best {
        float developability;
        std::vector<float> gains;
        float error;
        double sse;
    };
    std::optional<Best> best;
    for (float developability : Development::developabilities()) {
        Development trial = reference;
        trial.developability = developability;
        std::vector<std::vector<float>> fractions;
        for (float x : fitLogExposures()) fractions.push_back(trial.binFractions(exposure, x));
        auto formed = [&](const std::vector<float>& gains, const std::vector<float>& row) {
            float total = 0;
            for (int b : coated) {
                total += Development::dye(reference.bins[b].demandAtFull * gains[b] * row[b], reference.bins[b].pool);
            }
            return total;
        };
        auto residual = [&](const std::vector<float>& gains) {
            double sse = 0.0;
            for (size_t i = 0; i < fractions.size(); ++i) {
                double error = static_cast<double>(formed(gains, fractions[i]) - targets[i]);
                sse += error * error;
            }
            return sse;
        };
        std::vector<float> gains(reference.bins.size(), 1.0f);
        for (int pass = 0; pass < 40; ++pass) {
            float moved = 0;
            for (int b : coated) {
                float low = 0, high = 6;
                const float phi = 0.6180339887f;
                float c = high - phi * (high - low), d = low + phi * (high - low);
                std::vector<float> probe = gains;
                probe[b] = c;
                double fc = residual(probe);
                probe[b] = d;
                double fd = residual(probe);
                for (int i = 0; i < 30; ++i) {
                    if (fc < fd) {
                        high = d;
                        d = c;
                        fd = fc;
                        c = high - phi * (high - low);
                        probe[b] = c;
                        fc = residual(probe);
                    } else {
                        low = c;
                        c = d;
                        fc = fd;
                        d = low + phi * (high - low);
                        probe[b] = d;
                        fd = residual(probe);
                    }
                }
                float updated = (low + high) / 2;
                moved = swiftMax(moved, std::fabs(updated - gains[b]));
                gains[b] = updated;
            }
            if (moved < 1e-5f) break;
        }
        double sse = residual(gains);
        float worst = 0;
        for (size_t i = 0; i < fractions.size(); ++i) {
            worst = swiftMax(worst, std::fabs(formed(gains, fractions[i]) - targets[i]));
        }
        if (!best || sse < best->sse) best = Best{developability, gains, worst, sse};
    }
    if (!best) return;
    development.developability = best->developability;
    development.fitError = best->error;
    for (int b : coated) {
        float gain = best->gains[b];
        development.bins[b].gain = gain;
        development.bins[b].demandAtFull *= gain;
        development.bins[b].dyePerCloud *= gain;
    }
}

std::vector<float> CrystalGrainModel::formingFractions(float logExposure) const {
    return development.formingFractions(exposure, logExposure);
}

std::vector<float> CrystalGrainModel::binFractions(float logExposure) const {
    return development.binFractions(exposure, logExposure);
}

float CrystalGrainModel::meanDensity(float logExposure) const { return development.meanDensity(exposure, logExposure); }

float CrystalGrainModel::logExposure(float netDensity) const {
    float target = isReversal() ? dMin + range - netDensity : dMin + netDensity;
    return curve.logExposure(swiftMin(swiftMax(target, dMin), dMin + range));
}

float CrystalGrainModel::apertureVariance(float logExposure) const {
    return development.apertureVariance(exposure, logExposure);
}

float CrystalGrainModel::sigma(float netDensity) const { return std::sqrt(apertureVariance(logExposure(netDensity))); }

std::vector<float> CrystalGrainModel::nonNegativeLeastSquares(const std::vector<std::vector<double>>& basis,
                                                              const std::vector<double>& targets) {
    size_t n = basis.empty() ? 0 : basis[0].size();
    std::vector<double> gram(n * n, 0.0);
    std::vector<double> moment(n, 0.0);
    for (size_t r = 0; r < std::min(basis.size(), targets.size()); ++r) {
        const std::vector<double>& row = basis[r];
        double target = targets[r];
        for (size_t i = 0; i < n; ++i) {
            moment[i] += row[i] * target;
            for (size_t j = 0; j < n; ++j) gram[i * n + j] += row[i] * row[j];
        }
    }
    std::vector<double> weights(n, 0.0);
    for (int iteration = 0; iteration < 5000; ++iteration) {
        double moved = 0.0;
        for (size_t i = 0; i < n; ++i) {
            if (!(gram[i * n + i] > 0)) continue;
            double residual = moment[i];
            for (size_t j = 0; j < n; ++j) {
                if (j != i) residual -= gram[i * n + j] * weights[j];
            }
            double updated = swiftMax(residual / gram[i * n + i], 0.0);
            moved = swiftMax(moved, std::fabs(updated - weights[i]));
            weights[i] = updated;
        }
        if (moved < 1e-9) break;
    }
    return std::vector<float>(weights.begin(), weights.end());
}

}  // namespace fotufilm
