#include "tonetrace/tonetrace_engine.h"
#include "tonetrace/tonetrace_describe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename Function>
void requireThrows(Function&& function, const std::string& message) {
  try {
    function();
  } catch (const std::exception&) {
    return;
  }
  throw std::runtime_error(message);
}

void writeU16(std::ostream& stream, std::uint16_t value) {
  const unsigned char bytes[]{
      static_cast<unsigned char>(value & 0xffU),
      static_cast<unsigned char>((value >> 8U) & 0xffU)};
  stream.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void writeU32(std::ostream& stream, std::uint32_t value) {
  const unsigned char bytes[]{
      static_cast<unsigned char>(value & 0xffU),
      static_cast<unsigned char>((value >> 8U) & 0xffU),
      static_cast<unsigned char>((value >> 16U) & 0xffU),
      static_cast<unsigned char>((value >> 24U) & 0xffU)};
  stream.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

void writeExtensibleFloatFixture(const std::filesystem::path& path) {
  constexpr std::uint32_t sampleRate = 48000;
  constexpr std::uint16_t channels = 1;
  constexpr std::uint16_t bits = 32;
  constexpr std::uint16_t blockAlign = channels * (bits / 8U);
  const std::vector<float> samples{0.0F, 0.25F, -0.5F, 0.75F};
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));

  std::ofstream stream(path, std::ios::binary);
  require(static_cast<bool>(stream), "Could not create extensible WAV fixture");
  stream.write("RIFF", 4);
  writeU32(stream, 4U + (8U + 40U) + (8U + dataBytes));
  stream.write("WAVEfmt ", 8);
  writeU32(stream, 40U);
  writeU16(stream, 0xfffeU);
  writeU16(stream, channels);
  writeU32(stream, sampleRate);
  writeU32(stream, sampleRate * blockAlign);
  writeU16(stream, blockAlign);
  writeU16(stream, bits);
  writeU16(stream, 22U);
  writeU16(stream, bits);
  writeU32(stream, 0x4U);
  writeU32(stream, 3U);
  writeU16(stream, 0x0000U);
  writeU16(stream, 0x0010U);
  const unsigned char guidTail[]{0x80, 0x00, 0x00, 0xaa,
                                 0x00, 0x38, 0x9b, 0x71};
  stream.write(reinterpret_cast<const char*>(guidTail), sizeof(guidTail));
  stream.write("data", 4);
  writeU32(stream, dataBytes);
  stream.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
  require(static_cast<bool>(stream), "Could not finish extensible WAV fixture");
}

void writeExtensiblePcm24Fixture(const std::filesystem::path& path) {
  constexpr std::uint32_t sampleRate = 48000;
  constexpr std::uint16_t channels = 1;
  constexpr std::uint16_t bits = 24;
  constexpr std::uint16_t blockAlign = channels * (bits / 8U);
  const std::array<unsigned char, 12> samples{
      0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
      0x00, 0x00, 0xc0, 0x00, 0x00, 0x60};
  const auto dataBytes = static_cast<std::uint32_t>(samples.size());

  std::ofstream stream(path, std::ios::binary);
  require(static_cast<bool>(stream), "Could not create extensible PCM WAV fixture");
  stream.write("RIFF", 4);
  writeU32(stream, 4U + (8U + 40U) + (8U + dataBytes));
  stream.write("WAVEfmt ", 8);
  writeU32(stream, 40U);
  writeU16(stream, 0xfffeU);
  writeU16(stream, channels);
  writeU32(stream, sampleRate);
  writeU32(stream, sampleRate * blockAlign);
  writeU16(stream, blockAlign);
  writeU16(stream, bits);
  writeU16(stream, 22U);
  writeU16(stream, bits);
  writeU32(stream, 0x4U);
  writeU32(stream, 1U);
  writeU16(stream, 0x0000U);
  writeU16(stream, 0x0010U);
  const unsigned char guidTail[]{0x80, 0x00, 0x00, 0xaa,
                                 0x00, 0x38, 0x9b, 0x71};
  stream.write(reinterpret_cast<const char*>(guidTail), sizeof(guidTail));
  stream.write("data", 4);
  writeU32(stream, dataBytes);
  stream.write(reinterpret_cast<const char*>(samples.data()), samples.size());
  require(static_cast<bool>(stream), "Could not finish extensible PCM WAV fixture");
}

tonetrace::AudioBuffer makeNoise(int sampleRate, double seconds) {
  const std::size_t frames = static_cast<std::size_t>(sampleRate * seconds);
  tonetrace::AudioBuffer audio;
  audio.sampleRate = sampleRate;
  audio.channels.assign(2, std::vector<double>(frames));
  std::mt19937 generator(0x544f4e45U);
  std::normal_distribution<double> distribution(0.0, 0.12);
  for (auto& channel : audio.channels) {
    for (auto& sample : channel) sample = distribution(generator);
  }
  return audio;
}

tonetrace::CorrectionModel coloringModel() {
  tonetrace::CorrectionModel model;
  model.mode = tonetrace::MatchMode::CustomMaxCapture;
  model.analysisLowHz = 10.0;
  model.analysisHighHz = 30000.0;
  model.resolution = 120;
  model.nodes = {
      {10.0, 0.0, 1.0},    {35.0, 1.0, 1.0},    {80.0, 4.5, 1.0},
      {180.0, 2.0, 1.0},   {420.0, -2.0, 1.0},  {1000.0, -3.5, 1.0},
      {2200.0, 1.5, 1.0},  {5200.0, 5.0, 1.0},  {9000.0, 2.0, 1.0},
      {15000.0, -2.5, 1.0},{22000.0, 0.0, 1.0}, {30000.0, 0.0, 1.0},
  };
  return model;
}

tonetrace::CorrectionModel resonantLowTargetModel() {
  tonetrace::CorrectionModel model;
  model.mode = tonetrace::MatchMode::Voice;
  model.analysisLowHz = 10.0;
  model.analysisHighHz = 30000.0;
  model.resolution = 55;
  model.nodes = {
      {10.0, 0.0, 1.0},    {35.0, -17.0, 1.0}, {65.0, -17.0, 1.0},
      {100.0, -13.0, 1.0}, {145.0, -9.0, 1.0}, {165.0, -6.0, 1.0},
      {182.0, -1.0, 1.0},  {198.0, -1.0, 1.0}, {220.0, -6.0, 1.0},
      {270.0, -6.0, 1.0},  {400.0, -2.0, 1.0}, {650.0, 0.0, 1.0},
      {30000.0, 0.0, 1.0},
  };
  return model;
}

double irGainDbAt(const std::vector<double>& ir, int sampleRate, double frequencyHz) {
  const double twoPi = 6.283185307179586476925286766559;
  std::complex<double> response(0.0, 0.0);
  for (std::size_t i = 0; i < ir.size(); ++i) {
    const double phase = -twoPi * frequencyHz * static_cast<double>(i) / sampleRate;
    response += ir[i] * std::complex<double>(std::cos(phase), std::sin(phase));
  }
  return 20.0 * std::log10(std::max(1.0e-12, std::abs(response)));
}

double maximumAudioDifference(const tonetrace::AudioBuffer& a,
                              const tonetrace::AudioBuffer& b) {
  require(a.sampleRate == b.sampleRate, "Compared audio sample rates differ");
  require(a.channels.size() == b.channels.size(), "Compared audio channels differ");
  double maximum = 0.0;
  for (std::size_t channel = 0; channel < a.channels.size(); ++channel) {
    require(a.channels[channel].size() == b.channels[channel].size(),
            "Compared audio lengths differ");
    for (std::size_t frame = 0; frame < a.channels[channel].size(); ++frame) {
      maximum = std::max(maximum,
          std::abs(a.channels[channel][frame] - b.channels[channel][frame]));
    }
  }
  return maximum;
}

void testIdentity() {
  const auto audio = makeNoise(48000, 5.0);
  tonetrace::MatchSettings settings;
  settings.mode = tonetrace::MatchMode::FullMix;
  tonetrace::MatchEngine engine;
  const auto model = engine.match(audio, audio, settings);
  double maximum = 0.0;
  for (const auto& node : model.nodes) maximum = std::max(maximum, std::abs(node.gainDb));
  require(maximum < 0.02, "Identity match is not flat");
  std::cout << "identity maximum correction: " << maximum << " dB\n";
}

void testConfigurableCorrectionCeiling() {
  tonetrace::SpectrumCapture reference;
  reference.sampleRate = 48000;
  reference.fftSize = 4096;
  reference.acceptedFrames = 24;
  reference.confidence = 1.0;
  tonetrace::SpectrumCapture target = reference;
  for (const double frequency : {20.0, 80.0, 250.0, 1000.0, 4000.0,
                                 12000.0, 22000.0}) {
    reference.points.push_back({frequency, 50.0, 1.0, 0.0});
    target.points.push_back({frequency, 0.0, 1.0, 0.0});
  }

  tonetrace::MatchSettings settings;
  settings.removeBroadLevelDifference = false;
  tonetrace::MatchEngine engine;
  const auto defaultModel = engine.match(reference, target, settings);
  settings.maximumCorrectionDb = 36.0;
  const auto wideModel = engine.match(reference, target, settings);
  require(std::abs(defaultModel.gainDbAt(1000.0) - 18.0) < 1.0e-9,
          "Default correction ceiling changed from 18 dB");
  require(std::abs(wideModel.gainDbAt(1000.0) - 36.0) < 1.0e-9,
          "Opt-in 36 dB correction ceiling is not honored");
  std::cout << "configurable correction ceiling: 18 dB default, 36 dB opt-in passed\n";
}

void testRecovery() {
  const int sampleRate = 48000;
  const auto reference = makeNoise(sampleRate, 7.0);
  tonetrace::IrRenderSettings colorSettings;
  colorSettings.sampleRate = sampleRate;
  const auto colorIr = tonetrace::renderMinimumPhaseIr(coloringModel(), colorSettings);
  auto target = tonetrace::convolve(reference, colorIr);
  for (auto& channel : target.channels) channel.resize(reference.frames());
  const auto temporary = std::filesystem::temp_directory_path() / "tonetrace-clean-tests";
  std::filesystem::create_directories(temporary);
  tonetrace::writeFloatWav(temporary / "reference.wav", reference);
  tonetrace::writeFloatWav(temporary / "target.wav", target);

  tonetrace::MatchSettings settings;
  settings.mode = tonetrace::MatchMode::CustomMaxCapture;
  settings.resolution = 120;
  tonetrace::MatchEngine engine;
  const auto referenceCapture = engine.capture(reference, settings);
  const auto targetCapture = engine.capture(target, settings);
  const auto before = tonetrace::compareCaptures(referenceCapture, targetCapture, 45.0, 18000.0);
  const auto correction = engine.match(referenceCapture, targetCapture, settings);
  tonetrace::IrRenderSettings correctionSettings;
  correctionSettings.sampleRate = sampleRate;
  const auto correctionIr = tonetrace::renderMinimumPhaseIr(correction, correctionSettings);
  auto corrected = tonetrace::convolve(target, correctionIr);
  for (auto& channel : corrected.channels) channel.resize(reference.frames());
  const auto correctedCapture = engine.capture(corrected, settings);
  const auto after = tonetrace::compareCaptures(referenceCapture, correctedCapture, 45.0, 18000.0);
  std::cout << "recovery RMS before: " << before.rmsDb
            << " dB, after: " << after.rmsDb << " dB\n";
  require(before.rmsDb > 1.5, "Coloring fixture is too weak");
  require(after.rmsDb < 0.70, "Recovered spectrum misses the quality target");
  require(after.rmsDb < before.rmsDb * 0.30, "Matcher did not improve enough");
}

void testSourceResonanceControl() {
  const int sampleRate = 48000;
  const auto reference = makeNoise(sampleRate, 7.0);
  tonetrace::IrRenderSettings colorSettings;
  colorSettings.sampleRate = sampleRate;
  const auto colorIr = tonetrace::renderMinimumPhaseIr(resonantLowTargetModel(),
                                                       colorSettings);
  auto target = tonetrace::convolve(reference, colorIr);
  for (auto& channel : target.channels) channel.resize(reference.frames());

  tonetrace::MatchSettings settings;
  settings.mode = tonetrace::MatchMode::Voice;
  settings.resolution = 55;
  tonetrace::MatchEngine engine;
  const auto referenceCapture = engine.capture(reference, settings);
  const auto targetCapture = engine.capture(target, settings);
  const auto correction = engine.match(referenceCapture, targetCapture, settings);
  const double shoulder = 0.5 * (correction.gainDbAt(165.0) +
                                 correction.gainDbAt(220.0));
  const double resonanceDip = shoulder - correction.gainDbAt(190.0);
  require(resonanceDip > 2.0,
          "Broad low-frequency recovery failed to suppress the source resonance");
  require(correction.gainDbAt(63.0) > 12.5 && correction.gainDbAt(63.0) <= 18.01,
          "Supported deep-bass recovery is still trapped at the old 12 dB ceiling");

  const auto inverse = engine.match(targetCapture, referenceCapture, settings);
  double inverseResidual = 0.0;
  for (const auto& node : correction.nodes) {
    inverseResidual = std::max(inverseResidual,
        std::abs(node.gainDb + inverse.gainDbAt(node.frequencyHz)));
  }
  require(inverseResidual < 1.0e-9,
          "Source resonance control broke exact forward/reverse symmetry");

  tonetrace::IrRenderSettings correctionSettings;
  correctionSettings.sampleRate = sampleRate;
  const auto correctionIr = tonetrace::renderMinimumPhaseIr(correction,
                                                            correctionSettings);
  auto corrected = tonetrace::convolve(target, correctionIr);
  for (auto& channel : corrected.channels) channel.resize(reference.frames());
  const auto correctedCapture = engine.capture(corrected, settings);
  const auto after = tonetrace::compareCaptures(referenceCapture, correctedCapture,
                                                 45.0, 18000.0);
  std::cout << "source-resonance control: " << resonanceDip
            << " dB inverse dip, " << after.rmsDb
            << " dB corrected RMS; exact reversal passed\n";
  require(after.rmsDb < 0.60,
          "Resonant low-frequency fixture misses the recovery target");
}

void testPersistenceAndSampleRates() {
  const auto temporary = std::filesystem::temp_directory_path() / "tonetrace-clean-tests";
  std::filesystem::create_directories(temporary);
  const auto modelPath = temporary / "roundtrip.ttm";
  const auto original = coloringModel();
  original.save(modelPath);
  const auto loaded = tonetrace::CorrectionModel::load(modelPath);
  require(loaded.nodes.size() == original.nodes.size(), "Model node count changed on reload");
  require(std::abs(loaded.gainDbAt(5200.0) - 5.0) < 1.0e-9,
          "Model values changed on reload");
  const std::vector<int> sampleRates{44100, 48000, 88200, 96000, 176400, 192000};
  const std::vector<double> testFrequencies{80.0, 420.0, 1000.0, 2200.0, 5200.0,
                                           9000.0, 15000.0};
  for (const int sampleRate : sampleRates) {
    tonetrace::IrRenderSettings settings;
    settings.sampleRate = sampleRate;
    const auto ir = tonetrace::renderMinimumPhaseIr(loaded, settings);
    require(ir.size() >= static_cast<std::size_t>(sampleRate * 0.17),
            "IR is shorter than requested");
    require(std::all_of(ir.begin(), ir.end(), [](double value) { return std::isfinite(value); }),
            "IR contains a non-finite sample");
    for (const double frequency : testFrequencies) {
      const double expected = loaded.gainDbAt(frequency);
      const double actual = irGainDbAt(ir, sampleRate, frequency);
      require(std::abs(actual - expected) < 0.18,
              "IR response differs from the portable model");
    }

    settings.correctionStrength = -1.0;
    const auto inverseIr = tonetrace::renderMinimumPhaseIr(loaded, settings);
    const double inverseGain = irGainDbAt(inverseIr, sampleRate, 5200.0);
    require(std::abs(inverseGain + loaded.gainDbAt(5200.0)) < 0.18,
            "Negative Correction Strength did not invert the model");
  }
  std::cout << "model roundtrip, response accuracy, negative strength, and six IR sample rates: passed\n";
}

void testCorrectionSharpness() {
  tonetrace::IrRenderSettings settings;
  settings.sampleRate = 48000;
  const auto model = resonantLowTargetModel();
  settings.correctionSharpness = 0.5;
  const auto broad = tonetrace::renderMinimumPhaseIr(model, settings);
  settings.correctionSharpness = 1.0;
  const auto neutral = tonetrace::renderMinimumPhaseIr(model, settings);
  settings.correctionSharpness = 1.5;
  const auto focused = tonetrace::renderMinimumPhaseIr(model, settings);
  const double broadGain = irGainDbAt(broad, settings.sampleRate, 182.0);
  const double neutralGain = irGainDbAt(neutral, settings.sampleRate, 182.0);
  const double focusedGain = irGainDbAt(focused, settings.sampleRate, 182.0);
  require(broadGain < neutralGain && neutralGain < focusedGain &&
              focusedGain - broadGain > 0.05,
          "Correction Q / Sharpness does not broaden and focus local detail");
  std::cout << "Correction Q / Sharpness broad-to-focused response: passed\n";
}

void testExportedIrConvolutionEquivalence() {
  const int sampleRate = 48000;
  tonetrace::IrRenderSettings settings;
  settings.sampleRate = sampleRate;
  const auto memoryIr = tonetrace::renderMinimumPhaseIr(coloringModel(), settings);

  tonetrace::AudioBuffer irAudio;
  irAudio.sampleRate = sampleRate;
  irAudio.channels = {memoryIr};
  const auto temporary = std::filesystem::temp_directory_path() /
                         "tonetrace-clean-tests";
  std::filesystem::create_directories(temporary);
  const auto irPath = temporary / "canonical-export-ir.wav";
  tonetrace::writeFloatWav(irPath, irAudio);
  const auto loadedIrAudio = tonetrace::readWav(irPath);
  require(loadedIrAudio.channels.size() == 1, "Exported IR is not mono");
  require(loadedIrAudio.channels.front() == memoryIr,
          "Saved IR coefficients differ from the internal canonical kernel");

  const auto source = makeNoise(sampleRate, 1.0);
  const auto internalRender = tonetrace::convolve(source, memoryIr);
  const auto exportedIrRender = tonetrace::convolve(
      source, loadedIrAudio.channels.front());
  require(maximumAudioDifference(internalRender, exportedIrRender) == 0.0,
          "Exported IR convolution does not null exactly with internal correction");
  std::cout << "saved direct IR versus internal correction: sample-identical convolution passed\n";
}

void testWavRoundTrip() {
  const auto temporary = std::filesystem::temp_directory_path() / "tonetrace-clean-tests";
  std::filesystem::create_directories(temporary);
  const auto path = temporary / "roundtrip.wav";
  const auto original = makeNoise(44100, 0.25);
  tonetrace::writeFloatWav(path, original);
  const auto loaded = tonetrace::readWav(path);
  require(loaded.sampleRate == original.sampleRate, "WAV sample rate changed");
  require(loaded.channels.size() == original.channels.size(), "WAV channel count changed");
  require(loaded.frames() == original.frames(), "WAV frame count changed");
  require(std::abs(loaded.channels[1][200] - original.channels[1][200]) < 1.0e-6,
          "WAV sample changed");
  std::cout << "float WAV roundtrip: passed\n";
}

void testSilenceAndBufferValidation() {
  tonetrace::AudioBuffer silence;
  silence.sampleRate = 48000;
  silence.channels.assign(1, std::vector<double>(48000, 0.0));
  tonetrace::MatchEngine engine;
  tonetrace::MatchSettings settings;
  requireThrows([&] { (void)engine.capture(silence, settings); },
                "Digital silence was accepted as a valid capture");

  auto unequal = silence;
  unequal.channels.push_back(std::vector<double>(100, 0.0));
  requireThrows([&] { (void)engine.capture(unequal, settings); },
                "Unequal channel lengths were accepted");
  std::cout << "silence rejection and channel validation: passed\n";
}

void testExtensibleWavAndBasenameOutputs() {
  const auto temporary = std::filesystem::temp_directory_path() /
                         "tonetrace-clean-tests";
  std::filesystem::create_directories(temporary);
  const auto extensiblePath = temporary / "extensible-float.wav";
  writeExtensibleFloatFixture(extensiblePath);
  const auto extensible = tonetrace::readWav(extensiblePath);
  require(extensible.sampleRate == 48000 && extensible.channels.size() == 1 &&
              extensible.frames() == 4,
          "Extensible float WAV format changed on read");
  require(std::abs(extensible.channels[0][1] - 0.25) < 1.0e-9 &&
              std::abs(extensible.channels[0][2] + 0.5) < 1.0e-9,
          "Extensible float WAV samples changed on read");

  const auto extensiblePcmPath = temporary / "extensible-pcm24.wav";
  writeExtensiblePcm24Fixture(extensiblePcmPath);
  const auto extensiblePcm = tonetrace::readWav(extensiblePcmPath);
  require(extensiblePcm.sampleRate == 48000 &&
              extensiblePcm.channels.size() == 1 && extensiblePcm.frames() == 4,
          "Extensible PCM WAV format changed on read");
  require(std::abs(extensiblePcm.channels[0][1] - 0.25) < 1.0e-9 &&
              std::abs(extensiblePcm.channels[0][2] + 0.5) < 1.0e-9,
          "Extensible PCM WAV samples changed on read");

  const auto oldPath = std::filesystem::current_path();
  std::filesystem::current_path(temporary);
  try {
    const auto modelName = std::filesystem::path("basename-model.ttm");
    const auto wavName = std::filesystem::path("basename-audio.wav");
    coloringModel().save(modelName);
    tonetrace::AudioBuffer audio;
    audio.sampleRate = 48000;
    audio.channels = {{0.0, 0.25, -0.25, 0.0}};
    tonetrace::writeFloatWav(wavName, audio);
    require(std::filesystem::is_regular_file(modelName) &&
                std::filesystem::is_regular_file(wavName),
            "Basename-only output was not created");
    std::filesystem::remove(modelName);
    std::filesystem::remove(wavName);
    std::filesystem::current_path(oldPath);
  } catch (...) {
    std::filesystem::current_path(oldPath);
    throw;
  }
  std::cout << "extensible WAV and basename-only output paths: passed\n";
}

void testGlobalCorrectionGainAndModelValidation() {
  tonetrace::IrRenderSettings settings;
  settings.sampleRate = 48000;
  settings.rangeLowHz = 900.0;
  settings.rangeHighHz = 2000.0;
  settings.correctionGainDb = -6.0;
  const auto ir = tonetrace::renderMinimumPhaseIr(coloringModel(), settings);
  require(std::abs(irGainDbAt(ir, 48000, 100.0) + 6.0) < 0.18,
          "Correction Gain does not affect frequencies below the correction range");
  require(std::abs(irGainDbAt(ir, 48000, 5000.0) + 6.0) < 0.18,
          "Correction Gain does not affect frequencies above the correction range");

  auto malformed = coloringModel();
  malformed.nodes[2].frequencyHz = malformed.nodes[1].frequencyHz;
  requireThrows([&] { (void)tonetrace::renderMinimumPhaseIr(malformed, settings); },
                "Unordered or duplicate model nodes were accepted");
  malformed = coloringModel();
  malformed.nodes[2].gainDb = std::numeric_limits<double>::quiet_NaN();
  requireThrows([&] { (void)tonetrace::renderMinimumPhaseIr(malformed, settings); },
                "Non-finite model gain was accepted");
  malformed = coloringModel();
  malformed.mode = static_cast<tonetrace::MatchMode>(999);
  requireThrows([&] { (void)tonetrace::renderMinimumPhaseIr(malformed, settings); },
                "An unknown model mode was accepted");

  auto excessive = settings;
  excessive.durationSeconds = 11.0;
  requireThrows([&] { (void)tonetrace::renderMinimumPhaseIr(coloringModel(), excessive); },
                "A resource-exhausting IR duration was accepted");

  tonetrace::SpectrumCapture malformedCapture;
  malformedCapture.sampleRate = 48000;
  malformedCapture.fftSize = 4096;
  malformedCapture.acceptedFrames = 10;
  malformedCapture.confidence = std::numeric_limits<double>::quiet_NaN();
  malformedCapture.points = {{20.0, 0.0, 1.0, 0.0},
                             {1000.0, 0.0, 1.0, 0.0},
                             {20000.0, 0.0, 1.0, 0.0}};
  requireThrows([&] {
    (void)tonetrace::compareCaptures(malformedCapture, malformedCapture,
                                     20.0, 20000.0);
  }, "A non-finite overall capture confidence was accepted");

  const auto malformedPath = std::filesystem::temp_directory_path() /
                             "tonetrace-clean-tests" / "malformed.ttm";
  std::filesystem::create_directories(malformedPath.parent_path());
  std::ofstream malformedFile(malformedPath);
  malformedFile << "ToneTraceModel 1\nmode voice\nrange 10 30000\n"
                   "resolution 55\nnodes 2\n100 0 1\n100 2 1\n";
  malformedFile.close();
  requireThrows([&] { (void)tonetrace::CorrectionModel::load(malformedPath); },
                "Malformed saved model was accepted");
  std::cout << "global Correction Gain and model validation: passed\n";
}

void testSpectrumCaptureSerialization() {
  const auto temporary =
      std::filesystem::temp_directory_path() / "tonetrace-clean-tests";
  std::filesystem::create_directories(temporary);

  tonetrace::SpectrumCapture capture;
  capture.sampleRate = 48000;
  capture.fftSize = 4096;
  capture.acceptedFrames = 42;
  capture.confidence = 0.875;
  for (double hz = 20.0; hz <= 20000.0; hz *= 1.5) {
    capture.points.push_back({hz, std::sin(hz) * 3.0,
                              0.5 + 0.5 * std::sin(hz), 0.1});
  }
  require(capture.points.size() >= 3, "Capture fixture has too few points");

  const auto path = temporary / "spectrum-roundtrip.tts";
  tonetrace::saveSpectrumCapture(path, capture);
  const auto loaded = tonetrace::loadSpectrumCapture(path);
  require(loaded.sampleRate == capture.sampleRate,
          "Spectrum sample rate changed on roundtrip");
  require(loaded.fftSize == capture.fftSize,
          "Spectrum FFT size changed on roundtrip");
  require(loaded.acceptedFrames == capture.acceptedFrames,
          "Spectrum accepted frames changed on roundtrip");
  require(std::abs(loaded.confidence - capture.confidence) < 1.0e-15,
          "Spectrum confidence changed on roundtrip");
  require(loaded.points.size() == capture.points.size(),
          "Spectrum point count changed on roundtrip");
  for (std::size_t i = 0; i < loaded.points.size(); ++i) {
    require(std::abs(loaded.points[i].frequencyHz -
                     capture.points[i].frequencyHz) < 1.0e-9,
            "Spectrum point frequency changed on roundtrip");
    require(std::abs(loaded.points[i].levelDb -
                     capture.points[i].levelDb) < 1.0e-9,
            "Spectrum point level changed on roundtrip");
  }

  const auto malformed = temporary / "spectrum-malformed.tts";
  std::ofstream file(malformed);
  file << "ToneTraceSpectrum 1\n"
          "rate 48000\nfft 4096\nframes 42\nconfidence 0.9\npoints 4\n"
          "20 0 1 0\n100 0 1 0\n100 0 1 0\n20000 0 1 0\n";
  file.close();
  requireThrows([&] { (void)tonetrace::loadSpectrumCapture(malformed); },
                "A spectrum capture with duplicate points was accepted");

  const auto underflow = temporary / "spectrum-underflow.tts";
  std::ofstream underflowFile(underflow);
  underflowFile << "ToneTraceSpectrum 1\n"
                   "rate 48000\nfft 4096\nframes 42\nconfidence 0.9\npoints 2\n"
                   "20 0 1 0\n20000 0 1 0\n";
  underflowFile.close();
  requireThrows([&] { (void)tonetrace::loadSpectrumCapture(underflow); },
                "A spectrum capture with fewer than three points was accepted");

  auto invalid = capture;
  invalid.points[1].levelDb = std::numeric_limits<double>::quiet_NaN();
  requireThrows([&] { (void)tonetrace::validateSpectrumCapture(invalid); },
                "A spectrum capture with a non-finite point level was accepted");

  tonetrace::CorrectionModel model;
  model.resolution = 8192;
  model.mode = tonetrace::MatchMode::FullMix;
  model.analysisLowHz = 10.0;
  model.analysisHighHz = 30000.0;
  model.nodes = {{20.0, 0.0, 1.0}, {1000.0, 0.0, 1.0}, {20000.0, 0.0, 1.0}};
  requireThrows([&] { (void)tonetrace::validateCorrectionModel(model); },
                "A model with resolution above the 4096 bound was accepted");

  std::cout << "spectrum capture serialization and bounds: passed\n";
}

void testInMemorySerialization() {
  const auto temporary =
      std::filesystem::temp_directory_path() / "tonetrace-clean-tests";
  std::filesystem::create_directories(temporary);

  tonetrace::SpectrumCapture capture;
  capture.sampleRate = 48000;
  capture.fftSize = 4096;
  capture.acceptedFrames = 42;
  capture.confidence = 0.875;
  for (double hz = 20.0; hz <= 20000.0; hz *= 1.5) {
    capture.points.push_back({hz, std::sin(hz) * 3.0,
                              0.5 + 0.5 * std::sin(hz), 0.1});
  }
  const auto capturePath = temporary / "spectrum-memory-roundtrip.tts";
  tonetrace::saveSpectrumCapture(capturePath, capture);
  const auto captureText = tonetrace::serializeSpectrumCapture(capture);
  std::ifstream captureFile(capturePath);
  std::stringstream fileContents;
  fileContents << captureFile.rdbuf();
  require(fileContents.str() == captureText,
          "In-memory spectrum serialization differs from the file format");
  const auto restoredCapture = tonetrace::deserializeSpectrumCapture(captureText);
  require(restoredCapture.sampleRate == capture.sampleRate &&
              restoredCapture.points.size() == capture.points.size() &&
              std::abs(restoredCapture.points[2].levelDb -
                       capture.points[2].levelDb) < 1.0e-9,
          "Spectrum capture did not round-trip in memory");
  require(tonetrace::serializeSpectrumCapture(restoredCapture) == captureText,
          "In-memory spectrum serialization is not deterministic");
  requireThrows(
      [] {
        (void)tonetrace::deserializeSpectrumCapture(
            "ToneTraceSpectrum 1\nrate 48000\nfft 4096\nframes 42\n"
            "confidence 0.9\npoints 3\n20 0 1 0\n100 0 1 0\n20 0 1 0\n");
      },
      "An in-memory capture with duplicate points was accepted");

  auto model = coloringModel();
  const auto modelPath = temporary / "model-memory-roundtrip.ttm";
  model.save(modelPath);
  const auto modelText = tonetrace::serializeCorrectionModel(model);
  std::ifstream modelFile(modelPath);
  std::stringstream modelFileContents;
  modelFileContents << modelFile.rdbuf();
  require(modelFileContents.str() == modelText,
          "In-memory model serialization differs from the file format");
  const auto restoredModel = tonetrace::deserializeCorrectionModel(modelText);
  require(restoredModel.mode == model.mode &&
              restoredModel.resolution == model.resolution &&
              restoredModel.nodes.size() == model.nodes.size() &&
              std::abs(restoredModel.gainDbAt(5200.0) -
                       model.gainDbAt(5200.0)) < 1.0e-9,
          "Correction model did not round-trip in memory");
  require(tonetrace::serializeCorrectionModel(restoredModel) == modelText,
          "In-memory model serialization is not deterministic");
  requireThrows(
      [] {
        (void)tonetrace::deserializeCorrectionModel(
            "ToneTraceModel 1\nmode FullMix\nrange 10 30000\n"
            "resolution 30\nnodes 3\n20 0 1\n1000 0 1\n20 0 1\n");
      },
      "An in-memory model with duplicate nodes was accepted");

  std::cout << "in-memory capture and model serialization: passed\n";
}

tonetrace::SpectrumCapture regionalCapture(const std::array<double, 7>& levels) {
  tonetrace::SpectrumCapture capture;
  capture.sampleRate = 48000;
  capture.fftSize = 4096;
  capture.acceptedFrames = 100;
  capture.confidence = 0.95;
  const auto& bands = tonetrace::curveBands();
  for (double hz = 20.0; hz < 20000.0; hz *= 1.02) {
    std::size_t bandIndex = bands.size() - 1;
    for (std::size_t index = 0; index < bands.size(); ++index) {
      if (hz >= bands[index].lowHz && hz < bands[index].highHz) {
        bandIndex = index;
        break;
      }
    }
    capture.points.push_back({hz, levels[bandIndex], 0.95, 1.0});
  }
  return capture;
}

bool containsText(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

void testCurveDescription() {
  tonetrace::ProfileSnapshot snapshot;
  const std::string empty = tonetrace::curveDescriptionText(snapshot);
  const std::string expectedEmpty =
      "Tone Trace summary\n"
      "No learned match yet. Capture Reference to begin. Manual band EQ remains available on the Bands pages.\n\n"
      "Reference\nNo capture yet.\n\n"
      "Target\nNo capture yet.\n\n"
      "Correction\nNo correction has been computed yet.\n";
  require(empty == expectedEmpty,
          "Empty Curve Description no longer has one engine-owned document");

  const auto highCut = regionalCapture({0, 0, 0, 0, 0, -6, -6});
  const std::string highCutText = tonetrace::describeCapture(highCut);
  require(containsText(highCutText, "quieter above 6 kHz") &&
              !containsText(highCutText, "Most prominent") &&
              !containsText(highCutText, "bass") &&
              !containsText(highCutText, "60-250"),
          "High-frequency cut invented a complementary bass peak or old wording: " +
              highCutText);

  const auto lowShelf = regionalCapture({6, 6, 6, 6, 0, 0, 0});
  const std::string lowShelfText = tonetrace::describeCapture(lowShelf);
  require(containsText(lowShelfText, "louder below 2 kHz") &&
              !containsText(lowShelfText, "Most prominent"),
          "Broad four-region low shelf was not described as one shelf: " +
              lowShelfText);

  const auto highShelf = regionalCapture({0, 0, 0, 6, 6, 6, 6});
  const std::string highShelfText = tonetrace::describeCapture(highShelf);
  require(containsText(highShelfText, "louder above 500 Hz"),
          "Broad high shelf was not described as one high feature: " +
              highShelfText);

  const auto midBump = regionalCapture({0, 0, 6, 6, 6, 0, 0});
  const std::string midBumpText = tonetrace::describeCapture(midBump);
  require(containsText(midBumpText, "peak") &&
              containsText(midBumpText, "low mid") &&
              !containsText(midBumpText, "below 250 Hz"),
          "Broad mid bump was misclassified: " + midBumpText);

  const auto presenceBell = regionalCapture({0, 0, 0, 0, 6, 0, 0});
  const std::string bellText = tonetrace::describeCapture(presenceBell);
  require(containsText(bellText, "6 dB peak") &&
              containsText(bellText, "presence (2 to 6 kHz)") &&
              !containsText(bellText, "tilts"),
          "Presence bell description is not a local peak: " + bellText);

  const auto notch = regionalCapture({0, 0, 0, -6, 0, 0, 0});
  const std::string notchText = tonetrace::describeCapture(notch);
  require(containsText(notchText, "6 dB dip") &&
              containsText(notchText, "mid (500 Hz to 2 kHz)") &&
              !containsText(notchText, "smile"),
          "Mid notch description is not a local dip: " + notchText);

  std::array<double, 7> tiltLevels{};
  const auto& bands = tonetrace::curveBands();
  const double firstX = std::log(std::sqrt(bands.front().lowHz * bands.front().highHz));
  const double lastX = std::log(std::sqrt(bands.back().lowHz * bands.back().highHz));
  for (std::size_t index = 0; index < bands.size(); ++index) {
    const double x = std::log(std::sqrt(bands[index].lowHz * bands[index].highHz));
    const double fraction = (x - firstX) / (lastX - firstX);
    tiltLevels[index] = 6.0 - 12.0 * fraction;
  }
  const std::string tiltText =
      tonetrace::describeCapture(regionalCapture(tiltLevels));
  require(containsText(tiltText, "tilts down") &&
              !containsText(tiltText, "peak") && !containsText(tiltText, "dip"),
          "Smooth slope was not classified as one tilt: " + tiltText);

  const std::string stepText = tonetrace::describeCapture(
      regionalCapture({6, 6, 6, 6, -6, -6, -6}));
  require(containsText(stepText, "below 2 kHz") &&
              !containsText(stepText, "tilts"),
          "Hard step was incorrectly softened into a tilt: " + stepText);

  const std::string smileText = tonetrace::describeCapture(
      regionalCapture({6, 6, 0, -4, 0, 6, 6}));
  require(containsText(smileText, "smile") &&
              containsText(smileText, "dip"),
          "Smile shape lost one of its outer lobes: " + smileText);

  const std::string frownText = tonetrace::describeCapture(
      regionalCapture({-6, -6, 0, 4, 0, -6, -6}));
  require(containsText(frownText, "frown") &&
              containsText(frownText, "peak"),
          "Frown shape lost one of its outer lobes: " + frownText);

  const std::string outlierText = tonetrace::describeCapture(
      regionalCapture({0, 0, 0, 0, 3, 0, 0}));
  require(containsText(outlierText, "peak") &&
              !containsText(outlierText, "tilts") &&
              !containsText(outlierText, "below"),
          "Single outlier was misclassified as a broad feature: " + outlierText);

  std::cout << "curve description shape classifier: passed\n";
}

void testDescriptionHonesty() {
  tonetrace::ProfileSnapshot snapshot;
  snapshot.reference = regionalCapture({0, 0, 0, 0, 0, 0, 0});
  snapshot.target = regionalCapture({-3, -3, -3, -3, 0, 0, 0});
  const auto delta = tonetrace::describeToneTrace(snapshot);
  require(containsText(delta.summary, "Target is 3 dB quieter than Reference below 2 kHz"),
          "Target-vs-Reference broad low difference disappeared: " + delta.summary);

  snapshot.reference = regionalCapture({6, 6, 0, 0, 0, 0, 0});
  snapshot.target = regionalCapture({6, 6, 0, 0, 0, 0, 0});
  require(containsText(tonetrace::describeToneTrace(snapshot).summary,
                       "similar tonal shape"),
          "Equal bass-heavy captures were not described as a similar shape");

  tonetrace::CorrectionModel constant;
  constant.mode = tonetrace::MatchMode::FullMix;
  constant.analysisLowHz = 20.0;
  constant.analysisHighHz = 20000.0;
  constant.resolution = 30;
  constant.nodes = {{20.0, 10.0, 1.0}, {20000.0, 10.0, 1.0}};
  tonetrace::IrRenderSettings halfStrength;
  halfStrength.sampleRate = 48000;
  halfStrength.correctionStrength = 0.5;
  const auto expectedHalf = tonetrace::evaluateCorrectionAt(
      constant, 18.0, halfStrength, 1000.0);
  const std::string halfText =
      tonetrace::describeCorrection(constant, 18.0, halfStrength);
  require(std::abs(expectedHalf.tonalDb - 5.0) < 1.0e-9 &&
              containsText(halfText, "5 dB") &&
              !containsText(halfText, "10 dB"),
          "Correction description ignored applied Strength: " + halfText);

  tonetrace::CorrectionModel peaked;
  peaked.analysisLowHz = 20.0;
  peaked.analysisHighHz = 20000.0;
  peaked.nodes = {{100.0, 0.0, 1.0}, {2000.0, 10.0, 1.0},
                  {10000.0, 0.0, 1.0}};
  tonetrace::IrRenderSettings qOne;
  qOne.sampleRate = 48000;
  qOne.correctionSharpness = 1.0;
  tonetrace::IrRenderSettings qFocused = qOne;
  qFocused.correctionSharpness = 1.5;
  const auto qOneApplied = tonetrace::evaluateCorrectionAt(
      peaked, 18.0, qOne, 2000.0);
  const auto qFocusedApplied = tonetrace::evaluateCorrectionAt(
      peaked, 18.0, qFocused, 2000.0);
  const std::string qOneText = tonetrace::describeCorrection(peaked, 18.0, qOne);
  const std::string qFocusedText =
      tonetrace::describeCorrection(peaked, 18.0, qFocused);
  if (std::abs(qOneApplied.tonalDb - qFocusedApplied.tonalDb) >= 0.75) {
    require(qOneText != qFocusedText,
            "Correction Q changed the applied curve but not its description");
  }

  tonetrace::CorrectionModel flat;
  flat.analysisLowHz = 20.0;
  flat.analysisHighHz = 20000.0;
  flat.nodes = {{20.0, 0.0, 1.0}, {20000.0, 0.0, 1.0}};
  tonetrace::IrRenderSettings manual;
  manual.sampleRate = 48000;
  manual.manualGains.assign(10, 0.0);
  const double gridLow = 20.0;
  const double gridHigh = 20000.0;
  const double targetLog = std::log(100.0 / gridLow) / std::log(gridHigh / gridLow);
  const std::size_t manualIndex = static_cast<std::size_t>(
      std::lround(targetLog * static_cast<double>(manual.manualGains.size() - 1)));
  manual.manualGains[manualIndex] = 6.0;
  const std::string manualText = tonetrace::describeCorrection(flat, 18.0, manual);
  require(!containsText(manualText, "gentle across the whole range") &&
              (containsText(manualText, "bass") || containsText(manualText, "below")),
          "Manual trim did not reach the applied Correction description: " +
              manualText);

  tonetrace::IrRenderSettings partial;
  partial.sampleRate = 48000;
  partial.rangeLowHz = 60.0;
  partial.rangeHighHz = 16000.0;
  tonetrace::CorrectionModel sloped;
  sloped.analysisLowHz = 20.0;
  sloped.analysisHighHz = 20000.0;
  sloped.nodes = {{60.0, 6.0, 1.0}, {16000.0, -6.0, 1.0}};
  const std::string partialText =
      tonetrace::describeCorrection(sloped, 18.0, partial);
  require(!containsText(partialText, "sub (20 to 60 Hz)") &&
              containsText(partialText, "brilliance from 12 kHz to 16 kHz") &&
              !containsText(partialText, "brilliance (12 to 20 kHz)") &&
              !containsText(partialText, "60-250"),
          "Partial Correction range was described as full named regions: " +
              partialText);

  tonetrace::ProfileSnapshot referenceOnly;
  referenceOnly.reference = regionalCapture({0, 3, 0, 0, 0, 0, 0});
  require(tonetrace::describeToneTrace(referenceOnly).summary ==
              "Reference captured; target not yet learned.",
          "Reference-only summary changed its workflow meaning");

  std::cout << "description applied-curve honesty: passed\n";
}

void testDescriptionCeilingHonesty() {
  tonetrace::CorrectionModel model;
  model.mode = tonetrace::MatchMode::FullMix;
  model.analysisLowHz = 20.0;
  model.analysisHighHz = 20000.0;
  model.resolution = 60;
  model.nodes = {{20.0, 30.0, 1.0}, {20000.0, 30.0, 1.0}};
  tonetrace::IrRenderSettings settings;
  settings.sampleRate = 48000;

  const std::string capped = tonetrace::describeCorrection(model, 18.0, settings);
  require(containsText(capped, "18 dB") &&
              containsText(capped,
                           "Maximum Correction clipped the learned curve from 30 dB to 18 dB before Strength, Q, and manual trims.") &&
              !containsText(capped, "limit the applied correction") &&
              !containsText(capped, "60-250"),
          "Maximum Correction sentence mixed model ceiling with applied tonal gain: " +
              capped);

  tonetrace::CorrectionModel under = model;
  under.nodes = {{20.0, 10.0, 1.0}, {20000.0, 10.0, 1.0}};
  const std::string underText =
      tonetrace::describeCorrection(under, 18.0, settings);
  require(!containsText(underText, "Maximum Correction clipped"),
          "Under-ceiling model falsely claimed Maximum Correction clipping");

  tonetrace::IrRenderSettings halfStrength = settings;
  halfStrength.correctionStrength = 0.5;
  const std::string cappedHalf =
      tonetrace::describeCorrection(model, 18.0, halfStrength);
  require(containsText(cappedHalf, "9 dB") &&
              containsText(cappedHalf, "from 30 dB to 18 dB"),
          "Strength and Maximum Correction stages were conflated: " + cappedHalf);

  std::cout << "description ceiling honesty: passed\n";
}

}  // namespace

void testManualGainFold() {
  const int sampleRate = 48000;
  tonetrace::IrRenderSettings baseline;
  baseline.sampleRate = sampleRate;
  const auto plain = tonetrace::renderMinimumPhaseIr(coloringModel(), baseline);

  // A flat +6 dB manual trim shifts the whole response up.
  tonetrace::IrRenderSettings trimmed = baseline;
  trimmed.manualGains = {6.0, 6.0};
  const auto flat = tonetrace::renderMinimumPhaseIr(coloringModel(), trimmed);
  for (const double frequency : {120.0, 520.0, 1200.0, 8000.0}) {
    require(std::abs(irGainDbAt(flat, sampleRate, frequency) -
                     irGainDbAt(plain, sampleRate, frequency) - 6.0) < 0.6,
            "A flat manual trim was not added across the spectrum");
  }

  // Edge trim: only the low-frequency band is boosted, the high end is not.
  tonetrace::IrRenderSettings lowTrim = baseline;
  lowTrim.manualGains = {10.0, 0.0};
  const auto lowOnly = tonetrace::renderMinimumPhaseIr(coloringModel(), lowTrim);
  require(irGainDbAt(lowOnly, sampleRate, 60.0) >
              irGainDbAt(plain, sampleRate, 60.0) + 5.0,
          "A low-band manual trim was not audible");
  require(std::abs(irGainDbAt(lowOnly, sampleRate, 15000.0) -
                   irGainDbAt(plain, sampleRate, 15000.0)) < 0.8,
          "A low-band manual trim leaked into the high end");

  // Correction Range is a non-destructive mask over a stable manual band
  // grid. Both ends must suppress manual correction outside the active range
  // without remapping the stored band values.
  tonetrace::IrRenderSettings maskedBase = baseline;
  maskedBase.rangeLowHz = 900.0;
  maskedBase.rangeHighHz = 2000.0;
  const auto maskedPlain =
      tonetrace::renderMinimumPhaseIr(coloringModel(), maskedBase);
  tonetrace::IrRenderSettings maskedTrim = maskedBase;
  maskedTrim.manualGains = {6.0, 6.0};
  const auto maskedWithTrim =
      tonetrace::renderMinimumPhaseIr(coloringModel(), maskedTrim);
  require(std::abs(irGainDbAt(maskedWithTrim, sampleRate, 100.0) -
                   irGainDbAt(maskedPlain, sampleRate, 100.0)) < 0.4,
          "Correction Range Low did not mask manual trim below the range");
  require(std::abs(irGainDbAt(maskedWithTrim, sampleRate, 5000.0) -
                   irGainDbAt(maskedPlain, sampleRate, 5000.0)) < 0.4,
          "Correction Range High did not mask manual trim above the range");
  require(irGainDbAt(maskedWithTrim, sampleRate, 1200.0) >
              irGainDbAt(maskedPlain, sampleRate, 1200.0) + 5.0,
          "Manual trim inside the active correction range was suppressed");

  tonetrace::IrRenderSettings invalid = baseline;
  invalid.manualGains = {0.0, 130.0};
  requireThrows(
      [&] { (void)tonetrace::renderMinimumPhaseIr(coloringModel(), invalid); },
      "An out-of-range manual gain was accepted");
  invalid.manualGains = {0.0, std::numeric_limits<double>::quiet_NaN()};
  requireThrows(
      [&] { (void)tonetrace::renderMinimumPhaseIr(coloringModel(), invalid); },
      "A non-finite manual gain was accepted");
std::cout << "per-band manual gain fold and validation: passed\n";
}

void testImportCompatibility() {
  tonetrace::SpectrumCapture lowCapture;
  lowCapture.sampleRate = 48000;
  lowCapture.fftSize = 4096;
  lowCapture.acceptedFrames = 42;
  lowCapture.confidence = 0.9;
  for (double hz = 20.0; hz <= 20000.0; hz *= 1.5) {
    lowCapture.points.push_back({hz, 0.0, 1.0, 0.0});
  }

  tonetrace::SpectrumCapture highCapture = lowCapture;
  highCapture.points.clear();
  for (double hz : {25000.0, 27000.0, 29000.0, 30000.0}) {
    highCapture.points.push_back({hz, 0.0, 1.0, 0.0});
  }
  require(highCapture.points.size() >= 3, "High capture has too few points");

  const auto compatible = tonetrace::assessCaptureImport(
      lowCapture, 20.0, 20000.0, 48000.0);
  require(compatible.usable && !compatible.truncatedByNyquist,
          "A same-rate in-range capture was judged unusable");

  const auto truncated = tonetrace::assessCaptureImport(
      lowCapture, 20.0, 20000.0, 22050.0);
  require(truncated.usable,
          "A capture below the new project Nyquist was judged unusable");

  const auto aboveNyquist = tonetrace::assessCaptureImport(
      highCapture, 20.0, 20000.0, 44100.0);
  require(!aboveNyquist.usable,
          "A capture wholly above the project Nyquist was judged usable");

  const auto crossRate = tonetrace::assessCaptureImport(
      highCapture, 20000.0, 30000.0, 96000.0);
  require(crossRate.usable,
          "A capture re-applied in a higher-rate session was judged unusable");

  tonetrace::CorrectionModel model;
  model.resolution = 60;
  model.mode = tonetrace::MatchMode::FullMix;
  model.analysisLowHz = 10.0;
  model.analysisHighHz = 30000.0;
  model.nodes = {{20.0, 0.0, 1.0}, {1000.0, 0.0, 1.0}, {20000.0, 0.0, 1.0}};

  const auto modelOk = tonetrace::assessModelImport(model, 10.0, 20000.0, 48000.0);
  require(modelOk.usable,
          "A model overlapping the project range was judged unusable");
  require(modelOk.truncatedByNyquist,
          "A model reaching above the project Nyquist was not flagged");

  tonetrace::CorrectionModel highModel = model;
  highModel.analysisLowHz = 25000.0;
  highModel.analysisHighHz = 30000.0;
  highModel.nodes = {{25000.0, 0.0, 1.0}, {28000.0, 0.0, 1.0}, {30000.0, 0.0, 1.0}};
  const auto modelAbove = tonetrace::assessModelImport(
      highModel, 10.0, 20000.0, 44100.0);
  require(!modelAbove.usable,
          "A model wholly above the project Nyquist was judged usable");

  std::cout << "import cross-sample-rate compatibility: passed\n";
}

void testCorrectionBreakdown() {
  tonetrace::CorrectionModel model;
  model.mode = tonetrace::MatchMode::FullMix;
  model.analysisLowHz = 20.0;
  model.analysisHighHz = 20000.0;
  model.resolution = 3;
  model.nodes = {
      {20.0, 30.0, 1.0},
      {200.0, 30.0, 1.0},
      {20000.0, 30.0, 1.0},
  };

  tonetrace::IrRenderSettings settings;
  settings.sampleRate = 48000;
  settings.correctionStrength = 0.5;
  settings.correctionSharpness = 1.0;
  settings.correctionGainDb = -3.0;
  settings.rangeLowHz = 60.0;
  settings.rangeHighHz = 12000.0;
  settings.manualGains = {2.0, 2.0, 2.0};

  const auto inside =
      tonetrace::evaluateCorrectionAt(model, 18.0, settings, 200.0);
  require(inside.inRange, "Correction evaluator rejected an in-range band");
  require(std::abs(inside.automaticDb - 9.0) < 1.0e-12,
          "Correction evaluator did not apply ceiling then Strength");
  require(std::abs(inside.manualDb - 2.0) < 1.0e-12 &&
              std::abs(inside.tonalDb - 11.0) < 1.0e-12 &&
              std::abs(inside.outputDb - 8.0) < 1.0e-12,
          "Correction evaluator breakdown disagrees with renderer semantics");

  const auto below =
      tonetrace::evaluateCorrectionAt(model, 18.0, settings, 40.0);
  const auto above =
      tonetrace::evaluateCorrectionAt(model, 18.0, settings, 16000.0);
  require(!below.inRange && !above.inRange &&
              below.automaticDb == 0.0 && below.manualDb == 0.0 &&
              below.tonalDb == 0.0 && below.outputDb == -3.0 &&
              above.tonalDb == 0.0 && above.outputDb == -3.0,
          "Low/High masks were not symmetric in correction evaluation");
  std::cout << "shared correction evaluation and range masking: passed\n";
}

void testResolutionIndependence() {
  require(tonetrace::MatchSettings{}.resolution == 30,
          "The default band resolution is not 30");

  const auto reference = makeNoise(48000, 0.8);
  const auto target = makeNoise(48000, 0.8);
  tonetrace::MatchEngine engine;

  tonetrace::MatchSettings low;
  low.mode = tonetrace::MatchMode::FullMix;
  low.resolution = 30;
  tonetrace::MatchSettings high = low;
  high.resolution = 120;

  const auto lowReferenceCapture = engine.capture(reference, low);
  const auto highReferenceCapture = engine.capture(reference, high);
  const auto lowTargetCapture = engine.capture(target, low);
  const auto highTargetCapture = engine.capture(target, high);
  require(lowReferenceCapture.points.size() == highReferenceCapture.points.size(),
          "Capture resolution changed with the band count");
  require(lowTargetCapture.points.size() == highTargetCapture.points.size(),
          "Target capture resolution changed with the band count");
  for (std::size_t i = 0; i < lowReferenceCapture.points.size(); ++i) {
    require(std::abs(lowReferenceCapture.points[i].frequencyHz -
                     highReferenceCapture.points[i].frequencyHz) < 1.0e-9,
            "Capture grid depends on the band count");
    require(std::abs(lowReferenceCapture.points[i].levelDb -
                     highReferenceCapture.points[i].levelDb) < 1.0e-9,
            "Capture analysis depends on the band count");
  }

  const auto lowModel = engine.match(lowReferenceCapture, lowTargetCapture, low);
  const auto highModel =
      engine.match(highReferenceCapture, highTargetCapture, high);
  require(lowModel.nodes.size() == highModel.nodes.size(),
          "Match output resolution changed with the band count");
  for (std::size_t i = 0; i < lowModel.nodes.size(); ++i) {
    require(std::abs(lowModel.nodes[i].gainDb - highModel.nodes[i].gainDb) <
                1.0e-9,
            "Match curve depends on the band count");
  }
  require(lowModel.resolution == 30 && highModel.resolution == 120,
          "The band count was not recorded on the model");
  std::cout << "full-resolution analysis independent of the band count: passed\n";
}

void testManualGainResampling() {
  const std::vector<double> original{-6.0, 0.0, 6.0};
  const auto expanded =
      tonetrace::resampleManualGains(original, 5, 20.0, 20000.0);
  require(expanded.size() == 5, "Manual gain resampling returned the wrong size");
  require(std::abs(expanded[0] + 6.0) < 1.0e-12 &&
              std::abs(expanded[2]) < 1.0e-12 &&
              std::abs(expanded[4] - 6.0) < 1.0e-12,
          "Manual gain resampling moved existing frequency anchors");
  const auto collapsed =
      tonetrace::resampleManualGains(expanded, 3, 20.0, 20000.0);
  require(collapsed.size() == original.size(),
          "Manual gain downsampling returned the wrong size");
  for (std::size_t i = 0; i < original.size(); ++i) {
    require(std::abs(collapsed[i] - original[i]) < 1.0e-12,
            "Manual gain resolution round-trip moved a trim in frequency");
  }
  const auto empty = tonetrace::resampleManualGains({}, 7, 20.0, 20000.0);
  require(empty.size() == 7 &&
              std::all_of(empty.begin(), empty.end(),
                          [](double value) { return value == 0.0; }),
          "Empty manual gain curve did not remain flat when resized");
  std::cout << "frequency-preserving manual gain resampling: passed\n";
}

tonetrace::SpectrumCapture syntheticVoiceSpectrum(bool reference, bool rough,
                                                      int sampleRate = 48000) {
  tonetrace::SpectrumCapture capture;
  capture.sampleRate = sampleRate;
  capture.fftSize = 4096;
  capture.acceptedFrames = 100;
  capture.confidence = 1.0;
  constexpr int pointCount = 320;
  const double low = std::log(40.0);
  const double high = std::log(18000.0);
  capture.points.reserve(pointCount);
  for (int i = 0; i < pointCount; ++i) {
    const double ratio = static_cast<double>(i) / (pointCount - 1);
    const double frequency = std::exp(low + (high - low) * ratio);
    double level = 0.0;
    if (reference) {
      level = 3.0 * (ratio - 0.5);
      if (rough) {
        level += 5.5 * std::sin(i * 0.43) + 2.5 * std::sin(i * 0.91);
      }
    }
    capture.points.push_back({frequency, level, 1.0, 0.0});
  }
  return capture;
}

tonetrace::SpectrumCapture syntheticEarlyRingVoiceSpectrum(
    bool reference, int sampleRate = 48000) {
  tonetrace::SpectrumCapture capture;
  capture.sampleRate = sampleRate;
  capture.fftSize = 4096;
  capture.acceptedFrames = 100;
  capture.confidence = 1.0;
  constexpr int pointCount = 320;
  const double low = std::log(40.0);
  const double high = std::log(18000.0);
  capture.points.reserve(pointCount);
  for (int i = 0; i < pointCount; ++i) {
    const double ratio = static_cast<double>(i) / (pointCount - 1);
    const double frequency = std::exp(low + (high - low) * ratio);
    double level = 0.0;
    if (reference) {
      // This moderate, regularly spaced detail creates excess energy mainly
      // within the first 1-2 ms. The established 5/10 ms checks alone allow
      // too much of it, so it is the compact-ringing regression fixture.
      level = 3.0 * (ratio - 0.5) + 2.0 * std::sin(i * 0.4);
    }
    capture.points.push_back({frequency, level, 1.0, 0.0});
  }
  return capture;
}

double modelTotalVariation(const tonetrace::CorrectionModel& model) {
  double result = 0.0;
  for (std::size_t i = 1; i < model.nodes.size(); ++i) {
    result += std::abs(model.nodes[i].gainDb - model.nodes[i - 1].gainDb);
  }
  return result;
}

void testVoiceSafetyFallback() {
  tonetrace::MatchEngine engine;
  tonetrace::MatchSettings settings;
  settings.mode = tonetrace::MatchMode::Voice;
  settings.maximumCorrectionDb = 18.0;
  settings.removeBroadLevelDifference = false;
  auto legacySettings = settings;
  legacySettings.enableVoiceSafety = false;

  constexpr std::array<int, 6> sampleRates{
      44100, 48000, 88200, 96000, 176400, 192000};
  std::vector<double> safeRoughReference;
  for (int sampleRate : sampleRates) {
    const auto smoothReference =
        syntheticVoiceSpectrum(true, false, sampleRate);
    const auto smoothTarget =
        syntheticVoiceSpectrum(false, false, sampleRate);
    const auto safeSmooth = engine.match(smoothReference, smoothTarget, settings);
    const auto legacySmooth =
        engine.match(smoothReference, smoothTarget, legacySettings);
    require(safeSmooth.nodes.size() == legacySmooth.nodes.size(),
            "Voice safety changed a benign model's node count");
    for (std::size_t i = 0; i < safeSmooth.nodes.size(); ++i) {
      require(safeSmooth.nodes[i].gainDb == legacySmooth.nodes[i].gainDb,
              "Voice safety altered an already-safe full-detail match");
    }

    const auto roughReference =
        syntheticVoiceSpectrum(true, true, sampleRate);
    const auto roughTarget =
        syntheticVoiceSpectrum(false, true, sampleRate);
    const auto safeRough = engine.match(roughReference, roughTarget, settings);
    const auto legacyRough =
        engine.match(roughReference, roughTarget, legacySettings);
    require(modelTotalVariation(safeRough) <
                modelTotalVariation(legacyRough) * 0.75,
            "Voice safety did not withdraw suspicious narrow detail");

    // This severe fixture deliberately reaches the IR-tail stage. Once the
    // original full-detail curve has failed the roughness gate, becoming
    // smoother alone must not let an intermediate candidate bypass either the
    // early or late tail checks. Its only safe result is the broad fallback.
    auto expectedSafeSettings = settings;
    expectedSafeSettings.enableVoiceSafety = false;
    expectedSafeSettings.voiceDetailScale = 0.0;
    const auto expectedSafe =
        engine.match(roughReference, roughTarget, expectedSafeSettings);
    require(expectedSafe.nodes.size() == safeRough.nodes.size(),
            "Voice safety strict-tail fixture changed node count");
    double strictTailResidual = 0.0;
    for (std::size_t i = 0; i < safeRough.nodes.size(); ++i) {
      strictTailResidual = std::max(
          strictTailResidual,
          std::abs(safeRough.nodes[i].gainDb - expectedSafe.nodes[i].gainDb));
    }
    require(strictTailResidual < 1.0e-10,
            "Voice safety allowed a smoother but IR-unsafe intermediate candidate");

    // A less severe fixture concentrates its suspicious energy before 5 ms.
    // Preserve the highest detail candidate that passes the new 1/2 ms checks:
    // 25% remains unsafe, while 10% is accepted at both diagnostic rates.
    const auto earlyReference =
        syntheticEarlyRingVoiceSpectrum(true, sampleRate);
    const auto earlyTarget =
        syntheticEarlyRingVoiceSpectrum(false, sampleRate);
    const auto safeEarly = engine.match(earlyReference, earlyTarget, settings);
    auto expectedEarlySettings = settings;
    expectedEarlySettings.enableVoiceSafety = false;
    expectedEarlySettings.voiceDetailScale = 0.10;
    const auto expectedEarly =
        engine.match(earlyReference, earlyTarget, expectedEarlySettings);
    require(safeEarly.nodes.size() == expectedEarly.nodes.size(),
            "Voice early-tail fixture changed node count");
    double earlyTailResidual = 0.0;
    for (std::size_t i = 0; i < safeEarly.nodes.size(); ++i) {
      earlyTailResidual = std::max(
          earlyTailResidual,
          std::abs(safeEarly.nodes[i].gainDb - expectedEarly.nodes[i].gainDb));
    }
    require(earlyTailResidual < 1.0e-10,
            "Voice safety did not suppress compact 1-2 ms ringing detail");

    const auto safeEarlyInverse =
        engine.match(earlyTarget, earlyReference, settings);
    require(safeEarlyInverse.nodes.size() == safeEarly.nodes.size(),
            "Voice early-tail inverse uses a different node count");
    double earlyInverseResidual = 0.0;
    for (std::size_t i = 0; i < safeEarly.nodes.size(); ++i) {
      earlyInverseResidual = std::max(
          earlyInverseResidual,
          std::abs(safeEarly.nodes[i].gainDb + safeEarlyInverse.nodes[i].gainDb));
    }
    require(earlyInverseResidual < 1.0e-10,
            "Voice early-tail safety broke Reference/Target sign symmetry");

    const auto safeInverse = engine.match(roughTarget, roughReference, settings);
    require(safeInverse.nodes.size() == safeRough.nodes.size(),
            "Voice safety inverse uses a different node count");
    double inverseResidual = 0.0;
    for (std::size_t i = 0; i < safeRough.nodes.size(); ++i) {
      inverseResidual = std::max(
          inverseResidual,
          std::abs(safeRough.nodes[i].gainDb + safeInverse.nodes[i].gainDb));
    }
    require(inverseResidual < 1.0e-10,
            "Voice safety broke Reference/Target sign symmetry");

    // The IR test is expressed in milliseconds, so the safety decision should
    // not materially change merely because the host runs at another common
    // production rate. Compare the chosen correction curve against 44.1 kHz.
    if (safeRoughReference.empty()) {
      safeRoughReference.reserve(safeRough.nodes.size());
      for (const auto& node : safeRough.nodes) {
        safeRoughReference.push_back(node.gainDb);
      }
    } else {
      require(safeRoughReference.size() == safeRough.nodes.size(),
              "Voice safety changed node count across sample rates");
      double rateResidual = 0.0;
      for (std::size_t i = 0; i < safeRough.nodes.size(); ++i) {
        rateResidual = std::max(
            rateResidual,
            std::abs(safeRoughReference[i] - safeRough.nodes[i].gainDb));
      }
      require(rateResidual < 1.0e-10,
              "Voice safety decision changed across supported sample rates");
    }
  }
  std::cout << "Voice IR/roughness safety fallback at six rates: passed\n";
}

int main() {
  try {
    testIdentity();
    testConfigurableCorrectionCeiling();
    testRecovery();
    testSourceResonanceControl();
    testPersistenceAndSampleRates();
    testCorrectionSharpness();
    testExportedIrConvolutionEquivalence();
    testWavRoundTrip();
    testSilenceAndBufferValidation();
    testExtensibleWavAndBasenameOutputs();
    testGlobalCorrectionGainAndModelValidation();
    testSpectrumCaptureSerialization();
    testInMemorySerialization();
    testManualGainFold();
    testCorrectionBreakdown();
    testImportCompatibility();
    testResolutionIndependence();
    testManualGainResampling();
    testVoiceSafetyFallback();
    testCurveDescription();
    testDescriptionHonesty();
    testDescriptionCeilingHonesty();
    std::cout << "all Tone Trace clean-engine tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "TEST FAILURE: " << error.what() << '\n';
    return 1;
  }
}
