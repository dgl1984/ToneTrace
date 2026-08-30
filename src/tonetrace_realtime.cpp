#include "tonetrace/tonetrace_realtime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tonetrace {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr std::size_t kMaximumStatePoints = 100000;
constexpr std::size_t kMaximumRealtimeIrFrames = 1000000;

void validateRealtimeKernel(const std::vector<double>& ir) {
  if (ir.empty() || ir.size() > kMaximumRealtimeIrFrames) {
    throw std::runtime_error("Realtime IR has an invalid length");
  }
  if (!std::all_of(ir.begin(), ir.end(),
                   [](double value) { return std::isfinite(value); })) {
    throw std::runtime_error("Realtime IR contains a non-finite coefficient");
  }
}

class PartitionedTier {
 public:
  PartitionedTier(std::size_t partitionFrames,
                  const std::vector<double>& ir,
                  std::size_t begin,
                  std::size_t end,
                  std::size_t channels)
      : partitionFrames_(partitionFrames), fftSize_(partitionFrames * 2U) {
    initFftTables();
    if (begin >= end || begin >= ir.size()) return;
    end = std::min(end, ir.size());
    const std::size_t coefficientCount = end - begin;
    const std::size_t partitionCount =
        (coefficientCount + partitionFrames_ - 1U) / partitionFrames_;
    spectra_.assign(partitionCount,
                    std::vector<std::complex<double>>(fftSize_, {0.0, 0.0}));
    for (std::size_t partition = 0; partition < partitionCount; ++partition) {
      const std::size_t source = begin + partition * partitionFrames_;
      const std::size_t count = std::min(partitionFrames_, end - source);
      for (std::size_t i = 0; i < count; ++i) {
        spectra_[partition][i] = ir[source + i];
      }
      performFft(spectra_[partition], false);
    }
    states_.reserve(channels);
    for (std::size_t channel = 0; channel < channels; ++channel) {
      states_.emplace_back(partitionFrames_, fftSize_, partitionCount);
    }
  }

  [[nodiscard]] double processSample(std::size_t channel,
                                     double input) noexcept {
    if (spectra_.empty()) return 0.0;
    auto& state = states_[channel];
    const double output = state.output[state.position];
    state.input[state.position] = input;
    ++state.position;
    if (state.position == partitionFrames_) calculateNextBlock(state);
    return output;
  }

  void reset() noexcept {
    for (auto& state : states_) state.clear();
  }

 private:
  struct ChannelState {
    ChannelState(std::size_t partitionFrames,
                 std::size_t fftSize,
                 std::size_t partitionCount)
        : input(partitionFrames, 0.0),
          output(partitionFrames, 0.0),
          nextOutput(partitionFrames, 0.0),
          overlap(partitionFrames, 0.0),
          transform(fftSize, {0.0, 0.0}),
          sum(fftSize, {0.0, 0.0}),
          history(partitionCount,
                  std::vector<std::complex<double>>(fftSize, {0.0, 0.0})) {}

    void clear() noexcept {
      std::fill(input.begin(), input.end(), 0.0);
      std::fill(output.begin(), output.end(), 0.0);
      std::fill(nextOutput.begin(), nextOutput.end(), 0.0);
      std::fill(overlap.begin(), overlap.end(), 0.0);
      std::fill(transform.begin(), transform.end(),
                std::complex<double>(0.0, 0.0));
      std::fill(sum.begin(), sum.end(), std::complex<double>(0.0, 0.0));
      for (auto& block : history) {
        std::fill(block.begin(), block.end(), std::complex<double>(0.0, 0.0));
      }
      position = 0;
      historyWrite = 0;
    }

    std::vector<double> input;
    std::vector<double> output;
    std::vector<double> nextOutput;
    std::vector<double> overlap;
    std::vector<std::complex<double>> transform;
    std::vector<std::complex<double>> sum;
    std::vector<std::vector<std::complex<double>>> history;
    std::size_t position = 0;
    std::size_t historyWrite = 0;
  };

  void initFftTables() {
    if (fftSize_ == 0 || (fftSize_ & (fftSize_ - 1U)) != 0) return;
    bitReversal_.resize(fftSize_);
    for (std::size_t i = 0; i < fftSize_; ++i) {
      std::size_t j = 0;
      for (std::size_t bit = fftSize_ >> 1U, src = i; bit > 0; bit >>= 1U, src >>= 1U) {
        if (src & 1U) j |= bit;
      }
      bitReversal_[i] = j;
    }

    forwardTwiddles_.clear();
    inverseTwiddles_.clear();
    for (std::size_t length = 2; length <= fftSize_; length <<= 1U) {
      const std::size_t half = length / 2U;
      std::vector<std::complex<double>> fwd(half);
      std::vector<std::complex<double>> inv(half);
      const double fwdAngleBase = -2.0 * kPi / static_cast<double>(length);
      const double invAngleBase = 2.0 * kPi / static_cast<double>(length);
      for (std::size_t j = 0; j < half; ++j) {
        const double fwdAngle = fwdAngleBase * static_cast<double>(j);
        fwd[j] = {std::cos(fwdAngle), std::sin(fwdAngle)};
        const double invAngle = invAngleBase * static_cast<double>(j);
        inv[j] = {std::cos(invAngle), std::sin(invAngle)};
      }
      forwardTwiddles_.push_back(std::move(fwd));
      inverseTwiddles_.push_back(std::move(inv));
    }
  }

  void performFft(std::vector<std::complex<double>>& values, bool inverse) const noexcept {
    const std::size_t n = values.size();
    if (n != fftSize_) return;
    for (std::size_t i = 0; i < n; ++i) {
      const std::size_t j = bitReversal_[i];
      if (i < j) std::swap(values[i], values[j]);
    }
    std::size_t stageIndex = 0;
    const auto& twiddleSet = inverse ? inverseTwiddles_ : forwardTwiddles_;
    for (std::size_t length = 2; length <= n; length <<= 1U, ++stageIndex) {
      const std::size_t half = length / 2U;
      const auto& stageTwiddles = twiddleSet[stageIndex];
      for (std::size_t j = 0; j < half; ++j) {
        const auto rotation = stageTwiddles[j];
        for (std::size_t base = 0; base < n; base += length) {
          const auto even = values[base + j];
          const auto odd = values[base + j + half] * rotation;
          values[base + j] = even + odd;
          values[base + j + half] = even - odd;
        }
      }
    }
    if (inverse) {
      const double scale = 1.0 / static_cast<double>(n);
      for (auto& value : values) value *= scale;
    }
  }

  void calculateNextBlock(ChannelState& state) noexcept {
    std::fill(state.transform.begin(), state.transform.end(),
              std::complex<double>(0.0, 0.0));
    for (std::size_t i = 0; i < partitionFrames_; ++i) {
      state.transform[i] = state.input[i];
    }
    performFft(state.transform, false);
    std::copy(state.transform.begin(), state.transform.end(),
              state.history[state.historyWrite].begin());

    std::fill(state.sum.begin(), state.sum.end(),
              std::complex<double>(0.0, 0.0));
    const std::size_t count = spectra_.size();
    auto* sumPtr = state.sum.data();
    for (std::size_t partition = 0; partition < count; ++partition) {
      const std::size_t historyIndex =
          (state.historyWrite + count - partition) % count;
      const auto* histPtr = state.history[historyIndex].data();
      const auto* specPtr = spectra_[partition].data();
      for (std::size_t bin = 0; bin < fftSize_; ++bin) {
        const double hr = histPtr[bin].real();
        const double hi = histPtr[bin].imag();
        const double sr = specPtr[bin].real();
        const double si = specPtr[bin].imag();
        sumPtr[bin] += std::complex<double>(hr * sr - hi * si, hr * si + hi * sr);
      }
    }
    performFft(state.sum, true);
    for (std::size_t i = 0; i < partitionFrames_; ++i) {
      state.nextOutput[i] = state.sum[i].real() + state.overlap[i];
      state.overlap[i] = state.sum[i + partitionFrames_].real();
    }
    state.output.swap(state.nextOutput);
    std::fill(state.input.begin(), state.input.end(), 0.0);
    state.position = 0;
    state.historyWrite = (state.historyWrite + 1U) % count;
  }

  std::size_t partitionFrames_ = 0;
  std::size_t fftSize_ = 0;
  std::vector<std::size_t> bitReversal_;
  std::vector<std::vector<std::complex<double>>> forwardTwiddles_;
  std::vector<std::vector<std::complex<double>>> inverseTwiddles_;
  std::vector<std::vector<std::complex<double>>> spectra_;
  std::vector<ChannelState> states_;
};

class ConvolutionEngine {
 public:
  ConvolutionEngine(const RealtimeConvolverConfig& config,
                    const std::vector<double>& ir)
      : channels_(config.channels),
        headFrames_(config.directHeadFrames),
        irFrames_(ir.size()),
        direct_(ir.begin(), ir.begin() + std::min(headFrames_, ir.size())),
        rings_(channels_, std::vector<double>(headFrames_, 0.0)),
        ringPositions_(channels_, 0),
        early_(headFrames_, ir, headFrames_, config.earlyTailEndFrames,
               channels_),
        late_(config.earlyTailEndFrames, ir, config.earlyTailEndFrames,
              ir.size(), channels_) {}

  [[nodiscard]] double processSample(std::size_t channel,
                                     double input) noexcept {
    auto& ring = rings_[channel];
    auto& position = ringPositions_[channel];
    ring[position] = input;
    double output = 0.0;
    std::size_t read = position;
    for (const double coefficient : direct_) {
      output += coefficient * ring[read];
      read = read == 0 ? headFrames_ - 1U : read - 1U;
    }
    position = (position + 1U) % headFrames_;
    output += early_.processSample(channel, input);
    output += late_.processSample(channel, input);
    return std::isfinite(output) ? output : 0.0;
  }

  void reset() noexcept {
    for (auto& ring : rings_) std::fill(ring.begin(), ring.end(), 0.0);
    std::fill(ringPositions_.begin(), ringPositions_.end(), 0);
    early_.reset();
    late_.reset();
  }

  [[nodiscard]] std::size_t irFrames() const noexcept { return irFrames_; }

 private:
  std::size_t channels_ = 0;
  std::size_t headFrames_ = 0;
  std::size_t irFrames_ = 0;
  std::vector<double> direct_;
  std::vector<std::vector<double>> rings_;
  std::vector<std::size_t> ringPositions_;
  PartitionedTier early_;
  PartitionedTier late_;
};

void expectToken(std::istream& stream, const char* expected) {
  std::string actual;
  stream >> actual;
  if (!stream || actual != expected) {
    throw std::runtime_error(std::string("Malformed project state at ") + expected);
  }
}

void writeCapture(std::ostream& stream,
                  const char* name,
                  const SpectrumCapture& capture) {
  stream << "capture " << name << ' ' << capture.sampleRate << ' '
         << capture.fftSize << ' ' << capture.acceptedFrames << ' '
         << capture.confidence << ' ' << capture.points.size() << '\n';
  for (const auto& point : capture.points) {
    stream << "point " << point.frequencyHz << ' ' << point.levelDb << ' '
           << point.confidence << ' ' << point.varianceDb2 << '\n';
  }
}

SpectrumCapture readCapture(std::istream& stream, const char* expectedName) {
  expectToken(stream, "capture");
  std::string name;
  std::uint64_t accepted = 0;
  std::uint64_t count = 0;
  SpectrumCapture capture;
  stream >> name >> capture.sampleRate >> capture.fftSize >> accepted >>
      capture.confidence >> count;
  if (!stream || name != expectedName || count < 3 ||
      count > kMaximumStatePoints ||
      accepted > std::numeric_limits<std::size_t>::max()) {
    throw std::runtime_error("Malformed project capture header");
  }
  capture.acceptedFrames = static_cast<std::size_t>(accepted);
  capture.points.resize(static_cast<std::size_t>(count));
  for (auto& point : capture.points) {
    expectToken(stream, "point");
    stream >> point.frequencyHz >> point.levelDb >> point.confidence >>
        point.varianceDb2;
    if (!stream) throw std::runtime_error("Malformed project capture point");
  }
  return capture;
}

void writeDiagnostics(std::ostream& stream,
                      const char* name,
                      const CaptureDiagnostics& diagnostics) {
  stream << "diagnostics " << name << ' ' << diagnostics.sampleCount << ' '
         << diagnostics.nonFiniteSamples << ' ' << diagnostics.clippedSamples
         << ' ' << diagnostics.peakAbsolute << '\n';
}

CaptureDiagnostics readDiagnostics(std::istream& stream,
                                   const char* expectedName) {
  expectToken(stream, "diagnostics");
  std::string name;
  CaptureDiagnostics result;
  stream >> name >> result.sampleCount >> result.nonFiniteSamples >>
      result.clippedSamples >> result.peakAbsolute;
  if (!stream || name != expectedName || !std::isfinite(result.peakAbsolute) ||
      result.peakAbsolute < 0.0 || result.nonFiniteSamples > result.sampleCount ||
      result.clippedSamples > result.sampleCount) {
    throw std::runtime_error("Malformed capture diagnostics");
  }
  return result;
}

void writeModel(std::ostream& stream, const CorrectionModel& model) {
  stream << "model " << model.version << ' ' << toString(model.mode) << ' '
         << model.analysisLowHz << ' ' << model.analysisHighHz << ' '
         << model.resolution << ' ' << model.nodes.size() << '\n';
  for (const auto& node : model.nodes) {
    stream << "node " << node.frequencyHz << ' ' << node.gainDb << ' '
           << node.confidence << '\n';
  }
}

CorrectionModel readModel(std::istream& stream) {
  expectToken(stream, "model");
  std::string mode;
  std::uint64_t count = 0;
  CorrectionModel model;
  stream >> model.version >> mode >> model.analysisLowHz >>
      model.analysisHighHz >> model.resolution >> count;
  if (!stream || count == 0 || count > kMaximumStatePoints) {
    throw std::runtime_error("Malformed project model header");
  }
  model.mode = parseMatchMode(mode);
  model.nodes.resize(static_cast<std::size_t>(count));
  for (auto& node : model.nodes) {
    expectToken(stream, "node");
    stream >> node.frequencyHz >> node.gainDb >> node.confidence;
    if (!stream) throw std::runtime_error("Malformed project model node");
  }
  return model;
}

}  // namespace

// Every parameter must be automatable so that OSARA's FX parameter list exposes
// it to a screen-reader user; OSARA filters out any non-automatable parameter.
// Read-only feedback parameters are therefore automatable but not writable.
const std::vector<ParameterDescriptor>& parameterDescriptors() {
  static const std::vector<ParameterDescriptor> descriptors{
      {ParameterId::WorkflowAction, "Workflow Step", 0.0, 7.0, 0.0, "", true, false, true},
      {ParameterId::Status, "Status", 0.0, 29.0, 0.0, "", true, true, true},
      {ParameterId::LastCommand, "Last Command", 0.0, 7.0, 0.0, "", true, true, true},
      {ParameterId::MatchMode, "Match Mode", 0.0, 4.0, 1.0, "", true, false, true},
      {ParameterId::MaximumCorrectionDb, "Maximum Correction", 1.0, 60.0, 18.0, "dB", true, false, false},
      {ParameterId::CompleteMatch, "Full Correction Range", 0.0, 1.0, 0.0, "", true, false, true},
      {ParameterId::CorrectionStrength, "Correction Strength", -1.0, 1.0, 1.0, "x", true, false, false},
      {ParameterId::Resolution, "Correction Resolution", 1.0, 120.0, 30.0, "bands", true, false, true},
      {ParameterId::RangeLowHz, "Correction Range Low", 10.0, 18000.0, 10.0, "Hz", true, false, false},
      {ParameterId::RangeHighHz, "Correction Range High", 20.0, 30000.0, 30000.0, "Hz", true, false, false},
      {ParameterId::CorrectionSharpness, "Correction Q / Sharpness", 0.5, 1.5, 1.0, "", true, false, false},
      {ParameterId::CorrectionGainDb, "Correction Gain", -24.0, 12.0, 0.0, "dB", true, false, false},
      {ParameterId::EmergencyClipGuardDb, "Emergency Clip Guard", -12.0, 20.0, 6.0, "dB", true, false, false},
      {ParameterId::ToneLevelDb, "Confidence Tone Volume", -60.0, -12.0, -12.0, "dB", true, false, false},
      {ParameterId::Confidence, "Capture Confidence", 0.0, 1.0, 0.0, "", true, true, false},
      {ParameterId::CurveDriftDb, "Curve Drift", 0.0, 60.0, 60.0, "dB", true, true, false},
      {ParameterId::CaptureSeconds, "Capture Time", 0.0, 3600.0, 0.0, "s", true, true, false},
      {ParameterId::ToneNotifications, "Tone Notifications", 0.0, 1.0, 1.0, "", true, false, true},
      {ParameterId::Bypass, "Bypass", 0.0, 1.0, 0.0, "", true, false, true},
  };
  return descriptors;
}

void CaptureDiagnostics::observe(const float* const* channels,
                                 std::size_t channelCount,
                                 std::size_t frames) noexcept {
  if (channels == nullptr) return;
  for (std::size_t channel = 0; channel < channelCount; ++channel) {
    if (channels[channel] == nullptr) continue;
    for (std::size_t frame = 0; frame < frames; ++frame) {
      if (sampleCount != std::numeric_limits<std::uint64_t>::max()) ++sampleCount;
      const double sample = channels[channel][frame];
      if (!std::isfinite(sample)) {
        if (nonFiniteSamples != std::numeric_limits<std::uint64_t>::max()) {
          ++nonFiniteSamples;
        }
        continue;
      }
      const double absolute = std::abs(sample);
      peakAbsolute = std::max(peakAbsolute, absolute);
      if (absolute >= 0.999) {
        if (clippedSamples != std::numeric_limits<std::uint64_t>::max()) {
          ++clippedSamples;
        }
      }
    }
  }
}

double CaptureDiagnostics::clippedFraction() const noexcept {
  return sampleCount == 0
             ? 0.0
             : static_cast<double>(clippedSamples) /
                   static_cast<double>(sampleCount);
}

ProfileValidation validateProfileSnapshot(const ProfileSnapshot& snapshot) {
  try {
    if (snapshot.reference.acceptedFrames < 3 ||
        snapshot.target.acceptedFrames < 3) {
      return {false, ProfileIssue::InsufficientAudio,
              "Reference or Target has too little accepted audio"};
    }
    if (snapshot.referenceDiagnostics.nonFiniteSamples != 0 ||
        snapshot.targetDiagnostics.nonFiniteSamples != 0) {
      return {false, ProfileIssue::NonFiniteAudio,
              "Capture contains non-finite audio samples"};
    }
    if (snapshot.referenceDiagnostics.clippedFraction() > 0.01 ||
        snapshot.targetDiagnostics.clippedFraction() > 0.01) {
      return {false, ProfileIssue::SevereClipping,
              "More than one percent of a capture is clipped"};
    }
    if (snapshot.uncappedModel.mode != snapshot.matchSettings.mode ||
        snapshot.uncappedModel.resolution != snapshot.matchSettings.resolution) {
      return {false, ProfileIssue::InvalidModel,
              "Model mode or resolution does not match its capture settings"};
    }
    // Force complete capture validation before touching front()/back(). This
    // also makes hostile or truncated in-memory snapshots fail by exception
    // instead of reaching undefined behaviour.
    (void)compareCaptures(snapshot.reference, snapshot.reference, 1.0, 1.0e9);
    (void)compareCaptures(snapshot.target, snapshot.target, 1.0, 1.0e9);
    const double low = std::max(snapshot.reference.points.front().frequencyHz,
                                snapshot.target.points.front().frequencyHz);
    const double high = std::min(snapshot.reference.points.back().frequencyHz,
                                 snapshot.target.points.back().frequencyHz);
    (void)compareCaptures(snapshot.reference, snapshot.target, low, high);
    validateIrRenderSettings(snapshot.uncappedModel, snapshot.renderSettings);
    return {true, ProfileIssue::None, "Profile is valid"};
  } catch (const std::exception& error) {
    return {false, ProfileIssue::InvalidCapture, error.what()};
  }
}

namespace {

std::vector<double> renderValidatedProfileKernel(
    const ProfileSnapshot& snapshot) {
  const long double realtimeFrames = std::ceil(
      static_cast<long double>(snapshot.renderSettings.durationSeconds) *
      static_cast<long double>(snapshot.renderSettings.sampleRate));
  if (realtimeFrames >
      static_cast<long double>(kMaximumRealtimeIrFrames)) {
    throw std::runtime_error(
        "Profile IR is too long for the realtime renderer");
  }
  auto audibleModel = snapshot.uncappedModel;
  const double ceiling = snapshot.matchSettings.maximumCorrectionDb;
  if (!std::isfinite(ceiling) || ceiling <= 0.0 || ceiling > 60.0) {
    throw std::runtime_error("Invalid audible correction ceiling");
  }
  for (auto& node : audibleModel.nodes) {
    node.gainDb = std::clamp(node.gainDb, -ceiling, ceiling);
  }
  return renderMinimumPhaseIr(audibleModel, snapshot.renderSettings);
}

}  // namespace

std::vector<double> renderProfileKernel(const ProfileSnapshot& snapshot) {
  const auto validation = validateProfileSnapshot(snapshot);
  if (!validation.accepted) {
    throw std::runtime_error("Cannot render invalid profile: " +
                             validation.message);
  }
  return renderValidatedProfileKernel(snapshot);
}

std::string serializeProjectState(
    const ProfileSnapshot* latestValidatedSnapshot) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(std::numeric_limits<double>::max_digits10);
  stream << "ToneTraceProjectState 3\n";
  if (latestValidatedSnapshot == nullptr ||
      !validateProfileSnapshot(*latestValidatedSnapshot).accepted) {
    stream << "phase ready\nend\n";
    return stream.str();
  }

  const auto& snapshot = *latestValidatedSnapshot;
  stream << "phase frozen\n";
  stream << "match " << toString(snapshot.matchSettings.mode) << ' '
         << snapshot.matchSettings.rangeLowHz << ' '
         << snapshot.matchSettings.rangeHighHz << ' '
         << snapshot.matchSettings.maximumCorrectionDb << ' '
         << snapshot.matchSettings.resolution << ' '
         << (snapshot.matchSettings.removeBroadLevelDifference ? 1 : 0) << '\n';
  stream << "render " << snapshot.renderSettings.sampleRate << ' '
         << snapshot.renderSettings.durationSeconds << ' '
         << snapshot.renderSettings.correctionStrength << ' '
         << snapshot.renderSettings.correctionSharpness << ' '
         << snapshot.renderSettings.correctionGainDb << ' '
         << snapshot.renderSettings.rangeLowHz << ' '
         << snapshot.renderSettings.rangeHighHz << ' '
         << snapshot.renderSettings.manualGains.size();
  for (const double gain : snapshot.renderSettings.manualGains) {
    stream << ' ' << gain;
  }
  stream << '\n';
  writeDiagnostics(stream, "reference", snapshot.referenceDiagnostics);
  writeDiagnostics(stream, "target", snapshot.targetDiagnostics);
  writeCapture(stream, "reference", snapshot.reference);
  writeCapture(stream, "target", snapshot.target);
  writeModel(stream, snapshot.uncappedModel);
  stream << "end\n";
  return stream.str();
}

RestoredProjectState deserializeProjectState(const std::string& bytes) {
  if (bytes.size() > 64U * 1024U * 1024U) {
    throw std::runtime_error("Project state is unreasonably large");
  }
  std::istringstream stream(bytes);
  stream.imbue(std::locale::classic());
  expectToken(stream, "ToneTraceProjectState");
  int version = 0;
  stream >> version;
  if (!stream || (version < 1 || version > 3)) {
    throw std::runtime_error("Unsupported Tone Trace project state");
  }
  expectToken(stream, "phase");
  std::string phase;
  stream >> phase;
  if (phase == "ready") {
    expectToken(stream, "end");
    stream >> std::ws;
    if (!stream.eof()) throw std::runtime_error("Trailing project state data");
    return {};
  }
  if (phase != "frozen") {
    throw std::runtime_error("Project state may restore only Ready or Frozen");
  }

  auto snapshot = std::make_unique<ProfileSnapshot>();
  expectToken(stream, "match");
  std::string mode;
  int removeBroad = 0;
  stream >> mode >> snapshot->matchSettings.rangeLowHz >>
      snapshot->matchSettings.rangeHighHz >>
      snapshot->matchSettings.maximumCorrectionDb >>
      snapshot->matchSettings.resolution >> removeBroad;
  if (!stream || (removeBroad != 0 && removeBroad != 1)) {
    throw std::runtime_error("Malformed match settings in project state");
  }
  snapshot->matchSettings.mode = parseMatchMode(mode);
  snapshot->matchSettings.removeBroadLevelDifference = removeBroad != 0;

  expectToken(stream, "render");
  stream >> snapshot->renderSettings.sampleRate >>
      snapshot->renderSettings.durationSeconds >>
      snapshot->renderSettings.correctionStrength;
  if (version >= 2) {
    stream >> snapshot->renderSettings.correctionSharpness;
  }
  stream >> snapshot->renderSettings.correctionGainDb >>
      snapshot->renderSettings.rangeLowHz >>
      snapshot->renderSettings.rangeHighHz;
  if (!stream) throw std::runtime_error("Malformed render settings in project state");
  if (version >= 3) {
    std::size_t gainCount = 0;
    stream >> gainCount;
    if (!stream || gainCount > 256) {
      throw std::runtime_error("Malformed manual gains in project state");
    }
    snapshot->renderSettings.manualGains.resize(gainCount);
    for (auto& gain : snapshot->renderSettings.manualGains) {
      stream >> gain;
      if (!stream) {
        throw std::runtime_error("Malformed manual gains in project state");
      }
    }
  }

  snapshot->referenceDiagnostics = readDiagnostics(stream, "reference");
  snapshot->targetDiagnostics = readDiagnostics(stream, "target");
  snapshot->reference = readCapture(stream, "reference");
  snapshot->target = readCapture(stream, "target");
  snapshot->uncappedModel = readModel(stream);
  expectToken(stream, "end");
  stream >> std::ws;
  if (!stream.eof()) throw std::runtime_error("Trailing project state data");

  const auto validation = validateProfileSnapshot(*snapshot);
  if (!validation.accepted) {
    throw std::runtime_error("Invalid frozen project profile: " +
                             validation.message);
  }
  RestoredProjectState result;
  result.phase = WorkflowPhase::Frozen;
  result.snapshot = std::move(snapshot);
  return result;
}

class RealtimeConvolver::Impl {
 public:
  explicit Impl(const RealtimeConvolverConfig& config) : config_(config) {
    if (config_.sampleRate < 8000 || config_.channels == 0 ||
        config_.channels > 64 || config_.directHeadFrames < 16 ||
        (config_.directHeadFrames & (config_.directHeadFrames - 1U)) != 0 ||
        config_.earlyTailEndFrames < config_.directHeadFrames * 2U ||
        (config_.earlyTailEndFrames & (config_.earlyTailEndFrames - 1U)) != 0 ||
        config_.earlyTailEndFrames % config_.directHeadFrames != 0 ||
        !std::isfinite(config_.minimumCrossfadeSeconds) ||
        config_.minimumCrossfadeSeconds < 0.0 ||
        config_.minimumCrossfadeSeconds > 2.0) {
      throw std::runtime_error("Invalid realtime convolver configuration");
    }
    bypassStep_ = 1.0 / std::max(1.0, config_.sampleRate * 0.01);
  }

  ~Impl() = default;

  void installInitial(const std::vector<double>& ir) {
    validateRealtimeKernel(ir);
    if (pending_.load(std::memory_order_acquire) != nullptr || fading_ != nullptr) {
      throw std::runtime_error("A realtime kernel transition is already active");
    }
    // Prepare the replacement completely before releasing the current engine.
    // If allocation or FFT preparation fails, the last working kernel remains
    // owned and active instead of leaving active_ pointing at cleared storage.
    std::vector<std::unique_ptr<ConvolutionEngine>> replacement;
    replacement.reserve(1);
    replacement.push_back(std::make_unique<ConvolutionEngine>(config_, ir));
    collectRetired();
    owned_.swap(replacement);
    active_ = owned_.front().get();
  }

  bool submit(const std::vector<double>& ir) {
    validateRealtimeKernel(ir);
    auto engine = std::make_unique<ConvolutionEngine>(config_, ir);
    ConvolutionEngine* pointer = engine.get();
    owned_.push_back(std::move(engine));

    // There is deliberately only one prepared-but-not-started slot. A newer
    // control-thread edit replaces that slot atomically, while a kernel that
    // the audio thread has already taken becomes part of the active crossfade
    // and is left alone. This makes rapid slider/band edits converge on the
    // latest correction instead of reporting RendererBusy or applying stale
    // intermediate states later.
    ConvolutionEngine* observed = pending_.load(std::memory_order_acquire);
    while (!pending_.compare_exchange_weak(observed, pointer,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
    }
    if (observed != nullptr) {
      const auto found = std::find_if(
          owned_.begin(), owned_.end(),
          [observed](const auto& item) { return item.get() == observed; });
      if (found != owned_.end()) owned_.erase(found);
    }
    return true;
  }

  void collectRetired() noexcept {
    ConvolutionEngine* pointer = retired_.exchange(nullptr,
                                                    std::memory_order_acq_rel);
    if (pointer == nullptr) return;
    const auto found = std::find_if(
        owned_.begin(), owned_.end(),
        [pointer](const auto& item) { return item.get() == pointer; });
    if (found != owned_.end()) owned_.erase(found);
  }

  void setBypassed(bool bypassed) noexcept {
    bypassed_.store(bypassed, std::memory_order_release);
  }

  void reset() noexcept {
    ConvolutionEngine* act = active_;
    if (act != nullptr) act->reset();
    ConvolutionEngine* fad = fading_;
    if (fad != nullptr) fad->reset();
    fadePosition_ = 0;
    bypassMix_ = bypassed_.load(std::memory_order_acquire) ? 0.0 : 1.0;
  }

  void process(const float* const* inputs,
               float* const* outputs,
               std::size_t channelCount,
               std::size_t frames) noexcept {
    if (inputs == nullptr || outputs == nullptr ||
        channelCount == 0 || channelCount > config_.channels) {
      return;
    }
    beginTransitionIfPossible();
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const double targetMix = bypassed_.load(std::memory_order_relaxed) ? 0.0 : 1.0;
      if (bypassMix_ < targetMix) {
        bypassMix_ = std::min(targetMix, bypassMix_ + bypassStep_);
      } else if (bypassMix_ > targetMix) {
        bypassMix_ = std::max(targetMix, bypassMix_ - bypassStep_);
      }
      const double fade = fading_ == nullptr || fadeFrames_ == 0
                              ? 1.0
                              : std::min(1.0, static_cast<double>(fadePosition_) /
                                                  static_cast<double>(fadeFrames_));
      for (std::size_t channel = 0; channel < channelCount; ++channel) {
        if (inputs[channel] == nullptr || outputs[channel] == nullptr) continue;
        const double dry = std::isfinite(inputs[channel][frame])
                               ? inputs[channel][frame]
                               : 0.0;
        double wet = active_ == nullptr
                         ? dry
                         : active_->processSample(channel, dry);
        if (fading_ != nullptr) {
          const double oldWet = fading_->processSample(channel, dry);
          wet = oldWet + (wet - oldWet) * fade;
        }
        const double output = dry + (wet - dry) * bypassMix_;
        outputs[channel][frame] = static_cast<float>(
            std::isfinite(output) ? output : 0.0);
      }
      if (fading_ != nullptr) {
        ++fadePosition_;
        if (fadePosition_ >= fadeFrames_) finishTransition();
      }
    }
  }

  [[nodiscard]] bool hasPending() const noexcept {
    return pending_.load(std::memory_order_acquire) != nullptr ||
           transitionActive_.load(std::memory_order_acquire) ||
           retired_.load(std::memory_order_acquire) != nullptr;
  }

  [[nodiscard]] bool hasRetired() const noexcept {
    return retired_.load(std::memory_order_acquire) != nullptr;
  }

 private:
  void beginTransitionIfPossible() noexcept {
    if (fading_ != nullptr ||
        retired_.load(std::memory_order_acquire) != nullptr) {
      return;
    }
    ConvolutionEngine* next = pending_.exchange(nullptr,
                                                 std::memory_order_acq_rel);
    if (next == nullptr) return;
    if (active_ == nullptr) {
      active_ = next;
      return;
    }
    fading_ = active_;
    active_ = next;
    transitionActive_.store(true, std::memory_order_release);
    fadePosition_ = 0;
    const std::size_t minimum = static_cast<std::size_t>(std::ceil(
        config_.minimumCrossfadeSeconds * config_.sampleRate));
    fadeFrames_ = std::max({minimum, active_->irFrames(), fading_->irFrames()});
  }

  void finishTransition() noexcept {
    ConvolutionEngine* expected = nullptr;
    if (retired_.compare_exchange_strong(expected, fading_,
                                          std::memory_order_release,
                                          std::memory_order_relaxed)) {
      fading_ = nullptr;
      transitionActive_.store(false, std::memory_order_release);
      fadePosition_ = 0;
      fadeFrames_ = 0;
    }
  }

  RealtimeConvolverConfig config_;
  std::vector<std::unique_ptr<ConvolutionEngine>> owned_;
  ConvolutionEngine* active_ = nullptr;
  ConvolutionEngine* fading_ = nullptr;
  std::atomic<ConvolutionEngine*> pending_{nullptr};
  std::atomic<ConvolutionEngine*> retired_{nullptr};
  std::atomic<bool> bypassed_{false};
  std::atomic<bool> transitionActive_{false};
  std::size_t fadePosition_ = 0;
  std::size_t fadeFrames_ = 0;
  double bypassMix_ = 1.0;
  double bypassStep_ = 1.0;
};

RealtimeConvolver::RealtimeConvolver(const RealtimeConvolverConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

RealtimeConvolver::~RealtimeConvolver() = default;
RealtimeConvolver::RealtimeConvolver(RealtimeConvolver&&) noexcept = default;
RealtimeConvolver& RealtimeConvolver::operator=(RealtimeConvolver&&) noexcept = default;

void RealtimeConvolver::installInitialKernel(
    const std::vector<double>& impulseResponse) {
  impl_->installInitial(impulseResponse);
}

bool RealtimeConvolver::submitKernel(
    const std::vector<double>& impulseResponse) {
  return impl_->submit(impulseResponse);
}

void RealtimeConvolver::collectRetiredKernels() noexcept {
  impl_->collectRetired();
}

void RealtimeConvolver::setBypassed(bool bypassed) noexcept {
  impl_->setBypassed(bypassed);
}

void RealtimeConvolver::reset() noexcept { impl_->reset(); }

void RealtimeConvolver::process(const float* const* inputs,
                                float* const* outputs,
                                std::size_t channelCount,
                                std::size_t frames) noexcept {
  impl_->process(inputs, outputs, channelCount, frames);
}

std::size_t RealtimeConvolver::latencyFrames() const noexcept { return 0; }

bool RealtimeConvolver::hasPendingKernel() const noexcept {
  return impl_->hasPending();
}

bool RealtimeConvolver::hasRetiredKernel() const noexcept {
  return impl_->hasRetired();
}

HeadlessPluginCore::HeadlessPluginCore(const RealtimeConvolverConfig& config)
    : renderer_(config) {
  renderer_.installInitialKernel({1.0});
}

ProfileValidation HeadlessPluginCore::commitKernel(
    const std::vector<double>& kernel) {
  RendererRunState expected = RendererRunState::ReadyForInitialInstall;
  if (rendererRunState_.compare_exchange_strong(
          expected, RendererRunState::InitialInstall,
          std::memory_order_acq_rel, std::memory_order_acquire)) {
    try {
      renderer_.installInitialKernel(kernel);
      rendererRunState_.store(RendererRunState::ReadyForInitialInstall,
                              std::memory_order_release);
      return {true, ProfileIssue::None, "Realtime kernel is ready"};
    } catch (const std::exception& error) {
      rendererRunState_.store(RendererRunState::ReadyForInitialInstall,
                              std::memory_order_release);
      return {false, ProfileIssue::InvalidModel, error.what()};
    }
  }

  if (expected == RendererRunState::InitialInstall) {
    return {false, ProfileIssue::RendererBusy,
            "The initial correction is still being prepared"};
  }

  try {
    renderer_.collectRetiredKernels();
    if (!renderer_.submitKernel(kernel)) {
      return {false, ProfileIssue::RendererBusy,
              "A previous correction transition is still completing"};
    }
  } catch (const std::exception& error) {
    return {false, ProfileIssue::InvalidModel, error.what()};
  }
  return {true, ProfileIssue::None, "Realtime kernel is ready"};
}

void HeadlessPluginCore::copyBypassedAudio(const float* const* inputs,
                                           float* const* outputs,
                                           std::size_t channelCount,
                                           std::size_t frames) noexcept {
  if (inputs == nullptr || outputs == nullptr) return;
  for (std::size_t channel = 0; channel < channelCount; ++channel) {
    if (inputs[channel] == nullptr || outputs[channel] == nullptr) return;
    std::copy_n(inputs[channel], frames, outputs[channel]);
  }
}

ProfileValidation HeadlessPluginCore::commitCandidate(
    const ProfileSnapshot& candidate) {
  const auto validation = validateProfileSnapshot(candidate);
  if (!validation.accepted) return validation;
  std::vector<double> kernel;
  std::unique_ptr<ProfileSnapshot> committed;
  try {
    kernel = renderValidatedProfileKernel(candidate);
    committed = std::make_unique<ProfileSnapshot>(candidate);
  } catch (const std::exception& error) {
    return {false, ProfileIssue::InvalidModel, error.what()};
  }
  const auto rendererResult = commitKernel(kernel);
  if (!rendererResult.accepted) return rendererResult;
  snapshot_ = std::move(committed);
  return validation;
}

ProfileValidation HeadlessPluginCore::commitManualCorrection(
    const IrRenderSettings& settings) {
  try {
    if (!hasManualCorrection(settings)) {
      const auto result = commitKernel({1.0});
      if (result.accepted) snapshot_.reset();
      return result;
    }
    const auto kernel = renderManualCorrectionIr(settings);
    const auto result = commitKernel(kernel);
    if (result.accepted) snapshot_.reset();
    return result;
  } catch (const std::exception& error) {
    return {false, ProfileIssue::InvalidModel, error.what()};
  }
}

void HeadlessPluginCore::clearProfile() {
  const auto rendererResult = commitKernel({1.0});
  if (!rendererResult.accepted) {
    throw std::runtime_error(rendererResult.message);
  }
  snapshot_.reset();
}

std::string HeadlessPluginCore::saveProjectState() const {
  return serializeProjectState(snapshot_.get());
}

void HeadlessPluginCore::loadProjectState(const std::string& bytes) {
  RendererRunState expected = RendererRunState::ReadyForInitialInstall;
  if (!rendererRunState_.compare_exchange_strong(
          expected, RendererRunState::InitialInstall,
          std::memory_order_acq_rel, std::memory_order_acquire)) {
    throw std::runtime_error("Project state must load before audio processing starts");
  }
  try {
    auto restored = deserializeProjectState(bytes);
    if (restored.phase == WorkflowPhase::Frozen && restored.snapshot) {
      const auto kernel = renderProfileKernel(*restored.snapshot);
      renderer_.installInitialKernel(kernel);
      snapshot_ = std::move(restored.snapshot);
    } else {
      renderer_.installInitialKernel({1.0});
      snapshot_.reset();
    }
    rendererRunState_.store(RendererRunState::ReadyForInitialInstall,
                            std::memory_order_release);
  } catch (...) {
    rendererRunState_.store(RendererRunState::ReadyForInitialInstall,
                            std::memory_order_release);
    throw;
  }
}

void HeadlessPluginCore::setBypassed(bool bypassed) noexcept {
  renderer_.setBypassed(bypassed);
}

void HeadlessPluginCore::resetAudio() noexcept { renderer_.reset(); }

void HeadlessPluginCore::collectRetiredKernels() noexcept {
  renderer_.collectRetiredKernels();
}

bool HeadlessPluginCore::hasRetiredKernel() const noexcept {
  return renderer_.hasRetiredKernel();
}

void HeadlessPluginCore::process(const float* const* inputs,
                                 float* const* outputs,
                                 std::size_t channelCount,
                                 std::size_t frames) noexcept {
  RendererRunState expected = RendererRunState::ReadyForInitialInstall;
  if (!rendererRunState_.compare_exchange_strong(
          expected, RendererRunState::Processing,
          std::memory_order_acq_rel, std::memory_order_acquire) &&
      expected == RendererRunState::InitialInstall) {
    // Never wait on the audio thread. If the very first block arrives during
    // an initial control-thread install, pass that one block through and let
    // the next block claim the renderer after preparation has finished.
    copyBypassedAudio(inputs, outputs, channelCount, frames);
    return;
  }
  renderer_.process(inputs, outputs, channelCount, frames);
}

WorkflowPhase HeadlessPluginCore::phase() const noexcept {
  return snapshot_ ? WorkflowPhase::Frozen : WorkflowPhase::Ready;
}

std::size_t HeadlessPluginCore::latencyFrames() const noexcept {
  return renderer_.latencyFrames();
}

const ProfileSnapshot* HeadlessPluginCore::frozenSnapshot() const noexcept {
  return snapshot_.get();
}

}  // namespace tonetrace
