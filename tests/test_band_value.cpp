#include "tonetrace_band_value.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
  try {
    require(tonetrace::formatBandValueDb(0.0) == "+0.0 dB",
            "zero format changed");
    require(tonetrace::formatBandValueDb(-6.2) == "-6.2 dB",
            "tenths were not preserved");
    require(tonetrace::formatBandValueDb(-6.234) == "-6.234 dB",
            "thousandths were not preserved");
    require(tonetrace::formatBandValueDb(-0.006) == "-0.006 dB",
            "fine negative value was hidden");
    require(tonetrace::formatBandValueDb(1.25) == "+1.25 dB",
            "meaningless trailing zero was not removed");
    require(tonetrace::formatBandValueDbWide(-6.234, false) == L"-6.234",
            "exact-editor format disagrees with the accessible value");
    require(tonetrace::formatBandValueDb(
                std::numeric_limits<double>::quiet_NaN()) == "Unavailable",
            "unavailable analysis value was presented as a real number");
    std::cout << "Tone Trace band-value formatting tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Tone Trace band-value formatting tests failed: "
              << error.what() << '\n';
    return 1;
  }
}
