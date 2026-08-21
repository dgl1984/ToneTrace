#include "tonetrace/tonetrace_describe.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace tonetrace {

namespace {

constexpr double kLogLowHz = 20.0;
constexpr double kLogHighHz = 20000.0;

std::string magnitudeWord(double absolute) {
  if (absolute < 2.5) return "slightly";
  if (absolute < 5.0) return "noticeably";
  return "strongly";
}

std::string plainDb(double value) {
  std::ostringstream stream;
  stream << static_cast<int>(std::lround(value)) << " dB";
  return stream.str();
}

// Region names alone ("bass", "air") are meaningless without a frequency
// anchor. Always state the exact range so the reader knows what the words
// refer to, e.g. "bass (60-250 Hz)".
std::string regionLabel(const CurveBand& band) {
  std::ostringstream stream;
  stream << band.name << " (" << static_cast<int>(band.lowHz) << '-'
         << static_cast<int>(band.highHz) << " Hz)";
  return stream.str();
}

bool hasCapture(const SpectrumCapture& capture) {
  return capture.points.size() >= 3;
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
  if (weight <= 0.0) return 0.0;
  const double mean = sum / weight;

  double overallSum = 0.0;
  double overallWeight = 0.0;
  for (const auto& point : capture.points) {
    if (point.frequencyHz < kLogLowHz || point.frequencyHz >= kLogHighHz) {
      continue;
    }
    const double pointWeight =
        std::clamp(point.confidence, 0.0, 1.0) * 0.5 + 0.5;
    overallSum += point.levelDb * pointWeight;
    overallWeight += pointWeight;
  }
  if (overallWeight <= 0.0) return 0.0;
  return mean - overallSum / overallWeight;
}

double correctionBandDb(const CorrectionModel& model, const CurveBand& band) {
  return model.gainDbAt(std::sqrt(band.lowHz * band.highHz));
}

std::string describeCapture(const SpectrumCapture& capture) {
  if (!hasCapture(capture)) return "No capture yet.";
  const CurveBand* mostProminent = nullptr;
  const CurveBand* mostRecessed = nullptr;
  double peakDb = 0.0;
  double troughDb = 0.0;
  for (const auto& band : curveBands()) {
    const double delta = bandLevelDb(capture, band);
    // A small tolerance makes near-ties deterministic: when two bands are
    // equally loud or quiet, the lower-frequency band keeps the label.
    if (delta > peakDb + 1.0e-9) {
      peakDb = delta;
      mostProminent = &band;
    }
    if (delta < troughDb - 1.0e-9) {
      troughDb = delta;
      mostRecessed = &band;
    }
  }
  std::ostringstream out;
  const bool hasPeak = mostProminent != nullptr && peakDb >= 0.75;
  const bool hasTrough = mostRecessed != nullptr && troughDb <= -0.75;
  if (hasPeak && hasTrough) {
    out << "Most prominent in the " << regionLabel(*mostProminent) << " region, "
        << magnitudeWord(std::abs(peakDb))
        << " louder than the curve's average level; least prominent in the "
        << regionLabel(*mostRecessed) << " region, "
        << magnitudeWord(std::abs(troughDb))
        << " quieter than the curve's average level.";
  } else if (hasPeak) {
    out << "Most prominent in the " << regionLabel(*mostProminent) << " region, "
        << magnitudeWord(std::abs(peakDb))
        << " louder than the curve's average level.";
  } else if (hasTrough) {
    out << "Least prominent in the " << regionLabel(*mostRecessed) << " region, "
        << magnitudeWord(std::abs(troughDb))
        << " quieter than the curve's average level.";
  } else {
    out << "The curve is close to flat across the audible range.";
  }
  return out.str();
}

// Describes what the correction does to the target, band by band. The reported
// gains are the ones actually applied: the model is clamped to the audible
// ceiling (matchSettings.maximumCorrectionDb), and when a band's calculated
// correction exceeds that ceiling the text says so explicitly instead of
// describing a curve the renderer cannot produce.
std::string describeCorrection(const CorrectionModel& model,
                               double ceilingDb,
                               double rangeLowHz,
                               double rangeHighHz) {
  if (model.nodes.empty()) {
    return "No correction has been computed yet.";
  }
  double effectiveCeiling = ceilingDb;
  if (!std::isfinite(effectiveCeiling) || effectiveCeiling <= 0.0 ||
      effectiveCeiling > 60.0) {
    effectiveCeiling = 18.0;
  }
  std::vector<std::string> boosts;
  std::vector<std::string> cuts;
  double largestGain = 0.0;
  double largestUncapped = 0.0;
  const CurveBand* largestBand = nullptr;
  bool clamped = false;
  const double activeLow = std::max({rangeLowHz, model.analysisLowHz, kLogLowHz});
  const double activeHigh =
      std::min({rangeHighHz, model.analysisHighHz, kLogHighHz});
  for (const auto& band : curveBands()) {
    const double overlapLow = std::max(band.lowHz, activeLow);
    const double overlapHigh = std::min(band.highHz, activeHigh);
    if (overlapHigh <= overlapLow) continue;
    // Describe the part of a named region that is actually active. Sampling
    // the geometric centre of the overlap keeps the wording truthful when a
    // user places a range boundary inside a descriptive region.
    const double frequency = std::sqrt(overlapLow * overlapHigh);
    const double gain = model.gainDbAt(frequency);
    const double capped = std::clamp(gain, -effectiveCeiling, effectiveCeiling);
    if (std::abs(gain) > effectiveCeiling + 1.0e-9) clamped = true;
    if (std::abs(gain) > std::abs(largestUncapped)) largestUncapped = gain;
    if (std::abs(capped) >= 0.75) {
      std::string phrase = "the ";
      phrase += regionLabel(band);
      phrase += " by ";
      phrase += plainDb(std::abs(capped));
      (capped > 0.0 ? boosts : cuts).push_back(std::move(phrase));
    }
    if (std::abs(capped) > std::abs(largestGain)) {
      largestGain = capped;
      largestBand = &band;
    }
  }
  const auto join = [](const std::vector<std::string>& items) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < items.size(); ++index) {
      if (index > 0) {
        stream << (index + 1 == items.size() ? " and " : ", ");
      }
      stream << items[index];
    }
    return stream.str();
  };
  std::ostringstream out;
  if (boosts.empty() && cuts.empty()) {
    out << "The correction is gentle across the whole range";
  } else {
    if (!boosts.empty()) {
      out << "The correction boosts " << join(boosts);
      if (!cuts.empty()) out << ", and cuts ";
    } else {
      out << "The correction cuts ";
    }
    out << join(cuts);
    if (largestBand != nullptr && std::abs(largestGain) >= 2.0) {
      out << ", with the strongest move a " << plainDb(std::abs(largestGain))
          << ' ' << (largestGain > 0.0 ? "boost" : "cut") << " in the "
          << regionLabel(*largestBand) << " region";
    }
  }
  out << '.';
  if (clamped && std::abs(largestUncapped) > effectiveCeiling + 1.0e-9) {
    out << " The calculated correction reached "
        << plainDb(std::abs(largestUncapped))
        << ", but Tone Trace settings limit the applied correction to "
        << plainDb(effectiveCeiling) << '.';
  }
  return out.str();
}

CurveDescription describeToneTrace(const ProfileSnapshot& snapshot) {
  CurveDescription description;
  description.reference = describeCapture(snapshot.reference);
  description.target = describeCapture(snapshot.target);
  description.correction = describeCorrection(
      snapshot.uncappedModel, snapshot.matchSettings.maximumCorrectionDb,
      snapshot.renderSettings.rangeLowHz, snapshot.renderSettings.rangeHighHz);

  std::ostringstream summary;
  if (!hasCapture(snapshot.reference) && !hasCapture(snapshot.target)) {
    summary << "No captures yet. Capture Reference, then learn the Target.";
  } else if (!hasCapture(snapshot.target)) {
    summary << "Reference captured; target not yet learned.";
  } else if (snapshot.uncappedModel.nodes.empty()) {
    summary << "Both captures present; correction not yet computed.";
  } else {
    // Keep the summary as a workflow-status digest; the section sentences
    // below carry the band detail, so do not repeat them here.
    summary << "Ready. Reference and target captured; correction computed.";
  }
  description.summary = summary.str();
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
