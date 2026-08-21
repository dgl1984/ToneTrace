#include "tonetrace/tonetrace_engine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace tonetrace {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kFloorPower = 1.0e-20;
constexpr double kMinimumUsableFramePower = 1.0e-16;

void ensureParentDirectory(const std::filesystem::path& path) {
  const auto parent = path.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);
}

std::size_t nextPowerOfTwo(std::size_t value) {
  std::size_t result = 1;
  while (result < value) {
    if (result > std::numeric_limits<std::size_t>::max() / 2U) {
      throw std::runtime_error("Requested FFT is too large");
    }
    result <<= 1U;
  }
  return result;
}

void fft(std::vector<std::complex<double>>& values, bool inverse) {
  const std::size_t n = values.size();
  if (n == 0 || (n & (n - 1U)) != 0) {
    throw std::runtime_error("FFT size must be a non-zero power of two");
  }
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1U;
    for (; j & bit; bit >>= 1U) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(values[i], values[j]);
  }
  for (std::size_t length = 2; length <= n; length <<= 1U) {
    const double angleBase = (inverse ? 2.0 : -2.0) * kPi /
                             static_cast<double>(length);
    const std::size_t half = length / 2U;
    for (std::size_t j = 0; j < half; ++j) {
      const double angle = angleBase * static_cast<double>(j);
      const std::complex<double> rotation(std::cos(angle), std::sin(angle));
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

template <typename T>
T readLittle(std::istream& stream) {
  std::array<unsigned char, sizeof(T)> bytes{};
  stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  if (!stream) throw std::runtime_error("Unexpected end of WAV file");
  T value{};
  std::memcpy(&value, bytes.data(), sizeof(T));
  return value;
}

std::string readFourCc(std::istream& stream) {
  std::array<char, 4> chars{};
  stream.read(chars.data(), chars.size());
  if (!stream) throw std::runtime_error("Unexpected end of WAV file");
  return std::string(chars.data(), chars.size());
}

void writeU16(std::ostream& stream, std::uint16_t value) {
  stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

void writeU32(std::ostream& stream, std::uint32_t value) {
  stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

double hzToErb(double frequencyHz) {
  return 21.4 * std::log10(1.0 + 0.00437 * std::max(0.0, frequencyHz));
}

double erbToHz(double erb) {
  return (std::pow(10.0, erb / 21.4) - 1.0) / 0.00437;
}

double interpolateLinear(const std::vector<double>& x,
                         const std::vector<double>& y,
                         double value) {
  if (x.empty() || y.empty() || x.size() != y.size()) return 0.0;
  if (value <= x.front()) return y.front();
  if (value >= x.back()) return y.back();
  const auto upper = std::upper_bound(x.begin(), x.end(), value);
  const std::size_t hi = static_cast<std::size_t>(upper - x.begin());
  const std::size_t lo = hi - 1;
  const double ratio = (value - x[lo]) / std::max(1.0e-18, x[hi] - x[lo]);
  return y[lo] + (y[hi] - y[lo]) * ratio;
}

double weightedMean(const std::vector<SpectrumPoint>& points,
                    double lowHz,
                    double highHz) {
  double sum = 0.0;
  double weight = 0.0;
  for (const auto& point : points) {
    if (point.frequencyHz < lowHz || point.frequencyHz > highHz) continue;
    const double w = std::max(0.01, point.confidence);
    sum += point.levelDb * w;
    weight += w;
  }
  return weight > 0.0 ? sum / weight : 0.0;
}

struct ModeProfile {
  int pointCount;
  double gateBelowPeakDb;
  double temporalMoment;
  double uncertaintyScale;
  double smoothingErb;
  double resonanceDetailErb;
  double resonanceBaselineErb;
  double resonanceAmount;
  double resonanceLimitDb;
};

ModeProfile profileFor(MatchMode mode) {
  switch (mode) {
    case MatchMode::Voice:
      return {300, 52.0, 1.5, 0.75, 1.625,
              0.22, 0.72, 2.25, 5.0};
    case MatchMode::Drums:
      return {280, 62.0, 2.5, 0.90, 0.95,
              0.18, 0.58, 1.50, 6.0};
    case MatchMode::BassSynth:
      return {260, 60.0, 1.5, 0.75, 1.55,
              0.24, 0.78, 1.80, 5.0};
    case MatchMode::CustomMaxCapture:
      return {720, 66.0, 2.0, 0.85, 1.50,
              0.16, 0.52, 2.20, 6.0};
    case MatchMode::FullMix:
    default:
      return {360, 64.0, 1.5, 0.80, 1.25,
              0.20, 0.66, 1.60, 5.0};
  }
}

std::vector<double> gaussianSmooth(const std::vector<double>& x,
                                   const std::vector<double>& values,
                                   const std::vector<double>& weights,
                                   double sigma) {
  std::vector<double> result(values.size(), 0.0);
  const double radius = 3.5 * sigma;
  for (std::size_t i = 0; i < values.size(); ++i) {
    double total = 0.0;
    double totalWeight = 0.0;
    for (std::size_t j = 0; j < values.size(); ++j) {
      const double distance = std::abs(x[i] - x[j]);
      if (distance > radius) continue;
      const double kernel = std::exp(-0.5 * distance * distance /
                                     std::max(1.0e-12, sigma * sigma));
      const double reliability = 0.65 + 0.35 * std::clamp(weights[j], 0.0, 1.0);
      const double weight = kernel * reliability;
      total += values[j] * weight;
      totalWeight += weight;
    }
    result[i] = totalWeight > 0.0 ? total / totalWeight : values[i];
  }
  return result;
}

double finiteOrZero(double value) {
  return std::isfinite(value) ? value : 0.0;
}

double smoothStep01(double value) {
  const double x = std::clamp(value, 0.0, 1.0);
  return x * x * (3.0 - 2.0 * x);
}

void validateAudioBuffer(const AudioBuffer& audio) {
  if (audio.sampleRate <= 0 || audio.channels.empty() || audio.frames() == 0) {
    throw std::runtime_error("Audio buffer is empty or has an invalid sample rate");
  }
  const std::size_t frames = audio.frames();
  for (const auto& channel : audio.channels) {
    if (channel.size() != frames) {
      throw std::runtime_error("Audio buffer channels differ in length");
    }
  }
}

}  // namespace

void validateCorrectionModel(const CorrectionModel& model) {
  const bool validMode = model.mode == MatchMode::FullMix ||
                         model.mode == MatchMode::Voice ||
                         model.mode == MatchMode::Drums ||
                         model.mode == MatchMode::BassSynth ||
                         model.mode == MatchMode::CustomMaxCapture;
  if (model.version != 1 || !validMode ||
      !std::isfinite(model.analysisLowHz) ||
      !std::isfinite(model.analysisHighHz) || model.analysisLowHz <= 0.0 ||
      model.analysisHighHz <= model.analysisLowHz || model.resolution < 1 ||
      model.resolution > 4096 || model.nodes.empty() ||
      model.nodes.size() > 20000) {
    throw std::runtime_error("Invalid Tone Trace model metadata");
  }
  double previousFrequency = 0.0;
  for (const auto& node : model.nodes) {
    if (!std::isfinite(node.frequencyHz) ||
        !std::isfinite(node.gainDb) ||
        !std::isfinite(node.confidence) ||
        node.frequencyHz <= previousFrequency ||
        std::abs(node.gainDb) > 60.0 ||
        node.confidence < 0.0 || node.confidence > 1.0) {
      throw std::runtime_error("Invalid Tone Trace model node");
    }
    previousFrequency = node.frequencyHz;
  }
}

void validateSpectrumCapture(const SpectrumCapture& capture) {
  if (capture.sampleRate <= 0 || capture.fftSize <= 0 ||
      capture.acceptedFrames < 3 || !std::isfinite(capture.confidence) ||
      capture.confidence < 0.0 || capture.confidence > 1.0 ||
      capture.points.size() < 3) {
    throw std::runtime_error("Spectrum capture is incomplete");
  }
  double previousFrequency = 0.0;
  for (const auto& point : capture.points) {
    if (!std::isfinite(point.frequencyHz) || !std::isfinite(point.levelDb) ||
        !std::isfinite(point.confidence) || !std::isfinite(point.varianceDb2) ||
        point.frequencyHz <= previousFrequency || point.confidence < 0.0 ||
        point.confidence > 1.0 || point.varianceDb2 < 0.0) {
      throw std::runtime_error("Spectrum capture contains invalid points");
    }
    previousFrequency = point.frequencyHz;
  }
}

namespace {
std::string formatFrequency(double hz) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::fixed << std::setprecision(0) << hz;
  return stream.str();
}
}  // namespace

ImportCompatibility assessCaptureImport(const SpectrumCapture& capture,
                                        double rangeLowHz, double rangeHighHz,
                                        double projectSampleRate) {
  ImportCompatibility result;
  if (capture.points.empty() || !std::isfinite(projectSampleRate) ||
      projectSampleRate <= 0.0) {
    result.usable = false;
    result.reason = "Cannot evaluate the imported curve against this session.";
    return result;
  }
  const double nyquist = projectSampleRate * 0.5;
  const double usableLow =
      std::max({10.0, rangeLowHz, capture.points.front().frequencyHz});
  const double usableHigh =
      std::min({rangeHighHz, 30000.0, nyquist * 0.999,
                capture.points.back().frequencyHz});
  result.truncatedByNyquist = capture.points.back().frequencyHz > nyquist * 0.999;
  if (usableHigh <= usableLow) {
    result.usable = false;
    result.reason =
        "This curve has no frequencies in common with what the current "
        "session can analyze. The project applies " +
        formatFrequency(rangeLowHz) + " Hz to " + formatFrequency(rangeHighHz) +
        " Hz at a " + std::to_string(static_cast<int>(projectSampleRate)) +
        " Hz sample rate, while the curve spans " +
        formatFrequency(capture.points.front().frequencyHz) + " Hz to " +
        formatFrequency(capture.points.back().frequencyHz) +
        " Hz. Import it in a session whose sample rate and analysis range "
        "overlap the curve.";
  }
  return result;
}

ImportCompatibility assessModelImport(const CorrectionModel& model,
                                      double rangeLowHz, double rangeHighHz,
                                      double projectSampleRate) {
  ImportCompatibility result;
  if (!std::isfinite(projectSampleRate) || projectSampleRate <= 0.0) {
    result.usable = false;
    result.reason = "Cannot evaluate the imported model against this session.";
    return result;
  }
  const double nyquist = projectSampleRate * 0.5;
  const double usableLow = std::max(10.0, rangeLowHz);
  const double usableHigh = std::min(rangeHighHz, nyquist * 0.999);
  const double modelLow = std::max(usableLow, model.analysisLowHz);
  const double modelHigh = std::min(usableHigh, model.analysisHighHz);
  result.truncatedByNyquist = model.analysisHighHz > usableHigh;
  if (modelHigh <= modelLow) {
    result.usable = false;
    result.reason =
        "This correction model covers " + formatFrequency(model.analysisLowHz) +
        " Hz to " + formatFrequency(model.analysisHighHz) +
        " Hz, which does not overlap the frequencies the current session can "
        "apply (" +
        formatFrequency(usableLow) + " Hz to " + formatFrequency(usableHigh) +
        " Hz at a " + std::to_string(static_cast<int>(projectSampleRate)) +
        " Hz sample rate). Import it in a session whose sample rate reaches "
        "the model's range.";
  }
  return result;
}

std::size_t AudioBuffer::frames() const {
  if (channels.empty()) return 0;
  return channels.front().size();
}

bool AudioBuffer::empty() const {
  return sampleRate <= 0 || channels.empty() || frames() == 0;
}

AudioBuffer readWav(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot open WAV file: " + path.string());
  if (readFourCc(stream) != "RIFF") throw std::runtime_error("Not a RIFF WAV file");
  (void)readLittle<std::uint32_t>(stream);
  if (readFourCc(stream) != "WAVE") throw std::runtime_error("Not a WAVE file");

  std::uint16_t format = 0;
  std::uint16_t channels = 0;
  std::uint32_t sampleRate = 0;
  std::uint16_t bitsPerSample = 0;
  std::uint16_t blockAlign = 0;
  std::vector<unsigned char> data;

  while (stream && (!format || data.empty())) {
    const std::string id = readFourCc(stream);
    const std::uint32_t size = readLittle<std::uint32_t>(stream);
    if (id == "fmt ") {
      if (size < 16) throw std::runtime_error("Malformed WAV format chunk");
      format = readLittle<std::uint16_t>(stream);
      channels = readLittle<std::uint16_t>(stream);
      sampleRate = readLittle<std::uint32_t>(stream);
      (void)readLittle<std::uint32_t>(stream);
      blockAlign = readLittle<std::uint16_t>(stream);
      bitsPerSample = readLittle<std::uint16_t>(stream);
      std::uint32_t consumed = 16;
      if (format == 0xfffeU) {
        if (size < 40) throw std::runtime_error("Malformed extensible WAV format");
        const std::uint16_t extensionSize = readLittle<std::uint16_t>(stream);
        const std::uint16_t validBits = readLittle<std::uint16_t>(stream);
        (void)readLittle<std::uint32_t>(stream);  // channel mask
        const std::uint32_t subFormat = readLittle<std::uint32_t>(stream);
        const std::uint16_t guidData2 = readLittle<std::uint16_t>(stream);
        const std::uint16_t guidData3 = readLittle<std::uint16_t>(stream);
        std::array<unsigned char, 8> guidTail{};
        stream.read(reinterpret_cast<char*>(guidTail.data()), guidTail.size());
        if (!stream) throw std::runtime_error("Malformed extensible WAV format");
        consumed = 40;
        const std::array<unsigned char, 8> expectedTail{
            0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
        if (extensionSize < 22 || validBits == 0 || validBits > bitsPerSample ||
            guidData2 != 0x0000U || guidData3 != 0x0010U ||
            guidTail != expectedTail || (subFormat != 1U && subFormat != 3U)) {
          throw std::runtime_error("Unsupported extensible WAV subtype");
        }
        format = static_cast<std::uint16_t>(subFormat);
      }
      if (size > consumed) {
        stream.seekg(static_cast<std::streamoff>(size - consumed), std::ios::cur);
      }
    } else if (id == "data") {
      data.resize(size);
      stream.read(reinterpret_cast<char*>(data.data()), size);
    } else {
      stream.seekg(static_cast<std::streamoff>(size), std::ios::cur);
    }
    if (size & 1U) stream.seekg(1, std::ios::cur);
  }

  if (!format || channels == 0 || sampleRate == 0 ||
      sampleRate > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      data.empty()) {
    throw std::runtime_error("Incomplete WAV file");
  }
  if (bitsPerSample == 0 || bitsPerSample % 8U != 0) {
    throw std::runtime_error("Unsupported WAV bit depth");
  }
  const std::size_t bytesPerSample = bitsPerSample / 8U;
  const std::size_t frameBytes = bytesPerSample * channels;
  if (frameBytes == 0 || blockAlign != frameBytes || data.size() % frameBytes != 0) {
    throw std::runtime_error("Malformed WAV sample data");
  }
  const std::size_t frames = data.size() / frameBytes;
  AudioBuffer result;
  result.sampleRate = static_cast<int>(sampleRate);
  result.channels.assign(channels, std::vector<double>(frames, 0.0));

  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t channel = 0; channel < channels; ++channel) {
      const unsigned char* ptr = data.data() + frame * frameBytes + channel * bytesPerSample;
      double sample = 0.0;
      if (format == 1 && bitsPerSample == 8) {
        sample = (static_cast<double>(ptr[0]) - 128.0) / 128.0;
      } else if (format == 1 && bitsPerSample == 16) {
        std::int16_t value{};
        std::memcpy(&value, ptr, sizeof(value));
        sample = static_cast<double>(value) / 32768.0;
      } else if (format == 1 && bitsPerSample == 24) {
        std::int32_t value = static_cast<std::int32_t>(ptr[0]) |
                             (static_cast<std::int32_t>(ptr[1]) << 8) |
                             (static_cast<std::int32_t>(ptr[2]) << 16);
        if (value & 0x00800000) value |= static_cast<std::int32_t>(0xff000000);
        sample = static_cast<double>(value) / 8388608.0;
      } else if (format == 1 && bitsPerSample == 32) {
        std::int32_t value{};
        std::memcpy(&value, ptr, sizeof(value));
        sample = static_cast<double>(value) / 2147483648.0;
      } else if (format == 3 && bitsPerSample == 32) {
        float value{};
        std::memcpy(&value, ptr, sizeof(value));
        sample = value;
      } else if (format == 3 && bitsPerSample == 64) {
        std::memcpy(&sample, ptr, sizeof(sample));
      } else {
        throw std::runtime_error("Unsupported WAV encoding");
      }
      result.channels[channel][frame] = finiteOrZero(sample);
    }
  }
  return result;
}

void writeFloatWav(const std::filesystem::path& path, const AudioBuffer& audio) {
  validateAudioBuffer(audio);
  const std::size_t frames = audio.frames();
  if (audio.channels.size() > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("Too many WAV channels");
  }
  const std::uint64_t blockAlign64 = audio.channels.size() * sizeof(float);
  if (blockAlign64 > std::numeric_limits<std::uint16_t>::max() ||
      static_cast<std::uint64_t>(audio.sampleRate) * blockAlign64 >
          std::numeric_limits<std::uint32_t>::max() ||
      frames > (std::numeric_limits<std::uint32_t>::max() - 48U) / blockAlign64) {
    throw std::runtime_error("WAV output exceeds the RIFF size limit");
  }
  const std::uint64_t dataBytes64 = frames * blockAlign64;
  ensureParentDirectory(path);
  std::ofstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("Cannot write WAV file: " + path.string());
  const std::uint16_t channels = static_cast<std::uint16_t>(audio.channels.size());
  const std::uint16_t bits = 32;
  const std::uint16_t blockAlign = static_cast<std::uint16_t>(blockAlign64);
  const std::uint32_t dataBytes = static_cast<std::uint32_t>(dataBytes64);
  stream.write("RIFF", 4);
  writeU32(stream, 48U + dataBytes);
  stream.write("WAVEfmt ", 8);
  writeU32(stream, 16);
  writeU16(stream, 3);
  writeU16(stream, channels);
  writeU32(stream, static_cast<std::uint32_t>(audio.sampleRate));
  writeU32(stream, static_cast<std::uint32_t>(audio.sampleRate) * blockAlign);
  writeU16(stream, blockAlign);
  writeU16(stream, bits);
  stream.write("fact", 4);
  writeU32(stream, 4U);
  writeU32(stream, static_cast<std::uint32_t>(frames));
  stream.write("data", 4);
  writeU32(stream, dataBytes);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (const auto& channel : audio.channels) {
      const float value = static_cast<float>(finiteOrZero(channel[frame]));
      stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }
  }
  if (!stream) throw std::runtime_error("Failed while writing WAV file: " + path.string());
}

double CorrectionModel::gainDbAt(double frequencyHz) const {
  if (nodes.empty()) return 0.0;
  const double frequency = std::max(1.0e-9, frequencyHz);
  if (frequency <= nodes.front().frequencyHz) return nodes.front().gainDb;
  if (frequency >= nodes.back().frequencyHz) return nodes.back().gainDb;
  const auto upper = std::upper_bound(nodes.begin(), nodes.end(), frequency,
      [](double value, const CorrectionNode& node) { return value < node.frequencyHz; });
  const std::size_t hi = static_cast<std::size_t>(upper - nodes.begin());
  const std::size_t lo = hi - 1;
  const double x = std::log(frequency);
  const double x0 = std::log(nodes[lo].frequencyHz);
  const double x1 = std::log(nodes[hi].frequencyHz);
  const double ratio = (x - x0) / std::max(1.0e-18, x1 - x0);
  return nodes[lo].gainDb + (nodes[hi].gainDb - nodes[lo].gainDb) * ratio;
}

std::string serializeCorrectionModel(const CorrectionModel& model) {
  validateCorrectionModel(model);
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(17);
  stream << "ToneTraceModel " << model.version << '\n';
  stream << "mode " << toString(model.mode) << '\n';
  stream << "range " << model.analysisLowHz << ' ' << model.analysisHighHz << '\n';
  stream << "resolution " << model.resolution << '\n';
  stream << "nodes " << model.nodes.size() << '\n';
  for (const auto& node : model.nodes) {
    stream << node.frequencyHz << ' ' << node.gainDb << ' ' << node.confidence << '\n';
  }
  if (!stream) throw std::runtime_error("Failed while serializing model");
  return stream.str();
}

CorrectionModel deserializeCorrectionModel(const std::string& bytes) {
  std::istringstream stream(bytes);
  stream.imbue(std::locale::classic());
  std::string magic;
  CorrectionModel model;
  stream >> magic >> model.version;
  if (magic != "ToneTraceModel" || model.version != 1) {
    throw std::runtime_error("Unsupported Tone Trace model");
  }
  std::string key;
  std::string modeText;
  stream >> key >> modeText;
  if (key != "mode") throw std::runtime_error("Malformed Tone Trace model");
  model.mode = parseMatchMode(modeText);
  stream >> key >> model.analysisLowHz >> model.analysisHighHz;
  if (key != "range") throw std::runtime_error("Malformed Tone Trace model");
  stream >> key >> model.resolution;
  if (key != "resolution") throw std::runtime_error("Malformed Tone Trace model");
  std::size_t count = 0;
  stream >> key >> count;
  if (key != "nodes" || count == 0 || count > 100000) {
    throw std::runtime_error("Malformed Tone Trace model");
  }
  model.nodes.resize(count);
  for (auto& node : model.nodes) {
    stream >> node.frequencyHz >> node.gainDb >> node.confidence;
    if (!stream) throw std::runtime_error("Malformed model node");
  }
  stream >> std::ws;
  if (!stream.eof()) throw std::runtime_error("Trailing model data");
  validateCorrectionModel(model);
  return model;
}

void CorrectionModel::save(const std::filesystem::path& path) const {
  validateCorrectionModel(*this);
  ensureParentDirectory(path);
  std::ofstream stream(path);
  if (!stream) throw std::runtime_error("Cannot write model: " + path.string());
  stream << serializeCorrectionModel(*this);
  if (!stream) throw std::runtime_error("Failed while writing model: " + path.string());
}

CorrectionModel CorrectionModel::load(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream) throw std::runtime_error("Cannot open model: " + path.string());
  stream.imbue(std::locale::classic());
  std::ostringstream content;
  content << stream.rdbuf();
  return deserializeCorrectionModel(content.str());
}

std::string serializeSpectrumCapture(const SpectrumCapture& capture) {
  validateSpectrumCapture(capture);
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(17);
  stream << "ToneTraceSpectrum 1\n";
  stream << "rate " << capture.sampleRate << '\n';
  stream << "fft " << capture.fftSize << '\n';
  stream << "frames " << capture.acceptedFrames << '\n';
  stream << "confidence " << capture.confidence << '\n';
  stream << "points " << capture.points.size() << '\n';
  for (const auto& point : capture.points) {
    stream << point.frequencyHz << ' ' << point.levelDb << ' '
           << point.confidence << ' ' << point.varianceDb2 << '\n';
  }
  if (!stream) throw std::runtime_error("Failed while serializing spectrum capture");
  return stream.str();
}

SpectrumCapture deserializeSpectrumCapture(const std::string& bytes) {
  std::istringstream stream(bytes);
  stream.imbue(std::locale::classic());
  std::string magic;
  int version = 0;
  stream >> magic >> version;
  if (magic != "ToneTraceSpectrum" || version != 1) {
    throw std::runtime_error("Unsupported Tone Trace spectrum capture");
  }
  SpectrumCapture capture;
  std::string key;
  stream >> key >> capture.sampleRate;
  if (key != "rate") throw std::runtime_error("Malformed spectrum capture");
  stream >> key >> capture.fftSize;
  if (key != "fft") throw std::runtime_error("Malformed spectrum capture");
  stream >> key >> capture.acceptedFrames;
  if (key != "frames") throw std::runtime_error("Malformed spectrum capture");
  stream >> key >> capture.confidence;
  if (key != "confidence") throw std::runtime_error("Malformed spectrum capture");
  std::size_t count = 0;
  stream >> key >> count;
  if (key != "points" || count < 3 || count > 1000000) {
    throw std::runtime_error("Malformed spectrum capture");
  }
  capture.points.resize(count);
  for (auto& point : capture.points) {
    stream >> point.frequencyHz >> point.levelDb >> point.confidence >>
        point.varianceDb2;
    if (!stream) throw std::runtime_error("Malformed spectrum capture point");
  }
  stream >> std::ws;
  if (!stream.eof()) throw std::runtime_error("Trailing spectrum capture data");
  validateSpectrumCapture(capture);
  return capture;
}

void saveSpectrumCapture(const std::filesystem::path& path,
                         const SpectrumCapture& capture) {
  validateSpectrumCapture(capture);
  ensureParentDirectory(path);
  std::ofstream stream(path);
  if (!stream) {
    throw std::runtime_error("Cannot write spectrum capture: " + path.string());
  }
  stream << serializeSpectrumCapture(capture);
  if (!stream) {
    throw std::runtime_error("Failed while writing spectrum capture: " +
                             path.string());
  }
}

SpectrumCapture loadSpectrumCapture(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error("Cannot open spectrum capture: " + path.string());
  }
  stream.imbue(std::locale::classic());
  std::ostringstream content;
  content << stream.rdbuf();
  return deserializeSpectrumCapture(content.str());
}

SpectrumCapture MatchEngine::capture(const AudioBuffer& audio,
                                     const MatchSettings& settings) const {
  validateAudioBuffer(audio);
  if (!std::isfinite(settings.rangeLowHz) ||
      !std::isfinite(settings.rangeHighHz) ||
      settings.rangeLowHz <= 0.0 || settings.rangeHighHz <= settings.rangeLowHz ||
      settings.resolution < 1 || settings.resolution > 4096) {
    throw std::runtime_error("Invalid capture settings");
  }
  const auto profile = profileFor(settings.mode);
  const int desired = static_cast<int>(std::ceil(audio.sampleRate * 0.085));
  const int fftSize = static_cast<int>(nextPowerOfTwo(std::max(2048, desired)));
  const int hop = fftSize / 2;
  if (audio.frames() < static_cast<std::size_t>(fftSize)) {
    throw std::runtime_error("Capture audio is too short");
  }

  std::vector<double> window(fftSize);
  for (int i = 0; i < fftSize; ++i) {
    window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (fftSize - 1));
  }
  std::vector<double> framePowers;
  for (std::size_t start = 0; start + fftSize <= audio.frames(); start += hop) {
    double power = 0.0;
    for (const auto& channel : audio.channels) {
      for (int i = 0; i < fftSize; ++i) {
        const double sample = channel[start + i];
        power += sample * sample;
      }
    }
    power /= static_cast<double>(fftSize * audio.channels.size());
    framePowers.push_back(power);
  }
  const double peakPower = *std::max_element(framePowers.begin(), framePowers.end());
  if (!std::isfinite(peakPower) || peakPower < kMinimumUsableFramePower) {
    throw std::runtime_error("Capture contains no usable audio");
  }
  const double gatePower = std::max(
      kMinimumUsableFramePower,
      peakPower * std::pow(10.0, -profile.gateBelowPeakDb / 10.0));

  const std::size_t bins = static_cast<std::size_t>(fftSize / 2 + 1);
  std::vector<double> mean(bins, 0.0);
  std::vector<double> m2(bins, 0.0);
  std::size_t accepted = 0;
  std::vector<std::complex<double>> spectrum(fftSize);
  std::size_t frameIndex = 0;
  for (std::size_t start = 0; start + fftSize <= audio.frames(); start += hop, ++frameIndex) {
    if (framePowers[frameIndex] < gatePower) continue;
    std::vector<double> channelPower(bins, 0.0);
    for (const auto& channel : audio.channels) {
      std::fill(spectrum.begin(), spectrum.end(), std::complex<double>(0.0, 0.0));
      for (int i = 0; i < fftSize; ++i) spectrum[i] = channel[start + i] * window[i];
      fft(spectrum, false);
      for (std::size_t bin = 0; bin < bins; ++bin) {
        channelPower[bin] += std::norm(spectrum[bin]);
      }
    }
    ++accepted;
    for (std::size_t bin = 0; bin < bins; ++bin) {
      const double power = std::max(kFloorPower,
          channelPower[bin] / static_cast<double>(audio.channels.size()));
      const double moment = std::pow(power, profile.temporalMoment);
      const double delta = moment - mean[bin];
      mean[bin] += delta / static_cast<double>(accepted);
      const double delta2 = moment - mean[bin];
      m2[bin] += delta * delta2;
    }
  }
  if (accepted < 3) throw std::runtime_error("Not enough valid audio for capture");

  std::vector<double> binFrequencies(bins);
  for (std::size_t bin = 0; bin < bins; ++bin) {
    binFrequencies[bin] = static_cast<double>(bin) * audio.sampleRate / fftSize;
  }
  SpectrumCapture capture;
  capture.sampleRate = audio.sampleRate;
  capture.fftSize = fftSize;
  capture.acceptedFrames = accepted;
  const double lowHz = std::max(10.0, settings.rangeLowHz);
  const double highHz = std::min({settings.rangeHighHz,
                                  30000.0,
                                  audio.sampleRate * 0.49});
  if (highHz <= lowHz) {
    throw std::runtime_error("Capture range does not overlap the sample rate");
  }
  const double lowErb = hzToErb(lowHz);
  const double highErb = hzToErb(highHz);
  capture.points.reserve(profile.pointCount);
  double confidenceSum = 0.0;
  for (int i = 0; i < profile.pointCount; ++i) {
    const double ratio = static_cast<double>(i) / (profile.pointCount - 1);
    const double frequency = erbToHz(lowErb + (highErb - lowErb) * ratio);
    const double meanMoment = std::max(kFloorPower,
        interpolateLinear(binFrequencies, mean, frequency));
    const double varianceMoment = std::max(0.0,
        interpolateLinear(binFrequencies, m2, frequency) /
        std::max<std::size_t>(1, accepted - 1));
    const double relativeStandardError = std::sqrt(varianceMoment /
        std::max<std::size_t>(1, accepted)) / meanMoment;
    const double stability = std::exp(-relativeStandardError /
                                      profile.uncertaintyScale);
    const double durationConfidence = std::min(1.0, accepted / 24.0);
    const double confidence = std::clamp(stability * durationConfidence, 0.0, 1.0);
    const double generalizedPower = std::pow(meanMoment, 1.0 / profile.temporalMoment);
    const double level = 10.0 * std::log10(std::max(kFloorPower, generalizedPower));
    const double uncertaintyDb = 4.342944819 * relativeStandardError /
                                 profile.temporalMoment;
    capture.points.push_back({frequency, level, confidence,
                              uncertaintyDb * uncertaintyDb});
    confidenceSum += confidence;
  }
  if (settings.removeBroadLevelDifference) {
    const double center = weightedMean(capture.points,
                                       std::max(40.0, lowHz),
                                       std::min(16000.0, highHz));
    for (auto& point : capture.points) point.levelDb -= center;
  }
  capture.confidence = confidenceSum / std::max<std::size_t>(1, capture.points.size());
  return capture;
}

namespace {

CorrectionModel matchWithDetailScale(const SpectrumCapture& reference,
                                     const SpectrumCapture& target,
                                     const MatchSettings& settings,
                                     double detailScale) {
  validateSpectrumCapture(reference);
  validateSpectrumCapture(target);
  if (!std::isfinite(settings.rangeLowHz) ||
      !std::isfinite(settings.rangeHighHz) || settings.rangeLowHz <= 0.0 ||
      settings.rangeHighHz <= settings.rangeLowHz || settings.resolution < 1 ||
      settings.resolution > 4096 || !std::isfinite(detailScale) ||
      detailScale < 0.0 || detailScale > 1.0) {
    throw std::runtime_error("Invalid match settings");
  }
  if (!std::isfinite(settings.maximumCorrectionDb) ||
      settings.maximumCorrectionDb <= 0.0 ||
      settings.maximumCorrectionDb > 60.0) {
    throw std::runtime_error(
        "Maximum correction must be above 0 and no more than 60 dB");
  }
  const auto profile = profileFor(settings.mode);
  const double lowHz = std::max({10.0, settings.rangeLowHz,
                                 reference.points.front().frequencyHz,
                                 target.points.front().frequencyHz});
  const double highHz = std::min({settings.rangeHighHz, 30000.0,
                                  reference.points.back().frequencyHz,
                                  target.points.back().frequencyHz});
  if (highHz <= lowHz) throw std::runtime_error("Captures have no common frequency range");
  const int outputPoints = profile.pointCount;
  const double lowErb = hzToErb(lowHz);
  const double highErb = hzToErb(highHz);
  std::vector<double> refFreq, refLevel, refConfidence;
  std::vector<double> targetFreq, targetLevel, targetConfidence;
  for (const auto& point : reference.points) {
    refFreq.push_back(point.frequencyHz);
    refLevel.push_back(point.levelDb);
    refConfidence.push_back(point.confidence);
  }
  for (const auto& point : target.points) {
    targetFreq.push_back(point.frequencyHz);
    targetLevel.push_back(point.levelDb);
    targetConfidence.push_back(point.confidence);
  }
  std::vector<double> erbs(outputPoints);
  std::vector<double> raw(outputPoints);
  std::vector<double> weights(outputPoints);
  for (int i = 0; i < outputPoints; ++i) {
    const double ratio = static_cast<double>(i) / (outputPoints - 1);
    erbs[i] = lowErb + (highErb - lowErb) * ratio;
    const double frequency = erbToHz(erbs[i]);
    raw[i] = interpolateLinear(refFreq, refLevel, frequency) -
             interpolateLinear(targetFreq, targetLevel, frequency);
    weights[i] = std::sqrt(std::max(0.0,
        interpolateLinear(refFreq, refConfidence, frequency) *
        interpolateLinear(targetFreq, targetConfidence, frequency)));
  }
  auto smoothed = gaussianSmooth(erbs, raw, weights, profile.smoothingErb);

  // A broad match curve can raise a narrow room or microphone resonance that
  // already exists in the Target. Preserve the broad tonal move, then restore
  // only the locally supported fine difference between the two captures. Voice
  // mode may reduce detailScale after its IR sanity check; other modes always
  // use the historical full-detail value of 1.0.
  const auto fine = gaussianSmooth(erbs, raw, weights, profile.resonanceDetailErb);
  const auto fineBaseline = gaussianSmooth(erbs, fine, weights,
                                           profile.resonanceBaselineErb);
  for (int i = 0; i < outputPoints; ++i) {
    const double frequency = erbToHz(erbs[i]);
    const double lowTaper = std::clamp((frequency - 35.0) / 35.0, 0.0, 1.0);
    const double highTaper = std::clamp((20000.0 - frequency) / 4000.0, 0.0, 1.0);
    // Fine correction needs stronger evidence than the broad tonal curve.
    // Confidence below 0.25 contributes no narrow detail; stable bins reach
    // full authority by 0.75. This prevents a single loud room event from
    // carving a permanent EQ feature while retaining persistent resonances.
    const double reliability = smoothStep01((weights[i] - 0.25) / 0.50);
    const double detail = std::clamp(
        (fine[i] - fineBaseline[i]) * profile.resonanceAmount *
            detailScale * reliability,
        -profile.resonanceLimitDb,
        profile.resonanceLimitDb);
    smoothed[i] += detail * lowTaper * highTaper;
  }
  if (settings.removeBroadLevelDifference) {
    double sum = 0.0;
    double totalWeight = 0.0;
    for (int i = 0; i < outputPoints; ++i) {
      const double frequency = erbToHz(erbs[i]);
      if (frequency < 40.0 || frequency > 16000.0) continue;
      const double weight = std::max(0.02, weights[i]);
      sum += smoothed[i] * weight;
      totalWeight += weight;
    }
    const double center = totalWeight > 0.0 ? sum / totalWeight : 0.0;
    for (auto& value : smoothed) value -= center;
  }
  CorrectionModel model;
  model.mode = settings.mode;
  model.analysisLowHz = lowHz;
  model.analysisHighHz = highHz;
  model.resolution = settings.resolution;
  model.nodes.reserve(outputPoints);
  for (int i = 0; i < outputPoints; ++i) {
    // Confidence controls how strongly uncertain neighbours influence smoothing,
    // but a valid capture must not quietly turn a requested match into a partial
    // correction. Retain a conservative floor while still softening weak bins.
    const double confidenceShaping = 0.92 + 0.08 * weights[i];
    const double gain = std::clamp(smoothed[i] * confidenceShaping,
                                   -settings.maximumCorrectionDb,
                                   settings.maximumCorrectionDb);
    model.nodes.push_back({erbToHz(erbs[i]), gain, weights[i]});
  }
  return model;
}

struct VoiceTailMetrics {
  double after5Ms = 0.0;
  double after10Ms = 0.0;
};

double modelDerivativeRmsDb(const CorrectionModel& source) {
  if (source.nodes.size() < 2) return 0.0;
  double energy = 0.0;
  std::size_t count = 0;
  double previous = std::clamp(source.nodes.front().gainDb, -18.0, 18.0);
  for (std::size_t i = 1; i < source.nodes.size(); ++i) {
    const double current = std::clamp(source.nodes[i].gainDb, -18.0, 18.0);
    const double difference = current - previous;
    energy += difference * difference;
    ++count;
    previous = current;
  }
  return count > 0 ? std::sqrt(energy / count) : 0.0;
}

double irTailEnergyFraction(const std::vector<double>& ir,
                            int sampleRate,
                            double milliseconds) {
  if (ir.empty() || sampleRate <= 0) return 0.0;
  const std::size_t begin = std::min(
      ir.size(), static_cast<std::size_t>(std::ceil(
                     sampleRate * milliseconds / 1000.0)));
  double total = 0.0;
  double tail = 0.0;
  for (std::size_t i = 0; i < ir.size(); ++i) {
    const double energy = ir[i] * ir[i];
    total += energy;
    if (i >= begin) tail += energy;
  }
  return total > 0.0 ? tail / total : 0.0;
}

VoiceTailMetrics symmetricVoiceTailMetrics(const CorrectionModel& source,
                                           int sampleRate) {
  CorrectionModel model = source;
  CorrectionModel inverse = source;
  for (std::size_t i = 0; i < model.nodes.size(); ++i) {
    // Evaluate the structural tail at a normal audible ceiling. This keeps a
    // user's optional Full Correction Range from turning a large but smooth
    // tonal move into a false Voice warning.
    model.nodes[i].gainDb = std::clamp(model.nodes[i].gainDb, -18.0, 18.0);
    inverse.nodes[i].gainDb = -model.nodes[i].gainDb;
  }
  IrRenderSettings settings;
  settings.sampleRate = std::max(8000, sampleRate);
  settings.rangeLowHz = model.analysisLowHz;
  settings.rangeHighHz = model.analysisHighHz;
  const auto forwardIr = renderMinimumPhaseIr(model, settings);
  const auto inverseIr = renderMinimumPhaseIr(inverse, settings);

  VoiceTailMetrics result;
  result.after5Ms = std::max(
      irTailEnergyFraction(forwardIr, settings.sampleRate, 5.0),
      irTailEnergyFraction(inverseIr, settings.sampleRate, 5.0));
  result.after10Ms = std::max(
      irTailEnergyFraction(forwardIr, settings.sampleRate, 10.0),
      irTailEnergyFraction(inverseIr, settings.sampleRate, 10.0));
  return result;
}

bool voiceCandidateSafe(const CorrectionModel& candidate,
                        int sampleRate,
                        const VoiceTailMetrics* broadTail) {
  // Uniformly ERB-spaced Voice nodes make first-difference RMS a useful cheap
  // first gate. An established compact full-detail match can pass immediately
  // without any IR analysis. Once that full-detail curve has failed the gate,
  // however, every reduced-detail candidate must actually pass the IR-tail test;
  // becoming merely smoother is not enough to declare the ringing risk gone.
  if (broadTail == nullptr) return modelDerivativeRmsDb(candidate) <= 0.75;

  // Compare late energy with the broad-only Voice result, not with an absolute
  // IR-duration limit. Broad low-frequency EQ can legitimately have a long
  // minimum-phase tail. What is suspicious is *extra* coherent tail created by
  // restoring lots of narrow speech detail. Using max(forward, inverse) makes
  // this decision sign-symmetric, so swapping Reference/Target chooses the same
  // detail scale and negative Correction Strength remains a true inverse.
  const auto candidateTail = symmetricVoiceTailMetrics(candidate, sampleRate);
  const double excess5 = std::max(0.0, candidateTail.after5Ms - broadTail->after5Ms);
  const double excess10 = std::max(0.0, candidateTail.after10Ms - broadTail->after10Ms);
  return excess5 <= 0.0015 && excess10 <= 0.0005;
}

}  // namespace

CorrectionModel MatchEngine::match(const SpectrumCapture& reference,
                                   const SpectrumCapture& target,
                                   const MatchSettings& settings) const {
  if (settings.mode != MatchMode::Voice) {
    return matchWithDetailScale(reference, target, settings, 1.0);
  }
  if (!settings.enableVoiceSafety) {
    return matchWithDetailScale(reference, target, settings,
                                std::clamp(settings.voiceDetailScale, 0.0, 1.0));
  }

  // Keep the historical Voice curve whenever it is already structurally
  // compact. If it is suspicious, compare its IR tail with a broad-only baseline
  // and progressively withdraw only the narrow resonance-restoration component.
  // The broad tonal match is never replaced, and the same scale is chosen when
  // Reference and Target are swapped.
  constexpr std::array<double, 8> detailScales{
      1.0, 0.85, 0.70, 0.55, 0.40, 0.25, 0.10, 0.0};
  // The learned model itself is sample-rate independent, so the safety decision
  // must not change merely because the host runs at another rate. Evaluate the
  // structural IR at both common base-rate families and require a suspicious
  // Voice candidate to be safe at both. This also avoids making 44.1 kHz and
  // 48 kHz sessions choose different amounts of resonance detail because of FFT
  // discretization. The actual correction is still rendered natively at the
  // host/export sample rate.
  constexpr std::array<int, 2> kVoiceSafetyDiagnosticRates{44100, 48000};
  auto candidate = matchWithDetailScale(reference, target, settings, 1.0);
  if (modelDerivativeRmsDb(candidate) <= 0.75) return candidate;

  const auto broad = matchWithDetailScale(reference, target, settings, 0.0);
  std::array<VoiceTailMetrics, kVoiceSafetyDiagnosticRates.size()> broadTails{};
  for (std::size_t i = 0; i < kVoiceSafetyDiagnosticRates.size(); ++i) {
    broadTails[i] =
        symmetricVoiceTailMetrics(broad, kVoiceSafetyDiagnosticRates[i]);
  }

  const auto safeAtBothRates = [&](const CorrectionModel& model) {
    for (std::size_t i = 0; i < kVoiceSafetyDiagnosticRates.size(); ++i) {
      if (!voiceCandidateSafe(model, kVoiceSafetyDiagnosticRates[i],
                              &broadTails[i])) {
        return false;
      }
    }
    return true;
  };

  if (safeAtBothRates(candidate)) return candidate;
  for (std::size_t i = 1; i + 1 < detailScales.size(); ++i) {
    candidate = matchWithDetailScale(reference, target, settings, detailScales[i]);
    if (safeAtBothRates(candidate)) return candidate;
  }
  return broad;
}

CorrectionModel MatchEngine::match(const AudioBuffer& reference,
                                   const AudioBuffer& target,
                                   const MatchSettings& settings) const {
  return match(capture(reference, settings), capture(target, settings), settings);
}

double manualGainDbAt(double frequencyHz, const std::vector<double>& gains,
                      double lowHz, double highHz) {
  const std::size_t count = gains.size();
  if (count == 0) return 0.0;
  if (count == 1) return gains[0];
  double gridLow = std::max(lowHz, 20.0);
  double gridHigh = std::min(highHz, 20000.0);
  if (gridHigh <= gridLow) {
    gridLow = 20.0;
    gridHigh = 20000.0;
  }
  const double safeFrequency = std::max(1.0, frequencyHz);
  const double fraction =
      (std::log10(safeFrequency) - std::log10(gridLow)) /
      (std::log10(gridHigh) - std::log10(gridLow)) *
      static_cast<double>(count - 1);
  if (fraction <= 0.0) return gains[0];
  if (fraction >= static_cast<double>(count - 1)) return gains[count - 1];
  const std::size_t lower = static_cast<std::size_t>(fraction);
  const double t = fraction - static_cast<double>(lower);
  return gains[lower] * (1.0 - t) + gains[lower + 1] * t;
}

std::vector<double> resampleManualGains(const std::vector<double>& gains,
                                        std::size_t targetCount,
                                        double lowHz, double highHz) {
  if (targetCount == 0) return {};
  if (gains.empty()) return std::vector<double>(targetCount, 0.0);
  if (gains.size() == targetCount) return gains;

  double gridLow = std::max(lowHz, 20.0);
  double gridHigh = std::min(highHz, 20000.0);
  if (!std::isfinite(gridLow) || !std::isfinite(gridHigh) ||
      gridHigh <= gridLow) {
    gridLow = 20.0;
    gridHigh = 20000.0;
  }

  std::vector<double> result(targetCount, 0.0);
  if (targetCount == 1) {
    result[0] = manualGainDbAt(std::sqrt(gridLow * gridHigh), gains,
                               gridLow, gridHigh);
    return result;
  }
  for (std::size_t index = 0; index < targetCount; ++index) {
    const double fraction = static_cast<double>(index) /
                            static_cast<double>(targetCount - 1);
    const double frequency = gridLow * std::pow(gridHigh / gridLow, fraction);
    result[index] = manualGainDbAt(frequency, gains, gridLow, gridHigh);
  }
  return result;
}

namespace {

double gainDbAtWithCeiling(const CorrectionModel& model, double frequencyHz,
                           double maximumCorrectionDb) {
  if (model.nodes.empty()) return 0.0;
  const double ceiling = std::clamp(maximumCorrectionDb, 0.0, 60.0);
  const auto limited = [ceiling](double value) {
    return std::clamp(value, -ceiling, ceiling);
  };
  const double frequency = std::max(1.0e-9, frequencyHz);
  if (frequency <= model.nodes.front().frequencyHz) {
    return limited(model.nodes.front().gainDb);
  }
  if (frequency >= model.nodes.back().frequencyHz) {
    return limited(model.nodes.back().gainDb);
  }
  const auto upper = std::upper_bound(
      model.nodes.begin(), model.nodes.end(), frequency,
      [](double value, const CorrectionNode& node) {
        return value < node.frequencyHz;
      });
  const std::size_t hi = static_cast<std::size_t>(upper - model.nodes.begin());
  const std::size_t lo = hi - 1;
  const double x = std::log(frequency);
  const double x0 = std::log(model.nodes[lo].frequencyHz);
  const double x1 = std::log(model.nodes[hi].frequencyHz);
  const double ratio = (x - x0) / std::max(1.0e-18, x1 - x0);
  const double loGain = limited(model.nodes[lo].gainDb);
  const double hiGain = limited(model.nodes[hi].gainDb);
  return loGain + (hiGain - loGain) * ratio;
}

}  // namespace

CorrectionBreakdown evaluateCorrectionAt(const CorrectionModel& model,
                                         double maximumCorrectionDb,
                                         const IrRenderSettings& settings,
                                         double frequencyHz) {
  CorrectionBreakdown result;
  result.outputDb = settings.correctionGainDb;
  if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0 ||
      !std::isfinite(maximumCorrectionDb) || maximumCorrectionDb <= 0.0) {
    return result;
  }

  const double nyquist = settings.sampleRate * 0.5;
  const double effectiveLowHz =
      std::max(settings.rangeLowHz, model.analysisLowHz);
  const double effectiveHighHz =
      std::min({settings.rangeHighHz, model.analysisHighHz, nyquist * 0.999});
  if (effectiveHighHz <= effectiveLowHz || frequencyHz < effectiveLowHz ||
      frequencyHz > effectiveHighHz) {
    return result;
  }

  result.inRange = true;
  const double safeFrequency = std::max(1.0, frequencyHz);
  const double octaveWindow = 0.20;
  const double base =
      gainDbAtWithCeiling(model, safeFrequency, maximumCorrectionDb);
  const double localAverage =
      (gainDbAtWithCeiling(model, safeFrequency * std::exp2(-octaveWindow),
                           maximumCorrectionDb) +
       gainDbAtWithCeiling(model,
                           safeFrequency * std::exp2(-octaveWindow * 0.5),
                           maximumCorrectionDb) +
       base +
       gainDbAtWithCeiling(model,
                           safeFrequency * std::exp2(octaveWindow * 0.5),
                           maximumCorrectionDb) +
       gainDbAtWithCeiling(model, safeFrequency * std::exp2(octaveWindow),
                           maximumCorrectionDb)) /
      5.0;
  const double focused =
      base + (settings.correctionSharpness - 1.0) * (base - localAverage);
  result.automaticDb = focused * settings.correctionStrength;
  result.manualDb = manualGainDbAt(frequencyHz, settings.manualGains,
                                   model.analysisLowHz,
                                   model.analysisHighHz);
  result.tonalDb = result.automaticDb + result.manualDb;
  result.outputDb += result.tonalDb;
  return result;
}

void validateIrRenderSettings(const CorrectionModel& model,
                              const IrRenderSettings& settings) {
  validateCorrectionModel(model);
  if (settings.sampleRate < 8000 || settings.sampleRate > 768000) {
    throw std::runtime_error("Invalid IR sample rate");
  }
  if (!std::isfinite(settings.durationSeconds) || settings.durationSeconds <= 0.0 ||
      settings.durationSeconds > 10.0 ||
      !std::isfinite(settings.correctionStrength) ||
      std::abs(settings.correctionStrength) > 10.0 ||
      !std::isfinite(settings.correctionSharpness) ||
      settings.correctionSharpness < 0.5 ||
      settings.correctionSharpness > 1.5 ||
      !std::isfinite(settings.correctionGainDb) ||
      std::abs(settings.correctionGainDb) > 120.0 ||
      !std::isfinite(settings.rangeLowHz) ||
      !std::isfinite(settings.rangeHighHz) || settings.rangeLowHz <= 0.0 ||
      settings.rangeHighHz <= settings.rangeLowHz) {
    throw std::runtime_error("Invalid IR render settings");
  }
  if (settings.manualGains.size() > 256) {
    throw std::runtime_error("Too many manual gain bands");
  }
  for (const double gain : settings.manualGains) {
    if (!std::isfinite(gain) || std::abs(gain) > 120.0) {
      throw std::runtime_error("Invalid manual gain value");
    }
  }
  const long double requestedLength =
      static_cast<long double>(settings.durationSeconds) * settings.sampleRate;
  if (requestedLength > static_cast<long double>(
          std::numeric_limits<std::size_t>::max() / 4U)) {
    throw std::runtime_error("Requested IR is too large");
  }
}

std::vector<double> renderMinimumPhaseIr(const CorrectionModel& model,
                                         const IrRenderSettings& settings) {
  validateIrRenderSettings(model, settings);
  const std::size_t outputLength = std::max<std::size_t>(64,
      static_cast<std::size_t>(std::ceil(settings.durationSeconds * settings.sampleRate)));
  const std::size_t fftSize = nextPowerOfTwo(outputLength * 4);
  std::vector<std::complex<double>> logSpectrum(fftSize, {0.0, 0.0});
  for (std::size_t bin = 0; bin <= fftSize / 2; ++bin) {
    const double frequency =
        static_cast<double>(bin) * settings.sampleRate / fftSize;
    const double gainDb =
        evaluateCorrectionAt(model, 60.0, settings, frequency).outputDb;
    if (!std::isfinite(gainDb) || std::abs(gainDb) > 700.0) {
      throw std::runtime_error("IR gain exceeds the safe numerical range");
    }
    const double logMagnitude = gainDb * std::log(10.0) / 20.0;
    logSpectrum[bin] = {logMagnitude, 0.0};
    if (bin > 0 && bin < fftSize / 2) logSpectrum[fftSize - bin] = {logMagnitude, 0.0};
  }
  fft(logSpectrum, true);
  for (std::size_t i = 1; i < fftSize / 2; ++i) logSpectrum[i] *= 2.0;
  for (std::size_t i = fftSize / 2 + 1; i < fftSize; ++i) logSpectrum[i] = {0.0, 0.0};
  fft(logSpectrum, false);
  for (auto& value : logSpectrum) value = std::exp(value);
  fft(logSpectrum, true);
  std::vector<double> ir(outputLength, 0.0);
  for (std::size_t i = 0; i < outputLength; ++i) ir[i] = finiteOrZero(logSpectrum[i].real());
  const std::size_t fadeLength = std::max<std::size_t>(16, outputLength / 16);
  for (std::size_t i = 0; i < fadeLength; ++i) {
    const double ratio = static_cast<double>(i) / std::max<std::size_t>(1, fadeLength - 1);
    const double fade = 0.5 + 0.5 * std::cos(kPi * ratio);
    ir[outputLength - fadeLength + i] *= fade;
  }
  // The portable direct IR is a 32-bit float WAV. Make those stored
  // coefficients canonical for the internal renderer as well, so a saved and
  // reloaded IR drives sample-identical convolution instead of differing only
  // by an inaudible double-to-float round-off residue.
  for (auto& sample : ir) {
    sample = static_cast<double>(static_cast<float>(sample));
  }
  return ir;
}

AudioBuffer convolve(const AudioBuffer& input, const std::vector<double>& ir) {
  validateAudioBuffer(input);
  if (ir.empty()) throw std::runtime_error("Convolution needs an IR");
  if (input.frames() > std::numeric_limits<std::size_t>::max() - ir.size() + 1U) {
    throw std::runtime_error("Convolution output is too large");
  }
  const std::size_t outputFrames = input.frames() + ir.size() - 1;
  const std::size_t fftSize = nextPowerOfTwo(outputFrames);
  std::vector<std::complex<double>> irSpectrum(fftSize, {0.0, 0.0});
  for (std::size_t i = 0; i < ir.size(); ++i) irSpectrum[i] = ir[i];
  fft(irSpectrum, false);
  AudioBuffer result;
  result.sampleRate = input.sampleRate;
  result.channels.assign(input.channels.size(), std::vector<double>(outputFrames, 0.0));
  for (std::size_t channel = 0; channel < input.channels.size(); ++channel) {
    std::vector<std::complex<double>> spectrum(fftSize, {0.0, 0.0});
    for (std::size_t i = 0; i < input.frames(); ++i) spectrum[i] = input.channels[channel][i];
    fft(spectrum, false);
    for (std::size_t i = 0; i < fftSize; ++i) spectrum[i] *= irSpectrum[i];
    fft(spectrum, true);
    for (std::size_t i = 0; i < outputFrames; ++i) result.channels[channel][i] = finiteOrZero(spectrum[i].real());
  }
  return result;
}

MatchError compareCaptures(const SpectrumCapture& a,
                           const SpectrumCapture& b,
                           double lowHz,
                           double highHz) {
  validateSpectrumCapture(a);
  validateSpectrumCapture(b);
  if (!std::isfinite(lowHz) || !std::isfinite(highHz) || highHz <= lowHz) {
    throw std::runtime_error("Invalid capture comparison range");
  }
  std::vector<double> bFreq, bLevel;
  for (const auto& point : b.points) {
    bFreq.push_back(point.frequencyHz);
    bLevel.push_back(point.levelDb);
  }
  MatchError result;
  double squared = 0.0;
  for (const auto& point : a.points) {
    if (point.frequencyHz < lowHz || point.frequencyHz > highHz) continue;
    const double difference = point.levelDb - interpolateLinear(bFreq, bLevel, point.frequencyHz);
    squared += difference * difference;
    result.maximumDb = std::max(result.maximumDb, std::abs(difference));
    ++result.pointCount;
  }
  result.rmsDb = result.pointCount ? std::sqrt(squared / result.pointCount) : 0.0;
  return result;
}

std::string toString(MatchMode mode) {
  switch (mode) {
    case MatchMode::Voice: return "voice";
    case MatchMode::Drums: return "drums";
    case MatchMode::BassSynth: return "bass-synth";
    case MatchMode::CustomMaxCapture: return "custom-max";
    case MatchMode::FullMix:
    default: return "full-mix";
  }
}

MatchMode parseMatchMode(const std::string& text) {
  if (text == "full-mix" || text == "full") return MatchMode::FullMix;
  if (text == "voice") return MatchMode::Voice;
  if (text == "drums") return MatchMode::Drums;
  if (text == "bass-synth" || text == "bass") return MatchMode::BassSynth;
  if (text == "custom-max" || text == "custom") return MatchMode::CustomMaxCapture;
  throw std::runtime_error("Unknown match mode: " + text);
}

}  // namespace tonetrace
