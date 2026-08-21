#include "tonetrace/tonetrace_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double kConfidenceAvailable = 0.40;
constexpr double kConfidenceStrict = 0.50;
constexpr double kOnlineDriftDb = 0.50;
constexpr double kSettledDistanceDb = 0.75;
constexpr double kCompareLowHz = 80.0;
constexpr double kCompareHighHz = 13500.0;

tonetrace::AudioBuffer slice(const tonetrace::AudioBuffer& audio,
                             std::size_t start,
                             std::size_t frames) {
  if (start + frames > audio.frames()) {
    throw std::runtime_error("Requested capture window exceeds input audio");
  }
  tonetrace::AudioBuffer result;
  result.sampleRate = audio.sampleRate;
  result.channels.resize(audio.channels.size());
  for (std::size_t channel = 0; channel < audio.channels.size(); ++channel) {
    const auto begin = audio.channels[channel].begin() +
                       static_cast<std::ptrdiff_t>(start);
    result.channels[channel].assign(begin,
                                    begin + static_cast<std::ptrdiff_t>(frames));
  }
  return result;
}

int defaultResolution(tonetrace::MatchMode) {
  return 30;
}

std::uint64_t parseUnsigned(const std::string& text, const std::string& name) {
  std::size_t consumed = 0;
  const std::uint64_t value = std::stoull(text, &consumed);
  if (consumed != text.size()) {
    throw std::runtime_error("Invalid " + name + ": " + text);
  }
  return value;
}

double percentile(std::vector<double> values, double ratio) {
  values.erase(std::remove_if(values.begin(), values.end(),
                              [](double value) { return !std::isfinite(value); }),
               values.end());
  if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
  std::sort(values.begin(), values.end());
  const double position = std::clamp(ratio, 0.0, 1.0) * (values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - lower;
  return values[lower] + (values[upper] - values[lower]) * fraction;
}

double firstConfidenceTime(const std::vector<tonetrace::SpectrumCapture>& captures,
                           double threshold) {
  for (std::size_t i = 0; i < captures.size(); ++i) {
    if (captures[i].confidence >= threshold) return static_cast<double>(i + 1);
  }
  return std::numeric_limits<double>::quiet_NaN();
}

struct TrialResult {
  double startSeconds = 0.0;
  double confidence40Seconds = std::numeric_limits<double>::quiet_NaN();
  double confidence50Seconds = std::numeric_limits<double>::quiet_NaN();
  double onlineStableSeconds = std::numeric_limits<double>::quiet_NaN();
  double validatedSettledSeconds = std::numeric_limits<double>::quiet_NaN();
  double finalConfidence = 0.0;
  double finalToTrackDb = 0.0;
  std::vector<double> confidences;
  std::vector<double> drifts;
  std::vector<double> distancesToFinal;
};

std::string secondsText(double value, int maximumSeconds) {
  if (!std::isfinite(value)) return ">" + std::to_string(maximumSeconds);
  return std::to_string(static_cast<int>(std::lround(value)));
}

void printTimingSummary(const std::string& name,
                        const std::vector<double>& values,
                        int trials,
                        int maximumSeconds) {
  int successes = 0;
  for (double value : values) successes += std::isfinite(value) ? 1 : 0;
  std::cout << name << ": " << successes << '/' << trials;
  if (successes > 0) {
    std::cout << ", median " << std::fixed << std::setprecision(1)
              << percentile(values, 0.50) << " s, p90 "
              << percentile(values, 0.90) << " s";
  } else {
    std::cout << ", all >" << maximumSeconds << " s";
  }
  std::cout << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2 || argc > 6) {
      std::cerr << "Usage: tonetrace-stability-eval <capture.wav> [trials] "
                   "[seed] [mode] [maximum-seconds]\n";
      return 1;
    }

    const std::filesystem::path path(argv[1]);
    const std::uint64_t trialsValue =
        argc >= 3 ? parseUnsigned(argv[2], "trials") : 8;
    const std::uint64_t seed =
        argc >= 4 ? parseUnsigned(argv[3], "seed") : 20260814ULL;
    const auto mode = argc >= 5 ? tonetrace::parseMatchMode(argv[4])
                                : tonetrace::MatchMode::Voice;
    const std::uint64_t maximumSecondsValue =
        argc >= 6 ? parseUnsigned(argv[5], "maximum duration") : 12;
    if (trialsValue < 1 || trialsValue > 1000 ||
        maximumSecondsValue < 3 || maximumSecondsValue > 300) {
      throw std::runtime_error("Invalid trials or maximum duration");
    }
    const int trials = static_cast<int>(trialsValue);
    const int maximumSeconds = static_cast<int>(maximumSecondsValue);

    const auto audio = tonetrace::readWav(path);
    const std::size_t maximumFrames =
        static_cast<std::size_t>(maximumSeconds) * audio.sampleRate;
    if (audio.frames() < maximumFrames) {
      throw std::runtime_error("Input is shorter than the requested maximum window");
    }

    tonetrace::MatchSettings settings;
    settings.mode = mode;
    settings.resolution = defaultResolution(mode);
    settings.rangeLowHz = 10.0;
    settings.rangeHighHz = 30000.0;
    settings.maximumCorrectionDb = 18.0;
    settings.removeBroadLevelDifference = true;

    tonetrace::MatchEngine engine;
    const auto trackCapture = engine.capture(audio, settings);
    std::mt19937_64 random(seed);
    const std::size_t lastStart = audio.frames() - maximumFrames;
    std::vector<TrialResult> results;
    results.reserve(static_cast<std::size_t>(trials));

    for (int trial = 0; trial < trials; ++trial) {
      const std::size_t start = lastStart == 0 ? 0 : random() % (lastStart + 1);
      TrialResult result;
      result.startSeconds = static_cast<double>(start) / audio.sampleRate;
      std::vector<tonetrace::SpectrumCapture> captures;
      captures.reserve(static_cast<std::size_t>(maximumSeconds));
      for (int seconds = 1; seconds <= maximumSeconds; ++seconds) {
        const std::size_t frames = static_cast<std::size_t>(seconds) * audio.sampleRate;
        captures.push_back(engine.capture(slice(audio, start, frames), settings));
      }

      result.confidence40Seconds = firstConfidenceTime(captures, kConfidenceAvailable);
      result.confidence50Seconds = firstConfidenceTime(captures, kConfidenceStrict);
      result.confidences.reserve(captures.size());
      result.drifts.assign(captures.size(), std::numeric_limits<double>::quiet_NaN());
      result.distancesToFinal.reserve(captures.size());
      for (std::size_t i = 0; i < captures.size(); ++i) {
        result.confidences.push_back(captures[i].confidence);
        if (i > 0) {
          result.drifts[i] = tonetrace::compareCaptures(
              captures[i - 1], captures[i], kCompareLowHz, kCompareHighHz).rmsDb;
        }
        result.distancesToFinal.push_back(tonetrace::compareCaptures(
            captures[i], captures.back(), kCompareLowHz, kCompareHighHz).rmsDb);
      }

      for (std::size_t i = 2; i < captures.size(); ++i) {
        if (captures[i].confidence >= kConfidenceAvailable &&
            result.drifts[i] <= kOnlineDriftDb &&
            result.drifts[i - 1] <= kOnlineDriftDb) {
          result.onlineStableSeconds = static_cast<double>(i + 1);
          break;
        }
      }
      for (std::size_t i = 1; i < captures.size(); ++i) {
        if (captures[i].confidence < kConfidenceAvailable) continue;
        bool remainsSettled = true;
        for (std::size_t future = i; future < captures.size(); ++future) {
          if (result.distancesToFinal[future] > kSettledDistanceDb) {
            remainsSettled = false;
            break;
          }
        }
        if (remainsSettled) {
          result.validatedSettledSeconds = static_cast<double>(i + 1);
          break;
        }
      }
      result.finalConfidence = captures.back().confidence;
      result.finalToTrackDb = tonetrace::compareCaptures(
          captures.back(), trackCapture, kCompareLowHz, kCompareHighHz).rmsDb;
      results.push_back(std::move(result));
    }

    std::cout << std::fixed << std::setprecision(3)
              << "Tone Trace deterministic random-window capture evaluation\n"
              << "File: " << path.filename().string() << '\n'
              << "Mode: " << tonetrace::toString(mode) << '\n'
              << "Trials: " << trials << '\n'
              << "Seed: " << seed << '\n'
              << "Maximum window: " << maximumSeconds << " s\n"
              << "Thresholds: confidence " << kConfidenceAvailable
              << ", strict confidence " << kConfidenceStrict
              << ", two-update drift " << kOnlineDriftDb
              << " dB, settled-to-final " << kSettledDistanceDb << " dB\n\n"
              << "trial start_s conf40_s conf50_s online_stable_s "
                 "validated_settled_s final_conf final_to_track_db\n";

    std::vector<double> confidence40, confidence50, onlineStable, validatedSettled;
    std::vector<double> finalConfidence, finalToTrack;
    for (std::size_t i = 0; i < results.size(); ++i) {
      const auto& result = results[i];
      confidence40.push_back(result.confidence40Seconds);
      confidence50.push_back(result.confidence50Seconds);
      onlineStable.push_back(result.onlineStableSeconds);
      validatedSettled.push_back(result.validatedSettledSeconds);
      finalConfidence.push_back(result.finalConfidence);
      finalToTrack.push_back(result.finalToTrackDb);
      std::cout << (i + 1) << ' ' << result.startSeconds << ' '
                << secondsText(result.confidence40Seconds, maximumSeconds) << ' '
                << secondsText(result.confidence50Seconds, maximumSeconds) << ' '
                << secondsText(result.onlineStableSeconds, maximumSeconds) << ' '
                << secondsText(result.validatedSettledSeconds, maximumSeconds) << ' '
                << result.finalConfidence << ' ' << result.finalToTrackDb << '\n';
    }

    std::cout << "\nSummary\n";
    printTimingSummary("confidence >= 0.40", confidence40, trials, maximumSeconds);
    printTimingSummary("confidence >= 0.50", confidence50, trials, maximumSeconds);
    printTimingSummary("online two-update stability", onlineStable, trials, maximumSeconds);
    printTimingSummary("validated settling", validatedSettled, trials, maximumSeconds);
    std::cout << maximumSeconds << " s confidence: median "
              << percentile(finalConfidence, 0.50)
              << ", p10 " << percentile(finalConfidence, 0.10)
              << ", p90 " << percentile(finalConfidence, 0.90) << '\n'
              << maximumSeconds << " s window to whole-track capture: median "
              << percentile(finalToTrack, 0.50) << " dB, p90 "
              << percentile(finalToTrack, 0.90) << " dB\n\n"
              << "Per-second medians\n"
              << "seconds confidence drift_from_previous_db distance_to_"
              << maximumSeconds << "s_db\n";
    for (int seconds = 1; seconds <= maximumSeconds; ++seconds) {
      std::vector<double> confidenceValues, driftValues, distanceValues;
      for (const auto& result : results) {
        const std::size_t index = static_cast<std::size_t>(seconds - 1);
        confidenceValues.push_back(result.confidences[index]);
        driftValues.push_back(result.drifts[index]);
        distanceValues.push_back(result.distancesToFinal[index]);
      }
      std::cout << seconds << ' ' << percentile(confidenceValues, 0.50) << ' ';
      if (seconds == 1) std::cout << "n/a";
      else std::cout << percentile(driftValues, 0.50);
      std::cout << ' ' << percentile(distanceValues, 0.50) << '\n';
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 2;
  }
}
