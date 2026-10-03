#include "ui/ToneTraceBandPageLayout.h"

#include <algorithm>
#include <cmath>

namespace tonetrace::ui
{
std::vector<int> toneTraceBandPageSizes(int bandCount, int widthCapacity)
{
    if (bandCount <= 0) return {};

    constexpr int preferredMaximum = 10;
    const int capacity = std::clamp(widthCapacity, 1, preferredMaximum);
    const int pageCount = (bandCount + capacity - 1) / capacity;
    const int base = bandCount / pageCount;
    const int extra = bandCount % pageCount;

    std::vector<int> pages;
    pages.reserve(static_cast<std::size_t>(pageCount));
    for (int page = 0; page < pageCount; ++page)
        pages.push_back(base + (page < extra ? 1 : 0));
    return pages;
}

std::pair<double, double> toneTraceBandPageSweepEdges(int totalBandCount,
                                                       int firstBand,
                                                       int pageBandCount,
                                                       double rangeLowHz,
                                                       double rangeHighHz)
{
    if (totalBandCount <= 0 || firstBand < 0 || pageBandCount <= 0
        || firstBand >= totalBandCount || firstBand + pageBandCount > totalBandCount
        || !std::isfinite(rangeLowHz) || !std::isfinite(rangeHighHz)
        || rangeLowHz <= 0.0 || rangeHighHz <= rangeLowHz)
        return { 0.0, 0.0 };

    const auto frequencyAt = [=](int band)
    {
        if (totalBandCount <= 1) return std::sqrt(rangeLowHz * rangeHighHz);
        const double fraction = static_cast<double>(band)
            / static_cast<double>(totalBandCount - 1);
        return rangeLowHz * std::pow(rangeHighHz / rangeLowHz, fraction);
    };

    const int lastBand = firstBand + pageBandCount - 1;
    const double firstFrequency = frequencyAt(firstBand);
    const double lastFrequency = frequencyAt(lastBand);
    const double lowEdge = firstBand == 0
        ? rangeLowHz
        : std::sqrt(frequencyAt(firstBand - 1) * firstFrequency);
    const double highEdge = lastBand == totalBandCount - 1
        ? rangeHighHz
        : std::sqrt(lastFrequency * frequencyAt(lastBand + 1));
    return { lowEdge, highEdge };
}
}
