// Port of Sources/FotufilmCore/LayeredTransport.swift: the pack data type, its decoding and
// validation only. Layered transport rendering (the solver and kernels) is not ported; the type
// exists because schema-3 packs carry it and validation requires it.
#ifndef FOTUFILM_CORE_LAYERED_TRANSPORT_H
#define FOTUFILM_CORE_LAYERED_TRANSPORT_H

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "Json.h"

namespace fotufilm {

/// `TransportError.invalid`.
class TransportError : public std::runtime_error {
public:
    explicit TransportError(const std::string& message) : std::runtime_error(message) {}
};

struct LayeredTransport {
    struct Layer {
        std::string id;
        double thicknessMM;
        /// Either one constant or a sample on every wavelength in SpectralGrid.
        std::vector<double> refractiveIndex;
        std::vector<double> absorptionPerMM;

        bool operator==(const Layer& o) const {
            return id == o.id && thicknessMM == o.thicknessMM && refractiveIndex == o.refractiveIndex &&
                   absorptionPerMM == o.absorptionPerMM;
        }
    };

    int64_t revision = 1;
    std::string kind = "conditional-launch";
    std::string constructionID;
    std::vector<double> wavelengthsNM;
    /// Ordered from the exposing surface to the rear surface.
    std::vector<Layer> layers;
    std::vector<double> frontIndex;
    std::vector<double> rearIndex;
    std::optional<std::vector<double>> rearReflectance;
    std::optional<std::vector<double>> rearPlateReflectance;
    /// Capture/launch planes measured down from the exposing surface, in R,G,B order.
    std::vector<double> recordDepthMM;
    std::vector<std::vector<double>> angularExponent;
    std::vector<std::vector<double>> captureProbability;
    std::vector<std::vector<double>> returnedToDirect;
    std::vector<double> coreSigmaMM;

    /// Throws TransportError naming the first problem.
    void validate() const;

    static LayeredTransport decode(const JsonValue& value, const std::string& path);

    static double sample(const std::vector<double>& spectrum, int band) {
        return spectrum.size() == 1 ? spectrum[0] : spectrum[band];
    }

    bool operator==(const LayeredTransport& o) const;
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_LAYERED_TRANSPORT_H
