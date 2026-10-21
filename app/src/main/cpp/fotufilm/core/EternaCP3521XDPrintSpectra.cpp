// Port of Sources/FotufilmCore/EternaCP3521XDPrintSpectra.swift (published data, values unchanged).
#include "EternaCP3521XDPrintSpectra.h"

namespace fotufilm {
namespace EternaCP3521XDPrintSpectra {

const CharacteristicCurve& redCurve() {
    static const CharacteristicCurve curve(0.08145f, 7.8f, 3.52809f, 0.232236f, 4.08488f, 0.093767f);
    return curve;
}

const CharacteristicCurve& greenCurve() {
    static const CharacteristicCurve curve(0.0797436f, 4.95f, 2.33172f, 0.242163f, 3.29732f, 0.102849f);
    return curve;
}

const CharacteristicCurve& blueCurve() {
    static const CharacteristicCurve curve(0.139644f, 7.3f, 1.55147f, 0.253581f, 2.17227f, 0.105622f);
    return curve;
}

}  // namespace EternaCP3521XDPrintSpectra
}  // namespace fotufilm
