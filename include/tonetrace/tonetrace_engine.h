#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tonetrace {

struct AudioBuffer {
  int sampleRate = 0;
  std::vector<std::vector<double>> channels;

  [[nodiscard]] std::size_t frames() const;
  [[nodiscard]] bool empty() const;
};

AudioBuffer readWav(const std::filesystem::path& path);
void writeFloatWav(const std::filesystem::path& path, const AudioBuffer& audio);

enum class MatchMode {
  FullMix,
  Voice,
  Drums,
  BassSynth,
  CustomMaxCapture,
};

struct MatchSettings {
  MatchMode mode = MatchMode::FullMix;
  double rangeLowHz = 10.0;
  double rangeHighHz = 30000.0;
  double maximumCorrectionDb = 18.0;
  int resolution = 30;
  bool removeBroadLevelDifference = true;
  // Internal quality safeguard for Voice matching. Disable only in diagnostic
  // tools/tests that need to compare against the legacy full-detail curve.
  bool enableVoiceSafety = true;
  // Diagnostic-only scale for the Voice resonance-restoration component when
  // enableVoiceSafety is false. Normal plug-in operation leaves this at 1.0.
  double voiceDetailScale = 1.0;
};

struct SpectrumPoint {
  double frequencyHz = 0.0;
  double levelDb = 0.0;
  double confidence = 0.0;
  double varianceDb2 = 0.0;
};

struct SpectrumCapture {
  int sampleRate = 0;
  int fftSize = 0;
  std::size_t acceptedFrames = 0;
  double confidence = 0.0;
  std::vector<SpectrumPoint> points;
};

// Serializes a Reference/Target capture to the human-readable ".tts" format.
// The stored confidence and acceptedFrames fields let an imported capture be
// loaded back through the same validation used for live captures.
void saveSpectrumCapture(const std::filesystem::path& path,
                         const SpectrumCapture& capture);
SpectrumCapture loadSpectrumCapture(const std::filesystem::path& path);

// In-memory forms of the same ".tts" text format, used to transport a capture
// between threads or wrappers (for example editor to processor). The
// deserializers apply the same validation as a file load.
[[nodiscard]] std::string serializeSpectrumCapture(const SpectrumCapture& capture);
[[nodiscard]] SpectrumCapture deserializeSpectrumCapture(const std::string& bytes);

// Throws std::runtime_error when the capture or model fails validation. These
// gate imports and loaded state before they reach the plugin internals.
void validateSpectrumCapture(const SpectrumCapture& capture);

struct CorrectionNode {
  double frequencyHz = 0.0;
  double gainDb = 0.0;
  double confidence = 0.0;
};

class CorrectionModel {
 public:
  int version = 1;
  MatchMode mode = MatchMode::FullMix;
  double analysisLowHz = 10.0;
  double analysisHighHz = 30000.0;
  int resolution = 30;
  std::vector<CorrectionNode> nodes;

  [[nodiscard]] double gainDbAt(double frequencyHz) const;
  void save(const std::filesystem::path& path) const;
  static CorrectionModel load(const std::filesystem::path& path);
};

// Throws std::runtime_error when the model fails validation.
void validateCorrectionModel(const CorrectionModel& model);

// In-memory form of the ".ttm" text format, used to transport a model between
// threads or wrappers. The deserializer applies the same validation as a file
// load.
[[nodiscard]] std::string serializeCorrectionModel(const CorrectionModel& model);
[[nodiscard]] CorrectionModel deserializeCorrectionModel(const std::string& bytes);

// Imported curves (.tts/.ttm) store absolute frequencies, so they are
// sample-rate-independent by design and a curve captured at one project rate
// can normally be applied at another. These assessments return whether a
// loaded curve is usable at the *current* project rate and analysis range, and
// whether any of its content falls above the project Nyquist (which cannot be
// rendered and is silently dropped by the IR renderer). Callers show the
// reason to the user when usable is false.
struct ImportCompatibility {
  bool usable = true;
  bool truncatedByNyquist = false;
  std::string reason;
};

[[nodiscard]] ImportCompatibility assessCaptureImport(
    const SpectrumCapture& capture, double rangeLowHz, double rangeHighHz,
    double projectSampleRate);
[[nodiscard]] ImportCompatibility assessModelImport(
    const CorrectionModel& model, double rangeLowHz, double rangeHighHz,
    double projectSampleRate);

class MatchEngine {
 public:
  SpectrumCapture capture(const AudioBuffer& audio,
                          const MatchSettings& settings) const;
  CorrectionModel match(const SpectrumCapture& reference,
                        const SpectrumCapture& target,
                        const MatchSettings& settings) const;
  CorrectionModel match(const AudioBuffer& reference,
                        const AudioBuffer& target,
                        const MatchSettings& settings) const;
};

struct IrRenderSettings {
  int sampleRate = 48000;
  double durationSeconds = 0.18;
  double correctionStrength = 1.0;
  double correctionSharpness = 1.0;
  double correctionGainDb = 0.0;
  double rangeLowHz = 10.0;
  double rangeHighHz = 30000.0;
  // Optional per-band manual trim in dB, one entry per trace band, laid out
  // log-spaced across 20 Hz..20 kHz. Empty means no manual shaping. Entries
  // are additive on top of the auto-matched correction curve.
  std::vector<double> manualGains;
};

// A manual-only curve is meaningful when either the global correction gain or
// at least one graphic-EQ band differs from zero. Non-finite values count as
// active here so callers pass them to the renderer's normal validation instead
// of accidentally treating invalid state as a flat curve.
[[nodiscard]] bool hasManualCorrection(
    const IrRenderSettings& settings) noexcept;

// Renders the exact curve used by manual-only plug-in operation: a flat learned
// model plus Correction Gain, Correction Range, and the editable band gains.
// Callers should use hasManualCorrection() first when a flat curve is an error.
[[nodiscard]] std::vector<double> renderManualCorrectionIr(
    const IrRenderSettings& settings);

// Linear-in-log interpolation of the manual trim at a frequency. An empty
// vector contributes 0 dB; a single entry applies flat; otherwise the grid is
// log-spaced across [lowHz, highHz] (clamped to 20 Hz..20 kHz) with one point
// per entry, matching the editor's band grid.
[[nodiscard]] double manualGainDbAt(double frequencyHz,
                                    const std::vector<double>& gains,
                                    double lowHz, double highHz);

// Resamples a manual-trim curve onto a different number of log-spaced bands
// without moving edits to different frequencies. This is used when Correction
// Resolution changes after a match has already been edited.
[[nodiscard]] std::vector<double> resampleManualGains(
    const std::vector<double>& gains, std::size_t targetCount,
    double lowHz, double highHz);

// Evaluates the correction exactly as the renderer does at one frequency.
// `automaticDb` includes the learned ceiling, Q/sharpness and Strength.
// `manualDb` is the stored trace-band trim. `tonalDb` is their sum and is
// therefore what an individual band control represents. `outputDb` additionally
// includes the global Correction Gain. Outside the active Low/High mask the
// tonal terms are zero while Correction Gain remains global.
struct CorrectionBreakdown {
  bool inRange = false;
  double automaticDb = 0.0;
  double manualDb = 0.0;
  double tonalDb = 0.0;
  double outputDb = 0.0;
};

[[nodiscard]] CorrectionBreakdown evaluateCorrectionAt(
    const CorrectionModel& model, double maximumCorrectionDb,
    const IrRenderSettings& settings, double frequencyHz);

// Validates model/render settings without performing the FFT/IR render.
// Useful for profile/state checks that must stay lightweight.
void validateIrRenderSettings(const CorrectionModel& model,
                              const IrRenderSettings& settings);

std::vector<double> renderMinimumPhaseIr(const CorrectionModel& model,
                                         const IrRenderSettings& settings);
AudioBuffer convolve(const AudioBuffer& input, const std::vector<double>& ir);

struct MatchError {
  double rmsDb = 0.0;
  double maximumDb = 0.0;
  std::size_t pointCount = 0;
};

MatchError compareCaptures(const SpectrumCapture& a,
                           const SpectrumCapture& b,
                           double lowHz,
                           double highHz);

std::string toString(MatchMode mode);
MatchMode parseMatchMode(const std::string& text);

}  // namespace tonetrace
