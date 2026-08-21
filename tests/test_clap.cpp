#include <clap/clap.h>

#include "tonetrace/tonetrace_realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

constexpr clap_id kWorkflow = 100;
constexpr clap_id kMatchMode = 110;
constexpr clap_id kLastCommand = 195;
constexpr clap_id kConfidence = 200;
constexpr clap_id kCurveDrift = 210;
constexpr clap_id kCaptureTime = 220;
constexpr clap_id kStatus = 230;
constexpr clap_id kToneNotifications = 240;
constexpr clap_id kToneLevel = 250;
constexpr clap_id kEmergencyClipGuard = 270;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct DynamicLibrary {
#if defined(_WIN32)
  HMODULE handle = nullptr;
#else
  void* handle = nullptr;
#endif

  explicit DynamicLibrary(const char* path) {
#if defined(_WIN32)
    handle = LoadLibraryA(path);
#else
    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    require(handle != nullptr, "could not load CLAP module");
  }

  ~DynamicLibrary() {
    if (handle == nullptr) return;
#if defined(_WIN32)
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
  }

  template <typename T>
  T symbol(const char* name) const {
#if defined(_WIN32)
    return reinterpret_cast<T>(GetProcAddress(handle, name));
#else
    return reinterpret_cast<T>(dlsym(handle, name));
#endif
  }
};

struct HostState {
  clap_host_t host{};
  clap_host_params_t params{};
  std::uint32_t callbacks = 0;
  std::uint32_t rescans = 0;

  HostState() {
    host.clap_version = CLAP_VERSION;
    host.host_data = this;
    host.name = "Tone Trace test host";
    host.vendor = "LanesAudio tests";
    host.url = "https://lanesaudio.com";
    host.version = "1";
    host.get_extension = getExtension;
    host.request_restart = requestRestart;
    host.request_process = requestProcess;
    host.request_callback = requestCallback;
    params.rescan = rescan;
    params.clear = clear;
    params.request_flush = requestFlush;
  }

  static HostState* self(const clap_host_t* host) {
    return static_cast<HostState*>(host->host_data);
  }

  static const void* CLAP_ABI getExtension(const clap_host_t* host,
                                           const char* id) {
    return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &self(host)->params : nullptr;
  }
  static void CLAP_ABI requestRestart(const clap_host_t*) {}
  static void CLAP_ABI requestProcess(const clap_host_t*) {}
  static void CLAP_ABI requestCallback(const clap_host_t* host) {
    ++self(host)->callbacks;
  }
  static void CLAP_ABI rescan(const clap_host_t* host,
                              clap_param_rescan_flags) {
    ++self(host)->rescans;
  }
  static void CLAP_ABI clear(const clap_host_t*, clap_id,
                             clap_param_clear_flags) {}
  static void CLAP_ABI requestFlush(const clap_host_t*) {}
};

struct InputEvents {
  clap_input_events_t events_interface{};
  std::vector<clap_event_param_value_t> events;

  InputEvents() {
    events_interface.ctx = this;
    events_interface.size = size;
    events_interface.get = get;
  }

  void parameter(std::uint32_t time, clap_id id, double value) {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = time;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = id;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    events.push_back(event);
  }

  void command(std::uint32_t time, int value) {
    parameter(time, kWorkflow, value);
  }

  static uint32_t CLAP_ABI size(const clap_input_events_t* list) {
    return static_cast<uint32_t>(
        static_cast<const InputEvents*>(list->ctx)->events.size());
  }

  static const clap_event_header_t* CLAP_ABI get(
      const clap_input_events_t* list, uint32_t index) {
    const auto& events = static_cast<const InputEvents*>(list->ctx)->events;
    return index < events.size() ? &events[index].header : nullptr;
  }
};

struct OutputEvents {
  clap_output_events_t events_interface{};
  std::vector<clap_event_param_value_t> events;

  OutputEvents() {
    events_interface.ctx = this;
    events_interface.try_push = tryPush;
  }

  static bool CLAP_ABI tryPush(const clap_output_events_t* list,
                               const clap_event_header_t* header) {
    auto* self = static_cast<OutputEvents*>(list->ctx);
    if (header == nullptr || header->type != CLAP_EVENT_PARAM_VALUE ||
        header->size < sizeof(clap_event_param_value_t)) {
      return false;
    }
    self->events.push_back(
        *reinterpret_cast<const clap_event_param_value_t*>(header));
    return true;
  }
};

struct MemoryOutput {
  clap_ostream_t stream{};
  std::string bytes;

  MemoryOutput() {
    stream.ctx = this;
    stream.write = write;
  }

  static int64_t CLAP_ABI write(const clap_ostream_t* stream,
                                const void* data,
                                uint64_t size) {
    auto* self = static_cast<MemoryOutput*>(stream->ctx);
    const auto partial = static_cast<std::size_t>(std::min<uint64_t>(size, 17));
    self->bytes.append(static_cast<const char*>(data), partial);
    return static_cast<int64_t>(partial);
  }
};

struct MemoryInput {
  clap_istream_t stream{};
  const std::string* bytes = nullptr;
  std::size_t offset = 0;

  explicit MemoryInput(const std::string& data) : bytes(&data) {
    stream.ctx = this;
    stream.read = read;
  }

  static int64_t CLAP_ABI read(const clap_istream_t* stream,
                               void* output,
                               uint64_t size) {
    auto* self = static_cast<MemoryInput*>(stream->ctx);
    const std::size_t remaining = self->bytes->size() - self->offset;
    const std::size_t count = std::min<std::size_t>(
        {remaining, static_cast<std::size_t>(size), 13});
    if (count == 0) return 0;
    std::memcpy(output, self->bytes->data() + self->offset, count);
    self->offset += count;
    return static_cast<int64_t>(count);
  }
};

struct PluginInstance {
  const clap_plugin_t* plugin = nullptr;
  const clap_plugin_params_t* params = nullptr;
  const clap_plugin_state_t* state = nullptr;
  const clap_plugin_latency_t* latency = nullptr;
  const clap_plugin_tail_t* tail = nullptr;

  explicit PluginInstance(const clap_plugin_t* value) : plugin(value) {
    require(plugin != nullptr && plugin->init(plugin), "plugin init failed");
    params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    state = static_cast<const clap_plugin_state_t*>(
        plugin->get_extension(plugin, CLAP_EXT_STATE));
    latency = static_cast<const clap_plugin_latency_t*>(
        plugin->get_extension(plugin, CLAP_EXT_LATENCY));
    tail = static_cast<const clap_plugin_tail_t*>(
        plugin->get_extension(plugin, CLAP_EXT_TAIL));
    require(params && state && latency && tail, "required extension missing");
  }

  ~PluginInstance() {
    if (plugin != nullptr) plugin->destroy(plugin);
  }

  double parameter(clap_id id) const {
    double result = -1.0;
    require(params->get_value(plugin, id, &result), "parameter query failed");
    return result;
  }
};

template <typename Sample>
void process(const clap_plugin_t* plugin,
             std::array<std::vector<Sample>, 2>& input,
             std::array<std::vector<Sample>, 2>& output,
             InputEvents* inputEvents = nullptr,
             OutputEvents* outputEvents = nullptr) {
  require(input[0].size() == input[1].size(), "input size mismatch");
  const auto frames = static_cast<uint32_t>(input[0].size());
  output[0].assign(frames, Sample{});
  output[1].assign(frames, Sample{});
  Sample* inputPointers[]{input[0].data(), input[1].data()};
  Sample* outputPointers[]{output[0].data(), output[1].data()};
  clap_audio_buffer_t in{};
  clap_audio_buffer_t out{};
  if constexpr (std::is_same_v<Sample, float>) {
    in.data32 = inputPointers;
    out.data32 = outputPointers;
  } else {
    in.data64 = inputPointers;
    out.data64 = outputPointers;
  }
  in.channel_count = 2;
  out.channel_count = 2;
  clap_process_t block{};
  block.steady_time = -1;
  block.frames_count = frames;
  block.audio_inputs = &in;
  block.audio_outputs = &out;
  block.audio_inputs_count = 1;
  block.audio_outputs_count = 1;
  block.in_events = inputEvents ? &inputEvents->events_interface : nullptr;
  block.out_events = outputEvents ? &outputEvents->events_interface : nullptr;
  require(plugin->process(plugin, &block) == CLAP_PROCESS_CONTINUE,
          "audio process failed");
}

double processCommand(const clap_plugin_t* plugin,
                      int command,
                      int expectedPersisted = -1,
                      std::size_t frames = 64) {
  std::array<std::vector<float>, 2> input{
      std::vector<float>(frames), std::vector<float>(frames)};
  std::array<std::vector<float>, 2> output;
  InputEvents events;
  OutputEvents responses;
  events.command(0, command);
  process(plugin, input, output, &events, &responses);
  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  double persisted = -1.0;
  if (expectedPersisted < 0) expectedPersisted = command;
  require(params != nullptr && params->get_value(plugin, kWorkflow, &persisted) &&
              static_cast<int>(persisted) == expectedPersisted,
          "workflow step did not persist at the requested value");
  double energy = 0.0;
  for (const auto& channel : output) {
    for (const float sample : channel) energy += std::abs(sample);
  }
  return energy;
}

void processParameter(const clap_plugin_t* plugin,
                      clap_id id,
                      double requested) {
  std::array<std::vector<float>, 2> input{
      std::vector<float>(64), std::vector<float>(64)};
  std::array<std::vector<float>, 2> output;
  InputEvents events;
  events.parameter(0, id, requested);
  process(plugin, input, output, &events, nullptr);
}

void flushParameter(const clap_plugin_t* plugin,
                    clap_id id,
                    double requested) {
  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  require(params != nullptr && params->flush != nullptr,
          "CLAP parameter flush is unavailable");
  InputEvents input;
  OutputEvents output;
  input.parameter(0, id, requested);
  params->flush(plugin, &input.events_interface, &output.events_interface);
}

double processSignal(const clap_plugin_t* plugin,
                     const std::array<std::vector<float>, 2>& signal,
                     int firstCommand = 0) {
  constexpr std::size_t blockSize = 256;
  double outputDifference = 0.0;
  for (std::size_t offset = 0; offset < signal[0].size(); offset += blockSize) {
    const std::size_t count = std::min(blockSize, signal[0].size() - offset);
    std::array<std::vector<float>, 2> input;
    std::array<std::vector<float>, 2> output;
    for (std::size_t channel = 0; channel < 2; ++channel) {
      input[channel].assign(signal[channel].begin() + offset,
                            signal[channel].begin() + offset + count);
    }
    InputEvents events;
    OutputEvents responses;
    if (offset == 0 && firstCommand != 0) events.command(0, firstCommand);
    process(plugin, input, output, events.events.empty() ? nullptr : &events,
            &responses);
    for (std::size_t channel = 0; channel < 2; ++channel) {
      for (std::size_t frame = 0; frame < count; ++frame) {
        outputDifference += std::abs(output[channel][frame] -
                                     input[channel][frame]);
      }
    }
    require(std::none_of(responses.events.begin(), responses.events.end(),
                         [](const auto& event) {
                           return event.param_id == kWorkflow;
                         }),
            "audio emitted an unexpected workflow step event");
  }
  return outputDifference;
}

std::vector<float> impulseResponse(const clap_plugin_t* plugin,
                                   std::size_t frames) {
  std::array<std::vector<float>, 2> input{
      std::vector<float>(frames), std::vector<float>(frames)};
  input[0][0] = 1.0F;
  input[1][0] = 1.0F;
  std::array<std::vector<float>, 2> output;
  process(plugin, input, output);
  return output[0];
}

std::array<std::vector<float>, 2> fixtureSignal(
    const std::filesystem::path& path,
    std::size_t firstFrame,
    std::size_t frameCount) {
  const auto audio = tonetrace::readWav(path);
  require(audio.sampleRate > 0 && !audio.channels.empty(),
          "real fixture could not be decoded: " + path.string());
  const std::size_t available = audio.channels.front().size();
  require(firstFrame <= available, "real fixture slice begins past EOF");
  frameCount = std::min(frameCount, available - firstFrame);
  std::array<std::vector<float>, 2> signal;
  for (std::size_t channel = 0; channel < 2; ++channel) {
    const auto& source = audio.channels[std::min(channel, audio.channels.size() - 1)];
    signal[channel].resize(frameCount);
    std::transform(source.begin() + firstFrame,
                   source.begin() + firstFrame + frameCount,
                   signal[channel].begin(),
                   [](double sample) { return static_cast<float>(sample); });
  }
  return signal;
}

void runRealFixturePair(const clap_plugin_factory_t* factory,
                        const clap_plugin_descriptor_t* descriptor,
                        const std::filesystem::path& referencePath,
                        const std::filesystem::path& targetPath,
                        int mode) {
  const auto referenceAudio = tonetrace::readWav(referencePath);
  const auto targetAudio = tonetrace::readWav(targetPath);
  require(referenceAudio.sampleRate == targetAudio.sampleRate,
          "paired fixtures have different sample rates");
  const int sampleRate = referenceAudio.sampleRate;
  const std::size_t maximumFrames = static_cast<std::size_t>(sampleRate) * 29U;
  const std::size_t referenceFrames = std::min(
      maximumFrames, referenceAudio.channels.front().size());
  const std::size_t targetFrames = std::min(
      maximumFrames, targetAudio.channels.front().size());
  const std::size_t snapshotFrame = std::min<std::size_t>(
      static_cast<std::size_t>(sampleRate), referenceFrames);

  HostState host;
  PluginInstance instance(
      factory->create_plugin(factory, &host.host, descriptor->id));
  require(instance.plugin->activate(instance.plugin, sampleRate, 1, 16384),
          "real-fixture plugin activation failed");
  require(instance.plugin->start_processing(instance.plugin),
          "real-fixture processing start failed");
  processParameter(instance.plugin, kMatchMode, mode);
  processCommand(instance.plugin, 1);

  auto referenceStart = fixtureSignal(referencePath, 0, snapshotFrame);
  (void)processSignal(instance.plugin, referenceStart);
  MemoryOutput routineSnapshot;
  require(instance.state->save(instance.plugin, &routineSnapshot.stream),
          "routine host state snapshot failed during capture");
  require(instance.parameter(kWorkflow) == 1.0 &&
              instance.parameter(kStatus) != 0.0,
          "routine host state snapshot cancelled live Reference capture");
  auto referenceRest = fixtureSignal(
      referencePath, snapshotFrame, referenceFrames - snapshotFrame);
  (void)processSignal(instance.plugin, referenceRest);
  require(instance.parameter(kCaptureTime) >= 0.35 &&
              instance.parameter(kConfidence) >= (1.0 / 3.0),
          "real Reference did not reach usable confidence: " +
              referencePath.filename().string() + ", time=" +
              std::to_string(instance.parameter(kCaptureTime)) + ", drift=" +
              std::to_string(instance.parameter(kCurveDrift)) + ", status=" +
              std::to_string(instance.parameter(kStatus)));
  const double referenceTime = instance.parameter(kCaptureTime);
  const double referenceConfidence = instance.parameter(kConfidence);
  const double referenceDrift = instance.parameter(kCurveDrift);

  processCommand(instance.plugin, 2);
  require(instance.parameter(kWorkflow) == 2.0 &&
              instance.parameter(kStatus) != 8.0,
          "real Reference could not advance to Target: " +
              referencePath.filename().string());
  auto target = fixtureSignal(targetPath, 0, targetFrames);
  (void)processSignal(instance.plugin, target);
  require(instance.parameter(kCaptureTime) >= 0.35 &&
              instance.parameter(kConfidence) >= (1.0 / 3.0),
          "real Target did not reach usable confidence: " +
              targetPath.filename().string() + ", time=" +
              std::to_string(instance.parameter(kCaptureTime)) + ", drift=" +
              std::to_string(instance.parameter(kCurveDrift)) + ", status=" +
              std::to_string(instance.parameter(kStatus)));
  const double targetTime = instance.parameter(kCaptureTime);
  const double targetConfidence = instance.parameter(kConfidence);
  const double targetDrift = instance.parameter(kCurveDrift);
  processCommand(instance.plugin, 3);
  instance.plugin->on_main_thread(instance.plugin);
  require(instance.parameter(kStatus) == 4.0,
          "real fixture pair did not produce Preview correction");
  processCommand(instance.plugin, 4);
  require(instance.parameter(kStatus) == 5.0 &&
              instance.latency->get(instance.plugin) == 0,
          "real fixture pair did not Freeze at zero latency");
  instance.plugin->stop_processing(instance.plugin);
  instance.plugin->deactivate(instance.plugin);

  PluginInstance snapshotRestored(
      factory->create_plugin(factory, &host.host, descriptor->id));
  MemoryInput routineLoad(routineSnapshot.bytes);
  require(snapshotRestored.state->load(snapshotRestored.plugin,
                                       &routineLoad.stream) &&
              snapshotRestored.plugin->activate(snapshotRestored.plugin,
                                                sampleRate, 1, 16384) &&
              snapshotRestored.plugin->start_processing(snapshotRestored.plugin),
          "routine capture snapshot could not be restored safely");
  require(snapshotRestored.parameter(kWorkflow) == 0.0 &&
              snapshotRestored.parameter(kStatus) == 0.0 &&
              snapshotRestored.tail->get(snapshotRestored.plugin) == 0,
          "routine capture snapshot restored unfinished learning instead of Ready");
  snapshotRestored.plugin->stop_processing(snapshotRestored.plugin);
  snapshotRestored.plugin->deactivate(snapshotRestored.plugin);

  std::cout << "real pair " << referencePath.filename().string() << " -> "
            << targetPath.filename().string() << ": Reference "
            << referenceTime << " s, confidence " << referenceConfidence
            << ", drift " << referenceDrift << " dB; Target " << targetTime
            << " s, confidence " << targetConfidence << ", drift "
            << targetDrift << " dB; Preview/Freeze passed\n";
}


void fullCaptureZeroConfidenceFallback(const clap_plugin_factory_t* factory,
                                       const clap_plugin_descriptor_t* descriptor) {
  HostState host;
  PluginInstance instance(factory->create_plugin(factory, &host.host, descriptor->id));
  require(instance.plugin->activate(instance.plugin, 48000.0, 1, 16384),
          "full-capture fallback activation failed");
  require(instance.plugin->start_processing(instance.plugin),
          "full-capture fallback processing start failed");
  processParameter(instance.plugin, kMatchMode, 1.0);  // Voice: strictest confidence gate.

  constexpr std::size_t referenceFrames = 48000;
  std::array<std::vector<float>, 2> reference{
      std::vector<float>(referenceFrames), std::vector<float>(referenceFrames)};
  for (std::size_t frame = 0; frame < referenceFrames; ++frame) {
    const float sample = static_cast<float>(
        0.08 * std::sin(static_cast<double>(frame) * 0.071));
    reference[0][frame] = sample;
    reference[1][frame] = sample;
  }
  processSignal(instance.plugin, reference, 1);
  processCommand(instance.plugin, 2);

  constexpr std::size_t targetFrames = 48000 * 31U;
  std::array<std::vector<float>, 2> target{
      std::vector<float>(targetFrames), std::vector<float>(targetFrames)};
  constexpr std::size_t stableFrames = 48000 * 294U / 10U;  // 29.4 s.
  constexpr std::size_t interval = 2400;  // 50 ms.
  constexpr double pi = 3.14159265358979323846;
  const std::array<double, 7> frequencies{60.0, 180.0, 700.0, 2200.0,
                                          6000.0, 12000.0, 17000.0};
  for (std::size_t frame = 0; frame < targetFrames; ++frame) {
    double amplitude = 0.0035;
    double frequency = 180.0;
    if (frame >= stableFrames) {
      const std::size_t step = (frame - stableFrames) / interval;
      amplitude = std::min(0.85, 0.0035 * std::pow(2.0, static_cast<double>(step)));
      frequency = frequencies[step % frequencies.size()];
    }
    const float sample = static_cast<float>(amplitude *
        std::sin(2.0 * pi * frequency * static_cast<double>(frame) / 48000.0));
    target[0][frame] = sample;
    target[1][frame] = sample;
  }
  processSignal(instance.plugin, target);
  require(instance.parameter(kStatus) == 7.0,
          "full accepted Target did not report Capture full");
  require(instance.parameter(kConfidence) == 0.0,
          "fallback fixture did not end at zero confidence; confidence=" +
              std::to_string(instance.parameter(kConfidence)) + ", drift=" +
              std::to_string(instance.parameter(kCurveDrift)));
  processCommand(instance.plugin, 3);
  require(instance.parameter(kStatus) == 3.0,
          "full zero-confidence Target could not continue to analysis");
  instance.plugin->on_main_thread(instance.plugin);
  require(instance.parameter(kStatus) == 4.0,
          "full zero-confidence Target did not produce Preview");
  instance.plugin->stop_processing(instance.plugin);
  instance.plugin->deactivate(instance.plugin);
}

void run(const char* modulePath,
         const std::vector<std::pair<std::filesystem::path,
                                     std::filesystem::path>>& fixturePairs,
         const std::vector<int>& fixtureModes) {
  DynamicLibrary module(modulePath);
  const auto* entry = module.symbol<const clap_plugin_entry_t*>("clap_entry");
  require(entry != nullptr, "clap_entry is not exported");
  require(clap_version_is_compatible(entry->clap_version),
          "incompatible CLAP entry version");
  require(entry->init(modulePath) && entry->init(modulePath),
          "repeated entry initialization failed");
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  require(factory != nullptr && factory->get_plugin_count(factory) == 1,
          "plugin factory is invalid");
  const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
  require(descriptor != nullptr &&
              std::strcmp(descriptor->id, "com.lanesaudio.tonetrace-eq") == 0,
          "plugin descriptor is invalid");
  fullCaptureZeroConfidenceFallback(factory, descriptor);

  HostState host;
  {
    PluginInstance instance(
        factory->create_plugin(factory, &host.host, descriptor->id));
    const auto* ports = static_cast<const clap_plugin_audio_ports_t*>(
        instance.plugin->get_extension(instance.plugin, CLAP_EXT_AUDIO_PORTS));
    require(ports != nullptr && ports->count(instance.plugin, true) == 1 &&
                ports->count(instance.plugin, false) == 1,
            "audio port contract is invalid");
    clap_audio_port_info_t port{};
    require(ports->get(instance.plugin, 0, true, &port) &&
                port.channel_count == 2 &&
                (port.flags & CLAP_AUDIO_PORT_SUPPORTS_64BITS) != 0,
            "stereo input port is invalid");

    require(instance.params->count(instance.plugin) == 19,
            "unexpected parameter count");
    std::vector<clap_id> ids;
    std::vector<clap_id> orderedIds;
    for (uint32_t index = 0; index < instance.params->count(instance.plugin);
         ++index) {
      clap_param_info_t info{};
      require(instance.params->get_info(instance.plugin, index, &info),
              "parameter descriptor query failed");
      require(info.name[0] != '\0' && info.min_value <= info.default_value &&
                  info.default_value <= info.max_value,
              "invalid parameter descriptor");
      require((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0,
              "a parameter is hidden from OSARA because it is not automatable");
      ids.push_back(info.id);
      orderedIds.push_back(info.id);
    }
    std::sort(ids.begin(), ids.end());
    require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(),
            "duplicate parameter id");
    const std::vector<clap_id> expectedOrder{
        100, 230, 195, 110, 160, 170, 120, 130, 140, 150,
        190, 180, 270, 250, 200, 210, 220, 240, 260};
    require(orderedIds == expectedOrder,
            "generic parameter order no longer matches the frozen accessible design");
    char accessibleText[128]{};
    double parsedValue = -1.0;
    require(instance.params->value_to_text(instance.plugin, kWorkflow, 2.0,
                                           accessibleText,
                                           sizeof(accessibleText)) &&
                std::strcmp(accessibleText,
                            "Save Reference and Learn Target") == 0 &&
                instance.params->text_to_value(instance.plugin, kWorkflow,
                                               "Capture Reference",
                                               &parsedValue) &&
                parsedValue == 1.0,
            "workflow labels are not round-trippable through the generic host interface");
    require(instance.params->value_to_text(instance.plugin, kToneLevel, -60.0,
                                           accessibleText,
                                           sizeof(accessibleText)) &&
                std::strcmp(accessibleText, "Off") == 0 &&
                instance.params->text_to_value(instance.plugin, kToneLevel,
                                               "Off", &parsedValue) &&
                parsedValue == -60.0,
            "Confidence Tone Volume does not expose its accessible Off state");
    require(instance.latency->get(instance.plugin) == 0 &&
                instance.tail->get(instance.plugin) == 0,
            "ready plugin must be zero latency with no tail");
    require(instance.parameter(kToneNotifications) == 1.0,
            "confidence tones are not enabled by default");

    require(instance.plugin->activate(instance.plugin, 48000.0, 1, 16384),
            "activation failed");
    require(instance.plugin->start_processing(instance.plugin),
            "start processing failed");

    {
      std::array<std::vector<float>, 2> input{
          std::vector<float>(64), std::vector<float>(64)};
      std::array<std::vector<float>, 2> output;
      InputEvents attemptedWrite;
      OutputEvents restoredValue;
      attemptedWrite.parameter(0, kConfidence, 1.0);
      process(instance.plugin, input, output, &attemptedWrite, &restoredValue);
      require(instance.parameter(kConfidence) == 0.0 &&
                  std::any_of(restoredValue.events.begin(),
                              restoredValue.events.end(),
                              [](const auto& event) {
                                return event.param_id == kConfidence &&
                                       event.value == 0.0;
                              }),
              "a moved read-only status value did not self-restore for the host");
    }

    std::array<std::vector<double>, 2> doubleInput{
        std::vector<double>(257), std::vector<double>(257)};
    for (std::size_t i = 0; i < doubleInput[0].size(); ++i) {
      doubleInput[0][i] = std::sin(static_cast<double>(i) * 0.07) * 0.1;
      doubleInput[1][i] = std::cos(static_cast<double>(i) * 0.05) * 0.1;
    }
    std::array<std::vector<double>, 2> doubleOutput;
    process(instance.plugin, doubleInput, doubleOutput);
    double doublePathError = 0.0;
    for (std::size_t channel = 0; channel < 2; ++channel) {
      for (std::size_t frame = 0; frame < doubleInput[channel].size(); ++frame) {
        doublePathError = std::max(
            doublePathError,
            std::abs(doubleInput[channel][frame] - doubleOutput[channel][frame]));
      }
    }
    require(doublePathError < 1.0e-7,
            "ready double-precision path is not identity within float precision");

    // Stepped host values can arrive a few floating-point ulps away from the
    // intended integer. They must round to the nearest step rather than being
    // truncated backward (critical for Workflow Step / OSARA navigation).
    processParameter(instance.plugin, kToneNotifications, 0.999999999);
    require(instance.parameter(kToneNotifications) == 1.0,
            "A near-integer stepped parameter was truncated to the wrong step");
    processParameter(instance.plugin, kToneNotifications, 0.9);
    require(instance.parameter(kToneNotifications) == 0.0,
            "A genuinely fractional stepped parameter stopped following CLAP truncation semantics");
    processParameter(instance.plugin, kToneNotifications, 1.0);

    processParameter(instance.plugin, kEmergencyClipGuard, -12.0);
    std::array<std::vector<float>, 2> guardInput{
        std::vector<float>(256, 0.9F), std::vector<float>(256, -0.9F)};
    std::array<std::vector<float>, 2> guardOutput;
    process(instance.plugin, guardInput, guardOutput);
    const float guardLimit = static_cast<float>(std::pow(10.0, -12.0 / 20.0));
    require(std::all_of(guardOutput[0].begin(), guardOutput[0].end(),
                        [guardLimit](float sample) {
                          return std::abs(sample) <= guardLimit + 1.0e-6F;
                        }) &&
                std::all_of(guardOutput[1].begin(), guardOutput[1].end(),
                            [guardLimit](float sample) {
                              return std::abs(sample) <= guardLimit + 1.0e-6F;
                            }),
            "Emergency Clip Guard did not enforce its explicit last-resort ceiling");
    processParameter(instance.plugin, kEmergencyClipGuard, 6.0);

    std::array<std::vector<float>, 2> gatedSilence{
        std::vector<float>(48000 * 2), std::vector<float>(48000 * 2)};
    flushParameter(instance.plugin, kWorkflow, 1.0);
    require(instance.parameter(kWorkflow) == 1.0,
            "flushed Workflow Step was not retained for the audio callback");
    require(instance.parameter(kLastCommand) == 1.0 &&
                instance.parameter(kStatus) == 1.0,
            "a flushed workflow step did not execute without an audio callback");
    processSignal(instance.plugin, gatedSilence);
    require(instance.parameter(kConfidence) == 0.0,
            "silence produced live capture confidence");
    require(instance.parameter(kLastCommand) == 1.0,
            "Accessible Last Command did not mirror Capture Reference");
    processParameter(instance.plugin, kMatchMode, 1.0);
    require(instance.parameter(kMatchMode) == 0.0 &&
                instance.parameter(kStatus) == 18.0,
            "a setup control changed during capture instead of self-restoring");
    processCommand(instance.plugin, 0);
    processCommand(instance.plugin, 1);
    processParameter(instance.plugin, kToneLevel, -30.0);
    const double quietWarning = processCommand(instance.plugin, 2, 1, 12000);
    require(instance.parameter(kStatus) == 27.0 && quietWarning > 0.01,
            "premature Save did not recover to Capture Reference with a warning sweep");
    processParameter(instance.plugin, kToneLevel, -12.0);
    const double loudWarning = processCommand(instance.plugin, 2, 1, 12000);
    require(loudWarning > quietWarning * 4.0,
            "Tone Level did not control warning-sweep amplitude");
    processParameter(instance.plugin, kToneNotifications, 0.0);
    const double mutedWarning = processCommand(instance.plugin, 2, 1, 12000);
    require(mutedWarning == 0.0,
            "Tone Notifications Off did not mute real tone output");
    processParameter(instance.plugin, kToneNotifications, 1.0);

    std::array<std::vector<float>, 2> abandonedCapture{
        std::vector<float>(512, 0.05F), std::vector<float>(512, -0.05F)};
    processSignal(instance.plugin, abandonedCapture, 1);
    char collectingText[64]{};
    require(instance.parameter(kCaptureTime) > 0.0 &&
                instance.params->value_to_text(instance.plugin, kConfidence,
                                               0.0, collectingText,
                                               sizeof(collectingText)) &&
                std::strcmp(collectingText, "Not yet confident") == 0,
            "accepted audio was still mislabeled as no valid audio");
    MemoryOutput unfinishedState;
    require(instance.state->save(instance.plugin, &unfinishedState.stream) &&
                instance.parameter(kWorkflow) == 1.0 &&
                instance.parameter(kStatus) != 0.0,
            "routine state snapshot cancelled an incomplete live capture");
    processCommand(instance.plugin, 2, 1);
    require(instance.parameter(kStatus) == 27.0,
            "incomplete capture could advance to Target after a state snapshot");

    constexpr std::size_t captureFrames = 48000 * 2;
    std::array<std::vector<float>, 2> reference{
        std::vector<float>(captureFrames), std::vector<float>(captureFrames)};
    std::array<std::vector<float>, 2> target = reference;
    std::mt19937 generator(0x544f4e45U);
    std::uniform_real_distribution<float> noise(-0.12F, 0.12F);
    for (std::size_t channel = 0; channel < 2; ++channel) {
      for (std::size_t frame = 0; frame < captureFrames; ++frame) {
        reference[channel][frame] = noise(generator);
        const float previous = frame == 0 ? 0.0F : reference[channel][frame - 1];
        const float delayed = frame < 7 ? 0.0F : reference[channel][frame - 7];
        target[channel][frame] =
            0.58F * reference[channel][frame] + 0.33F * previous - 0.17F * delayed;
      }
    }
    const double referenceToneDifference =
        processSignal(instance.plugin, reference, 1);
    require(instance.parameter(kConfidence) > 0.0 &&
                referenceToneDifference > 0.01,
            "reference capture produced neither live confidence nor an audible sweep: confidence=" +
                std::to_string(instance.parameter(kConfidence)) +
                ", difference=" + std::to_string(referenceToneDifference) +
                ", time=" + std::to_string(instance.parameter(kCaptureTime)) +
                ", drift=" + std::to_string(instance.parameter(kCurveDrift)));
    const double stableConfidence = instance.parameter(kConfidence);
    // A changed passage must never create a false confidence rise. If the
    // accumulated learner does return to a lower level, its falling tone must
    // follow the no-tone-at-zero contract.
    std::array<std::vector<float>, 2> spectralShift{
        std::vector<float>(48000 * 2), std::vector<float>(48000 * 2)};
    for (std::size_t frame = 0; frame < spectralShift[0].size(); ++frame) {
      const float value = 0.12F;
      spectralShift[0][frame] = value;
      spectralShift[1][frame] = value;
    }
    const double fallingToneDifference =
        processSignal(instance.plugin, spectralShift);
    const double changedConfidence = instance.parameter(kConfidence);
    require(changedConfidence <= stableConfidence &&
                (changedConfidence < stableConfidence
                     ? (changedConfidence > 0.0
                            ? fallingToneDifference > 0.01
                            : fallingToneDifference == 0.0)
                     : fallingToneDifference == 0.0),
            "changed material produced false confidence or tone feedback: before=" +
                std::to_string(stableConfidence) + ", after=" +
                std::to_string(changedConfidence) +
                ", difference=" + std::to_string(fallingToneDifference) +
                ", drift=" + std::to_string(instance.parameter(kCurveDrift)));
    // Exercise the same host parameter-flush path used by REAPER's generic
    // parameter surface / OSARA: advance from Capture Reference to Learn
    // Target without relying on an audio-process parameter event. This is the
    // exact transition that regressed when host synchronization was coupled to
    // unrelated correction rebuilds.
    flushParameter(instance.plugin, kWorkflow, 2.0);
    require(instance.parameter(kWorkflow) == 2.0 &&
                instance.parameter(kLastCommand) == 2.0 &&
                instance.parameter(kStatus) == 2.0,
            "flushed Workflow Step could not advance Reference to Target");
    processSignal(instance.plugin, target);
    require(instance.parameter(kConfidence) > 0.0,
            "target capture did not update live confidence");
    processCommand(instance.plugin, 3);
    require(host.callbacks > 0 && instance.parameter(kStatus) == 3.0,
            "analysis was not scheduled");
    instance.plugin->on_main_thread(instance.plugin);
    require(instance.parameter(kStatus) == 4.0,
            "analysis did not produce a preview");
    require(instance.latency->get(instance.plugin) == 0 &&
                instance.tail->get(instance.plugin) > 0,
            "preview latency or tail is invalid");

    std::array<std::vector<float>, 2> silence{
        std::vector<float>(12000), std::vector<float>(12000)};
    std::array<std::vector<float>, 2> discarded;
    // Match Mode is also a post-capture comparison control before Freeze. Drain
    // the initial preview kernel, switch to Voice and back, and verify the
    // workflow remains in Preview instead of requiring a new Correct pass.
    process(instance.plugin, silence, discarded);
    instance.plugin->on_main_thread(instance.plugin);
    processParameter(instance.plugin, kMatchMode, 1.0);
    require(instance.parameter(kStatus) == 3.0,
            "Preview Match Mode change did not schedule reinterpretation");
    instance.plugin->on_main_thread(instance.plugin);
    require(instance.parameter(kStatus) == 4.0,
            "Preview Match Mode change did not return to Preview");
    process(instance.plugin, silence, discarded);
    instance.plugin->on_main_thread(instance.plugin);
    processParameter(instance.plugin, kMatchMode, 0.0);
    instance.plugin->on_main_thread(instance.plugin);
    require(instance.parameter(kStatus) == 4.0,
            "switching the Match Mode back before Freeze left Preview");
    process(instance.plugin, silence, discarded);
    instance.plugin->on_main_thread(instance.plugin);

    processCommand(instance.plugin, 4);
    require(instance.parameter(kStatus) == 5.0,
            "Freeze Correction did not enter Frozen");
    instance.plugin->on_main_thread(instance.plugin);
    instance.plugin->reset(instance.plugin);
    process(instance.plugin, silence, discarded);
    require(std::all_of(discarded[0].begin(), discarded[0].end(),
                        [](float sample) { return sample == 0.0F; }) &&
                std::all_of(discarded[1].begin(), discarded[1].end(),
                            [](float sample) { return sample == 0.0F; }),
            "Frozen processing emitted a confidence tone into silence");
    instance.plugin->reset(instance.plugin);
    const auto firstImpulse = impulseResponse(instance.plugin, 10000);
    double correctionEnergy = 0.0;
    for (std::size_t i = 1; i < firstImpulse.size(); ++i) {
      correctionEnergy += std::abs(firstImpulse[i]);
    }
    require(correctionEnergy > 1.0e-4,
            "learned correction did not alter the audio");

    processParameter(instance.plugin, kMatchMode, 1.0);
    require(instance.parameter(kMatchMode) == 1.0 &&
                instance.parameter(kStatus) == 3.0 && host.callbacks > 0,
            "post-capture Match Mode change did not schedule reinterpretation: mode=" +
                std::to_string(instance.parameter(kMatchMode)) + ", status=" +
                std::to_string(instance.parameter(kStatus)) + ", callbacks=" +
                std::to_string(host.callbacks));
    instance.plugin->on_main_thread(instance.plugin);
    require(instance.parameter(kStatus) == 5.0,
            "post-capture Match Mode reinterpretation did not return to Frozen");
    process(instance.plugin, silence, discarded);
    instance.plugin->on_main_thread(instance.plugin);
    instance.plugin->reset(instance.plugin);
    const auto voiceImpulse = impulseResponse(instance.plugin, 10000);
    double modeChangeDifference = 0.0;
    for (std::size_t i = 0; i < firstImpulse.size(); ++i) {
      modeChangeDifference = std::max(
          modeChangeDifference,
          std::abs(static_cast<double>(firstImpulse[i] - voiceImpulse[i])));
    }
    require(modeChangeDifference > 1.0e-6,
            "changing Match Mode after Freeze did not change the correction");

    processParameter(instance.plugin, kMatchMode, 0.0);
    require(instance.parameter(kStatus) == 3.0,
            "switching back to the original Match Mode was not scheduled");
    instance.plugin->on_main_thread(instance.plugin);
    require(instance.parameter(kStatus) == 5.0,
            "switching back to the original Match Mode did not return to Frozen");
    process(instance.plugin, silence, discarded);
    instance.plugin->on_main_thread(instance.plugin);
    instance.plugin->reset(instance.plugin);
    const auto restoredModeImpulse = impulseResponse(instance.plugin, 10000);
    double restoredModeError = 0.0;
    for (std::size_t i = 0; i < firstImpulse.size(); ++i) {
      restoredModeError = std::max(
          restoredModeError,
          std::abs(static_cast<double>(firstImpulse[i] -
                                       restoredModeImpulse[i])));
    }
    require(restoredModeError < 1.0e-6,
            "switching back did not reproduce the original correction");

    // Every advertised Match Mode must be selectable after Freeze from the
    // same retained capture pair. Drain each click-free kernel transition
    // before requesting the next mode so this checks mode interpretation, not
    // renderer back-pressure.
    for (int mode = 1; mode <= 4; ++mode) {
      processParameter(instance.plugin, kMatchMode, static_cast<double>(mode));
      require(instance.parameter(kStatus) == 3.0,
              "post-capture mode did not schedule reinterpretation: " +
                  std::to_string(mode));
      instance.plugin->on_main_thread(instance.plugin);
      require(instance.parameter(kStatus) == 5.0 &&
                  instance.latency->get(instance.plugin) == 0,
              "post-capture mode did not return Frozen at zero latency: " +
                  std::to_string(mode));
      process(instance.plugin, silence, discarded);
      instance.plugin->on_main_thread(instance.plugin);
    }
    processParameter(instance.plugin, kMatchMode, 0.0);
    instance.plugin->on_main_thread(instance.plugin);
    process(instance.plugin, silence, discarded);
    instance.plugin->on_main_thread(instance.plugin);
    instance.plugin->reset(instance.plugin);
    const auto allModesReturnImpulse = impulseResponse(instance.plugin, 10000);
    double allModesReturnError = 0.0;
    for (std::size_t i = 0; i < firstImpulse.size(); ++i) {
      allModesReturnError = std::max(
          allModesReturnError,
          std::abs(static_cast<double>(firstImpulse[i] -
                                       allModesReturnImpulse[i])));
    }
    require(allModesReturnError < 1.0e-6,
            "cycling through all Match Modes did not restore the original correction");

    MemoryOutput modeSwitchSaved;
    require(instance.state->save(instance.plugin, &modeSwitchSaved.stream) &&
                modeSwitchSaved.bytes.size() > 1000,
            "frozen mode-switchable profile could not be saved");

    processSignal(instance.plugin, reference, 1);
    processSignal(instance.plugin, target, 2);
    processCommand(instance.plugin, 3);
    require(instance.parameter(kStatus) == 3.0,
            "replacement analysis was not pending before project save");
    MemoryOutput saved;
    require(instance.state->save(instance.plugin, &saved.stream) &&
                saved.bytes.size() > 1000,
            "state save failed");
    require(instance.parameter(kStatus) == 3.0,
            "routine state snapshot cancelled pending replacement analysis");
    instance.plugin->on_main_thread(instance.plugin);
    require(instance.parameter(kStatus) == 4.0,
            "pending replacement analysis did not resume after a state snapshot");
    MemoryInput activeLoad(saved.bytes);
    require(!instance.state->load(instance.plugin, &activeLoad.stream),
            "active state load should be rejected");

    instance.plugin->stop_processing(instance.plugin);
    instance.plugin->deactivate(instance.plugin);

    PluginInstance modeRestored(
        factory->create_plugin(factory, &host.host, descriptor->id));
    MemoryInput modeSwitchInput(modeSwitchSaved.bytes);
    require(modeRestored.state->load(modeRestored.plugin, &modeSwitchInput.stream) &&
                modeRestored.plugin->activate(modeRestored.plugin, 48000.0, 1, 16384) &&
                modeRestored.plugin->start_processing(modeRestored.plugin),
            "saved mode-switchable profile could not be restored");
    require(modeRestored.parameter(kStatus) == 5.0 &&
                modeRestored.parameter(kMatchMode) == 0.0,
            "restored mode-switchable profile lost its frozen setup");
    modeRestored.plugin->reset(modeRestored.plugin);
    const auto beforeRestoredModeSwitch =
        impulseResponse(modeRestored.plugin, 10000);
    processParameter(modeRestored.plugin, kMatchMode, 1.0);
    require(modeRestored.parameter(kStatus) == 3.0,
            "restored profile could not schedule a Match Mode change");
    modeRestored.plugin->on_main_thread(modeRestored.plugin);
    require(modeRestored.parameter(kStatus) == 5.0,
            "restored profile Match Mode change did not return to Frozen");
    process(modeRestored.plugin, silence, discarded);
    modeRestored.plugin->on_main_thread(modeRestored.plugin);
    modeRestored.plugin->reset(modeRestored.plugin);
    const auto afterRestoredModeSwitch =
        impulseResponse(modeRestored.plugin, 10000);
    double restoredSwitchDifference = 0.0;
    for (std::size_t i = 0; i < beforeRestoredModeSwitch.size(); ++i) {
      restoredSwitchDifference = std::max(
          restoredSwitchDifference,
          std::abs(static_cast<double>(beforeRestoredModeSwitch[i] -
                                       afterRestoredModeSwitch[i])));
    }
    require(restoredSwitchDifference > 1.0e-6,
            "Match Mode switching was lost after project save/reload");
    modeRestored.plugin->stop_processing(modeRestored.plugin);
    modeRestored.plugin->deactivate(modeRestored.plugin);

    PluginInstance restored(
        factory->create_plugin(factory, &host.host, descriptor->id));
    std::string malformed = "ToneTraceClapState 1\nparameters 0\ncore 4\nnope";
    MemoryInput invalidInput(malformed);
    require(!restored.state->load(restored.plugin, &invalidInput.stream),
            "malformed state was accepted");
    std::string badDisplay =
        "ToneTraceClapState 2\nparameters 0\ntracedisplay 2\ncore 4\nnope";
    MemoryInput badDisplayInput(badDisplay);
    require(!restored.state->load(restored.plugin, &badDisplayInput.stream),
            "tracedisplay value outside 0/1 was accepted");
    MemoryInput savedInput(saved.bytes);
    require(restored.state->load(restored.plugin, &savedInput.stream),
            "saved state did not load");
    require(restored.plugin->activate(restored.plugin, 48000.0, 1, 16384) &&
                restored.plugin->start_processing(restored.plugin),
            "restored activation failed");
    require(restored.parameter(kStatus) == 5.0 &&
                restored.parameter(kConfidence) > 0.0 &&
                restored.latency->get(restored.plugin) == 0 &&
                restored.tail->get(restored.plugin) > 0,
            "restored profile is not frozen and zero latency");
    restored.plugin->reset(restored.plugin);
    const auto restoredImpulse = impulseResponse(restored.plugin, 10000);
    require(firstImpulse.size() == restoredImpulse.size(),
            "restored impulse size mismatch");
    double maximumError = 0.0;
    for (std::size_t i = 0; i < firstImpulse.size(); ++i) {
      maximumError = std::max(
          maximumError,
          std::abs(static_cast<double>(firstImpulse[i] - restoredImpulse[i])));
    }
    require(maximumError < 2.0e-5,
            "restored correction is not sample-equivalent");

    std::array<std::vector<float>, 2> clippedTarget = target;
    for (auto& channel : clippedTarget) {
      for (auto& sample : channel) sample = sample < 0.0F ? -1.0F : 1.0F;
    }
    processSignal(restored.plugin, reference, 1);
    processSignal(restored.plugin, clippedTarget, 2);
    processCommand(restored.plugin, 3);
    restored.plugin->on_main_thread(restored.plugin);
    restored.plugin->reset(restored.plugin);
    std::array<std::vector<float>, 2> warningOutput;
    process(restored.plugin, silence, warningOutput);
    double rejectedProfileWarning = 0.0;
    for (const auto& channel : warningOutput) {
      for (const float sample : channel) {
        rejectedProfileWarning += std::abs(sample);
      }
    }
    require(restored.parameter(kStatus) == 8.0 &&
                restored.tail->get(restored.plugin) > 0 &&
                rejectedProfileWarning > 0.01,
            "profile-damaging clipped audio was not rejected and reported audibly");
    restored.plugin->reset(restored.plugin);
    const auto afterRejectedCandidate = impulseResponse(restored.plugin, 10000);
    double rejectedCandidateError = 0.0;
    for (std::size_t i = 0; i < restoredImpulse.size(); ++i) {
      rejectedCandidateError = std::max(
          rejectedCandidateError,
          std::abs(static_cast<double>(restoredImpulse[i] -
                                       afterRejectedCandidate[i])));
    }
    require(rejectedCandidateError < 2.0e-5,
            "a rejected capture replaced the last-known-good correction");
    processCommand(restored.plugin, 4);
    require(restored.parameter(kStatus) == 5.0,
            "last-known-good correction could not be frozen after rejection");

    processCommand(restored.plugin, 5);
    require(restored.parameter(kStatus) == 6.0, "reset was not armed");
    processCommand(restored.plugin, 7);
    require(restored.parameter(kStatus) == 5.0 &&
                restored.tail->get(restored.plugin) > 0,
            "Cancel Reset did not restore the frozen last-known-good profile");
    processCommand(restored.plugin, 5);
    require(restored.parameter(kStatus) == 6.0,
            "reset could not be armed again after cancellation");
    processCommand(restored.plugin, 6);
    restored.plugin->on_main_thread(restored.plugin);
    require(restored.parameter(kStatus) == 0.0 &&
                restored.tail->get(restored.plugin) == 0,
            "confirmed reset did not clear the profile");
    process(restored.plugin, silence, discarded);
    restored.plugin->on_main_thread(restored.plugin);
    restored.plugin->reset(restored.plugin);
    const auto resetImpulse = impulseResponse(restored.plugin, 2048);
    require(resetImpulse[0] == 1.0F &&
                std::all_of(resetImpulse.begin() + 1, resetImpulse.end(),
                            [](float value) { return value == 0.0F; }),
            "reset plugin is not identity");

    restored.plugin->stop_processing(restored.plugin);
    restored.plugin->deactivate(restored.plugin);
  }
  require(fixturePairs.size() == fixtureModes.size(),
          "fixture pair/mode inventory mismatch");
  for (std::size_t index = 0; index < fixturePairs.size(); ++index) {
    runRealFixturePair(factory, descriptor, fixturePairs[index].first,
                       fixturePairs[index].second, fixtureModes[index]);
  }
  entry->deinit();
  entry->deinit();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc >= 2 && (argc - 2) % 3 == 0,
            "usage: tonetrace-clap-tests <plugin.clap> [reference.wav target.wav mode]...");
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> pairs;
    std::vector<int> modes;
    for (int index = 2; index < argc; index += 3) {
      pairs.emplace_back(argv[index], argv[index + 1]);
      modes.push_back(std::stoi(argv[index + 2]));
    }
    run(argv[1], pairs, modes);
    std::cout << "Tone Trace CLAP host tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Tone Trace CLAP host tests failed: " << error.what() << '\n';
    return 1;
  }
}
