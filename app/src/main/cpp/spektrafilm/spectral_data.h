#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace spektrafilm {
using Vec3 = std::array<double, 3>;
using DataSet = std::unordered_map<std::string, std::vector<double>>;
DataSet readDataSet(const uint8_t* bytes, size_t size);
}
