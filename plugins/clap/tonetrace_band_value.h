#pragma once

#include <charconv>
#include <cmath>
#include <string>
#include <system_error>

namespace tonetrace {

// Band values have one public resolution everywhere they are shown, edited,
// or exposed to accessibility clients. Three decimal places matches the CLAP
// continuous-parameter text contract while avoiding binary floating-point
// noise. Trailing zeroes beyond the first decimal are omitted.
inline constexpr int kBandValueDecimalPlaces = 3;

inline std::string formatBandValueDb(double value, bool includeUnit = true) {
  if (!std::isfinite(value)) return "Unavailable";

  // Do not expose a negative zero after rounding to the public resolution.
  constexpr double halfQuantum = 0.5e-3;
  if (std::abs(value) < halfQuantum) value = 0.0;

  char digits[64]{};
  const auto result = std::to_chars(
      digits, digits + sizeof(digits), value, std::chars_format::fixed,
      kBandValueDecimalPlaces);
  if (result.ec != std::errc{}) return includeUnit ? "+0.0 dB" : "+0.0";

  std::string text(digits, result.ptr);
  const std::size_t decimal = text.find('.');
  if (decimal != std::string::npos) {
    while (text.size() > decimal + 2 && text.back() == '0') text.pop_back();
  }
  if (text.empty() || text.front() != '-') text.insert(text.begin(), '+');
  if (includeUnit) text += " dB";
  return text;
}

inline std::wstring formatBandValueDbWide(double value,
                                          bool includeUnit = true) {
  const std::string text = formatBandValueDb(value, includeUnit);
  return std::wstring(text.begin(), text.end());
}

}  // namespace tonetrace
