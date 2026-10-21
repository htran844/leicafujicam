#pragma once

#include "spectral_data.h"
#include "spectral_gamut.h"

namespace spektrafilm {

// Pointwise LUT-mode scanning. The medium can be a print or a positive film.
class Scanner {
public:
    Scanner(const DataSet& shared, const DataSet& medium, const OutputGamut& gamut);
    Vec3 scan(const Vec3& density) const;
private:
    std::vector<double> dyes_, base_, valid_, illuminant_, observer_, matrix_;
    double normalization_ = 0;
    const OutputGamut& gamut_;
};

class Printing {
public:
    Printing(const DataSet& shared, const DataSet& film, const DataSet& paper,
             const std::string& paperName, const Vec3& midgrayDensity);
    Vec3 expose(const Vec3& filmDensity) const;
    Vec3 develop(const Vec3& logExposure) const;
private:
    std::vector<double> filmDyes_, filmBase_, valid_, weights_, axis_, curves_;
    double normalization_ = 1, exposure_ = 1;
};

double proPhotoDecode(double value);
double proPhotoEncode(double value);

}
