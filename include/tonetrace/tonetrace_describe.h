#pragma once

#include "tonetrace/tonetrace_engine.h"
#include "tonetrace/tonetrace_realtime.h"

#include <string>
#include <vector>

namespace tonetrace {

// Deterministic natural-language overview of a Tone Trace profile. This is the
// same product information for every user: the GUI renders the exact graph and
// readout, while this overview names the broad tonal geometry without trying to
// reproduce every graph point in prose.
struct CurveDescription {
  std::string reference;
  std::string target;
  std::string correction;
  std::string summary;
};

struct CurveBand {
  const char* name = "";
  double lowHz = 0.0;
  double highHz = 0.0;
};

// Fixed descriptive bands. Sub/bass/low-mid/mid/presence/air/brilliance. These
// are wording regions only; they do not change analysis or processing.
[[nodiscard]] const std::vector<CurveBand>& curveBands();

// Confidence-weighted in-band mean of the already level-normalised capture
// points. No second whole-curve mean or median is subtracted here.
[[nodiscard]] double bandLevelDb(const SpectrumCapture& capture,
                                 const CurveBand& band);

// Legacy helper retained for API stability in this round. The description
// parser itself does not use it; applied correction prose comes from
// evaluateCorrectionAt(...).tonalDb.
[[nodiscard]] double correctionBandDb(const CorrectionModel& model,
                                      const CurveBand& band);

// Broad tonal shape of one captured curve (shelf, peak/dip, tilt, smile/frown,
// or close to flat), using the seven fixed descriptive regions.
[[nodiscard]] std::string describeCapture(const SpectrumCapture& capture);

// Broad shape of the correction actually applied by the renderer. Strength,
// Q/sharpness, manual trims, range and Nyquist are reflected through
// evaluateCorrectionAt(...).tonalDb; global Correction Gain is intentionally
// excluded. Maximum Correction is reported separately only when uncapped model
// nodes exceed the pre-Strength/Q/trim node ceiling.
[[nodiscard]] std::string describeCorrection(
    const CorrectionModel& model, double ceilingDb,
    const IrRenderSettings& settings);

// Full description of a snapshot. Summary compares Target directly with
// Reference when both captures exist. An empty snapshot uses the engine-owned
// universal empty copy and points users to the still-available Bands pages.
[[nodiscard]] CurveDescription describeToneTrace(
    const ProfileSnapshot& snapshot);

// Concatenates Summary / Reference / Target / Correction with blank lines.
[[nodiscard]] std::string curveDescriptionText(const ProfileSnapshot& snapshot);

}  // namespace tonetrace
