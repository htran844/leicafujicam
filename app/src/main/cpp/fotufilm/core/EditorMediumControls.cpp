// Port of Sources/FotufilmEditModel/EditorMediumControls.swift (declared in EditorControlCatalogue.h).
#include "EditorControlCatalogue.h"
#include "Enlarger.h"
#include "FilmStock.h"
#include "PrinterLights.h"

namespace fotufilm {
namespace EditorControlCatalogue {

bool medium(PrintPaper paper, EditorControlField field, const FilmStock* stock, DigitalReferenceStyle digitalReference) {
    const EditorControl* described = control(field);
    if (described && described->section == EditorControlSection::printLamp) {
        return stock ? EnlargerOptics::illuminates(*stock, paper) : false;
    }
    switch (field) {
        case EditorControlField::printLight: return stock != nullptr && viewingLights(paper).size() > 1;
        case EditorControlField::printCorrection:
            if (!stock) return false;
            return !stock->isReversal && !stock->isMonochrome && acceptsPrintCorrection(paper);
        case EditorControlField::negativeViewing: return isNegative(paper);
        case EditorControlField::digitalReference:
            if (!stock) return false;
            return paper == PrintPaper::screen && !stock->isReflectionPrint;
        case EditorControlField::screenExposure:
            // Digital Reference's exposure, and Lab Scan's density key for a negative's scan.
            if (!stock || stock->isReflectionPrint) return false;
            return paper == PrintPaper::screen || (paper == PrintPaper::labScan && !stock->isReversal);
        case EditorControlField::screenCyan:
        case EditorControlField::screenMagenta:
        case EditorControlField::screenYellow:
            if (!stock) return false;
            return (paper == PrintPaper::screen || paper == PrintPaper::labScan) && !stock->isReversal &&
                   !stock->isMonochrome && !stock->isReflectionPrint;
        case EditorControlField::screenRedBand:
        case EditorControlField::screenGreenBand:
        case EditorControlField::screenBlueBand:
            if (!stock) return false;
            return paper == PrintPaper::screen && !stock->isMonochrome && !stock->isReflectionPrint;
        case EditorControlField::printerRed:
        case EditorControlField::printerGreen:
        case EditorControlField::printerBlue:
            // A release printer exposes a negative onto cinema print film; a slide is viewed.
            return stock ? PrinterLights::applies(*stock, paper) : false;
        case EditorControlField::labScanDodging:
        case EditorControlField::labScanLook:
            if (!stock) return false;
            return paper == PrintPaper::labScan && !stock->isReversal && !stock->isReflectionPrint;
        case EditorControlField::screenGrade:
            if (!stock) return false;
            return paper == PrintPaper::screen && !stock->isReflectionPrint && !stock->isReversal &&
                   digitalReference != DigitalReferenceStyle::referenceExposure;
        default: return true;
    }
}

}  // namespace EditorControlCatalogue
}  // namespace fotufilm
