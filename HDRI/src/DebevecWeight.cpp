#include "DebevecWeight.hpp"

#include <iostream>

namespace HDRI {

const int DebevecWeight::kZmin = 0;
const int DebevecWeight::kZmax = 255;
const int DebevecWeight::N = 256;

DebevecWeight::DebevecWeight() {

  mTable.resize(N);
  for (auto i = 0; i < N; ++i) {

    if (i <= (0.5 * (kZmax + kZmin))) {
      mTable[i] = static_cast<std::uint8_t>(i - kZmin);
    } else {
      mTable[i] = static_cast<std::uint8_t>(kZmax - i);
    }
  }

  std::cerr << N << std::endl;
}

auto DebevecWeight::getSize() const -> size_t { return N; }

auto DebevecWeight::getWeight(int index) const -> double {

  // cap ?

  if (index < kZmin || index > kZmax) {
    return 0;
  }
  return mTable[index];
}

} // namespace HDRI
