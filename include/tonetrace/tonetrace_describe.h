#pragma once

#include "tonetrace/tonetrace_engine.h"
#include "tonetrace/tonetrace_realtime.h"

#include <string>
#include <vector>

namespace tonetrace {

// Natural-language descriptions of a Tone Trace profile. The GUI renders the
// curves visually and offers this text to screen-reader users; the text is a
// pure function of the snapshot so it can be unit-tested and stays in sync
// with what is drawn.

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
// are for wording, not for DSP; the engine's own analysis remains authoritative.
[[nodiscard]] const std::vector<CurveBand>& curveBands();

// Mean level (dB) of a capture over one band, relative to the capture's own
// overall mean level in the same band set. Positive means the band is louder
// than the curve's own average shape.
[[nodiscard]] double bandLevelDb(const SpectrumCapture& capture,
                                 const CurveBand& band);

// Band level of the correction model's gain at a band's geometric center.
[[nodiscard]] double correctionBandDb(const CorrectionModel& model,
                                      const CurveBand& band);

// One or two sentences describing one capture's tone shape.
[[nodiscard]] std::string describeCapture(const SpectrumCapture& capture);

// Describes what the correction does to the target, band by band, reporting
// the applied (ceiling-clamped) gains and calling out when the calculated
// correction was limited by Tone Trace's Maximum Correction setting.
[[nodiscard]] std::string describeCorrection(
    const CorrectionModel& model, double ceilingDb,
    double rangeLowHz = 20.0, double rangeHighHz = 20000.0);

// Full description of a snapshot. An empty snapshot yields a short Ready
// message rather than a failure.
[[nodiscard]] CurveDescription describeToneTrace(
    const ProfileSnapshot& snapshot);

// Convenience: concatenates the sections with headings and blank lines.
[[nodiscard]] std::string curveDescriptionText(const ProfileSnapshot& snapshot);

}  // namespace tonetrace
