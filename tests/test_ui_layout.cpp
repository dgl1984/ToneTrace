#include "tonetrace/tonetrace_ui_layout.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void expectGeneric(int bands, int maxPerPage, std::vector<int> expected) {
  const auto actual = tonetrace::balancedPageSizes(bands, maxPerPage);
  require(actual == expected, "balanced page layout mismatch");
}

void expectToneTrace(int bands, int widthCapacity, std::vector<int> expected) {
  const auto actual = tonetrace::toneTraceBandPageSizes(bands, widthCapacity);
  require(actual == expected, "Tone Trace page policy mismatch");
  int total = 0;
  for (int size : actual) {
    require(size > 0 && size <= 10, "Tone Trace page exceeded ten bands");
    require(size <= widthCapacity, "Tone Trace page exceeded window capacity");
    total += size;
  }
  require(total == bands, "Tone Trace page layout lost bands");
}
}  // namespace

int main() {
  try {
    // Generic balancing helper remains useful outside the preferred UI policy.
    expectGeneric(30, 14, {10, 10, 10});
    expectGeneric(15, 14, {8, 7});

    // Public Tone Trace rule: prefer ten bands, then rebalance small remainders.
    expectToneTrace(30, 14, {10, 10, 10});
    expectToneTrace(60, 14, {10, 10, 10, 10, 10, 10});
    expectToneTrace(31, 14, {8, 8, 8, 7});
    expectToneTrace(40, 14, {10, 10, 10, 10});
    expectToneTrace(14, 14, {7, 7});
    expectToneTrace(15, 14, {8, 7});
    expectToneTrace(30, 6, {6, 6, 6, 6, 6});
    expectToneTrace(1, 14, {1});
    expectToneTrace(9, 14, {9});
    expectToneTrace(10, 14, {10});
    expectToneTrace(11, 14, {6, 5});
    expectToneTrace(80, 14, {10, 10, 10, 10, 10, 10, 10, 10});
    expectToneTrace(81, 14, {9, 9, 9, 9, 9, 9, 9, 9, 9});
    expectToneTrace(99, 14, {10, 10, 10, 10, 10, 10, 10, 10, 10, 9});
    expectToneTrace(100, 14, {10, 10, 10, 10, 10, 10, 10, 10, 10, 10});
    expectToneTrace(116, 14,
                    {10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9, 9});
    expectToneTrace(119, 14,
                    {10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9});
    expectToneTrace(120, 14,
                    {10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10});
    require(tonetrace::toneTraceBandPageSizes(0, 14).empty(),
            "zero-band layout should be empty");
    std::cout << "Tone Trace balanced band paging tests passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Tone Trace band paging tests failed: " << e.what() << '\n';
    return 1;
  }
}
