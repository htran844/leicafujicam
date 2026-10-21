#include "spectral_data.h"
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace spektrafilm {
DataSet readDataSet(const uint8_t* bytes, size_t size) {
    if (size < 12 || size > 64 * 1024 * 1024 || std::memcmp(bytes, "SPKDATA4", 8) != 0)
        throw std::invalid_argument("Invalid Spektrafilm data header");
    size_t offset = 8;
    auto readU32 = [&]() -> uint32_t {
        if (size - offset < 4) throw std::invalid_argument("Truncated spectral data");
        uint32_t n = 0;
        for (int b = 0; b < 4; ++b) n |= uint32_t(bytes[offset++]) << (b * 8);
        return n;
    };
    const uint32_t fields = readU32();
    if (fields == 0 || fields > 1024) throw std::invalid_argument("Invalid spectral tensor count");
    DataSet result;
    for (uint32_t i = 0; i < fields; ++i) {
        const uint32_t keySize = readU32();
        if (keySize == 0 || keySize > 256 || keySize > size - offset)
            throw std::invalid_argument("Invalid spectral tensor key");
        std::string key(reinterpret_cast<const char*>(bytes + offset), keySize);
        offset += keySize;
        const uint32_t encoding = readU32();
        const uint32_t count = readU32();
        if (count > 4 * 1024 * 1024)
            throw std::invalid_argument("Invalid spectral tensor length");
        std::vector<double> values;
        if (encoding == 0) {
            if (count > (size - offset) / 8) throw std::invalid_argument("Truncated spectral tensor");
            values.resize(count);
            for (double& v : values) {
                uint64_t bits = 0;
                for (int b = 0; b < 8; ++b) bits |= uint64_t(bytes[offset++]) << (b * 8);
                std::memcpy(&v, &bits, 8);
                if (!std::isfinite(v)) throw std::invalid_argument("Non-finite spectral tensor value");
            }
        } else if (encoding == 1) {
            const uint32_t rows = readU32(), columns = readU32(), channels = readU32();
            if (rows == 0 || columns == 0 || channels == 0 || rows > 4096 ||
                columns > 4096 || channels > 4096 || uint64_t(rows) * columns * channels != count ||
                count > (size - offset) / 2)
                throw std::invalid_argument("Invalid packed spectral grid dimensions");
            values.resize(count);
            // Wavelength-major rows, then separate low/high byte planes. Restore
            // half words modulo 65536 before any floating-point conversion.
            size_t packed = 0;
            for (uint32_t c = 0; c < channels; ++c) for (uint32_t r = 0; r < rows; ++r) {
                uint16_t previous = 0;
                for (uint32_t x = 0; x < columns; ++x, ++packed) {
                    const uint16_t difference = uint16_t(bytes[offset + packed]) |
                        (uint16_t(bytes[offset + count + packed]) << 8);
                    const uint16_t bits = uint16_t(previous + difference);
                    previous = bits;
                    const int exponent = (bits >> 10) & 31;
                    const int mantissa = bits & 1023;
                    if (exponent == 31) throw std::invalid_argument("Non-finite packed spectral value");
                    double value = exponent == 0 ? std::ldexp(double(mantissa), -24) :
                        std::ldexp(double(1024 + mantissa), exponent - 25);
                    if (bits & 0x8000) value = -value;
                    values[(size_t(r) * columns + x) * channels + c] = value;
                }
            }
            offset += size_t(count) * 2;
        } else {
            throw std::invalid_argument("Unsupported spectral tensor encoding");
        }
        if (!result.emplace(std::move(key), std::move(values)).second)
            throw std::invalid_argument("Duplicate spectral tensor key");
    }
    if (offset != size) throw std::invalid_argument("Excess spectral data payload");
    return result;
}
}
