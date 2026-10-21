// Port of Sources/FotufilmCore/SpectralModel.swift: SpectralRuntime's print receiver, the
// reflection-print setup trims, the lab-scan reference solve, `neutralPrintingBalance` and
// `neutralToneScale`.
#include <cfloat>
#include <cmath>
#include <map>
#include <mutex>

#include "BoundedCache.h"
#include "ColorScience.h"
#include "PrintPaperTables.h"
#include "SpectralGrid.h"
#include "SpectralModel.h"

namespace fotufilm {
namespace SpectralRuntime {

std::mutex& balanceLock();  // SpectralRuntime.cpp

namespace {
BoundedCache<uint64_t, std::vector<float>>& balanceCache() {
    static BoundedCache<uint64_t, std::vector<float>> cache(256);
    return cache;
}

Float3 cross(Float3 a, Float3 b) { return Float3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
float dot(Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/// Find a visual setup density, not a printer-light magnitude (Hunt, sections 14.16 and 16.2).
Float3 solveReflectionPrintSetup(const PrintDyeUnmix& unmix, float anchor, Float3 target,
                                 const std::optional<std::vector<float>>& illuminant) {
    auto printed = [&](Float3 offset) {
        Float3 amounts = unmix.amounts(Float3::repeating(anchor) + offset);
        return transmissionRGB({amounts.x, amounts.y, amounts.z}, unmix.dyes(), 0,
                               illuminant ? &*illuminant : nullptr);
    };
    Float3 offset = Float3::repeating(0);
    for (int iteration = 0; iteration < 24; ++iteration) {
        Float3 base = printed(offset);
        Float3 residual = base - target;
        if (swiftMax(std::fabs(residual.x), swiftMax(std::fabs(residual.y), std::fabs(residual.z))) < 1e-7f) break;
        const float epsilon = 1e-3f;
        Float3 columns[3];
        for (int channel = 0; channel < 3; ++channel) {
            Float3 stepped = offset;
            stepped[channel] += epsilon;
            columns[channel] = (printed(stepped) - base) / epsilon;
        }
        float determinant = dot(columns[0], cross(columns[1], columns[2]));
        if (!(std::fabs(determinant) > 1e-12f)) break;
        offset -= Float3(dot(residual, cross(columns[1], columns[2])) / determinant,
                         dot(columns[0], cross(residual, columns[2])) / determinant,
                         dot(columns[0], cross(columns[1], residual)) / determinant);
    }
    return offset;
}
}  // namespace

Float3 PrintReceiver::rgb(Float3 density) const {
    const std::vector<float>* light = viewingLight ? &*viewingLight : nullptr;
    if (!unmix) return transmissionRGB({density.x, density.y, density.z}, dyes, 0, light);
    Float3 amounts = unmix->amounts(density);
    return transmissionRGB({amounts.x, amounts.y, amounts.z}, dyes, 0, light);
}

PrintReceiver printReceiver(const FilmStock& stock, PrintPaper paper,
                            const std::optional<std::vector<float>>& viewingLight) {
    // A scan has no viewing dyes, the digital reference is not a sheet, and a monochrome print is
    // forced neutral, so none of them unmix.
    bool unmixes = !stock.isMonochrome && !isScan(paper) && paper != PrintPaper::screen && !isNegative(paper);
    if (!unmixes) return PrintReceiver{dyes(paper), viewingLight, std::nullopt};
    return PrintReceiver{analyticalDyes(paper), viewingLight, PrintDyeUnmix(analyticalDyes(paper))};
}

Float3 reflectionPrintDensityTrim(PrintPaper paper) {
    // Solved once per reflection medium, under its reference lamp.
    static const std::map<PrintPaper, Float3> trims = [] {
        std::map<PrintPaper, Float3> result;
        for (PrintPaper p : allPrintPapers()) {
            std::optional<PrintProcess> process = printProcess(p);
            if (!process || !isReflection(*process)) continue;
            result[p] = solveReflectionPrintSetup(PrintDyeUnmix(analyticalDyes(p)), anchorDensity(p),
                                                  Float3::repeating(std::pow(10.0f, -midDensity(p))),
                                                  referenceViewingLight(p));
        }
        return result;
    }();
    auto found = trims.find(paper);
    return found == trims.end() ? Float3() : found->second;
}

LabScanReferenceSolve labScanReferenceSolve(const FilmStock& stock) {
    PrintPaper paper = PrintPaper::labScan;
    std::vector<float> midDensity = {stock.curves[0].density(0), stock.curves[1].density(0), stock.curves[2].density(0)};
    Float3 midEnergy = paperExposure(midDensity, stock, SpectralGrid::equalEnergy(), sensitivity(paper));
    return {std::log10(swiftMax(midEnergy.x, 1e-12f) / swiftMax(midEnergy.y, 1e-12f)),
            std::log10(swiftMax(midEnergy.z, 1e-12f) / swiftMax(midEnergy.y, 1e-12f)),
            neutralPrintingBalance(stock, paper)};
}

std::vector<float> neutralPrintingBalance(const FilmStock& stock, PrintPaper paper) {
    if (stock.isReversal || stock.isMonochrome || !acceptsPrintCorrection(paper)) return {1, 1, 1};

    uint64_t key = cacheIdentifier(stock, paper);
    {
        std::lock_guard<std::mutex> guard(balanceLock());
        if (auto found = balanceCache().value(key)) return *found;
    }

    const float perStop = static_cast<float>(std::log10(2.0));
    std::vector<std::vector<float>> paperSensitivity = sensitivity(paper);
    const auto& dyes = stock.spectralProfile.imageDyeDensity;
    std::vector<float> midDensity = {stock.curves[0].density(0), stock.curves[1].density(0), stock.curves[2].density(0)};
    PrintingIllumination illumination = printingIllumination(stock, paper, midDensity, dyes);
    const std::vector<float>& lamp = illumination.lamp;
    Float3 midEnergy = illumination.referenceEnergy;
    std::vector<CharacteristicCurve> curves = printCurves(paper, stock);
    std::vector<float> xMids = printExposureMidpoints(paper, stock);

    std::vector<Float3> relatives;
    for (float stops : strideThrough(-5.0f, 5.0f, 0.25f)) {
        std::vector<float> density = {stock.developedDensity(0, stops * perStop), stock.developedDensity(1, stops * perStop),
                                      stock.developedDensity(2, stops * perStop)};
        Float3 energy = paperExposure(density, stock, lamp, paperSensitivity);
        relatives.push_back(Float3(std::log10(swiftMax(energy.x, 1e-12f) / swiftMax(midEnergy.x, 1e-12f)),
                                   std::log10(swiftMax(energy.y, 1e-12f) / swiftMax(midEnergy.y, 1e-12f)),
                                   std::log10(swiftMax(energy.z, 1e-12f) / swiftMax(midEnergy.z, 1e-12f))));
    }

    PrintReceiver receiver = printReceiver(stock, paper, referenceViewingLight(paper));

    // One wedge point as the viewer has it.
    auto viewed = [&](Float3 relative, const std::vector<float>& scale) {
        Float3 printed;
        for (int channel = 0; channel < 3; ++channel) {
            printed[channel] = curves[channel].density(xMids[channel] + scale[channel] * relative[channel]) -
                               curves[channel].dMin;
        }
        return receiver.rgb(printed);
    };
    // How far the printed wedge departs from grey, in log density, summed over the wedge.
    auto cost = [&](const std::vector<float>& scale) {
        float total = 0;
        for (const Float3& relative : relatives) {
            Float3 rgb = viewed(relative, scale);
            float g = std::log10(swiftMax(rgb.y, 1e-6f));
            float dr = std::log10(swiftMax(rgb.x, 1e-6f)) - g;
            float db = std::log10(swiftMax(rgb.z, 1e-6f)) - g;
            total += dr * dr + db * db;
        }
        return total;
    };

    std::vector<float> solved = {1, 1, 1};
    // Minimise along one channel with the others held: a coarse sweep and a golden-section refine.
    auto minimise = [&](int channel, float centre, float span) {
        std::vector<float> trial = solved;
        auto at = [&](float scale) {
            trial[channel] = scale;
            return cost(trial);
        };
        float bestScale = centre;
        float bestCost = FLT_MAX;
        for (float step : strideThrough(swiftMax(centre - span, 0.2f), centre + span, 0.01f)) {
            float c = at(step);
            if (c < bestCost) {
                bestCost = c;
                bestScale = step;
            }
        }
        float low = swiftMax(bestScale - 0.01f, 0.2f);
        float high = bestScale + 0.01f;
        const float phi = 0.6180339887f;
        float c1 = high - phi * (high - low);
        float c2 = low + phi * (high - low);
        float f1 = at(c1);
        float f2 = at(c2);
        for (int i = 0; i < 48; ++i) {
            if (f1 < f2) {
                high = c2;
                c2 = c1;
                f2 = f1;
                c1 = high - phi * (high - low);
                f1 = at(c1);
            } else {
                low = c1;
                c1 = c2;
                f1 = f2;
                c2 = low + phi * (high - low);
                f2 = at(c2);
            }
        }
        solved[channel] = (low + high) / 2;
    };

    minimise(0, 1, 0.6f);
    minimise(2, 1, 0.6f);
    for (int i = 0; i < 2; ++i) {
        minimise(0, solved[0], 0.1f);
        minimise(2, solved[2], 0.1f);
    }

    {
        std::lock_guard<std::mutex> guard(balanceLock());
        balanceCache().insert(solved, key);
    }
    return solved;
}

std::vector<float> neutralToneScale(const std::vector<float>& stops, const FilmStock& stock, PrintPaper requestedPaper,
                                    float printCorrection, const ToneScaleOptions& options) {
    PrintPaper paper = resolved(requestedPaper, stock);
    DigitalReferenceStyle digitalReference = options.digitalReference;
    ReceiverBands receiverBands = effectiveReceiverBands(options.receiverBands, stock, paper);
    float callier = callierCoefficient(options.callier, stock, paper);
    float grade = effectiveScreenGrade(options.screenGrade, stock, paper, digitalReference);
    float exposureShift =
        screenExposureShift(options.screenExposureEV, stock, paper, digitalReference, options.screenGrade);
    // Display-linear print RGB is Display P3, so its luminance uses the P3 weights.
    Float3 luma = ColorScience::displayP3LuminanceWeights;
    auto luminance = [&](Float3 rgb) { return luma.x * rgb.x + luma.y * rgb.y + luma.z * rgb.z; };
    const float perStop = static_cast<float>(std::log10(2.0));
    auto developed = [&](float logExposure) {
        return std::vector<float>{stock.developedDensity(0, logExposure), stock.developedDensity(1, logExposure),
                                  stock.developedDensity(2, logExposure)};
    };
    std::vector<float> result;
    result.reserve(stops.size());

    if (levelsPositive(paper, stock, digitalReference)) {
        // The same shift the kernel applies through its mid-point slots, on the same read.
        std::shared_ptr<const PositiveScreenRead> read = positiveScreenRead(stock);
        float shift =
            DigitalReferenceReceiver::levels(stock, digitalReference, options.sceneHighlightStops).shift + exposureShift;
        const CharacteristicCurve& curve = DigitalReferenceReceiver::positiveCurve();
        float xMid = curve.logExposure(curve.dMin + DigitalReferenceReceiver::anchorDensity);
        for (float s : stops) {
            Float3 rgb = read->transmittance(developed(s * perStop));
            Float3 printed;
            for (int c = 0; c < 3; ++c) {
                printed[c] = curve.density(xMid + shift - std::log10(swiftMax(rgb[c], 1e-6f) / 0.18f)) - curve.dMin;
            }
            result.push_back(luminance(DigitalReferenceReceiver::positiveRGB(printed)));
        }
        return result;
    }
    if (viewsFilmDirectly(paper, stock)) {
        NeutralDensityBasis basis = neutralDensityBasis(stock);
        DensityMap aligned = [&basis](const std::vector<float>& density) { return basis(density); };
        Float3 balance = reversalBalance(stock, aligned) *
                         std::exp2(directViewExposure(options.screenExposureEV, stock, paper, digitalReference));
        for (float s : stops) {
            std::vector<float> density = aligned(developed(s * perStop));
            Float3 rgb = transmissionRGB(density, stock) * balance;
            result.push_back(luminance(Float3(swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f))));
        }
        return result;
    }
    if (isNegative(paper)) {
        for (float s : stops) {
            Float3 rgb = transmissionRGB(developed(s * perStop), stock);
            result.push_back(luminance(Float3(swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f))));
        }
        return result;
    }

    std::shared_ptr<const NeutralDensityBasis> basis;
    if (stock.isReversal) basis = std::make_shared<const NeutralDensityBasis>(neutralDensityBasis(stock));
    DensityMap aligned = [basis](const std::vector<float>& density) { return basis ? (*basis)(density) : density; };
    std::vector<float> midDensity = aligned(developed(0));
    std::vector<float> scaledMid(3);
    for (int c = 0; c < 3; ++c) scaledMid[c] = midDensity[c] * callier;
    auto energyReader = [&](const ReceiverBands& bands) -> DensityRead {
        auto paperSensitivity = std::make_shared<const std::vector<std::vector<float>>>(sensitivity(paper, bands));
        PrintingIllumination illumination = printingIllumination(stock, paper, scaledMid,
                                                                 stock.spectralProfile.imageDyeDensity, 0, callier, bands);
        auto lamp = std::make_shared<const std::vector<float>>(illumination.lamp);
        Float3 midEnergy = illumination.referenceEnergy;
        // The same reference cast the printing LUT carries.
        Float3 castOffset = referenceCastOffset(midEnergy, stock, paper);
        const FilmStock* stockPointer = &stock;
        return [aligned, callier, lamp, paperSensitivity, midEnergy, castOffset,
                stockPointer](const std::vector<float>& density) {
            std::vector<float> scaled = aligned(density);
            for (float& d : scaled) d = d * callier;
            Float3 energy = paperExposure(scaled, *stockPointer, *lamp, *paperSensitivity, 0, callier);
            return Float3(std::log10(swiftMax(energy.x, 1e-12f) / swiftMax(midEnergy.x, 1e-12f)),
                          std::log10(swiftMax(energy.y, 1e-12f) / swiftMax(midEnergy.y, 1e-12f)),
                          std::log10(swiftMax(energy.z, 1e-12f) / swiftMax(midEnergy.z, 1e-12f))) +
                   castOffset;
        };
    };
    DensityRead relativeEnergy = energyReader(receiverBands);
    DensityRead screenRead =
        screenReader(stock, receiverBands, relativeEnergy, [&]() { return energyReader(ReceiverBands::paper()); });
    float neutralMid = neutralDensity(stock, 0);
    std::vector<CharacteristicCurve> curves = printCurves(paper, stock, digitalReference);
    std::vector<float> xMids = printExposureMidpoints(paper, stock, digitalReference);
    std::vector<float> masking = stock.printingContrastScale(printCorrection, paper);
    float shadowScale = screenShadowScale(stock, paper, digitalReference);
    ScreenToeBalance toeBalance = screenToeBalance(
        stock,
        paper == PrintPaper::screen && !readsLayersDirectly(paper, stock) ? digitalReference
                                                                           : DigitalReferenceStyle::gradedPrint,
        screenRead);
    if (paper == PrintPaper::screen) {
        // The same levels the kernel applies through its mid-point and contrast slots.
        ReceiverLevels levels = DigitalReferenceReceiver::levels(stock, digitalReference, options.sceneHighlightStops);
        for (float& m : masking) m = m * levels.scale;
        for (float& x : xMids) x = x + exposureDirection(paper) * (levels.shift + exposureShift);
    }
    Float3 colourShift = DigitalReferenceReceiver::colourShift(options.screenCMY, stock, paper);
    for (int channel = 0; channel < 3; ++channel) xMids[channel] += colourShift[channel];
    PrintReceiver receiver = printReceiver(stock, paper, referenceViewingLight(paper));
    for (float s : stops) {
        std::vector<float> density = developed(s * perStop);
        Float3 relative;
        if (readsLayersDirectly(paper, stock)) {
            for (int layer = 0; layer < 3; ++layer) {
                float exposure = stock.curves[layer].logExposure(density[layer]);
                relative[layer] =
                    DigitalReferenceReceiver::stretchShadows(neutralMid - neutralDensity(stock, exposure), shadowScale);
            }
        } else {
            relative = relativeEnergy(density);
            if (paper == PrintPaper::screen) {
                Float3 read = toeBalance(screenRead(density));
                relative = Float3(DigitalReferenceReceiver::stretchShadows(read.x, shadowScale),
                                  DigitalReferenceReceiver::stretchShadows(read.y, shadowScale),
                                  DigitalReferenceReceiver::stretchShadows(read.z, shadowScale));
            }
        }
        Float3 printed;
        for (int channel = 0; channel < 3; ++channel) {
            printed[channel] = curves[channel].density(xMids[channel] + masking[channel] * exposureDirection(paper) *
                                                                            relative[channel]) -
                               curves[channel].dMin;
        }
        Float3 rgb;
        if (paper == PrintPaper::screen) {
            rgb = DigitalReferenceReceiver::rgb(printed, digitalReference, stock, grade);
        } else if (isScan(paper)) {
            // A scan's characterization holds the receiver's luminance exactly.
            rgb = Float3(std::pow(10.0f, -printed.x), std::pow(10.0f, -printed.y), std::pow(10.0f, -printed.z));
        } else {
            rgb = receiver.rgb(printed);
        }
        result.push_back(luminance(Float3(swiftMax(rgb.x, 0.0f), swiftMax(rgb.y, 0.0f), swiftMax(rgb.z, 0.0f))));
    }
    return result;
}

}  // namespace SpectralRuntime
}  // namespace fotufilm
