#include "tonetrace/tonetrace_describe.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <limits>
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
constexpr int kRegionSamples = 5;
constexpr double kThinRegionRatio = 1.02;
constexpr double kPartialWidthFraction = 0.90;
constexpr std::size_t kMaximumRuns = 3;
constexpr double kEpsilon = 1.0e-9;

enum class DescriptionKind { Capture, Correction, Match };
enum class SignedClass { Flat, Boost, Cut };

struct RegionLevel {
  const CurveBand* band = nullptr;
  std::size_t bandIndex = 0;
  bool active = false;
  bool partial = false;
  double lowHz = 0.0;
  double highHz = 0.0;
  double levelDb = 0.0;
};

struct SignedRun {
  SignedClass classification = SignedClass::Flat;
  std::vector<RegionLevel> regions;

  [[nodiscard]] double meanDb() const {
    if (regions.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& region : regions) sum += region.levelDb;
    return sum / static_cast<double>(regions.size());
  }

  [[nodiscard]] double peakDb() const {
    double result = 0.0;
    for (const auto& region : regions) {
      if (std::abs(region.levelDb) > std::abs(result) + kEpsilon) {
        result = region.levelDb;
      }
    }
    return result;
  }

  [[nodiscard]] double lowHz() const {
    return regions.empty() ? 0.0 : regions.front().lowHz;
  }

  [[nodiscard]] double highHz() const {
    return regions.empty() ? 0.0 : regions.back().highHz;
  }

  [[nodiscard]] std::size_t firstBandIndex() const {
    return regions.empty() ? 0U : regions.front().bandIndex;
  }

  [[nodiscard]] std::size_t lastBandIndex() const {
    return regions.empty() ? 0U : regions.back().bandIndex;
  }
};

[[nodiscard]] bool hasCapture(const SpectrumCapture& capture) {
  return capture.points.size() >= 3;
}

[[nodiscard]] double roundHalfAwayFromZeroOneDecimal(double value) {
  if (!std::isfinite(value)) return 0.0;
  const double magnitude = std::floor(std::abs(value) * 10.0 + 0.5) / 10.0;
  return std::signbit(value) ? -magnitude : magnitude;
}

[[nodiscard]] std::string numberOneDecimal(double value) {
  const double rounded = roundHalfAwayFromZeroOneDecimal(value);
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << rounded;
  std::string text = out.str();
  if (text.size() >= 2 && text.substr(text.size() - 2) == ".0") {
    text.resize(text.size() - 2);
  }
  if (text == "-0") text = "0";
  return text;
}

[[nodiscard]] std::string plainDb(double value) {
  return numberOneDecimal(std::abs(value)) + " dB";
}

[[nodiscard]] std::string formatHz(double frequencyHz) {
  if (frequencyHz < 1000.0) {
    return std::to_string(static_cast<long long>(std::llround(frequencyHz))) +
           " Hz";
  }
  const double khz = frequencyHz / 1000.0;
  const double nearestWhole = std::round(khz);
  if (std::abs(khz - nearestWhole) <= 0.0005) {
    return std::to_string(static_cast<long long>(nearestWhole)) + " kHz";
  }
  return numberOneDecimal(khz) + " kHz";
}

[[nodiscard]] std::string formatRange(double lowHz, double highHz) {
  if (lowHz < 1000.0 && highHz < 1000.0) {
    return std::to_string(static_cast<long long>(std::llround(lowHz))) +
           " to " +
           std::to_string(static_cast<long long>(std::llround(highHz))) +
           " Hz";
  }
  if (lowHz >= 1000.0 && highHz >= 1000.0) {
    const auto khzNumber = [](double value) {
      const double khz = value / 1000.0;
      const double nearestWhole = std::round(khz);
      if (std::abs(khz - nearestWhole) <= 0.0005) {
        return std::to_string(static_cast<long long>(nearestWhole));
      }
      return numberOneDecimal(khz);
    };
    return khzNumber(lowHz) + " to " + khzNumber(highHz) + " kHz";
  }
  return formatHz(lowHz) + " to " + formatHz(highHz);
}

[[nodiscard]] std::string regionLabel(const RegionLevel& region) {
  if (region.band == nullptr) return {};
  return formatRange(region.lowHz, region.highHz) + " band";
}

[[nodiscard]] std::string runSpan(const SignedRun& run) {
  if (run.regions.empty()) return {};
  return formatRange(run.lowHz(), run.highHz());
}

[[nodiscard]] bool narrowNotch(const SignedRun& run) {
  return run.regions.size() == 1 && run.lowHz() > 0.0 &&
         run.highHz() / run.lowHz() <= 2.5;
}

struct CaptureBandMean {
  bool active = false;
  double valueDb = 0.0;
};

[[nodiscard]] CaptureBandMean captureBandMean(const SpectrumCapture& capture,
                                               const CurveBand& band) {
  double weightedSum = 0.0;
  double totalWeight = 0.0;
  for (const auto& point : capture.points) {
    if (point.frequencyHz < band.lowHz || point.frequencyHz >= band.highHz) {
      continue;
    }
    const double weight =
        std::clamp(point.confidence, 0.0, 1.0) * 0.5 + 0.5;
    weightedSum += point.levelDb * weight;
    totalWeight += weight;
  }
  if (totalWeight <= 0.0) return {};
  return {true, weightedSum / totalWeight};
}

[[nodiscard]] std::vector<RegionLevel> captureLevels(
    const SpectrumCapture& capture) {
  std::vector<RegionLevel> levels;
  levels.reserve(curveBands().size());
  for (std::size_t index = 0; index < curveBands().size(); ++index) {
    const CurveBand& band = curveBands()[index];
    const CaptureBandMean mean = captureBandMean(capture, band);
    levels.push_back(
        {&band, index, mean.active, false, band.lowHz, band.highHz, mean.valueDb});
  }
  return levels;
}

[[nodiscard]] std::vector<RegionLevel> correctionLevels(
    const CorrectionModel& model, double ceilingDb,
    const IrRenderSettings& settings) {
  std::vector<RegionLevel> levels;
  levels.reserve(curveBands().size());

  const double nyquist = static_cast<double>(settings.sampleRate) * 0.5;
  const double activeLow =
      std::max({settings.rangeLowHz, model.analysisLowHz, kLogLowHz});
  const double activeHigh = std::min(
      {settings.rangeHighHz, model.analysisHighHz, kLogHighHz, 0.999 * nyquist});

  for (std::size_t index = 0; index < curveBands().size(); ++index) {
    const CurveBand& band = curveBands()[index];
    const double overlapLow = std::max(band.lowHz, activeLow);
    const double overlapHigh = std::min(band.highHz, activeHigh);
    RegionLevel level{&band, index, false, false, overlapLow, overlapHigh, 0.0};
    if (!(overlapHigh > overlapLow)) {
      levels.push_back(level);
      continue;
    }

    const double fullWidth = std::log(band.highHz / band.lowHz);
    const double overlapWidth = std::log(overlapHigh / overlapLow);
    level.partial = overlapWidth < kPartialWidthFraction * fullWidth;

    std::vector<double> samples;
    if (overlapHigh / overlapLow < kThinRegionRatio) {
      samples.push_back(std::sqrt(overlapLow * overlapHigh));
    } else {
      samples.reserve(kRegionSamples);
      const double logLow = std::log(overlapLow);
      const double logHigh = std::log(overlapHigh);
      for (int sample = 0; sample < kRegionSamples; ++sample) {
        const double fraction = static_cast<double>(sample) /
                                static_cast<double>(kRegionSamples - 1);
        samples.push_back(std::exp(logLow + (logHigh - logLow) * fraction));
      }
    }

    double sum = 0.0;
    std::size_t count = 0;
    for (const double frequency : samples) {
      const CorrectionBreakdown applied =
          evaluateCorrectionAt(model, ceilingDb, settings, frequency);
      if (!applied.inRange) continue;
      sum += applied.tonalDb;
      ++count;
    }
    if (count > 0) {
      level.active = true;
      level.levelDb = sum / static_cast<double>(count);
    }
    levels.push_back(level);
  }
  return levels;
}

[[nodiscard]] std::vector<RegionLevel> differenceLevels(
    const std::vector<RegionLevel>& reference,
    const std::vector<RegionLevel>& target) {
  std::vector<RegionLevel> result;
  const std::size_t count = std::min(reference.size(), target.size());
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const RegionLevel& ref = reference[index];
    const RegionLevel& tgt = target[index];
    RegionLevel level = tgt;
    level.active = ref.active && tgt.active;
    level.partial = ref.partial || tgt.partial;
    level.lowHz = std::max(ref.lowHz, tgt.lowHz);
    level.highHz = std::min(ref.highHz, tgt.highHz);
    level.levelDb = level.active ? tgt.levelDb - ref.levelDb : 0.0;
    result.push_back(level);
  }
  return result;
}

[[nodiscard]] std::vector<RegionLevel> activeLevels(
    const std::vector<RegionLevel>& levels) {
  std::vector<RegionLevel> result;
  for (const auto& level : levels) {
    if (level.active) result.push_back(level);
  }
  return result;
}

[[nodiscard]] bool tiltSentence(const std::vector<RegionLevel>& levels,
                                DescriptionKind kind,
                                std::string& output) {
  const std::vector<RegionLevel> active = activeLevels(levels);
  if (active.size() < kTiltMinimumBands) return false;
  const double signedSpan = active.back().levelDb - active.front().levelDb;
  const double span = std::abs(signedSpan);
  if (span < kTiltSpanDb) return false;
  const double slopeSign = signedSpan > 0.0 ? 1.0 : -1.0;

  std::size_t reversals = 0;
  for (std::size_t index = 0; index + 1 < active.size(); ++index) {
    const double delta = active[index + 1].levelDb - active[index].levelDb;
    if (std::abs(delta) >= kTiltJumpFraction * span) return false;
    if (delta * slopeSign < 0.0) {
      ++reversals;
      if (reversals > kTiltMaximumReversals ||
          std::abs(delta) >= kTiltReversalDb) {
        return false;
      }
    }
  }

  double meanX = 0.0;
  double meanY = 0.0;
  for (const auto& level : active) {
    meanX += std::log(std::sqrt(level.lowHz * level.highHz));
    meanY += level.levelDb;
  }
  meanX /= static_cast<double>(active.size());
  meanY /= static_cast<double>(active.size());

  double denominator = 0.0;
  double numerator = 0.0;
  for (const auto& level : active) {
    const double x = std::log(std::sqrt(level.lowHz * level.highHz));
    denominator += (x - meanX) * (x - meanX);
    numerator += (x - meanX) * (level.levelDb - meanY);
  }
  if (denominator <= kEpsilon) return false;
  const double slope = numerator / denominator;
  double squaredResiduals = 0.0;
  for (const auto& level : active) {
    const double x = std::log(std::sqrt(level.lowHz * level.highHz));
    const double predicted = meanY + slope * (x - meanX);
    const double residual = level.levelDb - predicted;
    squaredResiduals += residual * residual;
  }
  const double rms =
      std::sqrt(squaredResiduals / static_cast<double>(active.size()));
  if (rms > kTiltResidualDb) return false;

  const char* direction = signedSpan > 0.0 ? "up" : "down";
  const std::string spanText =
      formatRange(active.front().lowHz, active.back().highHz);
  std::ostringstream sentence;
  if (kind == DescriptionKind::Capture) {
    sentence << "The curve tilts " << direction << " by " << plainDb(span)
             << " across " << spanText << '.';
  } else if (kind == DescriptionKind::Correction) {
    sentence << "The correction tilts " << direction << " by " << plainDb(span)
             << " across " << spanText << '.';
  } else {
    sentence << "Target tilts " << direction << " by " << plainDb(span)
             << " relative to Reference across " << spanText << '.';
  }
  output = sentence.str();
  return true;
}

[[nodiscard]] SignedClass classify(double levelDb) {
  if (levelDb >= kSignificantDb) return SignedClass::Boost;
  if (levelDb <= -kSignificantDb) return SignedClass::Cut;
  return SignedClass::Flat;
}

[[nodiscard]] std::vector<SignedRun> signedRuns(
    const std::vector<RegionLevel>& levels) {
  std::vector<SignedRun> runs;
  SignedRun current;
  bool hasCurrent = false;
  std::size_t previousBandIndex = std::numeric_limits<std::size_t>::max();

  auto flush = [&]() {
    if (hasCurrent && current.classification != SignedClass::Flat &&
        !current.regions.empty()) {
      runs.push_back(current);
    }
    current = SignedRun{};
    hasCurrent = false;
  };

  for (const auto& region : levels) {
    if (!region.active) {
      flush();
      previousBandIndex = std::numeric_limits<std::size_t>::max();
      continue;
    }
    const SignedClass c = classify(region.levelDb);
    if (c == SignedClass::Flat) {
      flush();
      previousBandIndex = region.bandIndex;
      continue;
    }
    const bool adjacent = previousBandIndex != std::numeric_limits<std::size_t>::max() &&
                          region.bandIndex == previousBandIndex + 1;
    if (!hasCurrent || current.classification != c || !adjacent) {
      flush();
      current.classification = c;
      hasCurrent = true;
    }
    current.regions.push_back(region);
    previousBandIndex = region.bandIndex;
  }
  flush();

  while (runs.size() > kMaximumRuns) {
    std::size_t drop = 0;
    for (std::size_t index = 1; index < runs.size(); ++index) {
      const double candidateMagnitude = std::abs(runs[index].meanDb());
      const double currentMagnitude = std::abs(runs[drop].meanDb());
      if (candidateMagnitude < currentMagnitude - kEpsilon) {
        drop = index;
        continue;
      }
      if (std::abs(candidateMagnitude - currentMagnitude) <= kEpsilon) {
        if (runs[index].regions.size() < runs[drop].regions.size()) {
          drop = index;
          continue;
        }
        if (runs[index].regions.size() == runs[drop].regions.size() &&
            runs[index].firstBandIndex() > runs[drop].firstBandIndex()) {
          drop = index;
        }
      }
    }
    runs.erase(runs.begin() + static_cast<std::ptrdiff_t>(drop));
  }
  return runs;
}

[[nodiscard]] std::pair<std::size_t, std::size_t> activeEdges(
    const std::vector<RegionLevel>& levels) {
  std::size_t lowest = std::numeric_limits<std::size_t>::max();
  std::size_t highest = 0;
  for (const auto& level : levels) {
    if (!level.active) continue;
    lowest = std::min(lowest, level.bandIndex);
    highest = std::max(highest, level.bandIndex);
  }
  if (lowest == std::numeric_limits<std::size_t>::max()) return {0, 0};
  return {lowest, highest};
}

[[nodiscard]] std::string featureFragment(const SignedRun& run,
                                          DescriptionKind kind) {
  const bool boost = run.meanDb() > 0.0;
  const double magnitude = std::abs(run.peakDb());
  const std::string range = runSpan(run);
  const std::string amount = plainDb(magnitude);

  std::ostringstream out;
  if (kind == DescriptionKind::Capture) {
    if (boost) {
      out << "a " << amount << " peak from " << range;
    } else if (narrowNotch(run)) {
      out << "a " << amount << " notch from " << range;
    } else {
      out << "a " << amount << " dip from " << range;
    }
  } else if (kind == DescriptionKind::Correction) {
    if (boost) {
      out << "a " << amount << " boost from " << range;
    } else if (narrowNotch(run)) {
      out << "a " << amount << " notch from " << range;
    } else {
      out << "a " << amount << " cut from " << range;
    }
  } else {
    out << amount << ' ' << (boost ? "higher" : "lower")
        << " from " << range;
  }
  return out.str();
}

[[nodiscard]] std::string joinFeatures(const std::vector<SignedRun>& runs,
                                       DescriptionKind kind) {
  std::ostringstream out;
  for (std::size_t index = 0; index < runs.size(); ++index) {
    if (index > 0) {
      out << (index + 1 == runs.size() ? ", and " : ", ");
    }
    out << featureFragment(runs[index], kind);
  }
  return out.str();
}

[[nodiscard]] std::string oneRunSentence(const SignedRun& run,
                                         const std::vector<RegionLevel>& levels,
                                         DescriptionKind kind) {
  const auto [lowestActive, highestActive] = activeEdges(levels);
  const bool touchesLow = run.firstBandIndex() == lowestActive;
  const bool touchesHigh = run.lastBandIndex() == highestActive;
  const double mean = run.meanDb();
  const bool boost = mean > 0.0;
  const std::string amount = plainDb(mean);

  std::ostringstream out;
  if (touchesLow && !touchesHigh) {
    const std::string boundary = formatHz(run.highHz());
    if (kind == DescriptionKind::Capture) {
      out << "A low shelf shape: the band below " << boundary << " is "
          << amount << ' ' << (boost ? "higher" : "lower")
          << " than the band above it.";
    } else if (kind == DescriptionKind::Correction) {
      out << "A low shelf " << (boost ? "boosts" : "cuts")
          << " frequencies below " << boundary << " by " << amount << '.';
    } else {
      out << "Target is " << amount << ' ' << (boost ? "higher" : "lower")
          << " than Reference below " << boundary << '.';
    }
  } else if (touchesHigh && !touchesLow) {
    const std::string boundary = formatHz(run.lowHz());
    if (kind == DescriptionKind::Capture) {
      out << "A high shelf shape: the band above " << boundary << " is "
          << amount << ' ' << (boost ? "higher" : "lower")
          << " than the band below it.";
    } else if (kind == DescriptionKind::Correction) {
      out << "A high shelf " << (boost ? "boosts" : "cuts")
          << " frequencies above " << boundary << " by " << amount << '.';
    } else {
      out << "Target is " << amount << ' ' << (boost ? "higher" : "lower")
          << " than Reference above " << boundary << '.';
    }
  } else if (touchesLow && touchesHigh) {
    const std::string range = runSpan(run);
    if (kind == DescriptionKind::Capture) {
      out << "The curve has a broad " << amount << ' '
          << (boost ? "peak" : "dip") << " from " << range << '.';
    } else if (kind == DescriptionKind::Correction) {
      out << "The correction applies a broad " << amount << ' '
          << (boost ? "boost" : "cut") << " from " << range << '.';
    } else {
      out << "Target is " << amount << ' ' << (boost ? "higher" : "lower")
          << " than Reference from " << range << '.';
    }
  } else {
    const std::string fragment = featureFragment(run, kind);
    if (kind == DescriptionKind::Capture) {
      out << "The curve has " << fragment << '.';
    } else if (kind == DescriptionKind::Correction) {
      out << "The correction applies " << fragment << '.';
    } else {
      out << "Compared with Reference, Target is " << fragment << '.';
    }
  }
  return out.str();
}

[[nodiscard]] std::string runSentence(const std::vector<RegionLevel>& levels,
                                      DescriptionKind kind) {
  const std::vector<RegionLevel> active = activeLevels(levels);
  if (active.empty()) {
    if (kind == DescriptionKind::Capture) return "No capture yet.";
    if (kind == DescriptionKind::Correction) {
      return "No correction has been computed yet.";
    }
    return "Reference and Target have a similar tonal shape.";
  }

  std::string tilt;
  if (tiltSentence(levels, kind, tilt)) return tilt;

  const std::vector<SignedRun> runs = signedRuns(levels);
  if (runs.empty()) {
    if (kind == DescriptionKind::Capture) {
      return "The curve is close to flat across the active frequency range.";
    }
    if (kind == DescriptionKind::Correction) {
      return "The correction is gentle across the active frequency range.";
    }
    return "Reference and Target have a similar tonal shape.";
  }
  if (runs.size() == 1) return oneRunSentence(runs.front(), levels, kind);

  // Two adjacent opposite-sign runs are one shelf/step relationship, not two
  // independent tonal features.
  if (runs.size() == 2 && runs[0].classification != runs[1].classification) {
    const double firstMean = runs[0].meanDb();
    const double secondMean = runs[1].meanDb();
    const double difference = std::abs(firstMean - secondMean);
    const bool firstHigher = firstMean > secondMean;
    const std::string boundary = formatHz(runs[1].lowHz());
    std::ostringstream out;
    if (kind == DescriptionKind::Capture) {
      out << "A shelf transition of " << plainDb(difference) << " around "
          << boundary << "; the band below it is "
          << (firstHigher ? "higher" : "lower") << '.';
    } else if (kind == DescriptionKind::Correction) {
      out << "A shelf correction changes by " << plainDb(difference)
          << " around " << boundary << ", with more gain "
          << (firstHigher ? "below" : "above") << " that point.";
    } else {
      out << "Compared with Reference, Target has a shelf transition of "
          << plainDb(difference) << " around " << boundary
          << "; the band below it is " << (firstHigher ? "higher" : "lower")
          << '.';
    }
    return out.str();
  }

  // For multiple separated features, report exactly what the frequency bands
  // do in low-to-high order. Do not substitute smile/frown or subjective
  // frequency-zone vocabulary.
  const std::string features = joinFeatures(runs, kind);
  if (kind == DescriptionKind::Capture) {
    return "The curve has " + features + ".";
  }
  if (kind == DescriptionKind::Correction) {
    return "The correction applies " + features + ".";
  }
  return "Compared with Reference, Target is " + features + ".";
}

[[nodiscard]] bool manualGainsActive(const IrRenderSettings& settings) {
  return std::any_of(settings.manualGains.begin(), settings.manualGains.end(),
                     [](double gain) {
                       return std::isfinite(gain) && std::abs(gain) > 1.0e-12;
                     });
}

[[nodiscard]] double safeCeiling(double ceilingDb) {
  if (!std::isfinite(ceilingDb) || ceilingDb <= 0.0 || ceilingDb > 60.0) {
    return 18.0;
  }
  return ceilingDb;
}

[[nodiscard]] std::string describeDifference(
    const SpectrumCapture& reference, const SpectrumCapture& target) {
  const auto difference = differenceLevels(captureLevels(reference), captureLevels(target));
  return runSentence(difference, DescriptionKind::Match);
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
  const CaptureBandMean mean = captureBandMean(capture, band);
  return mean.active ? mean.valueDb : 0.0;
}

double correctionBandDb(const CorrectionModel& model, const CurveBand& band) {
  return model.gainDbAt(std::sqrt(band.lowHz * band.highHz));
}

std::string describeCapture(const SpectrumCapture& capture) {
  if (!hasCapture(capture)) return "No capture yet.";
  return runSentence(captureLevels(capture), DescriptionKind::Capture);
}

std::string describeCorrection(const CorrectionModel& model, double ceilingDb,
                               const IrRenderSettings& settings) {
  if (model.nodes.empty() && !manualGainsActive(settings)) {
    return "No correction has been computed yet.";
  }
  const double effectiveCeiling = safeCeiling(ceilingDb);
  std::string description =
      runSentence(correctionLevels(model, effectiveCeiling, settings),
                  DescriptionKind::Correction);

  if (!model.nodes.empty()) {
    double peakUncapped = 0.0;
    bool ceilingHit = false;
    for (const auto& node : model.nodes) {
      peakUncapped = std::max(peakUncapped, std::abs(node.gainDb));
      if (std::abs(node.gainDb) > effectiveCeiling + kEpsilon) ceilingHit = true;
    }
    if (ceilingHit) {
      description += " Maximum Correction clipped the learned curve from " +
                     plainDb(peakUncapped) + " to " + plainDb(effectiveCeiling) +
                     " before Strength, Q, and manual trims.";
    }
  }
  return description;
}

CurveDescription describeToneTrace(const ProfileSnapshot& snapshot) {
  CurveDescription description;
  const bool referencePresent = hasCapture(snapshot.reference);
  const bool targetPresent = hasCapture(snapshot.target);

  description.reference = referencePresent ? describeCapture(snapshot.reference)
                                           : "No capture yet.";
  description.target = targetPresent ? describeCapture(snapshot.target)
                                     : "No capture yet.";

  if (!referencePresent && !targetPresent) {
    description.summary =
        "No learned match yet. Capture Reference to begin. Manual band EQ remains available on the Bands pages.";
    description.correction = "No correction has been computed yet.";
    return description;
  }

  if (referencePresent && !targetPresent) {
    description.summary = "Reference captured; target not yet learned.";
    description.correction = "No correction has been computed yet.";
    return description;
  }

  if (referencePresent && targetPresent) {
    description.summary = describeDifference(snapshot.reference, snapshot.target);
  } else {
    description.summary = "Reference and Target have a similar tonal shape.";
  }

  if (!snapshot.uncappedModel.nodes.empty() ||
      manualGainsActive(snapshot.renderSettings)) {
    description.correction = describeCorrection(
        snapshot.uncappedModel, snapshot.matchSettings.maximumCorrectionDb,
        snapshot.renderSettings);
  } else {
    description.correction = "No correction has been computed yet.";
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
