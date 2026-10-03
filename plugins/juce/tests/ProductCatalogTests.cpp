#include "../src/ToneTraceProductCatalog.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main()
{
    using namespace tonetrace::product;

    require(kReleasedParameters.size() == 19, "released parameter count");
    require(releasedCatalogValid(), "released catalog structural validation");

    const auto* workflow = findReleasedParameterById(100);
    const auto* mode = findReleasedParameterById(110);
    const auto* gainParameter = findReleasedParameterById(180);
    const auto* sharpnessParameter = findReleasedParameterById(190);
    const auto* clipGuard = findReleasedParameterById(270);
    require(workflow != nullptr && std::string(workflow->name) == "Workflow Step", "workflow identity");
    require(mode != nullptr && std::string(mode->name) == "Match Mode", "match mode identity");
    require(gainParameter != nullptr && std::string(gainParameter->name) == "Correction Gain", "gain identity");
    require(sharpnessParameter != nullptr && std::string(sharpnessParameter->name) == "Correction Q / Sharpness", "sharpness identity");
    require(clipGuard != nullptr && std::abs(clipGuard->defaultValue - 6.0) < 1.0e-12, "clip guard default");

    require(findReleasedParameterById(230)->readOnly, "Status read-only");
    require(findReleasedParameterById(195)->readOnly, "Last Command read-only");
    require(findReleasedParameterById(200)->readOnly, "Capture Confidence read-only");
    require(findReleasedParameterById(210)->readOnly, "Curve Drift read-only");
    require(findReleasedParameterById(220)->readOnly, "Capture Time read-only");

    require(kImplementedContinuousControls.size() == 9, "full released correction control count");
    require(kImplementedToggleControls.size() == 3, "full released toggle count");
    require(kImplementedContinuousControls[kMaximumCorrectionIndex].parameter == findReleasedParameterById(160), "Maximum Correction mapping");
    require(kImplementedContinuousControls[kCorrectionStrengthIndex].parameter == findReleasedParameterById(120), "Correction Strength mapping");
    require(kImplementedContinuousControls[kCorrectionResolutionIndex].parameter == findReleasedParameterById(130), "Correction Resolution mapping");
    require(kImplementedContinuousControls[kCorrectionRangeLowIndex].parameter == findReleasedParameterById(140), "Range Low mapping");
    require(kImplementedContinuousControls[kCorrectionRangeHighIndex].parameter == findReleasedParameterById(150), "Range High mapping");
    require(kImplementedContinuousControls[kEmergencyClipGuardIndex].parameter == findReleasedParameterById(270), "Emergency Clip Guard mapping");
    require(kImplementedToggleControls[kFullCorrectionRangeIndex] == findReleasedParameterById(170), "Full Correction Range mapping");

    const auto& gain = kImplementedContinuousControls[kCorrectionGainIndex];
    const auto& sharpness = kImplementedContinuousControls[kCorrectionSharpnessIndex];
    const auto& toneLevel = kImplementedContinuousControls[kConfidenceToneVolumeIndex];
    require(gain.parameter == gainParameter, "implemented gain maps to released catalog");
    require(sharpness.parameter == sharpnessParameter, "implemented sharpness maps to released catalog");
    require(toneLevel.parameter == findReleasedParameterById(250), "implemented tone level maps to released catalog");
    require(std::abs(gain.publicResolution - 0.001) < 1.0e-12, "gain public resolution");
    require(std::abs(gain.keyboardStep - 0.1) < 1.0e-12, "gain keyboard step");
    require(std::abs(sharpness.keyboardStep - 0.01) < 1.0e-12, "sharpness keyboard step");
    require(formatContinuous(gain, -6.234) == "-6.234 dB", "gain formatting");
    require(formatContinuous(sharpness, 1.237) == "1.237", "sharpness formatting");
    require(formatContinuous(toneLevel, -24.5) == "-24.5 dB", "tone level formatting");
    require(formatContinuous(toneLevel, -60.0) == "Off", "tone level Off formatting");

    double value = 0.0;
    require(parseContinuous(gain, " +1.125 dB ", value) && std::abs(value - 1.125) < 1.0e-9,
            "gain exact parser");
    require(!parseContinuous(gain, "99", value), "gain out-of-range rejection");
    require(parseContinuous(sharpness, "1.237", value) && std::abs(value - 1.237) < 1.0e-9,
            "sharpness exact parser");
    require(parseContinuous(toneLevel, "Off", value) && std::abs(value + 60.0) < 1.0e-9,
            "tone level Off parser");

    require(kImplementedToggleControls[kToneNotificationsIndex] == findReleasedParameterById(240),
            "Tone Notifications toggle mapping");
    require(kImplementedToggleControls[kBypassIndex] == findReleasedParameterById(260),
            "Bypass toggle mapping");

    require(kImplementedMatchMode.parameter == mode, "implemented Match Mode maps to released catalog");
    require(kImplementedMatchMode.defaultIndex == 1, "Voice default");
    require(std::string(kMatchModeChoices[0]) == "Full Mix", "Full Mix label");
    require(std::string(kMatchModeChoices[1]) == "Voice", "Voice label");
    require(std::string(kMatchModeChoices[2]) == "Drums", "Drums label");
    require(std::string(kMatchModeChoices[3]) == "Bass or Synth", "Bass/Synth label");
    require(std::string(kMatchModeChoices[4]) == "Custom Max Capture", "Custom Max Capture label");

    require(std::string(kWorkflowChoices[5]) == "Arm Reset", "protected Reset Arm value");
    require(std::string(kWorkflowChoices[6]) == "Confirm Reset", "protected Reset Confirm value");
    require(std::string(kWorkflowChoices[7]) == "Cancel Reset", "protected Reset Cancel value");

    std::cout << "PASS: full released Tone Trace correction/control catalog is mapped into JUCE.\n";
    return 0;
}
