#include "tonetrace/tonetrace_describe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tonetrace {

namespace {

constexpr double kLogLowHz = 20.0;
constexpr double kLogHighHz = 20000.0;
constexpr double kSignificantDb = 0.75;
constexpr double kTiltSpanDb = 3.0;
constexpr double kTiltResidualDb = 1.25;
constexpr double kTiltJumpFraction = 0.60;
constexpr double kTiltReversalDb = 1.25;
constexpr std::size_t kTiltMinimumBands = 5;
constexpr std::size_t kTiltMaximumReversals = 1;
constexpr std::size_t kCorrectionSamples = 5;
constexpr double kThinRatio = 1.02;
constexpr double kPartialFraction = 0.90;
constexpr std::size_t kMaximumRuns = 3;
constexpr double kEpsilon = 1.0e-9;

struct BandValue {
  const CurveBand* band = nullptr;
  bool active = false;
  bool partial = false;
  double lowHz = 0.0;
  double highHz = 0.0;
  double levelDb = 0.0;
};

enum class DescriptionKind { Capture, Correction, Match };

struct Run {
  int sign = 0;
  std::vector<std::size_t> indices;
  double meanDb = 0.0;
  double peakDb = 0.0;
  double lowHz = 0.0;
  double highHz = 0.0;
};

bool hasCapture(const SpectrumCapture& capture) {
  return capture.points.size() >= 3;
}

std::string trimNumber(double value, int decimals = 1) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(decimals) << value;
  std::string text = stream.str();
  while (text.size() > 1 && text.back() == '0') text.pop_back();
  if (!text.empty() && text.back() == '.') text.pop_back();
  if (text == "-0") text = "0";
  return text;
}

std::string proseDb(double value) {
  const double rounded = std::round(std::abs(value) * 10.0) / 10.0;
  return trimNumber(rounded) + " dB";
}

std::string hzValue(double hz, bool includeUnit = true) {
  if (hz < 1000.0) {
    const auto rounded = static_cast<long long>(std::llround(hz));
    return std::to_string(rounded) + (includeUnit ? " Hz" : "");
  }
  const double khz = hz / 1000.0;
  const double roundedWhole = std::round(khz);
  const double shown = std::abs(khz - roundedWhole) < 0.05
                           ? roundedWhole
                           : std::round(khz * 10.0) / 10.0;
  return trimNumber(shown) + (includeUnit ? " kHz" : "");
}

std::string hzRange(double low, double high) {
  if (low < 1000.0 && high < 1000.0) {
    return hzValue(low, false) + " to " + hzValue(high, false) + " Hz";
  }
  if (low >= 1000.0 && high >= 1000.0) {
    return hzValue(low, false) + " to " + hzValue(high, false) + " kHz";
  }
  return hzValue(low, true) + " to " + hzValue(high, true);
}

std::optional<double> captureBandMean(const SpectrumCapture& capture,
                                      const CurveBand& band) {
  double sum = 0.0;
  double weight = 0.0;
  for (const auto& point : capture.points) {
    if (point.frequencyHz < band.lowHz || point.frequencyHz >= band.highHz) {
      continue;
    }
    const double pointWeight =
        std::clamp(point.confidence, 0.0, 1.0) * 0.5 + 0.5;
    sum += point.levelDb * pointWeight;
    weight += pointWeight;
  }
  if (weight <= 0.0) return std::nullopt;
  return sum / weight;
}

std::vector<BandValue> captureValues(const SpectrumCapture& capture) {
  std::vector<BandValue> values;
  values.reserve(curveBands().size());
  for (const auto& band : curveBands()) {
    BandValue value;
    value.band = &band;
    value.lowHz = band.lowHz;
    value.highHz = band.highHz;
    const auto mean = captureBandMean(capture, band);
    if (mean.has_value()) {
      value.active = true;
      value.levelDb = *mean;
    }
    values.push_back(value);
  }
  return values;
}

std::vector<BandValue> correctionValues(const CorrectionModel& model,
                                        double ceilingDb,
                                        const IrRenderSettings& settings) {
  std::vector<BandValue> values;
  values.reserve(curveBands().size());
  const double nyquist = settings.sampleRate > 0
                             ? settings.sampleRate * 0.5
                             : std::numeric_limits<double>::infinity();
  const double activeLow = std::max({settings.rangeLowHz, model.analysisLowHz,
                                     kLogLowHz});
  const double activeHigh =
      std::min({settings.rangeHighHz, model.analysisHighHz, kLogHighHz,
                0.999 * nyquist});

  for (const auto& band : curveBands()) {
    BandValue value;
    value.band = &band;
    value.lowHz = std::max(band.lowHz, activeLow);
    value.highHz = std::min(band.highHz, activeHigh);
    if (!(value.highHz > value.lowHz)) {
      values.push_back(value);
      continue;
    }
    const double overlapWidth = std::log(value.highHz / value.lowHz);
    const double fullWidth = std::log(band.highHz / band.lowHz);
    value.partial = overlapWidth < kPartialFraction * fullWidth;

    double sum = 0.0;
    std::size_t count = 0;
    const double ratio = value.highHz / value.lowHz;
    const std::size_t samples = ratio < kThinRatio ? 1 : kCorrectionSamples;
    for (std::size_t i = 0; i < samples; ++i) {
      double frequency = std::sqrt(value.lowHz * value.highHz);
      if (samples > 1) {
        const double fraction = static_cast<double>(i) /
                                static_cast<double>(samples - 1);
        frequency = value.lowHz *
                    std::pow(value.highHz / value.lowHz, fraction);
      }
      const auto evaluated = evaluateCorrectionAt(model, ceilingDb, settings,
                                                  frequency);
      if (!evaluated.inRange) continue;
      sum += evaluated.tonalDb;
      ++count;
    }
    if (count > 0) {
      value.active = true;
      value.levelDb = sum / static_cast<double>(count);
    }
    values.push_back(value);
  }
  return values;
}

std::vector<BandValue> matchValues(const SpectrumCapture& reference,
                                   const SpectrumCapture& target) {
  const auto ref = captureValues(reference);
  const auto tgt = captureValues(target);
  std::vector<BandValue> values;
  values.reserve(curveBands().size());
  for (std::size_t i = 0; i < curveBands().size(); ++i) {
    BandValue value;
    value.band = &curveBands()[i];
    value.lowHz = value.band->lowHz;
    value.highHz = value.band->highHz;
    if (ref[i].active && tgt[i].active) {
      value.active = true;
      value.levelDb = tgt[i].levelDb - ref[i].levelDb;
    }
    values.push_back(value);
  }
  return values;
}

int signClass(double value) {
  if (value >= kSignificantDb) return 1;
  if (value <= -kSignificantDb) return -1;
  return 0;
}

bool describeTilt(const std::vector<BandValue>& values,
                  DescriptionKind kind,
                  std::string& output) {
  std::vector<std::size_t> active;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (values[i].active) active.push_back(i);
  }
  if (active.size() < kTiltMinimumBands) return false;

  const double first = values[active.front()].levelDb;
  const double last = values[active.back()].levelDb;
  const double delta = last - first;
  const double span = std::abs(delta);
  if (span < kTiltSpanDb) return false;
  const int direction = delta > 0.0 ? 1 : -1;

  std::size_t reversals = 0;
  for (std::size_t i = 1; i < active.size(); ++i) {
    const double step = values[active[i]].levelDb - values[active[i - 1]].levelDb;
    if (std::abs(step) >= kTiltJumpFraction * span) return false;
    if ((direction > 0 && step < 0.0) || (direction < 0 && step > 0.0)) {
      ++reversals;
      if (reversals > kTiltMaximumReversals ||
          std::abs(step) >= kTiltReversalDb) {
        return false;
      }
    }
  }

  double meanX = 0.0;
  double meanY = 0.0;
  for (const auto index : active) {
    meanX += std::log(std::sqrt(values[index].lowHz * values[index].highHz));
    meanY += values[index].levelDb;
  }
  meanX /= static_cast<double>(active.size());
  meanY /= static_cast<double>(active.size());
  double xx = 0.0;
  double xy = 0.0;
  for (const auto index : active) {
    const double x =
        std::log(std::sqrt(values[index].lowHz * values[index].highHz));
    xx += (x - meanX) * (x - meanX);
    xy += (x - meanX) * (values[index].levelDb - meanY);
  }
  if (xx <= kEpsilon) return false;
  const double slope = xy / xx;
  const double intercept = meanY - slope * meanX;
  double residual2 = 0.0;
  for (const auto index : active) {
    const double x =
        std::log(std::sqrt(values[index].lowHz * values[index].highHz));
    const double residual = values[index].levelDb - (intercept + slope * x);
    residual2 += residual * residual;
  }
  const double rms = std::sqrt(residual2 / static_cast<double>(active.size()));
  if (rms > kTiltResidualDb) return false;

  const std::string directionText = direction > 0 ? "up" : "down";
  if (kind == DescriptionKind::Capture) {
    output = "The curve tilts " + directionText + " by " + proseDb(span) +
             " from " + hzRange(values[active.front()].lowHz,
                                  values[active.back()].highHz) + ".";
  } else if (kind == DescriptionKind::Correction) {
    output = "The correction tilts " + directionText + " by " + proseDb(span) +
             " from " + hzRange(values[active.front()].lowHz,
                                  values[active.back()].highHz) + ".";
  } else {
    output = "Target tilts " + directionText + " by " + proseDb(span) +
             " relative to Reference from " +
             hzRange(values[active.front()].lowHz,
                     values[active.back()].highHz) + ".";
  }
  return true;
}

Run makeRun(int sign, const std::vector<std::size_t>& indices,
            const std::vector<BandValue>& values) {
  Run run;
  run.sign = sign;
  run.indices = indices;
  run.lowHz = values[indices.front()].lowHz;
  run.highHz = values[indices.back()].highHz;
  double sum = 0.0;
  double peak = 0.0;
  for (const auto index : indices) {
    const double value = values[index].levelDb;
    sum += value;
    if (std::abs(value) > std::abs(peak)) peak = value;
  }
  run.meanDb = sum / static_cast<double>(indices.size());
  run.peakDb = peak;
  return run;
}

std::vector<Run> signedRuns(const std::vector<BandValue>& values) {
  std::vector<Run> runs;
  std::vector<std::size_t> current;
  int currentSign = 0;
  for (std::size_t i = 0; i < values.size(); ++i) {
    const int sign = values[i].active ? signClass(values[i].levelDb) : 0;
    if (sign == 0) {
      if (!current.empty()) {
        runs.push_back(makeRun(currentSign, current, values));
        current.clear();
        currentSign = 0;
      }
      continue;
    }
    if (!current.empty() && sign != currentSign) {
      runs.push_back(makeRun(currentSign, current, values));
      current.clear();
    }
    currentSign = sign;
    current.push_back(i);
  }
  if (!current.empty()) runs.push_back(makeRun(currentSign, current, values));

  while (runs.size() > kMaximumRuns) {
    std::size_t drop = 0;
    for (std::size_t i = 1; i < runs.size(); ++i) {
      const double a = std::abs(runs[i].meanDb);
      const double b = std::abs(runs[drop].meanDb);
      if (a < b - kEpsilon ||
          (std::abs(a - b) <= kEpsilon &&
           (runs[i].indices.size() < runs[drop].indices.size() ||
            (runs[i].indices.size() == runs[drop].indices.size() && i > drop)))) {
        drop = i;
      }
    }
    runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(drop));
  }
  return runs;
}

std::pair<std::size_t, std::size_t> activeBounds(
    const std::vector<BandValue>& values) {
  std::size_t first = values.size();
  std::size_t last = 0;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (!values[i].active) continue;
    if (first == values.size()) first = i;
    last = i;
  }
  return {first, last};
}

std::string oneRunFragment(const Run& run,
                           const std::vector<BandValue>& values,
                           DescriptionKind kind,
                           bool sentence) {
  const auto [firstActive, lastActive] = activeBounds(values);
  const bool touchesLow = !run.indices.empty() && run.indices.front() == firstActive;
  const bool touchesHigh = !run.indices.empty() && run.indices.back() == lastActive;
  const bool boost = run.sign > 0;
  const double magnitude = (touchesLow || touchesHigh) ? std::abs(run.meanDb)
                                                       : std::abs(run.peakDb);
  std::string text;

  if (kind == DescriptionKind::Capture) {
    if (touchesLow && touchesHigh) {
      text = "The curve is " + proseDb(magnitude) +
             (boost ? " higher" : " lower") + " across the active range";
    } else if (touchesLow) {
      text = "The curve is " + proseDb(magnitude) +
             (boost ? " higher below " : " lower below ") +
             hzValue(run.highHz) + " than above it";
    } else if (touchesHigh) {
      text = "The curve is " + proseDb(magnitude) +
             (boost ? " higher above " : " lower above ") +
             hzValue(run.lowHz) + " than below it";
    } else {
      text = "The curve is " + proseDb(magnitude) +
             (boost ? " higher from " : " lower from ") +
             hzRange(run.lowHz, run.highHz);
    }
  } else if (kind == DescriptionKind::Correction) {
    if (touchesLow && touchesHigh) {
      text = "A broad " + proseDb(magnitude) + (boost ? " boost" : " cut") +
             " spans the active range";
    } else if (touchesLow) {
      text = "A low shelf " + std::string(boost ? "boosts below " : "cuts below ") +
             hzValue(run.highHz) + " by " + proseDb(magnitude);
    } else if (touchesHigh) {
      text = "A high shelf " + std::string(boost ? "boosts above " : "cuts above ") +
             hzValue(run.lowHz) + " by " + proseDb(magnitude);
    } else {
      text = std::string(boost ? "A boost of " : "A cut of ") + proseDb(magnitude) +
             " runs from " + hzRange(run.lowHz, run.highHz);
    }
  } else {
    if (touchesLow && touchesHigh) {
      text = "Target is " + proseDb(magnitude) +
             (boost ? " higher" : " lower") +
             " than Reference across the active range";
    } else if (touchesLow) {
      text = "Relative to Reference, Target is initially " + proseDb(magnitude) +
             (boost ? " higher below " : " lower below ") +
             hzValue(run.highHz);
    } else if (touchesHigh) {
      text = "Relative to Reference, Target is initially " + proseDb(magnitude) +
             (boost ? " higher above " : " lower above ") +
             hzValue(run.lowHz);
    } else {
      text = "Relative to Reference, Target is initially " + proseDb(magnitude) +
             (boost ? " higher from " : " lower from ") +
             hzRange(run.lowHz, run.highHz);
    }
  }
  if (sentence) text += ".";
  return text;
}

std::string describeRuns(const std::vector<BandValue>& values,
                         DescriptionKind kind) {
  auto runs = signedRuns(values);
  if (runs.empty()) {
    if (kind == DescriptionKind::Capture) {
      return "The curve is close to flat across the audible range.";
    }
    if (kind == DescriptionKind::Correction) {
      return "The correction is gentle across the whole range.";
    }
    return "Reference and Target have a similar tonal shape.";
  }
  if (runs.size() == 1) return oneRunFragment(runs[0], values, kind, true);

  if (runs.size() == 2 && runs[0].sign != runs[1].sign) {
    const double delta = std::abs(runs[0].meanDb - runs[1].meanDb);
    const double boundary = runs[1].lowHz;
    const bool firstHigher = runs[0].meanDb > runs[1].meanDb;
    if (kind == DescriptionKind::Capture) {
      return "A shelf transition makes the curve " + proseDb(delta) +
             (firstHigher ? " higher below " : " lower below ") +
             hzValue(boundary) + " than above it.";
    }
    if (kind == DescriptionKind::Correction) {
      return "A shelf transition makes the correction " + proseDb(delta) +
             (firstHigher ? " stronger below " : " weaker below ") +
             hzValue(boundary) + " than above it.";
    }
    return "Relative to Reference, Target is initially " + proseDb(delta) +
           (firstHigher ? " higher below " : " lower below ") +
           hzValue(boundary) + " than above it.";
  }

  if (runs.size() == 2 && runs[0].sign == runs[1].sign) {
    const bool boosts = runs[0].sign > 0;
    const std::string firstAmount = proseDb(std::abs(runs[0].peakDb));
    const std::string firstRange = hzRange(runs[0].lowHz, runs[0].highHz);
    const std::string secondAmount = proseDb(std::abs(runs[1].peakDb));
    const std::string secondRange = hzRange(runs[1].lowHz, runs[1].highHz);
    if (kind == DescriptionKind::Capture) {
      return "The curve is " + firstAmount +
             (boosts ? " higher from " : " lower from ") + firstRange +
             " and " + secondAmount +
             (boosts ? " higher from " : " lower from ") + secondRange + ".";
    }
    if (kind == DescriptionKind::Correction) {
      return std::string("The correction has two ") + (boosts ? "boosts: " : "cuts: ") +
             firstAmount + " from " + firstRange + " and " +
             secondAmount + " from " + secondRange + ".";
    }
    return "Relative to Reference, Target is initially " + firstAmount +
           (boosts ? " higher from " : " lower from ") + firstRange +
           " and " + secondAmount +
           (boosts ? " higher from " : " lower from ") + secondRange + ".";
  }

  if (runs.size() == 3 && runs[0].sign == runs[2].sign &&
      runs[0].sign != runs[1].sign) {
    const bool outerBoosts = runs[0].sign > 0;
    const std::string firstAmount = proseDb(std::abs(runs[0].peakDb));
    const std::string firstRange = hzRange(runs[0].lowHz, runs[0].highHz);
    const std::string middleAmount = proseDb(std::abs(runs[1].peakDb));
    const std::string middleRange = hzRange(runs[1].lowHz, runs[1].highHz);
    const std::string lastAmount = proseDb(std::abs(runs[2].peakDb));
    const std::string lastRange = hzRange(runs[2].lowHz, runs[2].highHz);
    if (kind == DescriptionKind::Capture) {
      return "The curve is " + firstAmount +
             (outerBoosts ? " higher from " : " lower from ") + firstRange +
             ", " + middleAmount +
             (outerBoosts ? " lower from " : " higher from ") + middleRange +
             ", and " + lastAmount +
             (outerBoosts ? " higher from " : " lower from ") + lastRange + ".";
    }
    if (kind == DescriptionKind::Correction) {
      return std::string("The correction has ") +
             (outerBoosts ? "boosts of " : "cuts of ") + firstAmount +
             " from " + firstRange + " and " + lastAmount + " from " + lastRange +
             ", with a " + (outerBoosts ? "cut of " : "boost of ") +
             middleAmount + " from " + middleRange + ".";
    }
    return "Relative to Reference, Target is initially " + firstAmount +
           (outerBoosts ? " higher from " : " lower from ") + firstRange +
           ", " + middleAmount +
           (outerBoosts ? " lower from " : " higher from ") + middleRange +
           ", and " + lastAmount +
           (outerBoosts ? " higher from " : " lower from ") + lastRange + ".";
  }

  std::ostringstream out;
  if (kind == DescriptionKind::Capture) {
    out << "The curve is ";
  } else if (kind == DescriptionKind::Correction) {
    out << "The correction has ";
  } else {
    out << "Relative to Reference, Target is initially ";
  }
  for (std::size_t i = 0; i < runs.size(); ++i) {
    if (i > 0) out << (i + 1 == runs.size() ? ", and " : ", ");
    if (kind == DescriptionKind::Correction) {
      out << (runs[i].sign > 0 ? "a boost of " : "a cut of ")
          << proseDb(std::abs(runs[i].peakDb)) << " from "
          << hzRange(runs[i].lowHz, runs[i].highHz);
    } else {
      out << proseDb(std::abs(runs[i].peakDb))
          << (runs[i].sign > 0 ? " higher from " : " lower from ")
          << hzRange(runs[i].lowHz, runs[i].highHz);
    }
  }
  out << '.';
  return out.str();
}

std::string describeValues(const std::vector<BandValue>& values,
                           DescriptionKind kind) {
  std::string tilt;
  if (describeTilt(values, kind, tilt)) return tilt;
  return describeRuns(values, kind);
}

std::string ceilingSentence(const CorrectionModel& model, double ceilingDb) {
  if (model.nodes.empty()) return {};
  double effective = ceilingDb;
  if (!std::isfinite(effective) || effective <= 0.0 || effective > 60.0) {
    effective = 18.0;
  }
  double peak = 0.0;
  bool hit = false;
  for (const auto& node : model.nodes) {
    peak = std::max(peak, std::abs(node.gainDb));
    if (std::abs(node.gainDb) > effective + kEpsilon) hit = true;
  }
  if (!hit) return {};
  return " Maximum Correction clipped the learned curve from " + proseDb(peak) +
         " to " + proseDb(effective) +
         " before Strength, Q, and manual trims.";
}

}  // namespace

const std::vector<CurveBand>& curveBands() {
  static const std::vector<CurveBand> bands{
      {"sub", 20.0, 60.0},
      {"bass", 60.0, 250.0},
      {"low mid", 250.0, 500.0},
      {"mid", 500.0, 2000.0},
      {"presence", 2000.0, 6000.0},
      {"air", 6000.0, 12000.0},
      {"brilliance", 12000.0, 20000.0},
  };
  return bands;
}

double bandLevelDb(const SpectrumCapture& capture, const CurveBand& band) {
  const auto mean = captureBandMean(capture, band);
  return mean.has_value() ? *mean : 0.0;
}

double correctionBandDb(const CorrectionModel& model, const CurveBand& band) {
  return model.gainDbAt(std::sqrt(band.lowHz * band.highHz));
}

std::string describeCapture(const SpectrumCapture& capture) {
  if (!hasCapture(capture)) return "No capture yet.";
  return describeValues(captureValues(capture), DescriptionKind::Capture);
}

std::string describeCorrection(const CorrectionModel& model,
                               double ceilingDb,
                               const IrRenderSettings& settings) {
  const bool hasManual = std::any_of(
      settings.manualGains.begin(), settings.manualGains.end(),
      [](double gain) { return std::isfinite(gain) && std::abs(gain) > 1.0e-12; });
  if (model.nodes.empty() && !hasManual) {
    return "No correction has been computed yet.";
  }
  std::string text =
      describeValues(correctionValues(model, ceilingDb, settings),
                     DescriptionKind::Correction);
  text += ceilingSentence(model, ceilingDb);
  return text;
}

CurveDescription describeToneTrace(const ProfileSnapshot& snapshot) {
  CurveDescription description;
  description.reference = describeCapture(snapshot.reference);
  description.target = describeCapture(snapshot.target);
  description.correction = describeCorrection(
      snapshot.uncappedModel, snapshot.matchSettings.maximumCorrectionDb,
      snapshot.renderSettings);

  if (!hasCapture(snapshot.reference) && !hasCapture(snapshot.target)) {
    description.summary =
        "No learned match yet. Capture Reference to begin. Manual band EQ remains "
        "available on the Bands pages.";
  } else if (!hasCapture(snapshot.target)) {
    description.summary = "Reference captured; target not yet learned.";
  } else {
    description.summary = describeValues(
        matchValues(snapshot.reference, snapshot.target), DescriptionKind::Match);
  }
  return description;
}

std::string curveDescriptionText(const ProfileSnapshot& snapshot) {
  const CurveDescription description = describeToneTrace(snapshot);
  std::ostringstream out;
  out << "Tone Trace summary\n" << description.summary << "\n\n";
  out << "Reference\n" << description.reference << "\n\n";
  out << "Target\n" << description.target << "\n\n";
  out << "Correction\n" << description.correction << '\n';
  return out.str();
}

}  // namespace tonetrace
