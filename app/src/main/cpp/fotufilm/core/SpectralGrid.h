// Port of Sources/FotufilmCore/SpectralModel.swift: enum SpectralGrid, with the grid extensions
// from PrintIllumination.swift and PrintPaperTables.swift declared here.
#ifndef FOTUFILM_CORE_SPECTRAL_GRID_H
#define FOTUFILM_CORE_SPECTRAL_GRID_H

#include <vector>

#include "FilmSpectralProfile.h"
#include "Simd.h"

namespace fotufilm {
namespace SpectralGrid {

/// Spacing of the grid in nanometres.
constexpr float stepNM = 5;
constexpr int count = 81;
/// 380...780 nm at 5 nm.
const std::vector<float>& wavelengths();
const std::vector<float>& xBar();
const std::vector<float>& yBar();
const std::vector<float>& zBar();
const std::vector<float>& d65();

std::vector<float> asymmetricGaussian(float peak, float leftSigma, float rightSigma);

/// Slowest rate at which a digitised record is continued past its printed trace (log10/nm).
constexpr float minimumTailDecayPerNM = 0.05f;
/// How far below a record's peak the continuation runs before the layer is treated as blind.
constexpr float tailDecades = 3;

/// Continues a measured sensitivity record beyond the wavelength span the publication printed.
std::vector<float> continuedTails(const std::vector<float>& layer);

std::vector<std::vector<float>> normalizeSensitivities(const std::vector<std::vector<float>>& layers);

std::vector<std::vector<float>> dyes(FilmDyeFamily family);

/// Rescales three dye density spectra so equal C, M, Y densities come out spectrally neutral.
std::vector<std::vector<float>> partition(const std::vector<std::vector<float>>& densities);

/// Planck's law on the grid, normalized to its own peak.
std::vector<float> blackbody(float kelvinK);

/// One value off a grid-sampled curve at an arbitrary wavelength, linear between samples and held
/// flat outside the grid.
float interpolate(const std::vector<float>& curve, float wavelength);

/// CIE 1931 tristimulus of a spectral power distribution on the grid.
Float3 xyz(const std::vector<float>& spectrum);

Float3 linearDisplayP3FromXYZ(Float3 v);
/// XYZ into the linear Rec.2020 working space, through the P3 matrix and the engine's seam digits.
Float3 linearRec2020FromXYZ(Float3 v);

const std::vector<float>& enlarger3200K();
/// The flat lamp a scan integrates under.
const std::vector<float>& equalEnergy();

/// Display primaries for the print side of the model, under D65.
Float3 toLinearDisplayP3(const std::vector<float>& reflectance);
/// The same observer under a stated light, Bradford-adapted to D65.
Float3 toLinearDisplayP3(const std::vector<float>& reflectance, const std::vector<float>& illuminant);

/// The RA-4 receiver: Kodak Ektacolor Edge's spectra, which also stand in for the outputs that need
/// a neutral visual judge but have no dyes of their own (defined in SpectralModel.cpp).
const std::vector<std::vector<float>>& paperDyeAmounts();
const std::vector<std::vector<float>>& paperDyes();
const std::vector<std::vector<float>>& paperSensitivity();

// PrintIllumination.swift's `extension SpectralGrid` (defined in PrintIllumination.cpp).
/// Idealized additive printer filters on a 3200 K tungsten source, in R/G/B order.
const std::vector<std::vector<float>>& releasePrinterBeams();

// PrintPaperTables.swift's `extension SpectralGrid` (defined in PrintPaperTables.cpp).
/// Minilab scanner sensitivity as LED emission bands at 630, 545, and 465 nm.
const std::vector<std::vector<float>>& labScanSensitivity();
/// SMPTE RP 180 telecine receiver sensitivity on the 5 nm grid.
const std::vector<std::vector<float>>& telecineSensitivity();

// SpectralControlPoints.swift's `public extension SpectralGrid`.
/// The dye record the engine builds for one of its published process families.
std::vector<std::vector<float>> familyDyeDensities(FilmDyeFamily family);
/// Rescales three dye density rows so the three sum to one at every wavelength.
std::vector<std::vector<float>> partitionedDyes(const std::vector<std::vector<float>>& densities);

}  // namespace SpectralGrid
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_SPECTRAL_GRID_H
