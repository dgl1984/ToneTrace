#include "tonetrace/tonetrace_engine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

double rmsDb(const tonetrace::AudioBuffer& audio) {
  double sum = 0.0;
  std::size_t count = 0;
  for (const auto& channel : audio.channels) {
    for (const double sample : channel) {
      sum += sample * sample;
      ++count;
    }
  }
  return 20.0 * std::log10(std::max(1.0e-12, std::sqrt(sum / std::max<std::size_t>(1, count))));
}

double responseDbAt(const std::vector<double>& ir, int sampleRate, double frequencyHz) {
  constexpr double twoPi = 6.283185307179586476925286766559;
  std::complex<double> response(0.0, 0.0);
  for (std::size_t i = 0; i < ir.size(); ++i) {
    const double phase = -twoPi * frequencyHz * static_cast<double>(i) / sampleRate;
    response += ir[i] * std::complex<double>(std::cos(phase), std::sin(phase));
  }
  return 20.0 * std::log10(std::max(1.0e-12, std::abs(response)));
}

struct ResponseError {
  double removedGainDb = 0.0;
  double rmsDb = 0.0;
  double maximumDb = 0.0;
};

ResponseError compareIrShape(const std::vector<double>& a,
                             const std::vector<double>& b,
                             int sampleRate,
                             double lowHz,
                             double highHz) {
  constexpr int pointCount = 240;
  std::vector<double> differences;
  differences.reserve(pointCount);
  const double logLow = std::log(lowHz);
  const double logHigh = std::log(highHz);
  for (int i = 0; i < pointCount; ++i) {
    const double ratio = static_cast<double>(i) / (pointCount - 1);
    const double frequency = std::exp(logLow + (logHigh - logLow) * ratio);
    differences.push_back(responseDbAt(a, sampleRate, frequency) -
                          responseDbAt(b, sampleRate, frequency));
  }
  ResponseError result;
  result.removedGainDb = std::accumulate(differences.begin(), differences.end(), 0.0) /
                         differences.size();
  double squared = 0.0;
  for (const double difference : differences) {
    const double centered = difference - result.removedGainDb;
    squared += centered * centered;
    result.maximumDb = std::max(result.maximumDb, std::abs(centered));
  }
  result.rmsDb = std::sqrt(squared / differences.size());
  return result;
}

void resizeTo(tonetrace::AudioBuffer& audio, std::size_t frames) {
  for (auto& channel : audio.channels) channel.resize(frames);
}

void printCaptureComparison(const std::string& name,
                            const tonetrace::SpectrumCapture& reference,
                            const tonetrace::SpectrumCapture& candidate) {
  const auto main = tonetrace::compareCaptures(reference, candidate, 80.0, 13500.0);
  const auto full = tonetrace::compareCaptures(reference, candidate, 45.0, 18000.0);
  std::cout << std::left << std::setw(26) << name
            << " main=" << std::right << std::fixed << std::setprecision(3) << main.rmsDb
            << " dB, full=" << full.rmsDb << " dB, level=";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 5) {
      std::cerr << "Usage: tonetrace-fixture-eval <reference.wav> <source.wav> "
                   "<oracle-ir.wav> <output-dir> [historical-render.wav ...]\n";
      return 1;
    }
    const auto reference = tonetrace::readWav(argv[1]);
    const auto source = tonetrace::readWav(argv[2]);
    const auto oracleAudio = tonetrace::readWav(argv[3]);
    if (reference.sampleRate != source.sampleRate ||
        reference.sampleRate != oracleAudio.sampleRate) {
      throw std::runtime_error("All fixture files must use the same sample rate");
    }
    const std::filesystem::path outputDirectory(argv[4]);
    std::filesystem::create_directories(outputDirectory);

    tonetrace::MatchSettings settings;
    settings.mode = tonetrace::MatchMode::Voice;
    settings.resolution = 30;
    settings.rangeLowHz = 10.0;
    settings.rangeHighHz = 30000.0;
    settings.maximumCorrectionDb = 18.0;
    settings.removeBroadLevelDifference = true;

    tonetrace::MatchEngine engine;
    const auto referenceCapture = engine.capture(reference, settings);
    const auto sourceCapture = engine.capture(source, settings);
    const auto model = engine.match(referenceCapture, sourceCapture, settings);
    model.save(outputDirectory / "melonare_to_twin_clean.ttm");

    tonetrace::IrRenderSettings irSettings;
    irSettings.sampleRate = source.sampleRate;
    irSettings.rangeLowHz = settings.rangeLowHz;
    irSettings.rangeHighHz = settings.rangeHighHz;
    const auto cleanIr = tonetrace::renderMinimumPhaseIr(model, irSettings);
    tonetrace::AudioBuffer cleanIrAudio;
    cleanIrAudio.sampleRate = source.sampleRate;
    cleanIrAudio.channels = {cleanIr};
    tonetrace::writeFloatWav(outputDirectory / "melonare_to_twin_clean_ir.wav", cleanIrAudio);

    auto cleanOutput = tonetrace::convolve(source, cleanIr);
    auto oracleOutput = tonetrace::convolve(source, oracleAudio.channels.front());
    resizeTo(cleanOutput, source.frames());
    resizeTo(oracleOutput, source.frames());
    tonetrace::writeFloatWav(outputDirectory / "melonare_to_twin_clean_render.wav", cleanOutput);
    tonetrace::writeFloatWav(outputDirectory / "melonare_to_twin_oracle_render.wav", oracleOutput);

    const auto cleanCapture = engine.capture(cleanOutput, settings);
    const auto oracleCapture = engine.capture(oracleOutput, settings);
    std::cout << "Voice fixture evaluation (shape RMS; captures are level-normalized)\n";
    printCaptureComparison("Uncorrected Melonare", referenceCapture, sourceCapture);
    std::cout << rmsDb(source) << " dBFS\n";
    printCaptureComparison("Clean engine", referenceCapture, cleanCapture);
    std::cout << rmsDb(cleanOutput) << " dBFS\n";
    printCaptureComparison("Null-verified oracle IR", referenceCapture, oracleCapture);
    std::cout << rmsDb(oracleOutput) << " dBFS\n";
    const auto cleanToOracle = tonetrace::compareCaptures(oracleCapture, cleanCapture, 80.0, 13500.0);
    std::cout << "Clean vs oracle render      main=" << cleanToOracle.rmsDb << " dB\n";

    const auto irShape = compareIrShape(cleanIr, oracleAudio.channels.front(),
                                        source.sampleRate, 80.0, 13500.0);
    std::cout << "Clean vs oracle IR shape    rms=" << irShape.rmsDb
              << " dB, max=" << irShape.maximumDb
              << " dB, removed gain=" << irShape.removedGainDb << " dB\n";

    for (int i = 5; i < argc; ++i) {
      const auto historical = tonetrace::readWav(argv[i]);
      if (historical.sampleRate != reference.sampleRate) {
        throw std::runtime_error("Historical render sample rate differs");
      }
      const auto capture = engine.capture(historical, settings);
      printCaptureComparison(std::filesystem::path(argv[i]).stem().string(), referenceCapture, capture);
      std::cout << rmsDb(historical) << " dBFS\n";
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 2;
  }
}
