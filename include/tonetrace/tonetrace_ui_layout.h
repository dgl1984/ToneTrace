#pragma once

#include <algorithm>
#include <vector>

namespace tonetrace {

// Split itemCount into the fewest pages allowed by maxItemsPerPage, then
// distribute the items as evenly as possible. Earlier pages receive at most
// one extra item. Example: 30 items with a 14-item maximum -> 10, 10, 10.
inline std::vector<int> balancedPageSizes(int itemCount, int maxItemsPerPage) {
  if (itemCount <= 0 || maxItemsPerPage <= 0) return {};
  const int pageCount =
      std::max(1, (itemCount + maxItemsPerPage - 1) / maxItemsPerPage);
  const int base = itemCount / pageCount;
  const int remainder = itemCount % pageCount;

  std::vector<int> sizes;
  sizes.reserve(static_cast<std::size_t>(pageCount));
  for (int page = 0; page < pageCount; ++page) {
    sizes.push_back(base + (page < remainder ? 1 : 0));
  }
  return sizes;
}

// Tone Trace prefers no more than ten editable bands on a page even when the
// window could physically fit more. Ten keeps the tab ranges predictable and
// the value boxes comfortably readable. If the total is not a multiple of ten,
// the final pages are rebalanced so there is never a tiny one- or two-band tab.
inline std::vector<int> toneTraceBandPageSizes(int itemCount, int widthCapacity) {
  if (itemCount <= 0 || widthCapacity <= 0) return {};
  return balancedPageSizes(itemCount, std::min(10, widthCapacity));
}

}  // namespace tonetrace
