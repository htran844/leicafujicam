// Port of Sources/FotufilmCore/PrintPaperTables.swift: the measurements each print medium is read
// through. `labScanReferenceBalance` stays in PrintPaper.h; the SpectralGrid scan bands are
// declared in SpectralGrid.h.
#ifndef FOTUFILM_CORE_PRINT_PAPER_TABLES_H
#define FOTUFILM_CORE_PRINT_PAPER_TABLES_H

#include <optional>
#include <vector>

#include "CharacteristicCurve.h"
#include "DigitalReferenceReceiver.h"
#include "PrintPaper.h"
#include "Simd.h"

namespace fotufilm {

struct FilmStock;

/// Viewing dye spectra normalized for flat neutral density (the RA-4 set where no stock prints).
const std::vector<std::vector<float>>& dyes(PrintPaper paper);
/// The sheet's own dye shapes at their neutral-forming amounts, before `partition`.
const std::vector<std::vector<float>>& analyticalDyes(PrintPaper paper);
/// Layer sensitivities with publication tails extended (Digital Reference at the paper bands).
std::vector<std::vector<float>> sensitivity(PrintPaper paper);
/// The layer sensitivities, with Digital Reference reading through `bands`.
std::vector<std::vector<float>> sensitivity(PrintPaper paper, const ReceiverBands& bands);
/// Gross Status A setup aims of a release print.
std::optional<Float3> ladStatusA(PrintPaper paper);
/// Log exposures at which the published reciprocal-exposure sensitivity was measured.
std::vector<float> sensitivityReferenceExposures(PrintPaper paper, const FilmStock& stock);
/// Fixed printer exposure-axis origins.
std::vector<float> printExposureMidpoints(PrintPaper paper, const FilmStock& stock,
                                          DigitalReferenceStyle digitalReference = kDefaultDigitalReferenceStyle);
/// Sensitivity-weighted energy needed at setup, relative to each record's sensitivity reference.
Float3 printingAim(PrintPaper paper, const FilmStock& stock);
/// The characteristic curve the print's timing is reckoned against: the green record.
CharacteristicCurve printCurve(PrintPaper paper, const FilmStock& stock,
                               DigitalReferenceStyle digitalReference = kDefaultDigitalReferenceStyle);
/// The characteristic curves the print's three records develop along, R, G, B.
std::vector<CharacteristicCurve> printCurves(PrintPaper paper, const FilmStock& stock,
                                             DigitalReferenceStyle digitalReference = kDefaultDigitalReferenceStyle);

namespace PrintPaperTables {
/// Editable scan tone scale.
const CharacteristicCurve& labScanCurve();
/// Telecine inversion on the RA-4 log-exposure axis.
const CharacteristicCurve& telecineCurve();
/// log10(red/green, blue/green) at mid-grey of the lab-scan reference stock (Portra 400).
constexpr Float2 labScanReferenceMidRatio{0.5609019f, -0.444564f};
/// Maximum retained reference-profile cast in log receiver exposure.
constexpr float labScanCastCeiling = 0.029f;
}  // namespace PrintPaperTables

}  // namespace fotufilm

#endif  // FOTUFILM_CORE_PRINT_PAPER_TABLES_H
