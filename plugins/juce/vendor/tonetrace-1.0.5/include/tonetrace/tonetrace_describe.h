#pragma once

#include "tonetrace/tonetrace_engine.h"
#include "tonetrace/tonetrace_realtime.h"

#include <string>
#include <vector>

namespace tonetrace {

// Deterministic natural-language overview of a Tone Trace profile. This is an
// accessibility companion to the graph, not another DSP path: capture text is
// derived from the already-normalized capture points and correction text calls
// evaluateCorrectionAt(), the same evaluator used by the renderer/UI.
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

// Fixed internal summary bands only. They do not change analysis or matching,
// and their historical names are not exposed in user-facing description text.
[[nodiscard]] const std::vector<CurveBand>& curveBands();

// Confidence-weighted in-band mean of the already level-normalized capture
// points. No second whole-curve mean/median is subtracted here.
[[nodiscard]] double bandLevelDb(const SpectrumCapture& capture,
                                 const CurveBand& band);

// Legacy utility retained for callers/tests that need the model at a band's
// geometric center. The description generator itself uses evaluateCorrectionAt.
[[nodiscard]] double correctionBandDb(const CorrectionModel& model,
                                      const CurveBand& band);

// Short overview of one captured curve using plain frequency-specific language.
// Measured Reference/Target shapes use higher/lower terminology; boost/cut is
// reserved for the correction Tone Trace actually applies.
[[nodiscard]] std::string describeCapture(const SpectrumCapture& capture);

// Overview of the correction actually heard from Strength, Q, range and manual
// band trims. Correction Gain is intentionally omitted because it is a global
// level change rather than tonal shape. Maximum Correction is described
// separately when learned nodes hit the ceiling.
[[nodiscard]] std::string describeCorrection(
    const CorrectionModel& model, double ceilingDb,
    const IrRenderSettings& settings);

// Full Reference/Target/Correction overview. When both captures exist, Summary
// directly describes Target relative to Reference instead of making the reader
// mentally subtract two independent paragraphs.
[[nodiscard]] CurveDescription describeToneTrace(
    const ProfileSnapshot& snapshot);

[[nodiscard]] std::string curveDescriptionText(const ProfileSnapshot& snapshot);

}  // namespace tonetrace
