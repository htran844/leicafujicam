// Port of Sources/FotufilmCore/ExpiredFilm.swift.
#include "ExpiredFilm.h"

#include <algorithm>
#include <cmath>

#include "FilmStock.h"

namespace fotufilm {

const std::vector<float>& ExpiredFilm::layerWeights() {
    static const std::vector<float> weights = {0.7f, 1.0f, 1.3f};
    return weights;
}

FilmStock FilmStock::expired(float years) const {
    if (!(years > 0)) return *this;
    FilmStock aged = *this;
    float decades = years / 10;
    float perStop = static_cast<float>(std::log10(2.0));
    float fogTotal = 0;
    for (int layer = 0; layer < 3; ++layer) {
        float lostStops = ExpiredFilm::speedLossStopsPerDecade * decades * ExpiredFilm::layerWeights()[layer];
        float fog = ExpiredFilm::fogPerStopLost * lostStops;
        fogTotal += fog;
        // Speed loss is a pure translation of the curve up the exposure axis.
        aged.curves[layer].toe += lostStops * perStop;
        aged.curves[layer].shoulder += lostStops * perStop;
        aged.curves[layer].dMin += fog;
        // A separately specified minimum must gain the same developed fog.
        if (aged.spectralProfile.minimumDensity) {
            const std::vector<float>& minimum = *aged.spectralProfile.minimumDensity;
            const std::vector<float>& dye = spectralProfile.imageDyeDensity[layer];
            size_t n = std::min(minimum.size(), dye.size());
            std::vector<float> raised(n);
            for (size_t i = 0; i < n; ++i) raised[i] = minimum[i] + fog * dye[i];
            aged.spectralProfile.minimumDensity = std::move(raised);
        }
        aged.curves[layer].sampled.reset();
        if (curves[layer].sampled) {
            aged.curves[layer].sampled = curves[layer].sampled->shifted(lostStops * perStop, fog);
        }
    }
    aged.grainStrength *= 1 + ExpiredFilm::grainPerFog * (fogTotal / 3);
    // The fog the emulsion gained is developed density, so the density law reads granularity off it.
    aged.grainFogDensity += fogTotal / 3;
    return aged;
}

}  // namespace fotufilm
