// Port of Sources/FotufilmCore/PrinterLights.swift.
#ifndef FOTUFILM_CORE_PRINTER_LIGHTS_H
#define FOTUFILM_CORE_PRINTER_LIGHTS_H

#include <cstdint>
#include <optional>

#include "PrintPaper.h"
#include "Simd.h"

namespace fotufilm {

struct FilmStock;

/// The red, green and blue lights of the additive printer a release print is exposed on, in
/// printer points. 25-25-25 prints the negative at the stock's LAD aim.
struct PrinterLights {
    float red = 25;
    float green = 25;
    float blue = 25;

    PrinterLights() = default;
    PrinterLights(float red_, float green_, float blue_) : red(red_), green(green_), blue(blue_) {}

    /// The lab's aim setup: every light at the centre of the scale.
    static PrinterLights balanced() { return PrinterLights(); }
    static constexpr float rangeLower = 1;
    static constexpr float rangeUpper = 50;
    static constexpr float centre = 25;
    /// Log exposure per printer point.
    static constexpr float logExposurePerPoint = 0.025f;

    float operator[](int channel) const { return channel == 0 ? red : (channel == 1 ? green : blue); }
    float& operator[](int channel) { return channel == 0 ? red : (channel == 1 ? green : blue); }

    /// Each light held on the scale; a non-finite one takes the centre.
    PrinterLights normalized() const;
    /// Each beam's intensity against the aim setup.
    Float3 beamGain() const;

    /// Whether a release printer exposes this stock on this medium.
    static bool applies(const FilmStock& stock, PrintPaper paper);
    /// The lights the printing table is built with, or nullopt where they change nothing.
    static std::optional<PrinterLights> resolved(const std::optional<PrinterLights>& lights, const FilmStock& stock,
                                                 PrintPaper paper);

    uint64_t spectralSignature() const;

    bool operator==(const PrinterLights& o) const { return red == o.red && green == o.green && blue == o.blue; }
    bool operator!=(const PrinterLights& o) const { return !(*this == o); }
};

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PRINTER_LIGHTS_H
