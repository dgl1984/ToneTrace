#include "ui/ToneTraceBandPageLayout.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace
{
void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void expect(int count, int widthCapacity, std::vector<int> expected)
{
    const auto actual = tonetrace::ui::toneTraceBandPageSizes(count, widthCapacity);
    require(actual == expected, "unexpected page layout for " + std::to_string(count) + " bands");
    require(std::accumulate(actual.begin(), actual.end(), 0) == count,
            "page layout lost or duplicated bands");
    for (const int size : actual)
        require(size >= 1 && size <= 10 && size <= std::max(1, widthCapacity),
                "page exceeded released/width capacity");
}
}

int main()
{
    expect(30, 30, {10, 10, 10});
    expect(31, 30, {8, 8, 8, 7});
    expect(20, 30, {10, 10});
    expect(11, 30, {6, 5});
    expect(9, 30, {9});
    expect(30, 7, {6, 6, 6, 6, 6});
    expect(31, 5, {5, 5, 5, 4, 4, 4, 4});
    require(tonetrace::ui::toneTraceBandPageSizes(0, 10).empty(),
            "zero bands should produce no pages");

    const auto first = tonetrace::ui::toneTraceBandPageSweepEdges(30, 0, 10, 20.0, 20000.0);
    const auto second = tonetrace::ui::toneTraceBandPageSweepEdges(30, 10, 10, 20.0, 20000.0);
    const auto third = tonetrace::ui::toneTraceBandPageSweepEdges(30, 20, 10, 20.0, 20000.0);
    require(std::abs(first.first - 20.0) < 1.0e-9,
            "first page sweep did not start at the model low edge");
    require(std::abs(third.second - 20000.0) < 1.0e-9,
            "last page sweep did not end at the model high edge");
    require(std::abs(first.second - second.first) < 1.0e-9,
            "first/second page sweeps do not meet at one edge");
    require(std::abs(second.second - third.first) < 1.0e-9,
            "second/third page sweeps do not meet at one edge");
    require(first.first < first.second && first.second < second.second
                && second.second < third.second,
            "page sweep edges are not strictly ordered");
    const auto invalid = tonetrace::ui::toneTraceBandPageSweepEdges(30, 29, 2, 20.0, 20000.0);
    require(invalid.first == 0.0 && invalid.second == 0.0,
            "invalid sweep page was not rejected");

    std::cout << "PASS: released Tone Trace band paging and page-sweep edge contract\n";
    return 0;
}
