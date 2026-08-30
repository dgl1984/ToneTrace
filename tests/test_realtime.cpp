#include "tonetrace/tonetrace_realtime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<std::uint64_t> gAllocationCalls{0};
}

void* operator new(std::size_t size) {
  gAllocationCalls.fetch_add(1, std::memory_order_relaxed);
  if (void* pointer = std::malloc(size)) return pointer;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
  gAllocationCalls.fetch_add(1, std::memory_order_relaxed);
  if (void* pointer = std::malloc(size)) return pointer;
  throw std::bad_alloc();
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

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

tonetrace::CorrectionModel testModel() {
  tonetrace::CorrectionModel model;
  model.mode = tonetrace::MatchMode::FullMix;
  model.analysisLowHz = 20.0;
  model.analysisHighHz = 20000.0;
  model.resolution = 60;
  model.nodes = {
      {20.0, 0.0, 0.9},
      {100.0, 2.5, 0.9},
      {1000.0, -3.0, 0.95},
      {5000.0, 4.0, 0.9},
      {20000.0, 0.0, 0.8},
  };
  return model;
}

tonetrace::SpectrumCapture testCapture(double offset) {
  tonetrace::SpectrumCapture capture;
  capture.sampleRate = 48000;
  capture.fftSize = 4096;
  capture.acceptedFrames = 24;
  capture.confidence = 0.8;
  capture.points = {
      {20.0, -2.0 + offset, 0.8, 0.1},
      {100.0, 1.0 + offset, 0.9, 0.1},
      {1000.0, -1.0 + offset, 0.9, 0.2},
      {5000.0, 2.0 + offset, 0.85, 0.2},
      {20000.0, 0.0 + offset, 0.7, 0.3},
  };
  return capture;
}

tonetrace::ProfileSnapshot testSnapshot() {
  tonetrace::ProfileSnapshot snapshot;
  snapshot.reference = testCapture(0.0);
  snapshot.target = testCapture(1.0);
  snapshot.uncappedModel = testModel();
  snapshot.matchSettings.mode = tonetrace::MatchMode::FullMix;
  snapshot.matchSettings.rangeLowHz = 20.0;
  snapshot.matchSettings.rangeHighHz = 20000.0;
  snapshot.matchSettings.maximumCorrectionDb = 60.0;
  snapshot.matchSettings.resolution = 60;
  snapshot.renderSettings.sampleRate = 48000;
  snapshot.renderSettings.durationSeconds = 0.02;
  snapshot.renderSettings.rangeLowHz = 20.0;
  snapshot.renderSettings.rangeHighHz = 20000.0;
  snapshot.referenceDiagnostics.sampleCount = 100000;
  snapshot.referenceDiagnostics.peakAbsolute = 0.8;
  snapshot.targetDiagnostics = snapshot.referenceDiagnostics;
  return snapshot;
}

void testParameterContract() {
  const auto& parameters = tonetrace::parameterDescriptors();
  require(parameters.size() == 19, "The generic parameter inventory is incomplete");
  const std::vector<std::string> expectedOrder{
      "Workflow Step", "Status", "Last Command",
      "Match Mode", "Maximum Correction", "Full Correction Range",
      "Correction Strength", "Correction Resolution",
      "Correction Range Low", "Correction Range High",
      "Correction Q / Sharpness", "Correction Gain",
      "Emergency Clip Guard", "Confidence Tone Volume",
      "Capture Confidence", "Curve Drift", "Capture Time",
      "Tone Notifications", "Bypass"};
  std::set<std::uint32_t> identifiers;
  std::set<std::string> names;
  for (const auto& parameter : parameters) {
    require(parameter.name != nullptr && std::string(parameter.name).size() >= 3,
            "A parameter lacks an accessible name");
    require(std::isfinite(parameter.minimum) && std::isfinite(parameter.maximum) &&
                std::isfinite(parameter.defaultValue) &&
                parameter.maximum >= parameter.minimum &&
                parameter.defaultValue >= parameter.minimum &&
                parameter.defaultValue <= parameter.maximum,
            "A parameter has an invalid range or default");
    require(identifiers.insert(static_cast<std::uint32_t>(parameter.id)).second,
            "A stable parameter identifier is duplicated");
    require(names.insert(parameter.name).second, "A parameter name is duplicated");
    require(parameter.automatable,
            "Every parameter must be automatable so OSARA exposes it "
            "to a screen-reader user");
  }
  for (std::size_t index = 0; index < expectedOrder.size(); ++index) {
    require(parameters[index].name == expectedOrder[index],
            "The frozen accessible parameter order changed");
  }
  const auto mode = std::find_if(
      parameters.begin(), parameters.end(), [](const auto& parameter) {
        return parameter.id == tonetrace::ParameterId::MatchMode;
      });
  require(mode != parameters.end() && mode->defaultValue == 1.0,
          "A fresh instance no longer starts in Voice Match Mode");
  std::cout << "stable generic parameter inventory: passed\n";
}

void testCaptureDiagnosticsAndValidation() {
  std::vector<float> samples{0.0F, 0.5F, 1.0F,
                             std::numeric_limits<float>::quiet_NaN()};
  const float* channels[]{samples.data()};
  tonetrace::CaptureDiagnostics diagnostics;
  diagnostics.observe(channels, 1, samples.size());
  require(diagnostics.sampleCount == 4 && diagnostics.nonFiniteSamples == 1 &&
              diagnostics.clippedSamples == 1 && diagnostics.peakAbsolute == 1.0,
          "Capture diagnostics did not classify dangerous samples");

  auto snapshot = testSnapshot();
  require(tonetrace::validateProfileSnapshot(snapshot).accepted,
          "A coherent profile snapshot was rejected");
  snapshot.targetDiagnostics.nonFiniteSamples = 1;
  require(!tonetrace::validateProfileSnapshot(snapshot).accepted,
          "Non-finite capture audio was accepted");
  snapshot = testSnapshot();
  snapshot.targetDiagnostics.clippedSamples = 1001;
  require(!tonetrace::validateProfileSnapshot(snapshot).accepted,
          "Severely clipped capture audio was accepted");
  snapshot = testSnapshot();
  snapshot.uncappedModel.nodes[2].gainDb =
      std::numeric_limits<double>::infinity();
  require(!tonetrace::validateProfileSnapshot(snapshot).accepted,
          "A non-finite correction model was accepted");
  snapshot = testSnapshot();
  snapshot.reference.points.clear();
  require(!tonetrace::validateProfileSnapshot(snapshot).accepted,
          "An empty capture was accepted or accessed unsafely");
  std::cout << "last-known-good profile validation guards: passed\n";
}

void testProjectState() {
  const auto readyBytes = tonetrace::serializeProjectState(nullptr);
  const auto ready = tonetrace::deserializeProjectState(readyBytes);
  require(ready.phase == tonetrace::WorkflowPhase::Ready && !ready.snapshot,
          "Ready project state did not round-trip");

  const auto snapshot = testSnapshot();
  const auto frozenBytes = tonetrace::serializeProjectState(&snapshot);
  const auto frozen = tonetrace::deserializeProjectState(frozenBytes);
  require(frozen.phase == tonetrace::WorkflowPhase::Frozen && frozen.snapshot,
          "Validated project state did not restore Frozen");
  require(frozen.snapshot->reference.points.size() == snapshot.reference.points.size() &&
              frozen.snapshot->target.points.size() == snapshot.target.points.size() &&
              frozen.snapshot->uncappedModel.nodes.size() ==
                  snapshot.uncappedModel.nodes.size() &&
              frozen.snapshot->uncappedModel.gainDbAt(5000.0) == 4.0 &&
              frozen.snapshot->matchSettings.maximumCorrectionDb == 60.0,
          "Frozen state changed a capture, model, or setting");
  require(tonetrace::serializeProjectState(frozen.snapshot.get()) == frozenBytes,
          "Project-state serialization is not deterministic");

  auto withGains = testSnapshot();
  withGains.renderSettings.manualGains = {1.5, -2.0, 3.0, 0.5, -1.0};
  const auto gainsBytes = tonetrace::serializeProjectState(&withGains);
  const auto gainsRestored = tonetrace::deserializeProjectState(gainsBytes);
  require(gainsRestored.phase == tonetrace::WorkflowPhase::Frozen &&
              gainsRestored.snapshot &&
              gainsRestored.snapshot->renderSettings.manualGains.size() == 5 &&
              gainsRestored.snapshot->renderSettings.manualGains[2] == 3.0,
          "Per-band manual gains did not round-trip");
  require(tonetrace::serializeProjectState(gainsRestored.snapshot.get()) ==
              gainsBytes,
          "Manual-gain project state is not deterministic");

  auto invalid = testSnapshot();
  invalid.reference.acceptedFrames = 1;
  const auto rejected = tonetrace::deserializeProjectState(
      tonetrace::serializeProjectState(&invalid));
  require(rejected.phase == tonetrace::WorkflowPhase::Ready && !rejected.snapshot,
          "An invalid live candidate did not fall back to Ready");
  requireThrows(
      [] {
        (void)tonetrace::deserializeProjectState(
            "ToneTraceProjectState 1\nphase learning\nend\n");
      },
      "An ambiguous learning phase was restorable");
  requireThrows(
      [&] { (void)tonetrace::deserializeProjectState(frozenBytes.substr(0, 80)); },
      "Truncated project state was accepted");
  requireThrows(
      [] { (void)tonetrace::deserializeProjectState(std::string(65U * 1024U * 1024U, 'x')); },
      "An unreasonably large project state was accepted");
  std::cout << "Ready/Frozen state round-trip and malformed-state rejection: passed\n";
}

void testProjectStateMutationSafety() {
  const auto snapshot = testSnapshot();
  const auto original = tonetrace::serializeProjectState(&snapshot);
  std::mt19937 generator(0x53544154U);
  std::uniform_int_distribution<std::size_t> position(0, original.size() - 1U);
  std::uniform_int_distribution<int> byte(0, 255);
  for (int trial = 0; trial < 2000; ++trial) {
    auto mutated = original;
    const int changes = 1 + trial % 8;
    for (int change = 0; change < changes; ++change) {
      mutated[position(generator)] = static_cast<char>(byte(generator));
    }
    try {
      (void)tonetrace::deserializeProjectState(mutated);
    } catch (const std::exception&) {
      // Rejection is expected for most mutations. The safety property is that
      // every input either restores a fully validated profile or throws.
    }
  }
  std::cout << "deterministic malformed-state mutation safety: passed\n";
}

std::vector<float> processInBlocks(tonetrace::RealtimeConvolver& processor,
                                   const std::vector<float>& input,
                                   const std::vector<std::size_t>& blockPattern) {
  std::vector<float> output(input.size(), 0.0F);
  std::size_t offset = 0;
  std::size_t pattern = 0;
  while (offset < input.size()) {
    const std::size_t count = std::min(blockPattern[pattern % blockPattern.size()],
                                       input.size() - offset);
    const float* inputs[]{input.data() + offset};
    float* outputs[]{output.data() + offset};
    processor.process(inputs, outputs, 1, count);
    offset += count;
    ++pattern;
  }
  return output;
}

double maximumDifference(const std::vector<float>& a,
                         const std::vector<float>& b) {
  require(a.size() == b.size(), "Compared realtime buffers differ in length");
  double maximum = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    maximum = std::max(maximum,
                       std::abs(static_cast<double>(a[i]) - b[i]));
  }
  return maximum;
}

void testRealtimeImpulseAndBlockSizes() {
  const std::vector<int> sampleRates{44100, 48000, 88200, 96000, 176400, 192000};
  for (const int sampleRate : sampleRates) {
    tonetrace::IrRenderSettings render;
    render.sampleRate = sampleRate;
    render.durationSeconds = 0.025;
    render.rangeLowHz = 20.0;
    render.rangeHighHz = 20000.0;
    const auto ir = tonetrace::renderMinimumPhaseIr(testModel(), render);
    std::vector<float> impulse(ir.size() + 2048U, 0.0F);
    impulse[0] = 1.0F;

    tonetrace::RealtimeConvolverConfig config;
    config.sampleRate = sampleRate;
    config.channels = 1;
    tonetrace::RealtimeConvolver processor(config);
    processor.installInitialKernel(ir);
    require(processor.latencyFrames() == 0,
            "Frozen convolution reports non-zero latency");
    const auto output = processInBlocks(processor, impulse,
                                        {1, 7, 31, 64, 127, 257, 511});
    double maximum = 0.0;
    for (std::size_t i = 0; i < ir.size(); ++i) {
      maximum = std::max(maximum,
          std::abs(static_cast<double>(output[i]) -
                   static_cast<double>(static_cast<float>(ir[i]))));
    }
    require(maximum < 2.0e-5,
            "Realtime impulse response differs from the canonical IR");
    require(std::abs(output.front() - static_cast<float>(ir.front())) < 1.0e-7F,
            "The direct FIR head did not produce sample zero immediately");
  }

  std::vector<double> longIr(5000, 0.0);
  longIr[0] = 0.8;
  longIr[19] = -0.2;
  longIr[200] = 0.15;
  longIr[1300] = -0.1;
  longIr[4097] = 0.05;
  std::vector<float> input(14000);
  std::mt19937 generator(0x54524143U);
  std::uniform_real_distribution<float> distribution(-0.5F, 0.5F);
  for (std::size_t i = 0; i < 9000; ++i) input[i] = distribution(generator);

  tonetrace::RealtimeConvolverConfig config;
  config.channels = 1;
  tonetrace::RealtimeConvolver sampleBlocks(config);
  tonetrace::RealtimeConvolver mixedBlocks(config);
  sampleBlocks.installInitialKernel(longIr);
  mixedBlocks.installInitialKernel(longIr);
  const auto a = processInBlocks(sampleBlocks, input, {1});
  const auto b = processInBlocks(mixedBlocks, input, {3, 64, 5, 1024, 17, 511});
  require(maximumDifference(a, b) == 0.0,
          "Realtime output depends on host block boundaries");
  std::cout << "zero-latency canonical IR at six rates and block invariance: passed\n";
}

void testStereoInPlaceSilenceAndBypass() {
  tonetrace::RealtimeConvolverConfig config;
  config.channels = 2;
  std::vector<double> ir(1500, 0.0);
  ir[0] = 0.5;
  ir[300] = 0.25;
  ir[1200] = -0.1;
  tonetrace::RealtimeConvolver processor(config);
  processor.installInitialKernel(ir);

  std::vector<float> left(4096, 0.0F);
  std::vector<float> right(4096, 0.0F);
  left[0] = 1.0F;
  right[10] = -1.0F;
  const float* inputs[]{left.data(), right.data()};
  float* inPlace[]{left.data(), right.data()};
  processor.process(inputs, inPlace, 2, left.size());
  require(std::abs(left[0] - 0.5F) < 1.0e-7F &&
              std::abs(right[10] + 0.5F) < 1.0e-7F &&
              std::all_of(left.begin(), left.end(),
                          [](float value) { return std::isfinite(value); }) &&
              std::all_of(right.begin(), right.end(),
                          [](float value) { return std::isfinite(value); }),
          "Stereo in-place processing failed");

  // Use a memoryless gain here so the transition assertion measures bypass
  // smoothing rather than a deliberately sparse IR's delayed echo onset.
  processor.installInitialKernel({0.5});
  processor.reset();
  processor.setBypassed(true);
  std::vector<float> constant(2000, 0.25F);
  const auto bypassed = processInBlocks(processor, constant, {37, 129});
  require(std::abs(bypassed.back() - 0.25F) < 1.0e-6F,
          "Smoothed bypass did not reach sample-aligned dry audio");
  double maximumStep = 0.0;
  for (std::size_t i = 1; i < bypassed.size(); ++i) {
    maximumStep = std::max(maximumStep,
        std::abs(static_cast<double>(bypassed[i] - bypassed[i - 1])));
  }
  require(maximumStep < 0.01,
          "Bypass transition produced a discontinuity");
  std::cout << "stereo, in-place, silence safety, and click-free bypass: passed\n";
}

void testKernelTransitionAndInputValidation() {
  tonetrace::RealtimeConvolverConfig config;
  config.channels = 1;
  config.minimumCrossfadeSeconds = 0.005;
  tonetrace::RealtimeConvolver processor(config);
  processor.installInitialKernel({1.0});
  std::vector<double> replacement(1500, 0.0);
  replacement[0] = 0.5;
  require(processor.submitKernel(replacement),
          "A prepared replacement kernel was not accepted");
  require(processor.submitKernel({0.25}),
          "A newer prepared kernel did not replace the stale pending edit");
  std::vector<float> constant(5000, 0.25F);
  const auto output = processInBlocks(processor, constant, {1, 73, 256, 11});
  double maximumStep = 0.0;
  for (std::size_t i = 1; i < output.size(); ++i) {
    maximumStep = std::max(maximumStep,
        std::abs(static_cast<double>(output[i] - output[i - 1])));
  }
  require(maximumStep < 0.002,
          "Kernel replacement produced an audible discontinuity");
  require(std::abs(output.back() - 0.0625F) < 1.0e-5F,
          "Rapid edits did not converge to the latest prepared kernel");
  processor.collectRetiredKernels();
  require(!processor.hasPendingKernel(),
          "Retired kernel ownership was not reclaimed on the control thread");

  requireThrows(
      [&] { processor.installInitialKernel({std::numeric_limits<double>::quiet_NaN()}); },
      "A non-finite realtime kernel was accepted");
  requireThrows(
      [] {
        tonetrace::RealtimeConvolverConfig invalid;
        invalid.directHeadFrames = 100;
        tonetrace::RealtimeConvolver rejected(invalid);
      },
      "A non-power-of-two direct partition was accepted");
  std::cout << "click-free kernel replacement and realtime input validation: passed\n";
}


void testRapidKernelCoalescing() {
  tonetrace::RealtimeConvolverConfig config;
  config.channels = 1;
  config.minimumCrossfadeSeconds = 0.0;
  tonetrace::RealtimeConvolver processor(config);
  processor.installInitialKernel({1.0});

  std::vector<double> first(256, 0.0);
  first[0] = 0.8;
  require(processor.submitKernel(first),
          "First rapid-edit kernel was not accepted");

  std::vector<float> start(16, 0.25F);
  (void)processInBlocks(processor, start, {16});

  require(processor.submitKernel({0.5}),
          "A kernel could not queue behind an active transition");
  require(processor.submitKernel({0.25}),
          "Latest rapid edit did not replace the queued intermediate kernel");

  std::vector<float> finishFirst(512, 0.25F);
  const auto beforeCleanup = processInBlocks(processor, finishFirst, {64});
  require(processor.hasRetiredKernel(),
          "Completed transition did not expose its retired kernel for cleanup");
  require(std::abs(beforeCleanup.back() - 0.2F) < 1.0e-4F,
          "A queued correction started before retired ownership was reclaimed");

  processor.collectRetiredKernels();
  require(!processor.hasRetiredKernel(),
          "Control-thread cleanup did not reclaim the retired kernel");

  std::vector<float> finishLatest(1024, 0.25F);
  const auto latest = processInBlocks(processor, finishLatest, {1, 31, 128});
  require(std::abs(latest.back() - 0.0625F) < 1.0e-5F,
          "Rapid edits did not settle on the latest queued correction");
  processor.collectRetiredKernels();
  require(!processor.hasPendingKernel(),
          "Rapid-edit transition did not fully settle after cleanup");
  std::cout << "rapid kernel edits coalesce to latest: passed\n";
}

void testAudioThreadAllocationFreedom() {
  tonetrace::RealtimeConvolverConfig config;
  config.channels = 2;
  std::vector<double> ir(9000, 0.0);
  ir[0] = 0.75;
  ir[200] = -0.1;
  ir[3000] = 0.05;
  ir[8999] = -0.01;
  tonetrace::RealtimeConvolver processor(config);
  processor.installInitialKernel(ir);
  std::vector<float> left(8192, 0.1F);
  std::vector<float> right(8192, -0.1F);
  std::vector<float> outputLeft(8192, 0.0F);
  std::vector<float> outputRight(8192, 0.0F);
  const float* inputs[]{left.data(), right.data()};
  float* outputs[]{outputLeft.data(), outputRight.data()};
  const auto before = gAllocationCalls.load(std::memory_order_relaxed);
  processor.process(inputs, outputs, 2, left.size());
  const auto after = gAllocationCalls.load(std::memory_order_relaxed);
  require(after == before,
          "Realtime process() performed a heap allocation");
  std::cout << "audio-thread heap-allocation check: passed\n";
}

void testHeadlessPluginCore() {
  tonetrace::RealtimeConvolverConfig config;
  config.channels = 1;
  tonetrace::HeadlessPluginCore core(config);
  require(core.phase() == tonetrace::WorkflowPhase::Ready &&
              core.latencyFrames() == 0,
          "A new headless core is not Ready at zero latency");
  const auto snapshot = testSnapshot();
  require(core.commitCandidate(snapshot).accepted &&
              core.phase() == tonetrace::WorkflowPhase::Frozen,
          "The headless core did not accept a valid frozen profile");
  const auto saved = core.saveProjectState();

  auto contaminated = snapshot;
  contaminated.targetDiagnostics.nonFiniteSamples = 1;
  require(!core.commitCandidate(contaminated).accepted,
          "The headless core accepted a contaminated replacement");
  require(core.saveProjectState() == saved,
          "A rejected replacement damaged the last-known-good state");

  tonetrace::HeadlessPluginCore restored(config);
  restored.loadProjectState(saved);
  require(restored.phase() == tonetrace::WorkflowPhase::Frozen &&
              restored.saveProjectState() == saved,
          "The headless core changed a restored Frozen state");
  const auto ir = tonetrace::renderMinimumPhaseIr(snapshot.uncappedModel,
                                                   snapshot.renderSettings);
  std::vector<float> impulse(ir.size() + 1024U, 0.0F);
  impulse[0] = 1.0F;
  std::vector<float> output(impulse.size(), 0.0F);
  const float* inputs[]{impulse.data()};
  float* outputs[]{output.data()};
  restored.process(inputs, outputs, 1, impulse.size());
  for (std::size_t i = 0; i < ir.size(); ++i) {
    require(std::abs(output[i] - static_cast<float>(ir[i])) < 2.0e-5F,
            "The restored headless core differs from its exported IR");
  }
  requireThrows([&] { restored.loadProjectState(saved); },
                "Project state changed after audio processing started");

  tonetrace::HeadlessPluginCore ready(config);
  ready.loadProjectState(tonetrace::serializeProjectState(nullptr));
  require(ready.phase() == tonetrace::WorkflowPhase::Ready,
          "Ready state unexpectedly restored a profile");
  std::cout << "headless host adapter, last-known-good, and state restore: passed\n";
}

void testManualOnlyGraphicEq() {
  tonetrace::RealtimeConvolverConfig config;
  config.sampleRate = 48000;
  config.channels = 1;
  tonetrace::HeadlessPluginCore core(config);

  tonetrace::IrRenderSettings settings;
  settings.sampleRate = config.sampleRate;
  settings.durationSeconds = 0.02;
  settings.rangeLowHz = 20.0;
  settings.rangeHighHz = 20000.0;
  settings.manualGains.assign(30, 0.0);
  settings.manualGains[15] = 6.0;
  require(tonetrace::hasManualCorrection(settings),
          "A nonzero graphic-EQ band was classified as flat");

  const auto committed = core.commitManualCorrection(settings);
  require(committed.accepted,
          "A valid manual-only graphic EQ curve was rejected");
  require(core.phase() == tonetrace::WorkflowPhase::Ready &&
              core.frozenSnapshot() == nullptr,
          "Manual-only graphic EQ incorrectly created a learned profile");
  const auto restored =
      tonetrace::deserializeProjectState(core.saveProjectState());
  require(restored.phase == tonetrace::WorkflowPhase::Ready &&
              !restored.snapshot,
          "Manual-only graphic EQ leaked into learned-profile project state");

  const auto expected = tonetrace::renderManualCorrectionIr(settings);
  std::vector<float> impulse(expected.size() + 64U, 0.0F);
  impulse[0] = 1.0F;
  std::vector<float> output(impulse.size(), 0.0F);
  const float* inputs[]{impulse.data()};
  float* outputs[]{output.data()};
  core.process(inputs, outputs, 1, impulse.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    require(std::abs(output[i] - static_cast<float>(expected[i])) < 2.0e-5F,
            "Manual-only graphic EQ audio differs from its rendered curve");
  }

  tonetrace::HeadlessPluginCore flat(config);
  settings.manualGains.assign(30, 0.0);
  require(!tonetrace::hasManualCorrection(settings),
          "A zeroed graphic EQ was classified as altered");
  require(flat.commitManualCorrection(settings).accepted,
          "A flat manual-only curve was rejected");
  std::vector<float> dry(128, 0.125F);
  std::vector<float> wet(dry.size(), 0.0F);
  const float* dryIn[]{dry.data()};
  float* wetOut[]{wet.data()};
  flat.process(dryIn, wetOut, 1, dry.size());
  require(std::equal(dry.begin(), dry.end(), wet.begin()),
          "A zeroed graphic EQ did not remain bit-exact bypass");

  settings.correctionGainDb = 2.25;
  require(tonetrace::hasManualCorrection(settings),
          "A nonzero global Correction Gain was classified as flat");
  require(!tonetrace::renderManualCorrectionIr(settings).empty(),
          "A gain-only manual correction did not render an IR");

  std::cout << "manual-only graphic EQ without learned profile: passed\n";
}

void testInitialCommitProcessHandoff() {
  for (int iteration = 0; iteration < 48; ++iteration) {
    tonetrace::RealtimeConvolverConfig config;
    config.channels = 1;
    tonetrace::HeadlessPluginCore core(config);
    const auto snapshot = testSnapshot();
    tonetrace::ProfileValidation committed;
    std::atomic<bool> start{false};
    std::vector<float> input(64, 0.125F);
    std::vector<float> output(64, 0.0F);
    const float* inputs[]{input.data()};
    float* outputs[]{output.data()};

    std::thread control([&] {
      while (!start.load(std::memory_order_acquire)) {
      }
      committed = core.commitCandidate(snapshot);
    });
    std::thread audio([&] {
      while (!start.load(std::memory_order_acquire)) {
      }
      core.process(inputs, outputs, 1, input.size());
    });
    start.store(true, std::memory_order_release);
    control.join();
    audio.join();

    require(committed.accepted,
            "The first process/commit handoff rejected a valid correction");
    require(std::all_of(output.begin(), output.end(),
                        [](float value) { return std::isfinite(value); }),
            "The first process/commit handoff produced invalid audio");
  }

  tonetrace::RealtimeConvolverConfig highRate;
  highRate.sampleRate = 192000;
  highRate.channels = 1;
  tonetrace::HeadlessPluginCore bounded(highRate);
  auto excessive = testSnapshot();
  excessive.renderSettings.sampleRate = highRate.sampleRate;
  excessive.renderSettings.durationSeconds = 10.0;
  const auto rejected = bounded.commitCandidate(excessive);
  require(!rejected.accepted &&
              rejected.issue == tonetrace::ProfileIssue::InvalidModel &&
              bounded.phase() == tonetrace::WorkflowPhase::Ready,
          "An oversized realtime profile escaped or damaged the ready state");
  std::cout << "first-block handoff and realtime IR limit: passed\n";
}

}  // namespace

int main() {
  try {
    require(!tonetrace::targetCaptureCanCorrect(
                tonetrace::WorkflowPhase::CapturingTarget, 0, false, false),
            "zero-confidence non-full Target unexpectedly became usable");
    require(tonetrace::targetCaptureCanCorrect(
                tonetrace::WorkflowPhase::CapturingTarget, 1, false, false),
            "low-confidence Target was rejected");
    require(tonetrace::targetCaptureCanCorrect(
                tonetrace::WorkflowPhase::CapturingTarget, 0, true, false),
            "full zero-confidence Target fallback was rejected");
    require(tonetrace::targetCaptureCanCorrect(
                tonetrace::WorkflowPhase::Ready, 0, false, true),
            "imported Target fallback was rejected");
    testParameterContract();
    testCaptureDiagnosticsAndValidation();
    testProjectState();
    testProjectStateMutationSafety();
    testRealtimeImpulseAndBlockSizes();
    testStereoInPlaceSilenceAndBypass();
    testKernelTransitionAndInputValidation();
    testRapidKernelCoalescing();
    testAudioThreadAllocationFreedom();
    testHeadlessPluginCore();
    testManualOnlyGraphicEq();
    testInitialCommitProcessHandoff();
    std::cout << "all Tone Trace realtime-core tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "REALTIME TEST FAILURE: " << error.what() << '\n';
    return 1;
  }
}
