// Port of Sources/FotufilmCore/FilmGrainAsset.swift.
#include "FilmGrainAsset.h"

#include <cmath>
#include <cstring>
#include <mutex>

namespace fotufilm {
namespace FilmGrainAsset {

namespace {

constexpr uint32_t formatVersion = 2;
constexpr uint32_t algorithmVersion = 3;
const std::string& magic() {
    static const std::string value("FFGRAIN\0", 8);
    return value;
}
constexpr size_t maximumIdentityBytes = 1024 * 1024;

struct Writer {
    std::string data;
    void word(uint32_t value) {
        for (int i = 0; i < 4; ++i) data.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
    void floatValue(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        word(bits);
    }
    void bytes(const std::string& value) {
        word(static_cast<uint32_t>(value.size()));
        data += value;
    }
    void floats(const std::vector<float>& values) {
        word(static_cast<uint32_t>(values.size()));
        for (float v : values) floatValue(v);
    }
    void curve(const CharacteristicCurve& value) {
        floats({value.dMin, value.gamma, value.toe, value.toeWidth, value.shoulder, value.shoulderWidth});
        word(value.secondary ? 1 : 0);
        if (value.secondary) {
            const auto& s = *value.secondary;
            floats({s.gamma, s.toe, s.toeWidth, s.shoulder, s.shoulderWidth});
        }
        word(value.sampled ? 1 : 0);
        if (value.sampled) {
            floats(value.sampled->logExposure);
            floats(value.sampled->density);
            floats(value.sampled->slopes);
        }
    }
    void stock(const FilmStock& value) {
        bytes(value.name);
        word(value.isMonochrome ? 1 : 0);
        word(value.isReversal ? 1 : 0);
        word(static_cast<uint32_t>(rawValue(value.grainDensityLaw)));
        bytes(rawValue(value.granularityReadDensity));
        word(static_cast<uint32_t>(value.curves.size()));
        for (const CharacteristicCurve& item : value.curves) curve(item);
        floats({value.grainStrength, value.grainSizeMM, value.grainFogDensity, value.grainLumaCorrelation,
                value.grainMottleShare, value.grainMottleSizeRatio});
        floats(value.grainLayerWeights);
        floats(value.grainLayerSizeRatio);
        word(static_cast<uint32_t>(value.grainDensityProfile.records().size()));
        for (const auto& record : value.grainDensityProfile.records()) floats(record);
        floats(value.grainReversalProfile);
        const CrystalGrainPopulation& population = value.crystalGrainPopulation;
        floats({population.radiusSpan, population.coatingDensityScale});
        floats(population.sublayerShares);
    }
};

struct Reader {
    const std::string& data;
    size_t offset;
    uint32_t word() {
        if (data.size() < 4 || offset > data.size() - 4) throw AssetError();
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(static_cast<uint8_t>(data[offset + i])) << (8 * i);
        offset += 4;
        return value;
    }
    size_t count(size_t maximum) {
        size_t value = word();
        if (value > maximum) throw AssetError();
        return value;
    }
    std::string bytes(size_t maximum) {
        size_t length = count(maximum);
        if (length > data.size() - offset) throw AssetError();
        std::string value = data.substr(offset, length);
        offset += length;
        return value;
    }
    float floatValue() {
        uint32_t bits = word();
        float value;
        std::memcpy(&value, &bits, sizeof value);
        if (!std::isfinite(value)) throw AssetError();
        return value;
    }
    std::vector<float> floats(size_t expected) {
        if (count(expected) != expected || expected > (data.size() - offset) / 4) throw AssetError();
        std::vector<float> values;
        values.reserve(expected);
        for (size_t i = 0; i < expected; ++i) values.push_back(floatValue());
        return values;
    }
};

uint32_t bitPattern(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

struct ProviderBox {
    std::mutex lock;
    Provider value;
};
ProviderBox& providerBox() {
    static ProviderBox& box = *new ProviderBox;
    return box;
}

Provider currentProvider() {
    ProviderBox& box = providerBox();
    std::lock_guard<std::mutex> guard(box.lock);
    return box.value;
}

}  // namespace

std::string identity(const FilmStock& stock, const FilmStock* reference) {
    Writer writer;
    writer.bytes("FilmGrainIdentity");
    writer.word(algorithmVersion);
    writer.word(static_cast<uint32_t>(FilmGrain::tileSide));
    writer.word(static_cast<uint32_t>(FilmGrain::tileLevels));
    writer.word(static_cast<uint32_t>(FilmGrain::tileSeed));
    writer.word(static_cast<uint32_t>(FilmGrain::tileSeed >> 32));
    writer.floats({FilmGrain::tileTexelMM, FilmGrain::tileBlockMM, FilmGrain::tileSeamMM, FilmGrain::dyeCloudDecayMM,
                   FilmGrain::silverGrainEdge, FilmGrain::silverGrainDensity, FilmGrain::fastestCrystalMM,
                   FilmGrain::finestSampleMM});
    std::vector<float> terms;
    for (const auto& term : FilmGrain::dyeCloudTerms()) {
        terms.push_back(term.sigma);
        terms.push_back(term.weight);
    }
    writer.floats(terms);
    writer.stock(stock);
    writer.word(reference == nullptr ? 0 : 1);
    if (reference) writer.stock(*reference);
    return writer.data;
}

std::string generate(const FilmStock& stock, const FilmStock* reference) {
    std::string key = identity(stock, reference);
    FilmGrain grain(stock, reference, 1, false, {});
    std::shared_ptr<const FilmGrain::Tiles> tiles = grain.buildTiles(FilmGrain::tileSeed);
    return encode(grain, key, tiles.get());
}

std::string encode(const FilmGrain& grain, const std::string& identityBytes, const FilmGrain::Tiles* supplied) {
    Writer writer;
    writer.data += magic();
    writer.word(formatVersion);
    writer.bytes(identityBytes);
    writer.word(grain.monochrome ? 1 : 0);
    writer.word(static_cast<uint32_t>(grain.records.size()));
    for (const FilmGrain::Record& record : grain.records) {
        writer.floatValue(record.dMin);
        writer.floatValue(record.dMax);
        writer.word(static_cast<uint32_t>(record.sublayers.size()));
        for (const FilmGrain::Sublayer& layer : record.sublayers) {
            writer.word(static_cast<uint32_t>(layer.profile));
            writer.floats({layer.coatedPerMM2, layer.sigmaMM, layer.peakDemand, layer.capacity, layer.edge,
                           layer.smallestSigmaMM, layer.dyePerCloudMM2, layer.cellMM, layer.voidIntegralMM2});
            writer.floats(layer.forming);
        }
    }
    std::shared_ptr<const FilmGrain::Tiles> built;
    const FilmGrain::Tiles* tiles = supplied;
    if (!tiles) {
        built = grain.tiles();
        tiles = built.get();
    }
    writer.floats(tiles->dMin);
    writer.floats(tiles->dMax);
    writer.word(static_cast<uint32_t>(tiles->levels.size()));
    for (const auto& record : tiles->levels) {
        writer.word(static_cast<uint32_t>(record.size()));
        for (const FilmGrain::TileLevel& level : record) {
            writer.floatValue(level.meanLight);
            writer.floats(level.sums);
        }
    }
    if (writer.data.size() > maximumByteCount) throw AssetError();
    (void)decode(writer.data, identityBytes);
    return writer.data;
}

std::shared_ptr<FilmGrain> decode(const std::string& data, const std::string& identityBytes) {
    if (data.size() > maximumByteCount || data.compare(0, magic().size(), magic()) != 0 ||
        identityBytes.size() > maximumIdentityBytes) {
        throw AssetError();
    }
    Reader reader{data, magic().size()};
    if (reader.word() != formatVersion) throw AssetError();
    if (reader.bytes(maximumIdentityBytes) != identityBytes) throw AssetError();
    uint32_t mono = reader.word();
    if (!(mono <= 1) || reader.word() != 3) throw AssetError();
    std::vector<FilmGrain::Record> records;
    for (int r = 0; r < 3; ++r) {
        float dMin = reader.floatValue(), dMax = reader.floatValue();
        if (!(dMax > dMin)) throw AssetError();
        size_t count = reader.count(CrystalGrainModel::binCount);
        std::vector<FilmGrain::Sublayer> sublayers;
        for (size_t i = 0; i < count; ++i) {
            uint32_t profile = reader.word();
            if (profile > 1) throw AssetError();
            std::vector<float> fields = reader.floats(9);
            for (float f : fields) {
                if (!(f > 0)) throw AssetError();
            }
            std::vector<float> forming = reader.floats(FilmGrain::tableSamples);
            sublayers.push_back(FilmGrain::Sublayer::restored(static_cast<FilmGrain::Profile>(profile), fields[0],
                                                               fields[1], fields[2], fields[3], fields[4], fields[5],
                                                               fields[6], fields[7], std::move(forming), fields[8]));
        }
        records.push_back(FilmGrain::Record{std::move(sublayers), dMin, dMax});
    }
    std::vector<float> dMin = reader.floats(3), dMax = reader.floats(3);
    for (int r = 0; r < 3; ++r) {
        if (bitPattern(dMin[r]) != bitPattern(records[r].dMin) || bitPattern(dMax[r]) != bitPattern(records[r].dMax)) {
            throw AssetError();
        }
    }
    if (reader.word() != 3) throw AssetError();
    std::vector<std::vector<FilmGrain::TileLevel>> levels;
    const size_t side = FilmGrain::tileSide + 1;
    for (int r = 0; r < 3; ++r) {
        bool active = (mono == 0 || r == 1) && !records[r].sublayers.empty();
        size_t count = reader.count(FilmGrain::tileLevels);
        if (count != (active ? static_cast<size_t>(FilmGrain::tileLevels) : 0)) throw AssetError();
        std::vector<FilmGrain::TileLevel> record;
        for (size_t i = 0; i < count; ++i) {
            float mean = reader.floatValue();
            if (!(mean > 0 && mean <= 1)) throw AssetError();
            std::vector<float> sums = reader.floats(side * side);
            for (size_t k = 0; k < side; ++k) {
                if (sums[k] != 0 || sums[k * side] != 0) throw AssetError();
            }
            record.push_back(FilmGrain::TileLevel{std::move(sums), mean});
        }
        levels.push_back(std::move(record));
    }
    if (reader.offset != data.size()) throw AssetError();
    auto tiles = std::make_shared<const FilmGrain::Tiles>(std::move(levels), dMin, dMax);
    return std::make_shared<FilmGrain>(std::move(records), mono == 1, identityBytes, tiles);
}

void install(Provider provider) {
    ProviderBox& box = providerBox();
    std::lock_guard<std::mutex> guard(box.lock);
    box.value = std::move(provider);
}

bool isProvided(const std::string& identityBytes) {
    Provider provider = currentProvider();
    return provider && provider(identityBytes).has_value();
}

std::shared_ptr<FilmGrain> provided(const std::string& identityBytes) {
    Provider provider = currentProvider();
    if (!provider) return nullptr;
    std::optional<std::string> data = provider(identityBytes);
    if (!data) return nullptr;
    try {
        return decode(*data, identityBytes);
    } catch (const std::exception&) {
        return nullptr;
    }
}

}  // namespace FilmGrainAsset
}  // namespace fotufilm
