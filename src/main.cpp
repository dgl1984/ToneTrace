#include "tonetrace/tonetrace_engine.h"

#include <filesystem>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

double parseNumber(const std::string& text, const std::string& name) {
  std::size_t consumed = 0;
  const double value = std::stod(text, &consumed);
  if (consumed != text.size() || !std::isfinite(value)) {
    throw std::runtime_error("Invalid " + name + ": " + text);
  }
  return value;
}

void usage() {
  std::cout
      << "Tone Trace clean match-engine prototype\n\n"
      << "Commands:\n"
      << "  tonetrace-match match <reference.wav> <target.wav> <model.ttm> "
         "[mode] [maximum-correction-db]\n"
      << "  tonetrace-match export-ir <model.ttm> <output-directory>\n"
      << "  tonetrace-match apply-ir <input.wav> <ir.wav> <output.wav>\n"
      << "  tonetrace-match inspect <model.ttm>\n\n"
      << "Modes: full-mix, voice, drums, bass-synth, custom-max\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 2) {
      usage();
      return 1;
    }
    const std::string command = argv[1];
    if (command == "match") {
      if (argc < 5 || argc > 7) {
        usage();
        return 1;
      }
      tonetrace::MatchSettings settings;
      if (argc >= 6) {
        settings.mode = tonetrace::parseMatchMode(argv[5]);
        switch (settings.mode) {
          case tonetrace::MatchMode::Voice: settings.resolution = 30; break;
          case tonetrace::MatchMode::Drums: settings.resolution = 30; break;
          case tonetrace::MatchMode::BassSynth: settings.resolution = 30; break;
          case tonetrace::MatchMode::CustomMaxCapture: settings.resolution = 30; break;
          case tonetrace::MatchMode::FullMix: settings.resolution = 30; break;
        }
      }
      if (argc == 7) {
        settings.maximumCorrectionDb = parseNumber(argv[6], "maximum correction");
      }
      const auto reference = tonetrace::readWav(argv[2]);
      const auto target = tonetrace::readWav(argv[3]);
      tonetrace::MatchEngine engine;
      const auto referenceCapture = engine.capture(reference, settings);
      const auto targetCapture = engine.capture(target, settings);
      const auto model = engine.match(referenceCapture, targetCapture, settings);
      model.save(argv[4]);
      std::cout << "Saved " << model.nodes.size() << " correction nodes to " << argv[4]
                << "\nReference confidence: " << referenceCapture.confidence
                << "\nTarget confidence: " << targetCapture.confidence << '\n';
      return 0;
    }
    if (command == "export-ir") {
      if (argc != 4) {
        usage();
        return 1;
      }
      const auto model = tonetrace::CorrectionModel::load(argv[2]);
      const std::filesystem::path directory(argv[3]);
      const std::vector<int> sampleRates{44100, 48000, 88200, 96000, 176400, 192000};
      for (const int sampleRate : sampleRates) {
        tonetrace::IrRenderSettings settings;
        settings.sampleRate = sampleRate;
        const auto ir = tonetrace::renderMinimumPhaseIr(model, settings);
        tonetrace::AudioBuffer audio;
        audio.sampleRate = sampleRate;
        audio.channels = {ir};
        const auto path = directory / ("ToneTrace_IR_" + std::to_string(sampleRate) + ".wav");
        tonetrace::writeFloatWav(path, audio);
        std::cout << "Wrote " << path.string() << '\n';
      }
      return 0;
    }
    if (command == "apply-ir") {
      if (argc != 5) {
        usage();
        return 1;
      }
      const auto input = tonetrace::readWav(argv[2]);
      const auto irAudio = tonetrace::readWav(argv[3]);
      if (input.sampleRate != irAudio.sampleRate) {
        throw std::runtime_error("Input and IR sample rates must match");
      }
      if (irAudio.channels.size() != 1) {
        throw std::runtime_error("The direct IR WAV must be mono");
      }
      const auto output = tonetrace::convolve(input, irAudio.channels.front());
      tonetrace::writeFloatWav(argv[4], output);
      std::cout << "Wrote " << argv[4] << '\n';
      return 0;
    }
    if (command == "inspect") {
      if (argc != 3) {
        usage();
        return 1;
      }
      const auto model = tonetrace::CorrectionModel::load(argv[2]);
      std::cout << "Mode: " << tonetrace::toString(model.mode)
                << "\nRange: " << model.analysisLowHz << " to " << model.analysisHighHz
                << " Hz\nResolution: " << model.resolution
                << "\nNodes: " << model.nodes.size() << '\n';
      return 0;
    }
    usage();
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 2;
  }
}
