#include "tonetrace/tonetrace_engine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void resizeTo(tonetrace::AudioBuffer& audio, std::size_t frames) {
  for (auto& channel : audio.channels) channel.resize(frames);
}

double parseNumber(const std::string& text, const std::string& name) {
  std::size_t consumed = 0;
  const double value = std::stod(text, &consumed);
  if (consumed != text.size() || !std::isfinite(value)) {
    throw std::runtime_error("Invalid " + name + ": " + text);
  }
  return value;
}

struct Difference {
  double rms = 0.0;
  double maximum = 0.0;
};

Difference modelInverseDifference(const tonetrace::CorrectionModel& aToB,
                                  const tonetrace::CorrectionModel& bToA) {
  if (aToB.nodes.size() != bToA.nodes.size()) {
    throw std::runtime_error("Forward and reverse models use different node counts");
  }
  Difference result;
  double sum = 0.0;
  for (std::size_t i = 0; i < aToB.nodes.size(); ++i) {
    const double error = aToB.nodes[i].gainDb + bToA.nodes[i].gainDb;
    sum += error * error;
    result.maximum = std::max(result.maximum, std::abs(error));
  }
  result.rms = std::sqrt(sum / std::max<std::size_t>(1, aToB.nodes.size()));
  return result;
}

Difference audioDifference(const tonetrace::AudioBuffer& a,
                           const tonetrace::AudioBuffer& b) {
  if (a.sampleRate != b.sampleRate || a.channels.size() != b.channels.size()) {
    throw std::runtime_error("Cannot compare audio with different formats");
  }
  Difference result;
  double sum = 0.0;
  std::size_t count = 0;
  for (std::size_t channel = 0; channel < a.channels.size(); ++channel) {
    const std::size_t frames = std::min(a.channels[channel].size(), b.channels[channel].size());
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const double error = a.channels[channel][frame] - b.channels[channel][frame];
      sum += error * error;
      result.maximum = std::max(result.maximum, std::abs(error));
      ++count;
    }
  }
  result.rms = std::sqrt(sum / std::max<std::size_t>(1, count));
  return result;
}

void writeIr(const std::filesystem::path& path,
             const std::vector<double>& ir,
             int sampleRate) {
  tonetrace::AudioBuffer audio;
  audio.sampleRate = sampleRate;
  audio.channels = {ir};
  tonetrace::writeFloatWav(path, audio);
}

int defaultResolution(tonetrace::MatchMode) {
  return 30;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 4 || argc > 6) {
      std::cerr << "Usage: tonetrace-pair-eval <capture-a.wav> <capture-b.wav> "
                   "<output-dir> [mode] [maximum-correction-db]\n";
      return 1;
    }

    const auto a = tonetrace::readWav(argv[1]);
    const auto b = tonetrace::readWav(argv[2]);
    if (a.sampleRate != b.sampleRate) {
      throw std::runtime_error("Both captures must use the same sample rate");
    }

    const std::filesystem::path outputDirectory(argv[3]);
    std::filesystem::create_directories(outputDirectory);

    tonetrace::MatchSettings settings;
    settings.mode = argc >= 5 ? tonetrace::parseMatchMode(argv[4])
                              : tonetrace::MatchMode::Voice;
    settings.resolution = defaultResolution(settings.mode);
    settings.rangeLowHz = 10.0;
    settings.rangeHighHz = 30000.0;
    settings.maximumCorrectionDb = argc == 6 ? parseNumber(argv[5], "maximum correction")
                                             : 18.0;
    if (!std::isfinite(settings.maximumCorrectionDb) ||
        settings.maximumCorrectionDb <= 0.0 ||
        settings.maximumCorrectionDb > 60.0) {
      throw std::runtime_error("Maximum correction must be above 0 and no more than 60 dB");
    }
    settings.removeBroadLevelDifference = true;

    tonetrace::MatchEngine engine;
    const auto captureA = engine.capture(a, settings);
    const auto captureB = engine.capture(b, settings);
    const auto aToB = engine.match(captureB, captureA, settings);
    const auto bToA = engine.match(captureA, captureB, settings);
    aToB.save(outputDirectory / "a_to_b.ttm");
    bToA.save(outputDirectory / "b_to_a.ttm");

    tonetrace::IrRenderSettings irSettings;
    irSettings.sampleRate = a.sampleRate;
    irSettings.rangeLowHz = settings.rangeLowHz;
    irSettings.rangeHighHz = settings.rangeHighHz;
    const auto aToBIr = tonetrace::renderMinimumPhaseIr(aToB, irSettings);
    const auto bToAIr = tonetrace::renderMinimumPhaseIr(bToA, irSettings);
    writeIr(outputDirectory / "a_to_b_ir.wav", aToBIr, a.sampleRate);
    writeIr(outputDirectory / "b_to_a_ir.wav", bToAIr, a.sampleRate);

    auto aToBRender = tonetrace::convolve(a, aToBIr);
    auto bToARender = tonetrace::convolve(b, bToAIr);
    resizeTo(aToBRender, a.frames());
    resizeTo(bToARender, b.frames());
    tonetrace::writeFloatWav(outputDirectory / "a_to_b_render.wav", aToBRender);
    tonetrace::writeFloatWav(outputDirectory / "b_to_a_render.wav", bToARender);

    irSettings.correctionStrength = -1.0;
    const auto negativeAToBIr = tonetrace::renderMinimumPhaseIr(aToB, irSettings);
    const auto negativeBToAIr = tonetrace::renderMinimumPhaseIr(bToA, irSettings);
    auto negativeAToBOnB = tonetrace::convolve(b, negativeAToBIr);
    auto negativeBToAOnA = tonetrace::convolve(a, negativeBToAIr);
    resizeTo(negativeAToBOnB, b.frames());
    resizeTo(negativeBToAOnA, a.frames());
    tonetrace::writeFloatWav(outputDirectory / "a_to_b_negative_on_b.wav", negativeAToBOnB);
    tonetrace::writeFloatWav(outputDirectory / "b_to_a_negative_on_a.wav", negativeBToAOnA);

    const auto uncorrected = tonetrace::compareCaptures(captureA, captureB, 80.0, 13500.0);
    const auto correctedAToB = engine.capture(aToBRender, settings);
    const auto correctedBToA = engine.capture(bToARender, settings);
    const auto aToBError = tonetrace::compareCaptures(captureB, correctedAToB, 80.0, 13500.0);
    const auto bToAError = tonetrace::compareCaptures(captureA, correctedBToA, 80.0, 13500.0);
    const auto inverseError = modelInverseDifference(aToB, bToA);
    const auto bRenderError = audioDifference(bToARender, negativeAToBOnB);
    const auto aRenderError = audioDifference(aToBRender, negativeBToAOnA);

    std::ostringstream report;
    report << std::fixed << std::setprecision(6)
           << "Tone Trace bidirectional " << tonetrace::toString(settings.mode)
           << " evaluation\n"
           << "Capture A: " << std::filesystem::path(argv[1]).filename().string() << "\n"
           << "Capture B: " << std::filesystem::path(argv[2]).filename().string() << "\n"
           << "Maximum correction: " << settings.maximumCorrectionDb << " dB\n"
           << "A confidence: " << captureA.confidence << "\n"
           << "B confidence: " << captureB.confidence << "\n"
           << "Uncorrected A/B spectral distance: " << uncorrected.rmsDb << " dB RMS\n"
           << "A to B corrected distance: " << aToBError.rmsDb << " dB RMS\n"
           << "B to A corrected distance: " << bToAError.rmsDb << " dB RMS\n"
           << "Forward plus reverse model residual: " << inverseError.rms
           << " dB RMS, " << inverseError.maximum << " dB maximum\n"
           << "B-to-A positive versus A-to-B negative audio residual: " << bRenderError.rms
           << " RMS, " << bRenderError.maximum << " maximum\n"
           << "A-to-B positive versus B-to-A negative audio residual: " << aRenderError.rms
           << " RMS, " << aRenderError.maximum << " maximum\n";
    std::cout << report.str();
    std::ofstream reportFile(outputDirectory / "PAIR_EVALUATION.txt");
    if (!reportFile) throw std::runtime_error("Could not write evaluation report");
    reportFile << report.str();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 2;
  }
}
