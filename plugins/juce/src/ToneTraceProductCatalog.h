#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace tonetrace::product
{
enum class ParameterKind
{
    Workflow,
    Choice,
    Continuous,
    Toggle,
    Telemetry,
};

struct ReleasedParameterSpec
{
    const char* key;
    unsigned int releasedNumericId;
    const char* name;
    const char* description;
    const char* unit;
    double minimum;
    double maximum;
    double defaultValue;
    bool automatable;
    bool readOnly;
    bool stepped;
    ParameterKind kind;
};

// Authoritative Tone Trace 1.0.5 host contract, commit
// 24462d9d80d9df23048f528bd81e8d8573397724. Numeric identifiers are retained
// as product identities even though JUCE owns the VST3 wrapper's native IDs.
inline constexpr std::array<ReleasedParameterSpec, 19> kReleasedParameters {{
    { "workflowStep", 100, "Workflow Step", "Persistent workflow command selector, including protected Reset commands.", "", 0.0, 7.0, 0.0, true, false, true, ParameterKind::Workflow },
    { "status", 230, "Status", "Read-only workflow and error status.", "", 0.0, 30.0, 0.0, true, true, true, ParameterKind::Telemetry },
    { "lastCommand", 195, "Last Command", "Read-only record of the most recently accepted workflow command.", "", 0.0, 7.0, 0.0, true, true, true, ParameterKind::Telemetry },
    { "matchMode", 110, "Match Mode", "Changes how retained spectral information is interpreted for different source material.", "", 0.0, 4.0, 1.0, true, false, true, ParameterKind::Choice },
    { "maximumCorrection", 160, "Maximum Correction", "Limits the magnitude of the learned tonal correction without acting as an output limiter.", "dB", 1.0, 60.0, 18.0, true, false, false, ParameterKind::Continuous },
    { "fullCorrectionRange", 170, "Full Correction Range", "Allows the learned correction to use Tone Trace's full supported internal correction range.", "", 0.0, 1.0, 0.0, true, false, true, ParameterKind::Toggle },
    { "correctionStrength", 120, "Correction Strength", "Scales or reverses the learned tonal correction while retaining the learned model.", "x", -1.0, 1.0, 1.0, true, false, false, ParameterKind::Continuous },
    { "correctionResolution", 130, "Correction Resolution", "Sets the number of editable trace bands without changing capture resolution.", "bands", 1.0, 120.0, 30.0, true, false, true, ParameterKind::Continuous },
    { "correctionRangeLow", 140, "Correction Range Low", "Masks rendered correction below this frequency without moving or discarding stored bands.", "Hz", 10.0, 18000.0, 10.0, true, false, false, ParameterKind::Continuous },
    { "correctionRangeHigh", 150, "Correction Range High", "Masks rendered correction above this frequency without moving or discarding stored bands.", "Hz", 20.0, 30000.0, 30000.0, true, false, false, ParameterKind::Continuous },
    { "correctionSharpness", 190, "Correction Q / Sharpness", "Controls how strongly local spectral detail stands out in the rendered correction.", "", 0.5, 1.5, 1.0, true, false, false, ParameterKind::Continuous },
    { "correctionGain", 180, "Correction Gain", "Global output trim applied separately from the learned tonal shape.", "dB", -24.0, 12.0, 0.0, true, false, false, ParameterKind::Continuous },
    { "emergencyClipGuard", 270, "Emergency Clip Guard", "Hard last-resort plug-in output ceiling; it is not included in exported impulse responses.", "dB", -12.0, 20.0, 6.0, true, false, false, ParameterKind::Continuous },
    { "confidenceToneVolume", 250, "Confidence Tone Volume", "Controls notification and trace-tone level; the released host text treats -60 dB as Off.", "dB", -60.0, -12.0, -12.0, true, false, false, ParameterKind::Continuous },
    { "captureConfidence", 200, "Capture Confidence", "Read-only reliability indicator for accepted capture material.", "", 0.0, 1.0, 0.0, true, true, false, ParameterKind::Telemetry },
    { "curveDrift", 210, "Curve Drift", "Read-only indicator of how much the running spectral estimate is still changing.", "dB", 0.0, 60.0, 60.0, true, true, false, ParameterKind::Telemetry },
    { "captureTime", 220, "Capture Time", "Read-only accepted-audio capture time, not transport wall-clock time.", "s", 0.0, 3600.0, 0.0, true, true, false, ParameterKind::Telemetry },
    { "toneNotifications", 240, "Tone Notifications", "Enables confidence, workflow warning, page-sweep, and trace navigation tones where applicable.", "", 0.0, 1.0, 1.0, true, false, true, ParameterKind::Toggle },
    { "bypass", 260, "Bypass", "Crossfades Tone Trace to dry input and suppresses correction, generated tones, and clip guard on output.", "", 0.0, 1.0, 0.0, true, false, true, ParameterKind::Toggle },
}};

inline constexpr std::array<const char*, 5> kMatchModeChoices {{
    "Full Mix", "Voice", "Drums", "Bass or Synth", "Custom Max Capture"
}};

inline constexpr std::array<const char*, 8> kWorkflowChoices {{
    "No action",
    "Capture Reference",
    "Save Reference and Learn Target",
    "Correct Target",
    "Freeze Correction",
    "Arm Reset",
    "Confirm Reset",
    "Cancel Reset",
}};

constexpr const ReleasedParameterSpec* findReleasedParameterById(unsigned int id)
{
    for (const auto& spec : kReleasedParameters)
        if (spec.releasedNumericId == id)
            return &spec;
    return nullptr;
}

constexpr bool releasedCatalogValid()
{
    for (std::size_t i = 0; i < kReleasedParameters.size(); ++i)
    {
        const auto& spec = kReleasedParameters[i];
        if (spec.key == nullptr || spec.name == nullptr || spec.description == nullptr
            || spec.minimum > spec.maximum || spec.defaultValue < spec.minimum
            || spec.defaultValue > spec.maximum)
            return false;
        if (spec.readOnly && spec.kind != ParameterKind::Telemetry)
            return false;
        for (std::size_t j = i + 1; j < kReleasedParameters.size(); ++j)
        {
            if (spec.releasedNumericId == kReleasedParameters[j].releasedNumericId)
                return false;
            if (std::string_view(spec.key) == std::string_view(kReleasedParameters[j].key))
                return false;
        }
    }
    return true;
}

static_assert(releasedCatalogValid());

struct ContinuousControlSpec
{
    const ReleasedParameterSpec* parameter;
    double publicResolution;
    double keyboardStep;
    double pageStep;
    int decimalPlaces;
    bool showPlus;
};

struct ChoiceControlSpec
{
    const ReleasedParameterSpec* parameter;
    std::array<const char*, 5> choices;
    int defaultIndex;
};

inline constexpr std::array<ContinuousControlSpec, 9> kImplementedContinuousControls {{
    { findReleasedParameterById(160), 0.001, 1.0, 6.0, 3, false },
    { findReleasedParameterById(120), 0.001, 0.01, 0.1, 3, true },
    { findReleasedParameterById(130), 1.0, 1.0, 10.0, 0, false },
    { findReleasedParameterById(140), 0.001, 10.0, 100.0, 3, false },
    { findReleasedParameterById(150), 0.001, 10.0, 100.0, 3, false },
    { findReleasedParameterById(190), 0.001, 0.01, 0.1, 3, false },
    { findReleasedParameterById(180), 0.001, 0.1, 1.0, 3, true },
    { findReleasedParameterById(270), 0.001, 0.1, 1.0, 3, true },
    { findReleasedParameterById(250), 0.001, 1.0, 6.0, 3, false },
}};
inline constexpr std::size_t kMaximumCorrectionIndex = 0;
inline constexpr std::size_t kCorrectionStrengthIndex = 1;
inline constexpr std::size_t kCorrectionResolutionIndex = 2;
inline constexpr std::size_t kCorrectionRangeLowIndex = 3;
inline constexpr std::size_t kCorrectionRangeHighIndex = 4;
inline constexpr std::size_t kCorrectionSharpnessIndex = 5;
inline constexpr std::size_t kCorrectionGainIndex = 6;
inline constexpr std::size_t kEmergencyClipGuardIndex = 7;
inline constexpr std::size_t kConfidenceToneVolumeIndex = 8;

inline constexpr std::array<const ReleasedParameterSpec*, 3> kImplementedToggleControls {{
    findReleasedParameterById(170),
    findReleasedParameterById(240),
    findReleasedParameterById(260),
}};
inline constexpr std::size_t kFullCorrectionRangeIndex = 0;
inline constexpr std::size_t kToneNotificationsIndex = 1;
inline constexpr std::size_t kBypassIndex = 2;

inline constexpr ChoiceControlSpec kImplementedMatchMode {
    findReleasedParameterById(110), kMatchModeChoices, 1
};

constexpr bool implementedControlsValid()
{
    for (const auto& control : kImplementedContinuousControls)
    {
        if (control.parameter->readOnly
            || control.parameter->kind != ParameterKind::Continuous
            || control.publicResolution <= 0.0 || control.keyboardStep <= 0.0
            || control.pageStep <= 0.0 || control.decimalPlaces < 0
            || control.decimalPlaces > 9)
            return false;
    }
    for (const auto* toggle : kImplementedToggleControls)
    {
        if (toggle == nullptr || toggle->readOnly || toggle->kind != ParameterKind::Toggle)
            return false;
    }
    return kImplementedMatchMode.parameter->kind == ParameterKind::Choice
        && kImplementedMatchMode.defaultIndex >= 0
        && kImplementedMatchMode.defaultIndex < static_cast<int>(kImplementedMatchMode.choices.size());
}
static_assert(implementedControlsValid());

inline double canonicalise(const ContinuousControlSpec& control, double value)
{
    const auto& spec = *control.parameter;
    if (!std::isfinite(value))
        return spec.defaultValue;
    value = std::clamp(value, spec.minimum, spec.maximum);
    const auto snapped = std::round(value / control.publicResolution) * control.publicResolution;
    const auto clean = std::abs(snapped) < (control.publicResolution * 0.5) ? 0.0 : snapped;
    return std::clamp(clean, spec.minimum, spec.maximum);
}

inline bool equalAsciiIgnoreCase(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    return a == b;
}

inline bool endsWithIgnoreCase(std::string_view text, std::string_view suffix)
{
    if (suffix.size() > text.size()) return false;
    const auto offset = text.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i)
        if (!equalAsciiIgnoreCase(text[offset + i], suffix[i]))
            return false;
    return true;
}

inline std::string trimCopy(std::string_view input)
{
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}

inline std::string formatContinuous(const ContinuousControlSpec& control,
                                    double value,
                                    bool includeUnit = true)
{
    const auto& spec = *control.parameter;
    value = canonicalise(control, value);
    if (spec.releasedNumericId == 250 && value <= -59.5) return "Off";
    char digits[96] {};
    const auto result = std::to_chars(digits, digits + sizeof(digits), value,
                                      std::chars_format::fixed, control.decimalPlaces);
    if (result.ec != std::errc {}) return {};
    std::string text(digits, result.ptr);
    const auto decimal = text.find('.');
    if (decimal != std::string::npos)
        while (text.size() > decimal + 2 && text.back() == '0') text.pop_back();
    if (control.showPlus && !text.empty() && text.front() != '-') text.insert(text.begin(), '+');
    if (includeUnit && spec.unit != nullptr && spec.unit[0] != '\0')
    {
        text.push_back(' ');
        text += spec.unit;
    }
    return text;
}

inline bool parseContinuous(const ContinuousControlSpec& control,
                            std::string_view input,
                            double& value)
{
    const auto& spec = *control.parameter;
    std::string copy = trimCopy(input);
    if (copy.empty()) return false;
    if (spec.releasedNumericId == 250
        && copy.size() == 3
        && equalAsciiIgnoreCase(copy[0], 'o')
        && equalAsciiIgnoreCase(copy[1], 'f')
        && equalAsciiIgnoreCase(copy[2], 'f'))
    {
        value = -60.0;
        return true;
    }
    const std::string_view unit = spec.unit != nullptr ? std::string_view(spec.unit) : std::string_view{};
    if (!unit.empty() && endsWithIgnoreCase(copy, unit))
    {
        copy.resize(copy.size() - unit.size());
        copy = trimCopy(copy);
        if (copy.empty()) return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || !std::isfinite(parsed)) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0' || parsed < spec.minimum || parsed > spec.maximum) return false;
    value = canonicalise(control, parsed);
    return true;
}
} // namespace tonetrace::product
