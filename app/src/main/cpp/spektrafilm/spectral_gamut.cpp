// Algorithm port of Spektrafilm 0.3.4 (3bb2c2d2801ff68b92019cf1dbcbb133d60832bc),
// utils/gamut_compression.py, and colour-science 0.4.7 CAM16 / CAM16-UCS.
// See the bundled Spektrafilm and colour-science license notices.
#include "spectral_gamut.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace spektrafilm {
namespace {

using Matrix = std::array<Vec3, 3>;
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr int kLightnessCount = 64;
constexpr int kHueCount = 720;
constexpr int kBisections = 18;
// colour-science intentionally uses these published rounded ProPhoto
// matrices. Replacing XYZ-to-RGB with a new inverse changes the boundary.
constexpr Matrix kRgbToXyz{{{0.7977, 0.1352, 0.0313},
                            {0.2880, 0.7119, 0.0001},
                            {0.0, 0.0, 0.8249}}};
constexpr Matrix kXyzToRgb{{{1.3460, -0.2556, -0.0511},
                            {-0.5446, 1.5082, 0.0205},
                            {0.0, 0.0, 1.2123}}};
constexpr Matrix kCat16{{{0.401288, 0.650173, -0.051461},
                         {-0.250268, 1.204414, 0.045854},
                         {-0.002079, 0.048952, 0.953127}}};
constexpr Matrix kInverseCat16{{
    {1.8620678550872327, -1.0112546305316843, 0.14918677544445175},
    {0.3875265432361372, 0.6214474419314753, -0.00897398516761252},
    {-0.01584149884933386, -0.03412293802851557, 1.0499644368778496}}};

Vec3 multiply(const Matrix& matrix, const Vec3& value) {
    Vec3 out{};
    for (int i = 0; i < 3; ++i) {
        out[i] = matrix[i][0] * value[0] + matrix[i][1] * value[1] +
                 matrix[i][2] * value[2];
    }
    return out;
}

double signedPower(double value, double exponent) {
    return std::copysign(std::pow(std::abs(value), exponent), value);
}

double safeDivide(double numerator, double denominator) {
    // colour.algebra.sdiv's default "Ignore Zero Conversion" mode.
    return denominator == 0.0 ? 0.0 : numerator / denominator;
}

double knee(double value, double threshold, double limit, double power) {
    if (value <= threshold) return value;
    const double scale = limit - threshold;
    const double x = (value - threshold) / scale;
    return threshold + scale * x / std::pow(1.0 + std::pow(x, power), 1.0 / power);
}

struct Cam16 {
    double fl;
    double nbb;
    double z;
    double aw;
    double chromaFactor;
    double flQuarter;
    Vec3 adaptation;

    Cam16() {
        constexpr double adaptingLuminance = 64.0;
        constexpr double backgroundRatio = 20.0 / 100.0;
        const double k = 1.0 / (5.0 * adaptingLuminance + 1.0);
        const double k4 = std::pow(k, 4.0);
        fl = 0.2 * k4 * 5.0 * adaptingLuminance +
             0.1 * std::pow(1.0 - k4, 2.0) * std::cbrt(5.0 * adaptingLuminance);
        flQuarter = std::pow(fl, 0.25);
        nbb = 0.725 * std::pow(1.0 / backgroundRatio, 0.2);
        z = 1.48 + std::sqrt(backgroundRatio);
        chromaFactor = std::pow(1.64 - std::pow(0.29, backgroundRatio), 0.73);
        const double d = std::clamp(1.0 - std::exp((-adaptingLuminance - 42.0) / 92.0) / 3.6, 0.0, 1.0);
        // CAM16-UCS wrappers multiply normalized XYZ and its white by 100;
        // Y_b remains 20. Using a Y=1 white directly inside CAM16 is wrong.
        const Vec3 white{100.0 * 0.3457 / 0.3585, 100.0,
                         100.0 * (1.0 - 0.3457 - 0.3585) / 0.3585};
        const Vec3 rgbWhite = multiply(kCat16, white);
        Vec3 response{};
        for (int i = 0; i < 3; ++i) {
            adaptation[i] = d * 100.0 / rgbWhite[i] + 1.0 - d;
            response[i] = compressResponse(adaptation[i] * rgbWhite[i]);
        }
        aw = achromatic(response);
    }

    double compressResponse(double value) const {
        const double f = std::pow(fl * std::abs(value) / 100.0, 0.42);
        return 400.0 * std::copysign(f, value) / (27.13 + f) + 0.1;
    }

    double expandResponse(double value) const {
        const double shifted = value - 0.1;
        const double magnitude = std::abs(shifted);
        return std::copysign(1.0, shifted) * 100.0 / fl *
               signedPower(27.13 * magnitude / (400.0 - magnitude), 1.0 / 0.42);
    }

    double achromatic(const Vec3& response) const {
        return (2.0 * response[0] + response[1] + response[2] / 20.0 - 0.305) * nbb;
    }

    Vec3 forward(const Vec3& xyz) const {
        Vec3 response = multiply(kCat16, xyz);
        for (int i = 0; i < 3; ++i) response[i] = compressResponse(response[i] * 100.0 * adaptation[i]);
        const double a = response[0] - 12.0 * response[1] / 11.0 + response[2] / 11.0;
        const double b = (response[0] + response[1] - 2.0 * response[2]) / 9.0;
        const double h = std::atan2(b, a);
        const double et = (std::cos(h + 2.0) + 3.8) / 4.0;
        const double j = 100.0 * signedPower(achromatic(response) / aw, 0.69 * z);
        const double t = (50000.0 / 13.0) * nbb * safeDivide(
            et * std::sqrt(a * a + b * b), response[0] + response[1] + 21.0 * response[2] / 20.0);
        const double c = signedPower(t, 0.9) * signedPower(j / 100.0, 0.5) * chromaFactor;
        const double m = c * flQuarter;
        const double mp = std::log1p(0.0228 * m) / 0.0228;
        return {1.7 * j / (1.0 + 0.007 * j), mp * std::cos(h), mp * std::sin(h)};
    }

    Vec3 inverse(const Vec3& jab) const {
        const double j = jab[0] / (1.7 - 0.007 * jab[0]);
        const double mp = std::hypot(jab[1], jab[2]);
        const double m = std::expm1(0.0228 * mp) / 0.0228;
        const double c = m / flQuarter;
        const double h = std::atan2(jab[2], jab[1]);
        const double sh = std::sin(h);
        const double ch = std::cos(h);
        const double t = signedPower(c / (std::sqrt(std::max(j, std::numeric_limits<double>::epsilon()) / 100.0) * chromaFactor), 1.0 / 0.9);
        const double et = (std::cos(h + 2.0) + 3.8) / 4.0;
        const double ach = aw * signedPower(j / 100.0, 1.0 / (0.69 * z));
        const double p1 = safeDivide((50000.0 / 13.0) * nbb * et, t);
        const double p2 = ach / nbb + 0.305;
        constexpr double p3 = 21.0 / 20.0;
        const double n = p2 * (2.0 + p3) * (460.0 / 1403.0);
        double a = 0.0;
        double b = 0.0;
        if (t != 0.0) {
            if (std::abs(sh) >= std::abs(ch)) {
                b = n / (p1 / sh + (2.0 + p3) * (220.0 / 1403.0) * ch / sh -
                         27.0 / 1403.0 + p3 * (6300.0 / 1403.0));
                a = b * ch / sh;
            } else if (std::abs(sh) < std::abs(ch)) {
                a = n / (p1 / ch + (2.0 + p3) * (220.0 / 1403.0) -
                         (27.0 / 1403.0 - p3 * (6300.0 / 1403.0)) * sh / ch);
                b = a * sh / ch;
            }
        }
        Vec3 response{(460.0 * p2 + 451.0 * a + 288.0 * b) / 1403.0,
                      (460.0 * p2 - 891.0 * a - 261.0 * b) / 1403.0,
                      (460.0 * p2 - 220.0 * a - 6300.0 * b) / 1403.0};
        for (int i = 0; i < 3; ++i) response[i] = expandResponse(response[i]) / adaptation[i];
        Vec3 xyz = multiply(kInverseCat16, response);
        for (double& component : xyz) component /= 100.0;
        return xyz;
    }
};

struct GamutBoundary {
    Cam16 cam;
    std::vector<double> cmax;
    double whiteJp;
    // Match numpy.linspace's step followed by h_grid[1]-h_grid[0].
    double hueStep = (-kPi + 2.0 * kPi / kHueCount) - (-kPi);

    GamutBoundary() : cmax(kLightnessCount * kHueCount) {
        const Vec3 white{0.3457 / 0.3585, 1.0, (1.0 - 0.3457 - 0.3585) / 0.3585};
        whiteJp = cam.forward(white)[0];
        for (int l = 0; l < kLightnessCount; ++l) {
            const double lightness = 1.0 + l * (109.0 / (kLightnessCount - 1));
            for (int h = 0; h < kHueCount; ++h) {
                const double hue = -kPi + h * (2.0 * kPi / kHueCount);
                const double ch = std::cos(hue);
                const double sh = std::sin(hue);
                double lo = 0.0;
                double hi = 150.0;
                for (int iteration = 0; iteration < kBisections; ++iteration) {
                    const double mid = (lo + hi) * 0.5;
                    const Vec3 rgb = multiply(kXyzToRgb, cam.inverse({lightness, mid * ch, mid * sh}));
                    const bool inside = std::all_of(rgb.begin(), rgb.end(), [](double v) {
                        return v >= -1e-6 && v <= 1.0 + 1e-6;
                    });
                    if (inside) lo = mid; else hi = mid;
                }
                cmax[l * kHueCount + h] = lo;
            }
        }
    }

    double lookup(double lightness, double hue) const {
        const double li = (std::clamp(lightness, 1.0, 110.0) - 1.0) / 109.0 * (kLightnessCount - 1);
        const int l0 = std::clamp(static_cast<int>(std::floor(li)), 0, kLightnessCount - 2);
        const double lf = li - l0;
        const double hi = (hue + kPi) / hueStep;
        const int floorHue = static_cast<int>(std::floor(hi));
        const int h0 = (floorHue % kHueCount + kHueCount) % kHueCount;
        const int h1 = (h0 + 1) % kHueCount;
        const double hf = hi - std::floor(hi);
        return cmax[l0 * kHueCount + h0] * (1.0 - lf) * (1.0 - hf) +
               cmax[l0 * kHueCount + h1] * (1.0 - lf) * hf +
               cmax[(l0 + 1) * kHueCount + h0] * lf * (1.0 - hf) +
               cmax[(l0 + 1) * kHueCount + h1] * lf * hf;
    }
};

const GamutBoundary& boundary() {
    // C++11 initialization is thread-safe. All subsequent operations are read-only.
    static const GamutBoundary instance;
    return instance;
}

} // namespace

OutputGamut::OutputGamut() { (void) boundary(); }

Vec3 OutputGamut::compress(const Vec3& linearProPhoto) const {
    const GamutBoundary& state = boundary();
    Vec3 jab = state.cam.forward(multiply(kRgbToXyz, linearProPhoto));
    jab[0] = knee(jab[0] / state.whiteJp, 0.7, 1.0, 2.2) * state.whiteJp;
    const double chroma = std::hypot(jab[1], jab[2]);
    const double hue = std::atan2(jab[2], jab[1]);
    // Extreme ProPhoto primaries can have undefined CAM16 chroma. Upstream
    // propagates it through Cmax and the knee; inverse CAM16's two masked
    // hue branches then leave opponent a/b at zero. Preserve that result
    // without NumPy's intermediate NaN-to-integer table-coordinate cast.
    if (!std::isfinite(hue) || !std::isfinite(jab[0])) {
        const double invalid = std::numeric_limits<double>::quiet_NaN();
        return multiply(kXyzToRgb, state.cam.inverse({jab[0], invalid, invalid}));
    }
    const double maxChroma = std::fmax(state.lookup(jab[0], hue), 1e-9);
    const double newChroma = knee(chroma / maxChroma, 0.0, 1.0, 6.0) * maxChroma;
    jab[1] = newChroma * std::cos(hue);
    jab[2] = newChroma * std::sin(hue);
    return multiply(kXyzToRgb, state.cam.inverse(jab));
}

} // namespace spektrafilm
