// Port of Sources/FotufilmCore/FilmGrainAsset.swift. Swift `Data` is a byte string here.
#ifndef FOTUFILM_CORE_FILM_GRAIN_ASSET_H
#define FOTUFILM_CORE_FILM_GRAIN_ASSET_H

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "FilmGrain.h"

namespace fotufilm {

/// Portable, lossless storage for a solved Film population and its canonical texture bank.
namespace FilmGrainAsset {

struct AssetError : std::runtime_error {
    AssetError() : std::runtime_error("invalid film grain asset") {}
};

constexpr size_t maximumByteCount = 32 * 1024 * 1024;

/// Canonical little-endian input bits, suitable for hashing as an application asset key.
std::string identity(const FilmStock& stock, const FilmStock* reference = nullptr);
/// Generates canonical data independently of the installed provider.
std::string generate(const FilmStock& stock, const FilmStock* reference = nullptr);
std::string encode(const FilmGrain& grain, const std::string& identity, const FilmGrain::Tiles* supplied = nullptr);
/// Throws AssetError on anything but a canonical asset for `identity`.
std::shared_ptr<FilmGrain> decode(const std::string& data, const std::string& identity);

/// An optional application-owned source of lossless banks: identity in, asset bytes (or nullopt)
/// out. The function must not request another Film binding.
using Provider = std::function<std::optional<std::string>(const std::string&)>;
void install(Provider provider);
bool isProvided(const std::string& identity);
/// The provider's population for `identity`, or nullptr when it has none or it is invalid.
std::shared_ptr<FilmGrain> provided(const std::string& identity);

}  // namespace FilmGrainAsset
}  // namespace fotufilm

#endif  // FOTUFILM_CORE_FILM_GRAIN_ASSET_H
