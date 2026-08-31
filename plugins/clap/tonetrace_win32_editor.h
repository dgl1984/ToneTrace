#pragma once

#include <cstdint>
#include <memory>

#include "tonetrace/tonetrace_engine.h"

namespace tonetrace {
struct ProfileSnapshot;
}

struct clap_plugin;
struct clap_plugin_params;

// Native Win32 editor for Tone Trace EQ. It is a thin shell over the CLAP
// parameter surface plus the instance's live snapshot. Every value shown is
// read through clap_plugin_params_t and every edit is routed through the same
// set-parameter path the host uses; the snapshot is read-only.
//
// The painted canvas creates no accessible object. Screen-reader access comes
// from native controls: a read-only description box, a trace-cursor readout,
// workflow buttons, and exact-value edits.
class ToneTraceWin32Editor {
 public:
  using GetSnapshotFn = const tonetrace::ProfileSnapshot* (*)(void* context);
  // Returns a committed Reference/Target spectrum that exists before a full
  // correction snapshot is available. This lets Learn Target genuinely serve
  // as "save Reference and begin Target" for reference-only export workflows.
  using GetStagedSpectrumFn = const tonetrace::SpectrumCapture* (*)(
      void* context, int which);
  using SetParamFn = void (*)(void* context, std::uint32_t paramId,
                              double value);
  using ResetFn = void (*)(void* context);
  using PlayToneFn = void (*)(void* context, double frequencyHz);
  using PlayBandSweepFn = void (*)(void* context, double fromHz, double toHz,
                                   int bandCount, double durationMs);
  using SetBandGainFn = void (*)(void* context, std::size_t index,
                                 double gainDb);
  using GetBandGainFn = double (*)(void* context, std::size_t index);
  using GetBandCountFn = std::size_t (*)(void* context);
  // The current project/host sample rate, needed to reconcile imported curves
  // captured at other sample rates against this session's Nyquist.
  using GetSampleRateFn = double (*)(void* context);
  using RefreshFn = void (*)(void* context);
  // The wrapper stores the handed-off capture/model and applies it on the main
  // thread via a WorkImport callback, so import stays thread-safe even though
  // the file dialog runs on the UI thread.
  using SetImportedSpectrumFn = void (*)(void* context, int which,
                                         const tonetrace::SpectrumCapture&);
  using SetImportedModelFn = void (*)(void* context,
                                      const tonetrace::CorrectionModel&);

  ToneTraceWin32Editor(const struct clap_plugin* plugin,
                       const struct clap_plugin_params* params,
                       void* context, GetSnapshotFn getSnapshot,
                       GetStagedSpectrumFn getStagedSpectrum,
                       SetParamFn setParam, ResetFn reset, PlayToneFn playTone,
                       PlayBandSweepFn playBandSweep, SetBandGainFn setBandGain,
                       GetBandGainFn getBandGain, GetBandCountFn getBandCount,
                       SetImportedSpectrumFn setImportedSpectrum = nullptr,
                       SetImportedModelFn setImportedModel = nullptr,
                       GetSampleRateFn getSampleRate = nullptr,
                       RefreshFn refresh = nullptr);
  ~ToneTraceWin32Editor();

  ToneTraceWin32Editor(const ToneTraceWin32Editor&) = delete;
  ToneTraceWin32Editor& operator=(const ToneTraceWin32Editor&) = delete;

  bool create();
  void destroy();
  bool setParent(void* window);
  bool setScale(double scale);
  bool getSize(std::uint32_t& width, std::uint32_t& height) const;
  bool setSize(std::uint32_t width, std::uint32_t height);
  bool adjustSize(std::uint32_t& width, std::uint32_t& height) const;
  bool show();
  bool hide();
  void setOfflineRendering(bool offline);

  static void unregisterWindowClasses();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
