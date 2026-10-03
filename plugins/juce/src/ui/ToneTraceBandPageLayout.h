#pragma once

#include <utility>
#include <vector>

namespace tonetrace::ui
{
// Released 1.0.5 paging contract: never show more than ten editable bands on
// one page. Width may reduce that capacity further. Page sizes are balanced so
// a remainder never becomes a nearly empty final page (31 => 8/8/8/7).
[[nodiscard]] std::vector<int> toneTraceBandPageSizes(int bandCount,
                                                       int widthCapacity);

// Returns the released page-sweep frequency edges for a contiguous band page.
// Band centers are logarithmically spaced across [rangeLowHz, rangeHighHz].
// Interior edges are geometric midpoints between neighboring centers so
// adjacent pages meet at exactly the same frequency without gaps or overlap.
[[nodiscard]] std::pair<double, double> toneTraceBandPageSweepEdges(
    int totalBandCount,
    int firstBand,
    int pageBandCount,
    double rangeLowHz,
    double rangeHighHz);
}
