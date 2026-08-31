#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include "tonetrace_win32_editor.h"
#include "tonetrace_accessible_fader.h"
#include "tonetrace_band_value.h"
#include "resource.h"

#include <clap/clap.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>

#include "tonetrace/tonetrace_describe.h"
#include "tonetrace/tonetrace_engine.h"
#include "tonetrace/tonetrace_realtime.h"
#include "tonetrace/tonetrace_ui_layout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr UINT_PTR kTimerIntervalMs = 33;
constexpr UINT_PTR kTraceAnnounceTimerId = 2;
constexpr UINT kTraceAnnounceDelayMs = 190;

constexpr UINT kReaperWrapperReadyMessage = WM_APP + 1;

constexpr const wchar_t* kSpectrumFilter =
    L"Tone Trace Curve (*.tts)\0*.tts\0All Files (*.*)\0*.*\0\0";
constexpr const wchar_t* kModelFilter =
    L"Tone Trace Model (*.ttm)\0*.ttm\0All Files (*.*)\0*.*\0\0";
constexpr const wchar_t* kWavFilter =
    L"Wave Audio (*.wav)\0*.wav\0All Files (*.*)\0*.*\0\0";

bool isReaperPluginWrapper(HWND window) {
  wchar_t className[64]{};
  return window != nullptr &&
         GetClassNameW(window, className,
                       static_cast<int>(std::size(className))) != 0 &&
         std::wcscmp(className, L"reaperPluginHostWrapProc") == 0;
}

constexpr int kFirstButtonId = 100;
constexpr int kCaptureReferenceId = 100;
constexpr int kLearnTargetId = 101;
constexpr int kCorrectTargetId = 102;
constexpr int kFreezeId = 103;
constexpr int kDescribeId = 104;
constexpr int kTraceId = 105;
constexpr int kExportId = 106;
constexpr int kImportId = 107;
constexpr int kOptionsId = 108;
constexpr int kModeComboId = 200;
constexpr int kModeLabelId = 201;
constexpr int kDescriptionLabelId = 202;
constexpr int kDescriptionEditId = 203;
constexpr int kResolutionLabelId = 204;
constexpr int kResolutionComboId = 205;
constexpr int kStatusLabelId = 206;
constexpr int kStatusEditId = 207;
constexpr int kReadoutEditId = 208;
constexpr int kEditFirstId = 300;
constexpr int kTabControlId = 50;
constexpr int kBandSliderFirstId = 2000;
constexpr int kBandLabelFirstId = 3000;

// Context-menu commands for the Import/Export buttons. These are never window
// control ids so they cannot collide with the edit/band id ranges.
constexpr int kExportMenuBase = 600;
constexpr int kImportMenuBase = 700;
enum {
  kExportIr = kExportMenuBase,
  kExportReference,
  kExportTarget,
  kExportModel,
  kImportReference = kImportMenuBase,
  kImportTarget,
  kImportModel,
};

// Continuous parameters surfaced as exact-value edits. Order matches layout.
constexpr tonetrace::ParameterId kEditedParams[]{
    tonetrace::ParameterId::CorrectionStrength,
    tonetrace::ParameterId::MaximumCorrectionDb,
    tonetrace::ParameterId::CorrectionGainDb,
    // Keep the last-resort guard immediately after the gain that can expose
    // peaks. The label, edit, and tab order all derive from this one array.
    tonetrace::ParameterId::EmergencyClipGuardDb,
    tonetrace::ParameterId::CorrectionSharpness,
    tonetrace::ParameterId::RangeLowHz,
    tonetrace::ParameterId::RangeHighHz,
};

constexpr double kLogLowHz = 20.0;
constexpr double kLogHighHz = 20000.0;
constexpr double kMinimumDisplayDb = 12.0;
constexpr double kMaximumDisplayDb = 60.0;

std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int length = MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
  if (length <= 1) return {};
  // MultiByteToWideChar includes the terminator in `length`; allocate room for
  // it, then remove it from the returned C++ string. The previous length-1
  // allocation wrote the terminator one wchar past the string buffer.
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length) !=
      length) {
    return {};
  }
  wide.resize(static_cast<std::size_t>(length - 1));
  return wide;
}

std::string narrow(const std::wstring& text) {
  if (text.empty()) return {};
  const int length = WideCharToMultiByte(
      CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) return {};
  std::string utf8(static_cast<std::size_t>(length), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), length,
                          nullptr, nullptr) != length) {
    return {};
  }
  utf8.resize(static_cast<std::size_t>(length - 1));
  return utf8;
}

bool setWindowTextIfChanged(HWND window, const std::wstring& text) {
  if (window == nullptr) return false;
  const int length = GetWindowTextLengthW(window);
  if (length >= 0 && static_cast<std::size_t>(length) == text.size()) {
    std::wstring current(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(window, current.data(),
                                      static_cast<int>(current.size()));
    if (copied == length &&
        std::equal(text.begin(), text.end(), current.begin())) {
      return false;
    }
  }
  SetWindowTextW(window, text.c_str());
  return true;
}

double freqToX(double frequencyHz, int width) {
  const double logF = std::log10(std::clamp(frequencyHz, kLogLowHz, kLogHighHz));
  const double logMin = std::log10(kLogLowHz);
  const double logMax = std::log10(kLogHighHz);
  return static_cast<double>(width) *
         (logF - logMin) / (logMax - logMin);
}

std::wstring formatFrequency(double frequencyHz) {
  wchar_t buffer[32]{};
  if (frequencyHz >= 1000.0) {
    const double kilo = frequencyHz / 1000.0;
    if (std::abs(kilo - std::lround(kilo)) < 0.05) {
      std::swprintf(buffer, std::size(buffer), L"%d kHz",
                    static_cast<int>(std::lround(kilo)));
    } else {
      std::swprintf(buffer, std::size(buffer), L"%.1f kHz", kilo);
    }
  } else {
    std::swprintf(buffer, std::size(buffer), L"%d Hz",
                  static_cast<int>(std::lround(frequencyHz)));
  }
  return buffer;
}

double dbToY(double levelDb, int height, double displayDb) {
  const double range = std::clamp(displayDb, kMinimumDisplayDb, kMaximumDisplayDb);
  const double clipped = std::clamp(levelDb, -range, range);
  return static_cast<double>(height) *
         (1.0 - (clipped + range) / (2.0 * range));
}

std::wstring win32MultilineText(const std::wstring& text) {
  std::wstring converted;
  converted.reserve(text.size() + 16);
  for (std::size_t index = 0; index < text.size(); ++index) {
    const wchar_t ch = text[index];
    if (ch == L'\n' && (index == 0 || text[index - 1] != L'\r')) {
      converted += L'\r';
    }
    converted += ch;
  }
  return converted;
}

bool cursorInsideWindow(HWND window) {
  if (window == nullptr) return false;
  POINT point{};
  RECT bounds{};
  if (!GetCursorPos(&point) || !ScreenToClient(window, &point) ||
      !GetClientRect(window, &bounds)) {
    return false;
  }
  return PtInRect(&bounds, point) != FALSE;
}

void addControlTooltip(HWND tooltip, HWND owner, HWND control,
                       const wchar_t* text) {
  if (tooltip == nullptr || owner == nullptr || control == nullptr ||
      text == nullptr) {
    return;
  }
  TOOLINFOW tool{};
  // Hosts without a Common Controls v6 manifest reject the v3 structure size
  // (which adds lpReserved) and silently register zero tools. V2 contains every
  // field we use and works with both legacy and current comctl32 versions.
  tool.cbSize = TTTOOLINFOW_V2_SIZE;
  tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  tool.hwnd = owner;
  tool.uId = reinterpret_cast<UINT_PTR>(control);
  tool.lpszText = const_cast<LPWSTR>(text);
  SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

int confirmManualIrExport(HWND owner) {
  return MessageBoxW(
      owner,
      L"No learned match is available. Tone Trace will export an impulse "
      L"response of the manually created curve that is currently active.\n\n"
      L"Continue?",
      L"No learned match is available. Export the impulse response of the "
      L"manually created curve?",
      MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON1 | MB_SETFOREGROUND);
}

}  // namespace

class ToneTraceWin32Editor::Impl {
 public:
  Impl(const clap_plugin_t* plugin, const clap_plugin_params_t* params,
       void* context, GetSnapshotFn getSnapshot,
       GetStagedSpectrumFn getStagedSpectrum, SetParamFn setParam, ResetFn reset,
       PlayToneFn playTone, PlayBandSweepFn playBandSweep,
       SetBandGainFn setBandGain, GetBandGainFn getBandGain,
       GetBandCountFn getBandCount, SetImportedSpectrumFn setImportedSpectrum,
       SetImportedModelFn setImportedModel, GetSampleRateFn getSampleRate,
       RefreshFn refresh)
      : plugin_(plugin), params_(params), context_(context),
        getSnapshot_(getSnapshot), getStagedSpectrum_(getStagedSpectrum),
        setParam_(setParam), reset_(reset), playTone_(playTone),
        playBandSweep_(playBandSweep), setBandGain_(setBandGain),
        getBandGain_(getBandGain), getBandCount_(getBandCount),
        setImportedSpectrum_(setImportedSpectrum),
        setImportedModel_(setImportedModel), getSampleRate_(getSampleRate),
        refresh_(refresh) {}

  ~Impl() { destroy(); }

  bool create() {
    // The dialog is created in setParent() once the real parent window is
    // known; the CLAP spec calls set_parent before show() for embedded GUIs.
    return tonetrace::win32::registerAccessibleFaderClass(moduleInstance());
  }

  void destroy() {
    if (window_ == nullptr) return;
    if (tooltip_ != nullptr) {
      DestroyWindow(tooltip_);
      tooltip_ = nullptr;
    }
    DestroyWindow(window_);
    window_ = nullptr;
    parent_ = nullptr;
    reaperWrapper_ = nullptr;
  }

  bool setParent(void* parent) {
    const HWND requestedParent = static_cast<HWND>(parent);

    // REAPER passes a wrapper window (reaperPluginHostWrapProc) that can
    // intercept Tab navigation and hit testing. When detected, create this
    // editor as a sibling of the wrapper (child of the wrapper's parent) so
    // the host dialog manager recurses into the DS_CONTROL dialog and drives
    // Tab in and out without traps. app2clap-inspired.
    HWND actualParent = requestedParent;
    reaperWrapper_ = nullptr;
    if (isReaperPluginWrapper(requestedParent) &&
        GetParent(requestedParent) != nullptr) {
      reaperWrapper_ = requestedParent;
      actualParent = GetParent(requestedParent);
    }

    if (window_ == nullptr) {
      window_ = CreateDialogParamW(
          moduleInstance(), MAKEINTRESOURCEW(IDD_TONETRACE_EDITOR),
          actualParent, &Impl::dialogProc, reinterpret_cast<LPARAM>(this));
      if (window_ == nullptr) return false;
      // The template's DIALOGEX coordinates are dialog units, not pixels.
      // Force the client area to the exact design size.
      const int initialWidth = requestedWidth_ != 0
                                   ? static_cast<int>(requestedWidth_)
                                   : px(kWidth);
      const int initialHeight = requestedHeight_ != 0
                                    ? static_cast<int>(requestedHeight_)
                                    : px(kHeight);
      SetWindowPos(window_, nullptr, 0, 0, initialWidth, initialHeight,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
      const LONG_PTR exStyle =
          GetWindowLongPtrW(window_, GWL_EXSTYLE) | WS_EX_CONTROLPARENT;
      SetWindowLongPtrW(window_, GWL_EXSTYLE, exStyle);
      SetParent(window_, actualParent);
    }
    parent_ = actualParent;

    const POINT origin = editorOrigin();
    SetWindowPos(window_, nullptr, origin.x, origin.y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
  }

  bool setScale(double scale) {
    if (!std::isfinite(scale) || scale <= 0.0) return false;
    const double previous = scale_;
    scale_ = std::clamp(scale, 0.5, 4.0);
    if (window_ != nullptr && scale_ != previous) {
      if (!recreateFonts()) {
        scale_ = previous;
        return false;
      }
      layoutChildren();
      InvalidateRect(window_, nullptr, TRUE);
    }
    return true;
  }

  bool getSize(std::uint32_t& width, std::uint32_t& height) const {
    if (window_ != nullptr) {
      RECT bounds{};
      if (GetClientRect(window_, &bounds)) {
        width = static_cast<std::uint32_t>(std::max<LONG>(0, bounds.right));
        height = static_cast<std::uint32_t>(std::max<LONG>(0, bounds.bottom));
        return true;
      }
    }
    if (requestedWidth_ != 0 && requestedHeight_ != 0) {
      width = requestedWidth_;
      height = requestedHeight_;
      return true;
    }
    width = static_cast<std::uint32_t>(px(kWidth));
    height = static_cast<std::uint32_t>(px(kHeight));
    return true;
  }

  bool setSize(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0 ||
        width > static_cast<std::uint32_t>(INT_MAX) ||
        height > static_cast<std::uint32_t>(INT_MAX)) {
      return false;
    }
    requestedWidth_ = width;
    requestedHeight_ = height;
    // Hosts may negotiate the editor size before set_parent(). Remember that
    // choice and use it when the native child window is created.
    if (window_ == nullptr) return true;
    if (reaperWrapper_ != nullptr) {
      const POINT origin = editorOrigin();
      SetWindowPos(window_, nullptr, origin.x, origin.y,
                   static_cast<int>(width), static_cast<int>(height),
                   SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
      SetWindowPos(window_, nullptr, 0, 0,
                   static_cast<int>(width), static_cast<int>(height),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
  }

  bool adjustSize(std::uint32_t& width, std::uint32_t& height) const {
    width = std::max<std::uint32_t>(width,
                                    static_cast<std::uint32_t>(px(740)));
    height = std::max<std::uint32_t>(height,
                                     static_cast<std::uint32_t>(px(500)));
    return true;
  }

  bool show() {
    if (window_ == nullptr) return false;
    ShowWindow(window_, SW_SHOW);
    startTimer();
    if (reaperWrapper_ != nullptr) {
      // REAPER shows its wrapper after gui.show() returns. Defer hiding it so
      // the wrapper does not sit above this editor and intercept input. Focus
      // is forwarded only here so F6 can land on the first control; we never
      // steal focus when a host merely opens the editor.
      PostMessageW(window_, kReaperWrapperReadyMessage, 0, 0);
    }
    return true;
  }

  bool hide() {
    if (window_ == nullptr) return false;
    ShowWindow(window_, SW_HIDE);
    KillTimer(window_, kTimerId);
    cancelTraceAnnounce();
    return true;
  }

  void setOfflineRendering(bool offline) {
    offline_ = offline;
    if (offline) {
      KillTimer(window_, kTimerId);
    } else if (window_ != nullptr && IsWindowVisible(window_)) {
      startTimer();
    }
  }

  void startTimer() {
    if (window_ != nullptr && !offline_) {
      SetTimer(window_, kTimerId, kTimerIntervalMs, nullptr);
    }
  }

  static void unregisterWindowClasses() {
    tonetrace::win32::unregisterAccessibleFaderClass(moduleInstance());
  }

  static Impl* fromWindow(HWND window) {
    return reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  }

  static HINSTANCE moduleInstance() {
    HINSTANCE instance = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&Impl::dialogProc), &instance);
    return instance;
  }

  INT_PTR dialogMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

 private:
  static LRESULT CALLBACK keyForwardProc(HWND hwnd, UINT message, WPARAM wParam,
                                         LPARAM lParam);
  static LRESULT CALLBACK traceButtonProc(HWND hwnd, UINT message,
                                          WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK tabControlProc(HWND hwnd, UINT message,
                                         WPARAM wParam, LPARAM lParam);
  static INT_PTR CALLBACK dialogProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  POINT editorOrigin() const;
  void focusFirstControl();
  void setTabOrder();
  void setSubclass(HWND control, WNDPROC proc);
  bool recreateFonts();
  [[nodiscard]] bool forwardTraceArrows(HWND control) const;
  static constexpr int kWidth = 980;
  static constexpr int kHeight = 640;

  int px(int value) const {
    return static_cast<int>(std::lround(value * scale_));
  }

  int clientWidth() const {
    RECT rect{};
    GetClientRect(window_, &rect);
    return rect.right;
  }

  int clientHeight() const {
    RECT rect{};
    GetClientRect(window_, &rect);
    return rect.bottom;
  }

  RECT tabStripRect() const {
    const int margin = px(10);
    RECT rect{margin, px(66), clientWidth() - margin, px(94)};
    return rect;
  }

  RECT canvasRect() const {
    const int margin = px(10);
    const int strip = px(190);
    // The plot starts below the tab strip and the readout row so curves never
    // run underneath them.
    RECT rect{margin, tabStripRect().bottom + margin + px(24) + margin,
              descriptionRect().left - px(8),
              clientHeight() - margin - strip};
    return rect;
  }

  RECT bandCanvasRect() const {
    const int margin = px(10);
    // Band-page help is the same concise accessible readout as before, but the
    // native edit is allowed to wrap visually instead of clipping its tail.
    // Match-only controls are hidden on these pages, so do not reserve their
    // 168 px lower strip and needlessly shorten every fader.
    RECT rect{margin, tabStripRect().bottom + margin + px(42) + margin,
              clientWidth() - margin,
              clientHeight() - margin};
    return rect;
  }

  RECT graphPlotRect(const RECT& canvas) const {
    RECT plot{canvas.left, canvas.top + px(18), canvas.right,
              canvas.bottom - px(30)};
    if (plot.bottom <= plot.top) plot.bottom = plot.top + 1;
    return plot;
  }

  RECT descriptionRect() const {
    const int margin = px(10);
    const int strip = px(190);
    const int width = clientWidth();
    const int panelWidth =
        std::max(px(300), (width - margin * 2) * 2 / 5);
    RECT rect{width - margin - panelWidth,
              tabStripRect().bottom + margin + px(24) + margin,
              width - margin,
              clientHeight() - margin - strip};
    return rect;
  }

  bool createChildren();
  void buildModeCombo();
  void buildResolutionCombo();
  void layoutChildren();
  void createTabControl();
  void rebuildTraceTabs();
  void destroyTracePages();
  void insertTraceTabs();
  void layoutBandSliders();
  void showTracePage(int page);
  void stackCurrentPage();
  void onTabChanged();
  void onTabFocus();
  void setBandValueDb(int band, double valueDb);
  void playBandTone(int band) const;
  void onBandFocus(int band);
  void announceBandValue(int band);
  struct TracePage;
  void playPageSweep(const TracePage& page) const;
  void showPageDescription(const TracePage& page, bool announce);
  void neutralizeBand(int band);
  void adjustBandStep(int band, int step);
  void bandToExtreme(int band, bool maximum);
  [[nodiscard]] double bandRangeDb(bool maximum) const;
  static double faderGetValue(void* context, int band);
  static double faderGetMinimum(void* context, int band);
  static double faderGetMaximum(void* context, int band);
  static void faderSetValue(void* context, int band, double value);
  static std::wstring faderGetName(void* context, int band);
  static void faderOnFocus(void* context, int band);
  static void faderToggleTrace(void* context);
  static double faderGetScale(void* context);
  void correctionAt(double frequencyHz, double& matchDb, double& trimDb,
                    double& finalDb) const;
  [[nodiscard]] double finalCorrectionDb(double frequencyHz) const;
  void refreshBandSliders();
  [[nodiscard]] std::wstring bandFrequencyText(int band) const;
  [[nodiscard]] std::wstring bandValueText(int band) const;
  [[nodiscard]] double matchDbAtBand(int band) const;
  [[nodiscard]] double bandValueDb(int band) const;
  void paintCanvas(HDC dc, const RECT& bounds);
  void paintCurve(HDC dc, const RECT& bounds,
                  const std::vector<POINT>& points, COLORREF color, int width);
  void drawButton(const DRAWITEMSTRUCT& item);
  void refresh();
  void refreshValues();
  void refreshStatus();
  void refreshDescription();
  void updateReadout();
  void moveCursor(int step);
  void moveTrace(int step);
  void setTraceBand(int index, bool withBeep);
  void toggleTrace();
  void armTraceAnnounce();
  void cancelTraceAnnounce();
  [[nodiscard]] int traceBandCount() const;
  bool traceRange(double& lowHz, double& highHz) const;
  [[nodiscard]] bool frequencyInCorrectionRange(double frequencyHz) const;
  [[nodiscard]] double traceBandFrequency(int index) const;
  [[nodiscard]] double traceFrequency() const;
  void playTraceTone() const;
  void onCommand(int id, int notificationCode, HWND source);
  void applyEdit(int editIndex);
  void copyDescriptionToClipboard();
  void showOptionsDialog();
  static INT_PTR CALLBACK optionsDialogProc(HWND hwnd, UINT message,
                                             WPARAM wParam, LPARAM lParam);
  bool applyOptionsDialog(HWND dialog);
  void showTransferMenu(bool exportMenu);
  void exportCurve(int menuId);
  void importCurve(int menuId);
  [[nodiscard]] tonetrace::IrRenderSettings currentManualIrSettings() const;
  [[nodiscard]] bool chooseSavePath(const wchar_t* filter,
                                    const wchar_t* defaultExt,
                                    std::wstring& path) const;
  [[nodiscard]] bool chooseOpenPath(const wchar_t* filter,
                                    std::wstring& path) const;
  void announceMessage(const std::wstring& message);

  double paramValue(tonetrace::ParameterId id) const {
    if (params_ == nullptr || params_->get_value == nullptr) return 0.0;
    double value = 0.0;
    if (!params_->get_value(plugin_, static_cast<clap_id>(id), &value)) {
      return 0.0;
    }
    return value;
  }

  std::string paramText(tonetrace::ParameterId id) const {
    if (params_ == nullptr || params_->value_to_text == nullptr) return {};
    char buffer[CLAP_NAME_SIZE]{};
    if (!params_->value_to_text(plugin_, static_cast<clap_id>(id),
                                paramValue(id), buffer, sizeof(buffer))) {
      return {};
    }
    return std::string(buffer);
  }

  const clap_plugin_t* plugin_;
  const clap_plugin_params_t* params_;
  void* context_;
  GetSnapshotFn getSnapshot_;
  GetStagedSpectrumFn getStagedSpectrum_;
  SetParamFn setParam_;
  ResetFn reset_;
  PlayToneFn playTone_;
  PlayBandSweepFn playBandSweep_;
  SetBandGainFn setBandGain_;
  GetBandGainFn getBandGain_;
  GetBandCountFn getBandCount_;
  SetImportedSpectrumFn setImportedSpectrum_;
  SetImportedModelFn setImportedModel_;
  GetSampleRateFn getSampleRate_;
  RefreshFn refresh_ = nullptr;

  HWND window_ = nullptr;
  HWND parent_ = nullptr;
  HWND reaperWrapper_ = nullptr;
  HWND tabControl_ = nullptr;
  HWND statusLabel_ = nullptr;
  HWND statusEdit_ = nullptr;
  HWND readoutEdit_ = nullptr;
  HWND descriptionLabel_ = nullptr;
  HWND descriptionEdit_ = nullptr;
  HWND modeLabel_ = nullptr;
  HWND modeCombo_ = nullptr;
  HWND resolutionLabel_ = nullptr;
  HWND resolutionCombo_ = nullptr;
  HWND traceButton_ = nullptr;
  HWND optionsButton_ = nullptr;
  HWND tooltip_ = nullptr;
  struct TracePage {
    int firstBand = 0;
    int bandCount = 0;
    double lowHz = 0.0;
    double highHz = 0.0;
    std::vector<HWND> edits;
    std::vector<HWND> labels;
  };
  std::vector<TracePage> tracePages_;
  int selectedPage_ = 0;
  int builtResolution_ = 0;
  std::vector<HWND> workflowButtons_;
  std::vector<HWND> editControls_;
  std::vector<HWND> editLabels_;
  std::wstring lastDescriptionText_;
  std::wstring lastStatusMeaningfulKey_;
  std::uint64_t lastStatusTelemetryTicks_ = 0;
  UINT_PTR traceAnnounceTimer_ = 0;
  double scale_ = 1.0;
  std::uint32_t requestedWidth_ = 0;
  std::uint32_t requestedHeight_ = 0;
  bool offline_ = false;
  bool traceMode_ = false;
  bool cursorVisible_ = false;
  int cursorIndex_ = -1;
  int traceIndex_ = 0;
  int lastWorkflowStep_ = 0;
  int lastSweepPage_ = -1;
  std::uint64_t lastSweepTicks_ = 0;
  HFONT font_ = nullptr;
  HFONT titleFont_ = nullptr;
  HFONT smallFont_ = nullptr;
  HBRUSH bgBrush_ = nullptr;
  HBRUSH panelBrush_ = nullptr;
  HBRUSH editBrush_ = nullptr;
  HBRUSH accentBrush_ = nullptr;
};

INT_PTR CALLBACK ToneTraceWin32Editor::Impl::dialogProc(HWND hwnd, UINT message,
                                                        WPARAM wParam,
                                                        LPARAM lParam) {
  if (message == WM_INITDIALOG) {
    auto* impl = reinterpret_cast<Impl*>(lParam);
    if (impl == nullptr) return FALSE;
    impl->window_ = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
    return impl->createChildren() ? TRUE : FALSE;
  }
  Impl* impl = fromWindow(hwnd);
  if (impl != nullptr) return impl->dialogMessage(hwnd, message, wParam, lParam);
  return FALSE;
}

INT_PTR ToneTraceWin32Editor::Impl::dialogMessage(HWND hwnd, UINT message,
                                                  WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_SETFOCUS:
      focusFirstControl();
      return TRUE;
    case kReaperWrapperReadyMessage: {
      const HWND focusedBefore = GetFocus();
      if (reaperWrapper_ != nullptr && IsWindow(reaperWrapper_)) {
        ShowWindow(reaperWrapper_, SW_HIDE);
      }
      // Forward focus only when the host directed focus at the plugin (F6 /
      // OSARA lands on the wrapper or this editor). Never yank focus away from
      // a REAPER control the user is already interacting with.
      if (focusedBefore == window_ || focusedBefore == reaperWrapper_ ||
          focusedBefore == nullptr) {
        focusFirstControl();
      }
      return TRUE;
    }
    case WM_SIZE: {
      const int width = clientWidth();
      const int height = clientHeight();
      if (width > 0 && height > 0) {
        layoutChildren();
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return TRUE;
    }
    case WM_ERASEBKGND:
      // The background is fully repainted in WM_PAINT; suppress flicker.
      return TRUE;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(hwnd, &paint);
      RECT rect{};
      GetClientRect(hwnd, &rect);
      FillRect(dc, &rect, bgBrush_);
      SetBkMode(dc, TRANSPARENT);
      SelectObject(dc, titleFont_);
      SetTextColor(dc, RGB(255, 240, 210));
      RECT accent{px(10), px(10), px(14), px(42)};
      FillRect(dc, &accent, accentBrush_);
      RECT title{px(22), px(7), descriptionRect().left - px(8), px(38)};
      DrawTextW(dc, L"Tone Trace EQ", -1, &title,
                 DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
      SelectObject(dc, smallFont_);
      SetTextColor(dc, RGB(205, 205, 215));
      RECT subtitle{px(22), px(36), descriptionRect().left - px(8), px(54)};
      DrawTextW(dc, L"CAPTURE  /  COMPARE  /  REFINE", -1, &subtitle,
                DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
      if (selectedPage_ == 0) {
        paintCanvas(dc, canvasRect());
      } else {
        // A trace tab shows only its band sliders; give the page a quiet
        // backing panel instead of a curve plot under the controls.
        RECT page = bandCanvasRect();
        HBRUSH background = CreateSolidBrush(RGB(18, 18, 22));
        FillRect(dc, &page, background);
        DeleteObject(background);
      }
      EndPaint(hwnd, &paint);
      return TRUE;
    }
    case WM_DRAWITEM: {
      const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
      if (item != nullptr && item->CtlType == ODT_BUTTON) {
        drawButton(*item);
        return TRUE;
      }
      break;
    }
    case WM_TIMER:
      // While offline rendering, suppress every rest of the GUI activity: the
      // 33 ms refresh/repaint timer above is already gated, and the deferred
      // trace-announce event below must also be dropped so a bounce never
      // triggers a repaint or an accessibility announcement.
      if (wParam == kTimerId && !offline_) {
        if (refresh_ != nullptr) refresh_(context_);
        refresh();
      } else if (wParam == kTraceAnnounceTimerId) {
        traceAnnounceTimer_ = 0;
        KillTimer(window_, kTraceAnnounceTimerId);
        if (!offline_ && readoutEdit_ != nullptr &&
            (traceMode_ || selectedPage_ > 0)) {
          NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, readoutEdit_, OBJID_CLIENT,
                         CHILDID_SELF);
        }
      }
      return TRUE;
    case WM_NOTIFY: {
      const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
      if (header != nullptr && header->idFrom == kTabControlId &&
          header->code == TCN_SELCHANGE) {
        onTabChanged();
        return TRUE;
      }
      break;
    }
    case WM_COMMAND:
      onCommand(static_cast<int>(LOWORD(wParam)),
                static_cast<int>(HIWORD(wParam)),
                reinterpret_cast<HWND>(lParam));
      return TRUE;
    case WM_KEYDOWN: {
      const int virtualKey = static_cast<int>(wParam);
      if (virtualKey == 'T') {
        toggleTrace();
        return TRUE;
      }
      if (traceMode_) {
        if (virtualKey == VK_LEFT) {
          moveTrace(-1);
          return TRUE;
        }
        if (virtualKey == VK_RIGHT) {
          moveTrace(1);
          return TRUE;
        }
        if (virtualKey == VK_UP) {
          adjustBandStep(traceIndex_, 1);
          return TRUE;
        }
        if (virtualKey == VK_DOWN) {
          adjustBandStep(traceIndex_, -1);
          return TRUE;
        }
        if (virtualKey == VK_PRIOR) {
          adjustBandStep(traceIndex_, 6);
          return TRUE;
        }
        if (virtualKey == VK_NEXT) {
          adjustBandStep(traceIndex_, -6);
          return TRUE;
        }
        if (virtualKey == VK_HOME) {
          bandToExtreme(traceIndex_, true);
          return TRUE;
        }
        if (virtualKey == VK_END) {
          bandToExtreme(traceIndex_, false);
          return TRUE;
        }
        if (virtualKey == 'N') {
          neutralizeBand(traceIndex_);
          return TRUE;
        }
      } else {
        if (virtualKey == VK_LEFT) {
          moveCursor(-1);
          return TRUE;
        }
        if (virtualKey == VK_RIGHT) {
          moveCursor(1);
          return TRUE;
        }
      }
      break;
    }
    case WM_MOUSEMOVE: {
      const RECT canvas = selectedPage_ == 0 ? canvasRect() : bandCanvasRect();
      const RECT bounds = selectedPage_ == 0 ? graphPlotRect(canvas) : canvas;
      const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      const int plotWidth = static_cast<int>(bounds.right - bounds.left);
      if (plotWidth > 0 && PtInRect(&bounds, point)) {
        const double fraction =
            static_cast<double>(point.x - bounds.left) /
            static_cast<double>(plotWidth);
        if (traceMode_) {
          double low = kLogLowHz;
          double high = kLogHighHz;
          traceRange(low, high);
          const int count = traceBandCount();
          const double frequency =
              kLogLowHz * std::pow(kLogHighHz / kLogLowHz, fraction);
          const double normalized =
              std::log(std::clamp(frequency, low, high) / low) /
              std::log(high / low);
          const int index = std::clamp(
              static_cast<int>(std::lround(normalized * (count - 1))),
              0, count - 1);
          if (index != traceIndex_) {
            traceIndex_ = index;
            updateReadout();
            InvalidateRect(window_, nullptr, FALSE);
          }
          return TRUE;
        }
        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = hwnd;
        TrackMouseEvent(&tracking);
        cursorVisible_ = true;
        const double frequency =
            kLogLowHz * std::pow(kLogHighHz / kLogLowHz, fraction);
        const int index = std::clamp(
            static_cast<int>(freqToX(frequency, plotWidth) + 0.5),
            0, plotWidth - 1);
        if (index != cursorIndex_) {
          cursorIndex_ = index;
          updateReadout();
          InvalidateRect(hwnd, &canvas, FALSE);
        }
      } else if (cursorVisible_) {
        cursorVisible_ = false;
        InvalidateRect(hwnd, &canvas, FALSE);
      }
      return TRUE;
    }
    case WM_MOUSELEAVE:
      if (cursorVisible_) {
        cursorVisible_ = false;
        const RECT canvas = canvasRect();
        InvalidateRect(hwnd, &canvas, FALSE);
      }
      return TRUE;
    case WM_SETCURSOR:
      if (LOWORD(lParam) == HTCLIENT && selectedPage_ == 0) {
        POINT point{};
        if (GetCursorPos(&point) && ScreenToClient(hwnd, &point)) {
          const RECT plot = graphPlotRect(canvasRect());
          if (PtInRect(&plot, point)) {
            SetCursor(LoadCursor(nullptr, IDC_CROSS));
            return TRUE;
          }
        }
      }
      break;
    case WM_CTLCOLOREDIT: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      SetBkMode(dc, OPAQUE);
      SetBkColor(dc, RGB(245, 245, 245));
      SetTextColor(dc, RGB(20, 20, 24));
      return reinterpret_cast<INT_PTR>(editBrush_);
    }
    case WM_CTLCOLORSTATIC: {
      HDC dc = reinterpret_cast<HDC>(wParam);
      const HWND control = reinterpret_cast<HWND>(lParam);
      const int controlId = control != nullptr ? GetDlgCtrlID(control) : 0;
      const bool editLabel =
          std::find(editLabels_.begin(), editLabels_.end(), control) !=
          editLabels_.end();
      const bool bandValue =
          controlId >= kBandSliderFirstId && controlId < kBandSliderFirstId + 120;
      // Parameter labels are ordinary STATIC controls with no useful control
      // ID. Handle them by HWND before any ID-based classification so Windows
      // can never paint one with the white edit-field brush.
      if (editLabel) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(215, 215, 224));
        return reinterpret_cast<INT_PTR>(bgBrush_);
      }
      if (control == readoutEdit_ || bandValue) {
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(245, 245, 245));
        SetTextColor(dc, RGB(20, 20, 24));
        return reinterpret_cast<INT_PTR>(editBrush_);
      }
      if (control == statusEdit_ || control == descriptionEdit_) {
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(30, 30, 36));
        SetTextColor(dc, RGB(225, 225, 232));
        return reinterpret_cast<INT_PTR>(panelBrush_);
      }
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(215, 215, 224));
      return reinterpret_cast<INT_PTR>(bgBrush_);
    }
    case WM_DESTROY:
      KillTimer(hwnd, kTimerId);
      KillTimer(hwnd, kTraceAnnounceTimerId);
      if (font_) DeleteObject(font_);
      if (titleFont_) DeleteObject(titleFont_);
      if (smallFont_) DeleteObject(smallFont_);
      if (bgBrush_) DeleteObject(bgBrush_);
      if (panelBrush_) DeleteObject(panelBrush_);
      if (editBrush_) DeleteObject(editBrush_);
      if (accentBrush_) DeleteObject(accentBrush_);
      font_ = titleFont_ = smallFont_ = nullptr;
      bgBrush_ = panelBrush_ = editBrush_ = accentBrush_ = nullptr;
      return TRUE;
    default:
      break;
  }
  return FALSE;
}

POINT ToneTraceWin32Editor::Impl::editorOrigin() const {
  POINT origin{0, 0};
  if (reaperWrapper_ == nullptr || !IsWindow(reaperWrapper_)) return origin;
  RECT bounds{};
  if (!GetWindowRect(reaperWrapper_, &bounds)) return origin;
  origin = {bounds.left, bounds.top};
  const HWND parent = GetParent(window_);
  if (parent != nullptr) ScreenToClient(parent, &origin);
  return origin;
}

void ToneTraceWin32Editor::Impl::focusFirstControl() {
  if (window_ == nullptr) return;
  HWND first = GetNextDlgTabItem(window_, nullptr, FALSE);
  if (first != nullptr && IsWindowEnabled(first)) SetFocus(first);
}

void ToneTraceWin32Editor::Impl::setSubclass(HWND control, WNDPROC proc) {
  if (control == nullptr) return;
  SetWindowLongPtrW(control, GWLP_USERDATA,
                    GetWindowLongPtrW(control, GWLP_WNDPROC));
  SetWindowLongPtrW(control, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(proc));
}

bool ToneTraceWin32Editor::Impl::forwardTraceArrows(HWND control) const {
  if (control == nullptr || control == descriptionEdit_ ||
      control == modeCombo_ || control == resolutionCombo_) {
    return false;
  }
  for (HWND edit : editControls_) {
    if (edit == control) return false;
  }
  return true;
}

// Shared keyboard route for every control except the Trace Curve checkbox.
// REAPER grabs Space for transport, so T (or F2) toggles trace mode from any
// focused control, and once tracing, the arrow keys move bands without the
// user having to tab across the dialog to the checkbox.
LRESULT CALLBACK ToneTraceWin32Editor::Impl::keyForwardProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const WNDPROC original =
      reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_MOUSEMOVE) {
    TRACKMOUSEEVENT tracking{};
    tracking.cbSize = sizeof(tracking);
    tracking.dwFlags = TME_LEAVE;
    tracking.hwndTrack = hwnd;
    TrackMouseEvent(&tracking);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else if (message == WM_MOUSELEAVE) {
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  if (message == WM_GETDLGCODE) {
    const LRESULT code =
        CallWindowProcW(original, hwnd, message, wParam, lParam);
    Impl* impl = fromWindow(GetParent(hwnd));
    // A multiline edit claims DLGC_WANTALLKEYS, which swallows Tab and would
    // trap keyboard users inside the box. Drop that claim so Tab flows on
    // through the dialog manager; arrows keep scrolling the text.
    if (impl != nullptr &&
        (hwnd == impl->descriptionEdit_ || hwnd == impl->readoutEdit_ ||
         hwnd == impl->statusEdit_)) {
      return (code & ~static_cast<LRESULT>(DLGC_WANTALLKEYS)) |
             DLGC_WANTARROWS;
    }
    return code;
  }
  if (message == WM_KEYDOWN) {
    Impl* impl = fromWindow(GetParent(hwnd));
    if (impl != nullptr) {
      const int key = static_cast<int>(wParam);
      if (key == VK_RETURN) {
        const auto found = std::find(impl->editControls_.begin(),
                                     impl->editControls_.end(), hwnd);
        if (found != impl->editControls_.end()) {
          impl->applyEdit(static_cast<int>(
              std::distance(impl->editControls_.begin(), found)));
          return 0;
        }
      }
      if (key == 'T' || key == VK_F2) {
        impl->toggleTrace();
        return 0;
      }
      if (impl->traceMode_ && impl->forwardTraceArrows(hwnd)) {
        switch (key) {
          case VK_LEFT:
            impl->moveTrace(-1);
            return 0;
          case VK_RIGHT:
            impl->moveTrace(1);
            return 0;
          case VK_UP:
            impl->adjustBandStep(impl->traceIndex_, 1);
            return 0;
          case VK_DOWN:
            impl->adjustBandStep(impl->traceIndex_, -1);
            return 0;
          case VK_PRIOR:
            impl->adjustBandStep(impl->traceIndex_, 6);
            return 0;
          case VK_NEXT:
            impl->adjustBandStep(impl->traceIndex_, -6);
            return 0;
          case VK_HOME:
            impl->bandToExtreme(impl->traceIndex_, true);
            return 0;
          case VK_END:
            impl->bandToExtreme(impl->traceIndex_, false);
            return 0;
          case '0':
          case VK_NUMPAD0:
          case 'N':
            impl->neutralizeBand(impl->traceIndex_);
            return 0;
          default:
            break;
        }
      }
    }
  }
  return CallWindowProcW(original, hwnd, message, wParam, lParam);
}

void ToneTraceWin32Editor::Impl::drawButton(const DRAWITEMSTRUCT& item) {
  if (item.hDC == nullptr || item.hwndItem == nullptr) return;

  const int id = static_cast<int>(item.CtlID);
  const bool workflow = id >= kCaptureReferenceId && id <= kFreezeId;
  const bool utility = id == kDescribeId || id == kExportId ||
                       id == kImportId || id == kOptionsId;
  if (!workflow && !utility) return;

  const bool disabled = (item.itemState & ODS_DISABLED) != 0;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0;
  const bool focused = (item.itemState & ODS_FOCUS) != 0;
  const bool hovered = cursorInsideWindow(item.hwndItem);
  const bool current = workflow && lastWorkflowStep_ == id - kFirstButtonId + 1;

  COLORREF fill = utility ? RGB(39, 43, 51) : RGB(43, 50, 61);
  COLORREF border = utility ? RGB(91, 98, 112) : RGB(104, 124, 146);
  COLORREF text = RGB(235, 238, 243);
  if (current) {
    fill = RGB(222, 159, 55);
    border = RGB(255, 220, 145);
    text = RGB(24, 24, 28);
  } else if (pressed) {
    fill = workflow ? RGB(58, 82, 102) : RGB(51, 57, 68);
    border = RGB(255, 200, 90);
  } else if (hovered) {
    fill = workflow ? RGB(55, 69, 84) : RGB(49, 55, 66);
    border = workflow ? RGB(112, 196, 221) : RGB(151, 160, 177);
  }
  if (disabled) {
    fill = RGB(35, 36, 41);
    border = RGB(65, 67, 74);
    text = RGB(126, 128, 136);
  }

  const int saved = SaveDC(item.hDC);
  FillRect(item.hDC, &item.rcItem, bgBrush_);
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN pen = CreatePen(PS_SOLID, std::max(1, px(current ? 2 : 1)), border);
  SelectObject(item.hDC, brush);
  SelectObject(item.hDC, pen);
  const RECT& bounds = item.rcItem;
  RoundRect(item.hDC, bounds.left, bounds.top, bounds.right, bounds.bottom,
            px(7), px(7));

  RECT content = bounds;
  content.left += px(10);
  content.right -= px(10);
  if (workflow) {
    const int sequence = id - kFirstButtonId + 1;
    const int centerX = bounds.left + px(17);
    const int centerY = (bounds.top + bounds.bottom) / 2;
    HBRUSH badgeBrush = CreateSolidBrush(
        current ? RGB(40, 40, 45) : RGB(27, 31, 38));
    HPEN badgePen = CreatePen(PS_SOLID, 1,
                              current ? RGB(40, 40, 45) : border);
    const int badgeSaved = SaveDC(item.hDC);
    SelectObject(item.hDC, badgeBrush);
    SelectObject(item.hDC, badgePen);
    Ellipse(item.hDC, centerX - px(9), centerY - px(9),
            centerX + px(9), centerY + px(9));
    RestoreDC(item.hDC, badgeSaved);
    DeleteObject(badgeBrush);
    DeleteObject(badgePen);
    wchar_t number[4]{};
    std::swprintf(number, std::size(number), L"%d", sequence);
    RECT badgeText{centerX - px(9), centerY - px(9), centerX + px(9),
                   centerY + px(9)};
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, current ? RGB(255, 220, 145) : RGB(221, 228, 237));
    SelectObject(item.hDC, smallFont_);
    DrawTextW(item.hDC, number, -1, &badgeText,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    content.left += px(20);
  }

  wchar_t label[96]{};
  GetWindowTextW(item.hwndItem, label, static_cast<int>(std::size(label)));
  SetBkMode(item.hDC, TRANSPARENT);
  SetTextColor(item.hDC, text);
  SelectObject(item.hDC, font_);
  DrawTextW(item.hDC, label, -1, &content,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                DT_NOPREFIX);

  if (focused) {
    RECT focus = bounds;
    InflateRect(&focus, -px(4), -px(4));
    DrawFocusRect(item.hDC, &focus);
  }
  RestoreDC(item.hDC, saved);
  DeleteObject(brush);
  DeleteObject(pen);
}

LRESULT CALLBACK ToneTraceWin32Editor::Impl::traceButtonProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const WNDPROC original =
      reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_GETDLGCODE) {
    // Claim the arrow keys so they navigate trace bands instead of moving
    // focus; Tab still flows through the dialog manager.
    const LRESULT code =
        CallWindowProcW(original, hwnd, message, wParam, lParam);
    return code | DLGC_WANTARROWS;
  }
  if (message == WM_KEYDOWN) {
    Impl* impl = fromWindow(GetParent(hwnd));
    if (impl != nullptr) {
      const int key = static_cast<int>(wParam);
      // Keyboard activation: REAPER grabs Space for transport, so T (or F2)
      // toggles trace mode instead.
      if (key == 'T' || key == VK_F2) {
        impl->toggleTrace();
        return 0;
      }
      if (key == VK_LEFT) {
        impl->moveTrace(-1);
        return 0;
      }
      if (key == VK_RIGHT) {
        impl->moveTrace(1);
        return 0;
      }
      if (key == VK_UP) {
        impl->adjustBandStep(impl->traceIndex_, 1);
        return 0;
      }
      if (key == VK_DOWN) {
        impl->adjustBandStep(impl->traceIndex_, -1);
        return 0;
      }
      if (key == VK_PRIOR) {
        impl->adjustBandStep(impl->traceIndex_, 6);
        return 0;
      }
      if (key == VK_NEXT) {
        impl->adjustBandStep(impl->traceIndex_, -6);
        return 0;
      }
      if (key == VK_HOME) {
        impl->bandToExtreme(impl->traceIndex_, true);
        return 0;
      }
      if (key == VK_END) {
        impl->bandToExtreme(impl->traceIndex_, false);
        return 0;
      }
      if (key == '0' || key == VK_NUMPAD0 || key == 'N') {
        impl->neutralizeBand(impl->traceIndex_);
        return 0;
      }
    }
  }
  return CallWindowProcW(original, hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ToneTraceWin32Editor::Impl::tabControlProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const WNDPROC original =
      reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_SETFOCUS) {
    Impl* impl = fromWindow(GetParent(hwnd));
    if (impl != nullptr) impl->onTabFocus();
  }
  return CallWindowProcW(original, hwnd, message, wParam, lParam);
}

void ToneTraceWin32Editor::Impl::setTabOrder() {
  const auto moveTop = [](HWND control) {
    if (control != nullptr) {
      SetWindowPos(control, HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  };
  const auto buttonById = [&](int id) -> HWND {
    for (HWND button : workflowButtons_) {
      if (button != nullptr && GetDlgCtrlID(button) == id) return button;
    }
    return nullptr;
  };

  // Desired Match-page tab order (first to last):
  // Capture Reference, Learn Target, Correct Target, Freeze Correction,
  // Match Mode, Trace Curve, Options, labeled exact-value edits, Status,
  // Readout, Curve Description, Copy Description, Export, Import.
  // Static labels are deliberately placed immediately before the native
  // control they name in Z-order so MSAA/UIA and sighted users receive the
  // same label.
  moveTop(buttonById(kImportId));
  moveTop(buttonById(kExportId));
  moveTop(buttonById(kDescribeId));
  moveTop(descriptionEdit_);
  moveTop(descriptionLabel_);
  moveTop(readoutEdit_);
  moveTop(statusEdit_);
  moveTop(statusLabel_);
  for (int index = static_cast<int>(editControls_.size()) - 1; index >= 0;
       --index) {
    moveTop(editControls_[index]);
    if (index < static_cast<int>(editLabels_.size())) {
      moveTop(editLabels_[index]);
    }
  }
  moveTop(optionsButton_);
  moveTop(traceButton_);
  moveTop(modeCombo_);
  moveTop(modeLabel_);
  moveTop(buttonById(kFreezeId));
  moveTop(buttonById(kCorrectTargetId));
  moveTop(buttonById(kLearnTargetId));
  moveTop(buttonById(kCaptureReferenceId));
  moveTop(tabControl_);
}

bool ToneTraceWin32Editor::Impl::recreateFonts() {
  HFONT regular = CreateFontW(
      -px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  HFONT title = CreateFontW(
      -px(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  HFONT small = CreateFontW(
      -px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  if (regular == nullptr || title == nullptr || small == nullptr) {
    if (regular != nullptr) DeleteObject(regular);
    if (title != nullptr) DeleteObject(title);
    if (small != nullptr) DeleteObject(small);
    return false;
  }

  const HFONT oldRegular = font_;
  const HFONT oldTitle = titleFont_;
  const HFONT oldSmall = smallFont_;
  font_ = regular;
  titleFont_ = title;
  smallFont_ = small;

  const auto apply = [](HWND control, HFONT font) {
    if (control != nullptr) {
      SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
  };
  apply(tabControl_, font_);
  apply(statusLabel_, smallFont_);
  apply(statusEdit_, font_);
  apply(readoutEdit_, font_);
  apply(descriptionLabel_, smallFont_);
  apply(descriptionEdit_, font_);
  apply(modeLabel_, smallFont_);
  apply(modeCombo_, font_);
  apply(resolutionLabel_, smallFont_);
  apply(resolutionCombo_, font_);
  apply(traceButton_, font_);
  apply(optionsButton_, font_);
  for (HWND control : workflowButtons_) apply(control, font_);
  for (HWND control : editControls_) apply(control, font_);
  for (HWND label : editLabels_) apply(label, smallFont_);
  for (const TracePage& page : tracePages_) {
    for (HWND control : page.edits) apply(control, font_);
    for (HWND label : page.labels) apply(label, smallFont_);
  }

  if (oldRegular != nullptr) DeleteObject(oldRegular);
  if (oldTitle != nullptr) DeleteObject(oldTitle);
  if (oldSmall != nullptr) DeleteObject(oldSmall);
  return true;
}

bool ToneTraceWin32Editor::Impl::createChildren() {
  const HINSTANCE instance = moduleInstance();

  INITCOMMONCONTROLSEX commonControls{};
  commonControls.dwSize = sizeof(commonControls);
  commonControls.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
  InitCommonControlsEx(&commonControls);

  bgBrush_ = CreateSolidBrush(RGB(22, 22, 26));
  panelBrush_ = CreateSolidBrush(RGB(30, 30, 36));
  editBrush_ = CreateSolidBrush(RGB(245, 245, 245));
  accentBrush_ = CreateSolidBrush(RGB(255, 200, 90));
  if (!recreateFonts()) return false;

  constexpr std::pair<int, const wchar_t*> kWorkflow[]{
      {kCaptureReferenceId, L"Capture Reference"},
      {kLearnTargetId, L"Learn Target"},
      {kCorrectTargetId, L"Correct Target"},
      {kFreezeId, L"Freeze Correction"},
  };
  for (const auto& [id, label] : kWorkflow) {
    HWND button = CreateWindowExW(
        0, L"BUTTON", label,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 4, 4, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance, nullptr);
    if (button != nullptr) {
      SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
      setSubclass(button, &Impl::keyForwardProc);
      workflowButtons_.push_back(button);
    }
  }

  // Keep the visible button concise, but make its two actions discoverable to
  // pointer users. The host generic parameter already exposes the longer
  // "Save Reference and Learn Target" wording for screen-reader workflows.
  tooltip_ = CreateWindowExW(
      WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
      WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, instance, nullptr);
  if (tooltip_ != nullptr) {
    SetWindowPos(tooltip_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    const auto addTooltip = [&](int id, const wchar_t* text) {
      HWND control = GetDlgItem(window_, id);
      if (control == nullptr) return;
      TOOLINFOW tool{};
      tool.cbSize = TTTOOLINFOW_V2_SIZE;
      tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
      tool.hwnd = window_;
      tool.uId = reinterpret_cast<UINT_PTR>(control);
      tool.lpszText = const_cast<LPWSTR>(text);
      SendMessageW(tooltip_, TTM_ADDTOOLW, 0,
                   reinterpret_cast<LPARAM>(&tool));
    };
    addTooltip(kCaptureReferenceId,
               L"Begin listening to the sound you want to match.");
    addTooltip(kLearnTargetId,
               L"Save the Reference, then begin listening to the Target.");
    addTooltip(kCorrectTargetId,
               L"Build and preview the correction from both captures.");
    addTooltip(kFreezeId,
               L"Stop learning and keep the current correction.");
  }

  HWND copyButton = CreateWindowExW(
      0, L"BUTTON", L"Copy Curve Description",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDescribeId)), instance,
      nullptr);
  if (copyButton != nullptr) {
    SendMessageW(copyButton, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font_), FALSE);
    setSubclass(copyButton, &Impl::keyForwardProc);
    workflowButtons_.push_back(copyButton);
  }

  if (setImportedSpectrum_ != nullptr || setImportedModel_ != nullptr) {
    // Import/Export only exists when the wrapper provides the transfer hooks
    // (the CLAP wrapper does; a host integration without import wiring can
    // pass null and get no transfer buttons).
    constexpr std::pair<int, const wchar_t*> kTransfer[]{
        {kExportId, L"Export..."},
        {kImportId, L"Import..."},
    };
    for (const auto& [id, label] : kTransfer) {
      HWND button = CreateWindowExW(
          0, L"BUTTON", label,
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
          0, 0, 4, 4, window_,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
      if (button != nullptr) {
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
        setSubclass(button, &Impl::keyForwardProc);
        workflowButtons_.push_back(button);
      }
    }
  }
  addControlTooltip(tooltip_, window_, copyButton,
                    L"Copy the complete text description of the curves.");
  addControlTooltip(tooltip_, window_, GetDlgItem(window_, kExportId),
                    L"Export an impulse response, curve, or correction model.");
  addControlTooltip(tooltip_, window_, GetDlgItem(window_, kImportId),
                    L"Import a Reference, Target, or correction model.");

  modeLabel_ = CreateWindowExW(
      WS_EX_TRANSPARENT, L"STATIC", L"Match Mode",
      WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kModeLabelId)), instance,
      nullptr);
  if (modeLabel_ != nullptr) {
    SendMessageW(modeLabel_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(smallFont_), FALSE);
  }

  modeCombo_ = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kModeComboId)), instance,
      nullptr);
  if (modeCombo_ != nullptr) {
    SendMessageW(modeCombo_, WM_SETFONT, reinterpret_cast<WPARAM>(font_),
                 FALSE);
    setSubclass(modeCombo_, &Impl::keyForwardProc);
    buildModeCombo();
  }

  resolutionLabel_ = CreateWindowExW(
      WS_EX_TRANSPARENT, L"STATIC", L"Correction Resolution",
      WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResolutionLabelId)),
      instance, nullptr);
  if (resolutionLabel_ != nullptr) {
    SendMessageW(resolutionLabel_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(smallFont_), FALSE);
  }

  resolutionCombo_ = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kResolutionComboId)),
      instance, nullptr);
  if (resolutionCombo_ != nullptr) {
    SendMessageW(resolutionCombo_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font_), FALSE);
    setSubclass(resolutionCombo_, &Impl::keyForwardProc);
    buildResolutionCombo();
    addControlTooltip(tooltip_, window_, resolutionCombo_,
                      L"Choose how many editable bands appear on the Bands pages.");
  }

  traceButton_ = CreateWindowExW(
      0, L"BUTTON", L"Trace Curve",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_PUSHLIKE,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTraceId)), instance,
      nullptr);
  if (traceButton_ != nullptr) {
    SendMessageW(traceButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font_),
                 FALSE);
    setSubclass(traceButton_, &Impl::traceButtonProc);
    addControlTooltip(tooltip_, window_, traceButton_,
                      L"Audition the curve by moving through its band centers.");
  }

  optionsButton_ = CreateWindowExW(
      0, L"BUTTON", L"Options...",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOptionsId)), instance,
      nullptr);
  if (optionsButton_ != nullptr) {
    SendMessageW(optionsButton_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font_), FALSE);
    setSubclass(optionsButton_, &Impl::keyForwardProc);
    addControlTooltip(tooltip_, window_, optionsButton_,
                      L"Global correction, tone, bypass, and reset options.");
  }

  for (std::size_t index = 0; index < std::size(kEditedParams); ++index) {
    HWND edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 4, 4, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditFirstId +
                                                    static_cast<int>(index))),
        instance, nullptr);
    if (edit != nullptr) {
      SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
      setSubclass(edit, &Impl::keyForwardProc);
      editControls_.push_back(edit);
    }
  }

  const auto& descriptors = tonetrace::parameterDescriptors();
  for (std::size_t index = 0; index < editControls_.size(); ++index) {
    if (index >= std::size(kEditedParams)) break;
    const tonetrace::ParameterId id = kEditedParams[index];
    std::string name;
    for (const auto& descriptor : descriptors) {
      if (descriptor.id == id) {
        name = descriptor.name;
        break;
      }
    }
    std::wstring label = widen(name);
    HWND labelWindow = CreateWindowExW(
        WS_EX_TRANSPARENT, L"STATIC", label.c_str(),
        WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
        0, 0, 4, 4, window_, nullptr, nullptr, nullptr);
    if (labelWindow != nullptr) {
      SendMessageW(labelWindow, WM_SETFONT,
                   reinterpret_cast<WPARAM>(smallFont_), FALSE);
      editLabels_.push_back(labelWindow);
    }
  }

  statusLabel_ = CreateWindowExW(
      WS_EX_TRANSPARENT, L"STATIC", L"Status",
      WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusLabelId)), instance,
      nullptr);
  if (statusLabel_ != nullptr) {
    SendMessageW(statusLabel_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(smallFont_), FALSE);
  }

  statusEdit_ = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_MULTILINE,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStatusEditId)), instance,
      nullptr);
  if (statusEdit_ != nullptr) {
    SendMessageW(statusEdit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font_), FALSE);
    setSubclass(statusEdit_, &Impl::keyForwardProc);
  }

  readoutEdit_ = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_MULTILINE,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReadoutEditId)), instance,
      nullptr);
  if (readoutEdit_ != nullptr) {
    SendMessageW(readoutEdit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font_), FALSE);
    setSubclass(readoutEdit_, &Impl::keyForwardProc);
  }

  descriptionLabel_ = CreateWindowExW(
      WS_EX_TRANSPARENT, L"STATIC", L"Curve Description",
      WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_NOPREFIX,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDescriptionLabelId)),
      instance, nullptr);
  if (descriptionLabel_ != nullptr) {
    SendMessageW(descriptionLabel_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(smallFont_), FALSE);
  }

  descriptionEdit_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_MULTILINE |
          ES_AUTOVSCROLL | WS_VSCROLL,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDescriptionEditId)),
      instance, nullptr);
  if (descriptionEdit_ != nullptr) {
    SendMessageW(descriptionEdit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font_), FALSE);
    setSubclass(descriptionEdit_, &Impl::keyForwardProc);
  }

  createTabControl();

  setTabOrder();
  layoutChildren();
  rebuildTraceTabs();
  refresh();
  return true;
}

void ToneTraceWin32Editor::Impl::buildModeCombo() {
  if (modeCombo_ == nullptr) return;
  SendMessageW(modeCombo_, CB_RESETCONTENT, 0, 0);
  const auto& descriptors = tonetrace::parameterDescriptors();
  for (std::size_t index = 0; index < descriptors.size(); ++index) {
    if (descriptors[index].id != tonetrace::ParameterId::MatchMode) continue;
    if (params_ == nullptr || params_->value_to_text == nullptr) break;
    const double minimum = descriptors[index].minimum;
    const double maximum = descriptors[index].maximum;
    for (int value = static_cast<int>(minimum);
         value <= static_cast<int>(maximum); ++value) {
      char buffer[CLAP_NAME_SIZE]{};
      if (params_->value_to_text(
              plugin_, static_cast<clap_id>(descriptors[index].id),
              static_cast<double>(value), buffer, sizeof(buffer))) {
        const auto text = widen(buffer);
        SendMessageW(modeCombo_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text.c_str()));
      }
    }
    break;
  }
}

void ToneTraceWin32Editor::Impl::buildResolutionCombo() {
  if (resolutionCombo_ == nullptr) return;
  SendMessageW(resolutionCombo_, CB_RESETCONTENT, 0, 0);
  const auto& descriptors = tonetrace::parameterDescriptors();
  for (const auto& descriptor : descriptors) {
    if (descriptor.id != tonetrace::ParameterId::Resolution) continue;
    const int minimum = static_cast<int>(std::lround(descriptor.minimum));
    const int maximum = static_cast<int>(std::lround(descriptor.maximum));
    for (int value = minimum; value <= maximum; ++value) {
      std::wstring text;
      if (params_ != nullptr && params_->value_to_text != nullptr) {
        char buffer[CLAP_NAME_SIZE]{};
        if (params_->value_to_text(
                plugin_, static_cast<clap_id>(descriptor.id),
                static_cast<double>(value), buffer, sizeof(buffer))) {
          text = widen(buffer);
        }
      }
      if (text.empty()) {
        text = std::to_wstring(value) + (value == 1 ? L" band" : L" bands");
      }
      SendMessageW(resolutionCombo_, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(text.c_str()));
    }
    break;
  }
}

void ToneTraceWin32Editor::Impl::createTabControl() {
  if (tabControl_ != nullptr) return;
  tabControl_ = CreateWindowExW(
      0, L"SysTabControl32", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_HOTTRACK,
      0, 0, 4, 4, window_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTabControlId)),
      moduleInstance(), nullptr);
  if (tabControl_ != nullptr) {
    SendMessageW(tabControl_, WM_SETFONT, reinterpret_cast<WPARAM>(font_),
                 FALSE);
    setSubclass(tabControl_, &Impl::tabControlProc);
  }
}

void ToneTraceWin32Editor::Impl::insertTraceTabs() {
  if (tabControl_ == nullptr) return;
  SendMessageW(tabControl_, TCM_DELETEALLITEMS, 0, 0);
  TCITEMW item{};
  item.mask = TCIF_TEXT;
  std::wstring matchLabel = L"Match";
  item.pszText = const_cast<wchar_t*>(matchLabel.c_str());
  // The build is not UNICODE, so TCM_INSERTITEM would expand to the ANSI
  // variant and truncate the wide text to its first character. Send the wide
  // message explicitly so the stored label (and MSAA name) is the full text.
  SendMessageW(tabControl_, TCM_INSERTITEMW, 0,
               reinterpret_cast<LPARAM>(&item));
  for (std::size_t index = 0; index < tracePages_.size(); ++index) {
    const TracePage& page = tracePages_[index];
    // Keep the complete visible and spoken name at every resolution. The
    // native tab strip supplies scroll arrows when all full labels do not fit.
    std::wstring label = L"Bands ";
    label += std::to_wstring(page.firstBand + 1) + L"-" +
             std::to_wstring(page.firstBand + page.bandCount);
    item.pszText = const_cast<wchar_t*>(label.c_str());
    SendMessageW(tabControl_, TCM_INSERTITEMW,
                 static_cast<WPARAM>(index + 1),
                 reinterpret_cast<LPARAM>(&item));
  }
  // Let the native tab control size captions to their text instead of forcing
  // every tab to consume an equal wide slot. Fixed-width tabs caused the last
  // visible caption to slide under the native scroll-arrow buttons at some
  // REAPER window sizes. Natural-width tabs reserve that arrow area correctly
  // and still scroll normally at very high resolutions.
  SendMessageW(tabControl_, TCM_SETPADDING, 0,
               MAKELPARAM(px(12), px(4)));
  // Programmatic replacement updates the native items but does not reliably
  // invalidate accessibility-client caches. Refresh both the pixels and the
  // standard MSAA child structure after a Resolution change.
  RedrawWindow(tabControl_, nullptr, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
  NotifyWinEvent(EVENT_OBJECT_REORDER, tabControl_, OBJID_CLIENT, CHILDID_SELF);
  const int itemCount =
      static_cast<int>(SendMessageW(tabControl_, TCM_GETITEMCOUNT, 0, 0));
  for (int index = 0; index < itemCount; ++index) {
    NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, tabControl_, OBJID_CLIENT,
                   index + 1);
  }
}

void ToneTraceWin32Editor::Impl::layoutChildren() {
  const int margin = px(10);
  const int width = clientWidth();
  const int height = clientHeight();
  const int bottom = height - margin;

  const RECT tabStrip = tabStripRect();
  SetWindowPos(tabControl_, nullptr, tabStrip.left, tabStrip.top,
               tabStrip.right - tabStrip.left,
               tabStrip.bottom - tabStrip.top,
               SWP_NOACTIVATE | SWP_NOZORDER);

  const int buttonCount = static_cast<int>(workflowButtons_.size());
  const int buttonGap = px(6);
  const int groupGap = px(16);
  const int totalButtonGaps =
      std::max(0, buttonCount - 1) * buttonGap +
      (buttonCount > 4 ? groupGap - buttonGap : 0);
  const int availableButtonWidth =
      std::max(buttonCount, width - margin * 2 - totalButtonGaps);
  std::vector<double> buttonWeights(static_cast<std::size_t>(buttonCount), 1.0);
  double remainingWeight = 0.0;
  for (int index = 0; index < buttonCount; ++index) {
    const int id = GetDlgCtrlID(workflowButtons_[static_cast<std::size_t>(index)]);
    // Allocate for the painted caption and the numbered workflow badge. Keep
    // the full native text visible: it is also the button's accessible name,
    // so shortening it just for layout would make the visual and spoken UI
    // disagree. Export/Import donate their unused width to longer actions.
    if (id == kCaptureReferenceId || id == kFreezeId) {
      buttonWeights[static_cast<std::size_t>(index)] = 1.25;
    } else if (id == kCorrectTargetId) {
      buttonWeights[static_cast<std::size_t>(index)] = 1.05;
    } else if (id == kDescribeId) {
      buttonWeights[static_cast<std::size_t>(index)] = 1.40;
    } else if (id == kExportId || id == kImportId) {
      buttonWeights[static_cast<std::size_t>(index)] = 0.60;
    }
    remainingWeight += buttonWeights[static_cast<std::size_t>(index)];
  }
  int remainingButtonWidth = availableButtonWidth;
  int x = margin;
  int y = bottom - px(42);
  for (int index = 0; index < buttonCount; ++index) {
    const double weight = buttonWeights[static_cast<std::size_t>(index)];
    const int buttonWidth =
        index + 1 == buttonCount
            ? remainingButtonWidth
            : std::max(1, static_cast<int>(std::lround(
                              remainingButtonWidth * weight / remainingWeight)));
    HWND button = workflowButtons_[static_cast<std::size_t>(index)];
    SetWindowPos(button, nullptr, x, y, buttonWidth, px(34),
                 SWP_NOACTIVATE | SWP_NOZORDER);
    const int gap = index == 3 && buttonCount > 4 ? groupGap : buttonGap;
    x += buttonWidth + gap;
    remainingButtonWidth -= buttonWidth;
    remainingWeight -= weight;
  }

  const int comboY = bottom - px(88);
  SetWindowPos(modeLabel_, nullptr, margin, comboY - px(32), px(220), px(30),
               SWP_NOACTIVATE | SWP_NOZORDER);
  SetWindowPos(modeCombo_, nullptr, margin, comboY, px(220), px(220),
               SWP_NOACTIVATE | SWP_NOZORDER);

  const int editStartX = margin + px(236);
  const int editCount = std::max(1, static_cast<int>(editControls_.size()));
  const int naturalEditWidth =
      (width - editStartX - margin - (editCount - 1) * px(6)) / editCount;
  const int editWidth = std::max(px(64), naturalEditWidth);
  x = editStartX;
  for (std::size_t index = 0; index < editControls_.size(); ++index) {
    if (index < editLabels_.size()) {
      // Preserve the full parameter name for accessibility while letting the
      // sighted label wrap to two lines when seven controls share the row.
      SetWindowPos(editLabels_[index], nullptr, x, comboY - px(32), editWidth,
                   px(30), SWP_NOACTIVATE | SWP_NOZORDER);
    }
    SetWindowPos(editControls_[index], nullptr, x, comboY, editWidth, px(26),
                 SWP_NOACTIVATE | SWP_NOZORDER);
    x += editWidth + px(6);
  }

  const int statusLabelY = bottom - px(188);
  const int statusY = statusLabelY + px(16);
  SetWindowPos(statusLabel_, nullptr, margin, statusLabelY,
               std::max(px(200), width - margin * 2), px(16),
               SWP_NOACTIVATE | SWP_NOZORDER);
  SetWindowPos(statusEdit_, nullptr, margin, statusY,
               std::max(px(200), width - margin * 2), px(48),
               SWP_NOACTIVATE | SWP_NOZORDER);

  const RECT description = descriptionRect();
  SetWindowPos(descriptionLabel_, nullptr, description.left, px(46),
               description.right - description.left, px(16),
               SWP_NOACTIVATE | SWP_NOZORDER);
  SetWindowPos(descriptionEdit_, nullptr, description.left, description.top,
               description.right - description.left,
               description.bottom - description.top,
               SWP_NOACTIVATE | SWP_NOZORDER);

  // Keep Trace Curve beside the always-visible readout rather than leaving a
  // checkbox floating in the lower-right corner of band pages. On Bands pages,
  // Correction Resolution shares this row so pointer and keyboard users can
  // change the grid without leaving the editor for a host-specific parameter
  // view. BS_PUSHLIKE gives the trace toggle the same readable themed treatment
  // as the other buttons.
  const int readoutY = tabStrip.bottom + margin;
  const int traceWidth = px(126);
  const int optionsWidth = px(92);
  const int controlGap = px(8);
  const int resolutionWidth = px(150);
  const int traceX = width - margin - traceWidth;
  const int optionsX = traceX - controlGap - optionsWidth;
  const int resolutionX = traceX - controlGap - resolutionWidth;
  const int readoutRight = selectedPage_ > 0 ? resolutionX - controlGap
                                              : optionsX - controlGap;
  const int readoutWidth = std::max(px(200), readoutRight - margin);
  const int readoutHeight = selectedPage_ > 0 ? px(42) : px(24);
  SetWindowPos(readoutEdit_, nullptr, margin, readoutY, readoutWidth,
               readoutHeight, SWP_NOACTIVATE | SWP_NOZORDER);
  if (selectedPage_ > 0) {
    SetWindowPos(resolutionLabel_, nullptr, resolutionX, readoutY,
                 resolutionWidth, px(16), SWP_NOACTIVATE | SWP_NOZORDER);
    SetWindowPos(resolutionCombo_, nullptr, resolutionX, readoutY + px(16),
                 resolutionWidth, px(240), SWP_NOACTIVATE | SWP_NOZORDER);
    SetWindowPos(traceButton_, nullptr, traceX, readoutY + px(9), traceWidth,
                 px(24), SWP_NOACTIVATE | SWP_NOZORDER);
  } else {
    SetWindowPos(optionsButton_, nullptr, optionsX, readoutY, optionsWidth, px(24),
                 SWP_NOACTIVATE | SWP_NOZORDER);
    SetWindowPos(traceButton_, nullptr, traceX, readoutY, traceWidth, px(24),
                 SWP_NOACTIVATE | SWP_NOZORDER);
  }

  layoutBandSliders();
}

void ToneTraceWin32Editor::Impl::paintCanvas(HDC dc, const RECT& bounds) {
  if (bounds.right <= bounds.left || bounds.bottom <= bounds.top) return;
  HBRUSH background = CreateSolidBrush(RGB(18, 18, 22));
  FillRect(dc, &bounds, background);
  DeleteObject(background);

  SetBkMode(dc, TRANSPARENT);
  SelectObject(dc, smallFont_);

  const RECT plot = graphPlotRect(bounds);
  const int plotWidth = std::max(1, static_cast<int>(plot.right - plot.left));
  const int plotHeight = std::max(1, static_cast<int>(plot.bottom - plot.top));

  const auto* snapshot = getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  const tonetrace::SpectrumCapture* reference =
      snapshot != nullptr ? &snapshot->reference : nullptr;
  const tonetrace::SpectrumCapture* target =
      snapshot != nullptr ? &snapshot->target : nullptr;
  const tonetrace::CorrectionModel* model =
      snapshot != nullptr ? &snapshot->uncappedModel : nullptr;

  const auto curveBias = [](const std::vector<tonetrace::SpectrumPoint>& points) {
    double sum = 0.0;
    std::size_t count = 0;
    for (const auto& point : points) {
      if (!std::isfinite(point.levelDb)) continue;
      sum += point.levelDb;
      ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
  };

  const double referenceBias =
      reference != nullptr ? curveBias(reference->points) : 0.0;
  const double targetBias = target != nullptr ? curveBias(target->points) : 0.0;

  // Choose a readable vertical range from the material actually being drawn.
  // A 95th-percentile spectrum range ignores isolated floor bins, while the
  // correction itself is always included at its true peak so the thick result
  // curve never disappears against a hard +/-12 dB display clamp.
  std::vector<double> spectrumMagnitudes;
  const auto collectMagnitude = [&](const tonetrace::SpectrumCapture* spectrum,
                                    double biasDb) {
    if (spectrum == nullptr) return;
    for (const auto& point : spectrum->points) {
      if (!std::isfinite(point.levelDb)) continue;
      spectrumMagnitudes.push_back(std::abs(point.levelDb - biasDb));
    }
  };
  collectMagnitude(reference, referenceBias);
  collectMagnitude(target, targetBias);

  double correctionPeak = 0.0;
  if (model != nullptr && !model->nodes.empty()) {
    constexpr int kCorrectionSamples = 256;
    for (int index = 0; index < kCorrectionSamples; ++index) {
      const double fraction = static_cast<double>(index) /
                              static_cast<double>(kCorrectionSamples - 1);
      const double frequency =
          kLogLowHz * std::pow(kLogHighHz / kLogLowHz, fraction);
      const double correction = finalCorrectionDb(frequency);
      if (std::isfinite(correction)) {
        correctionPeak = std::max(correctionPeak, std::abs(correction));
      }
    }
  }

  double displayPeak = correctionPeak;
  if (!spectrumMagnitudes.empty()) {
    std::sort(spectrumMagnitudes.begin(), spectrumMagnitudes.end());
    const std::size_t percentileIndex = static_cast<std::size_t>(
        0.95 * static_cast<double>(spectrumMagnitudes.size() - 1));
    displayPeak =
        std::max(displayPeak, spectrumMagnitudes[percentileIndex]);
  }
  double displayDb = kMinimumDisplayDb;
  if (displayPeak > kMinimumDisplayDb - 2.0) {
    displayDb = std::ceil((displayPeak + 1.0) / 6.0) * 6.0;
  }
  displayDb = std::clamp(displayDb, kMinimumDisplayDb, kMaximumDisplayDb);

  // Frequency labels live in the reserved top margin; curves and grid lines
  // stay inside plot so neither the labels nor the legend are painted over.
  SetTextColor(dc, RGB(190, 190, 202));
  const double decades[] = {20, 50, 100, 200, 500, 1000,
                            2000, 5000, 10000, 20000};
  HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(48, 48, 58));
  HGDIOBJ oldPen = SelectObject(dc, gridPen);
  for (const double decade : decades) {
    const int x = plot.left + static_cast<int>(
        freqToX(decade, plotWidth) + 0.5);
    MoveToEx(dc, x, plot.top, nullptr);
    LineTo(dc, x, plot.bottom);
    wchar_t label[32]{};
    if (decade < 1000) {
      std::swprintf(label, std::size(label), L"%d", static_cast<int>(decade));
    } else {
      std::swprintf(label, std::size(label), L"%dk",
                    static_cast<int>(decade / 1000));
    }
    // The final 20 kHz tick lands on plot.right. Keep the label rectangle
    // inside the graph so the neighboring Curve Description panel cannot
    // cover the far-right frequency label.
            const int labelWidth = px(44);
            const int plotLeft = static_cast<int>(plot.left);
            const int plotRight = static_cast<int>(plot.right);
            const bool clampRight = x + labelWidth > plotRight;
            RECT text{
                clampRight ? std::max(plotLeft, plotRight - labelWidth) : x + px(3),
                bounds.top + px(2),
                clampRight ? std::max(plotLeft, plotRight - px(3))
                           : std::min(plotRight, x + labelWidth),
                bounds.top + px(16)};
    const UINT textAlign = clampRight ? DT_RIGHT : DT_LEFT;
    DrawTextW(dc, label, -1, &text,
              textAlign | DT_SINGLELINE | DT_NOPREFIX);
  }

  // Five horizontal divisions follow the adaptive +/- display range.
  for (int division = -2; division <= 2; ++division) {
    if (division == 0) continue;
    const double level = displayDb * static_cast<double>(division) / 2.0;
    const int y = plot.top + static_cast<int>(
        dbToY(level, plotHeight, displayDb) + 0.5);
    MoveToEx(dc, plot.left, y, nullptr);
    LineTo(dc, plot.right, y);
  }
  SelectObject(dc, oldPen);
  DeleteObject(gridPen);

  HPEN zeroPen = CreatePen(PS_SOLID, 1, RGB(88, 88, 102));
  oldPen = SelectObject(dc, zeroPen);
  const int zeroY = plot.top + static_cast<int>(
      dbToY(0.0, plotHeight, displayDb) + 0.5);
  MoveToEx(dc, plot.left, zeroY, nullptr);
  LineTo(dc, plot.right, zeroY);
  SelectObject(dc, oldPen);
  DeleteObject(zeroPen);

  // Make an adaptive graph self-explanatory without adding another control.
  wchar_t rangeLabel[32]{};
  std::swprintf(rangeLabel, std::size(rangeLabel), L"+/-%.0f dB", displayDb);
  // In trace mode the top-right corner belongs to the TRACE badge; park the
  // range label to its left so the two texts never overdraw each other.
  const int rangeRight =
      traceMode_ ? plot.right - px(90) : plot.right - px(6);
  RECT rangeText{rangeRight - px(66), plot.top + px(3), rangeRight,
                 plot.top + px(18)};
  SetTextColor(dc, RGB(175, 175, 188));
  DrawTextW(dc, rangeLabel, -1, &rangeText,
            DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

  if (snapshot == nullptr) {
    wchar_t hint[] = L"No tone trace yet. Capture Reference to begin.";
    RECT text = plot;
    SetTextColor(dc, RGB(205, 205, 215));
    DrawTextW(dc, hint, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  auto buildPoints = [&](const std::vector<tonetrace::SpectrumPoint>& points,
                         double biasDb) {
    std::vector<POINT> result;
    result.reserve(points.size());
    for (const auto& point : points) {
      if (!std::isfinite(point.frequencyHz) || !std::isfinite(point.levelDb)) {
        continue;
      }
      const int x = plot.left + static_cast<int>(
          freqToX(point.frequencyHz, plotWidth) + 0.5);
      const int y = plot.top + static_cast<int>(
          dbToY(point.levelDb - biasDb, plotHeight, displayDb) + 0.5);
      result.push_back({x, y});
    }
    return result;
  };

  // Reference and target: thin colored curves, level relative to own mean.
  if (reference != nullptr && reference->points.size() >= 3) {
    auto points = buildPoints(reference->points, referenceBias);
    paintCurve(dc, plot, points, RGB(230, 150, 60), 2);
  }
  if (target != nullptr && target->points.size() >= 3) {
    auto points = buildPoints(target->points, targetBias);
    paintCurve(dc, plot, points, RGB(80, 160, 220), 2);
  }

  // Correction: thick, high-contrast curve from the model. The curve shows
  // the final result, so any manual band trims are folded in.
  if (model != nullptr && !model->nodes.empty()) {
    std::vector<POINT> points;
    constexpr int kCorrectionSamples = 256;
    points.reserve(kCorrectionSamples);
    for (int index = 0; index < kCorrectionSamples; ++index) {
      const double fraction = static_cast<double>(index) /
                              static_cast<double>(kCorrectionSamples - 1);
      const double frequency =
          kLogLowHz * std::pow(kLogHighHz / kLogLowHz, fraction);
      const double correction = finalCorrectionDb(frequency);
      if (!std::isfinite(correction)) continue;
      const int x = plot.left + static_cast<int>(
          freqToX(frequency, plotWidth) + 0.5);
      const int y = plot.top + static_cast<int>(
          dbToY(correction, plotHeight, displayDb) + 0.5);
      points.push_back({x, y});
    }
    paintCurve(dc, plot, points, RGB(255, 240, 210), 4);
  }

  // Horizontal legend in the reserved bottom margin. The old stacked legend
  // put its third row below the plot, which made labels appear missing.
  const auto drawLegend = [&](int xOffset, const wchar_t* label,
                              COLORREF color) {
    const int legendY = bounds.bottom - px(18);
    HBRUSH swatch = CreateSolidBrush(color);
    RECT box{bounds.left + px(xOffset), legendY,
             bounds.left + px(xOffset + 14), legendY + px(8)};
    FillRect(dc, &box, swatch);
    DeleteObject(swatch);
    RECT text{bounds.left + px(xOffset + 20), legendY - px(3),
              bounds.left + px(xOffset + 116), legendY + px(14)};
    SetTextColor(dc, RGB(220, 220, 228));
    DrawTextW(dc, label, -1, &text, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
  };
  drawLegend(8, L"Reference", RGB(230, 150, 60));
  drawLegend(126, L"Target", RGB(80, 160, 220));
  drawLegend(218, L"Correction", RGB(255, 240, 210));

  if (!traceMode_ && cursorVisible_ && cursorIndex_ >= 0) {
    const int x = plot.left + std::clamp(cursorIndex_, 0, plotWidth - 1);
    HPEN cursorPen = CreatePen(PS_DOT, 1, RGB(104, 210, 188));
    oldPen = SelectObject(dc, cursorPen);
    MoveToEx(dc, x, plot.top, nullptr);
    LineTo(dc, x, plot.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(cursorPen);

    POINT marker[3]{
        {x - px(5), plot.bottom - px(2)},
        {x + px(5), plot.bottom - px(2)},
        {x, plot.bottom - px(9)},
    };
    HBRUSH markerBrush = CreateSolidBrush(RGB(104, 210, 188));
    HPEN markerPen = CreatePen(PS_SOLID, 1, RGB(104, 210, 188));
    const int markerSaved = SaveDC(dc);
    SelectObject(dc, markerBrush);
    SelectObject(dc, markerPen);
    Polygon(dc, marker, 3);
    RestoreDC(dc, markerSaved);
    DeleteObject(markerBrush);
    DeleteObject(markerPen);

    // A tooltip-style chip names the frequency under the cursor so pointer
    // users can explore the curve without looking down at the readout. The
    // fraction math mirrors updateReadout() so chip and readout always agree.
    const double fraction =
        static_cast<double>(cursorIndex_) / static_cast<double>(plotWidth);
    const double cursorFrequency =
        kLogLowHz * std::pow(kLogHighHz / kLogLowHz, fraction);
    const std::wstring chipText = formatFrequency(cursorFrequency);
    const int chipWidth = px(52);
    const int plotLeft = static_cast<int>(plot.left);
    const int plotRight = static_cast<int>(plot.right);
    const int chipLeft =
        std::clamp(x - chipWidth / 2, plotLeft + px(2),
                   std::max(plotLeft + px(2), plotRight - chipWidth - px(2)));
    RECT chip{chipLeft, plot.top + px(2), chipLeft + chipWidth,
              plot.top + px(16)};
    HBRUSH chipBrush = CreateSolidBrush(RGB(28, 28, 34));
    FillRect(dc, &chip, chipBrush);
    DeleteObject(chipBrush);
    HPEN chipPen = CreatePen(PS_SOLID, 1, RGB(104, 210, 188));
    HGDIOBJ chipOldPen = SelectObject(dc, chipPen);
    HGDIOBJ chipOldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, chip.left, chip.top, chip.right, chip.bottom);
    SelectObject(dc, chipOldBrush);
    SelectObject(dc, chipOldPen);
    DeleteObject(chipPen);
    SetTextColor(dc, RGB(220, 220, 228));
    DrawTextW(dc, chipText.c_str(), -1, &chip,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  if (traceMode_) {
    // Band centers as small ticks along the top edge, so the resolution grid
    // is visible at a glance and the mode is unmistakable.
    const int bandCount = traceBandCount();
    HPEN tickPen = CreatePen(PS_SOLID, 1, RGB(120, 120, 134));
    oldPen = SelectObject(dc, tickPen);
    for (int band = 0; band < bandCount; ++band) {
      const int x = plot.left + static_cast<int>(
          freqToX(traceBandFrequency(band), plotWidth) + 0.5);
      MoveToEx(dc, x, plot.top, nullptr);
      LineTo(dc, x, plot.top + px(4));
    }
    SelectObject(dc, oldPen);
    DeleteObject(tickPen);

    const double frequency = traceFrequency();
    const int x = plot.left + static_cast<int>(
        freqToX(frequency, plotWidth) + 0.5);
    HPEN cursorPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    oldPen = SelectObject(dc, cursorPen);
    MoveToEx(dc, x, plot.top, nullptr);
    LineTo(dc, x, plot.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(cursorPen);

    POINT marker[3]{
        {x - px(5), plot.top + px(2)},
        {x + px(5), plot.top + px(2)},
        {x, plot.top + px(9)},
    };
    HBRUSH markerBrush = CreateSolidBrush(RGB(255, 200, 90));
    HPEN markerPen = CreatePen(PS_SOLID, 1, RGB(255, 200, 90));
    HGDIOBJ oldBrush = SelectObject(dc, markerBrush);
    oldPen = SelectObject(dc, markerPen);
    Polygon(dc, marker, 3);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(markerBrush);
    DeleteObject(markerPen);

    RECT badge{plot.right - px(84), plot.top + px(4), plot.right - px(8),
               plot.top + px(20)};
    SetTextColor(dc, RGB(255, 200, 90));
    DrawTextW(dc, L"TRACE", -1, &badge,
              DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
  }
}

void ToneTraceWin32Editor::Impl::paintCurve(HDC dc, const RECT& bounds,
                                            const std::vector<POINT>& points,
                                            COLORREF color, int penWidth) {
  if (points.size() < 2) return;
  const int saved = SaveDC(dc);
  IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
  HPEN pen = CreatePen(PS_SOLID, std::max(1, px(penWidth)), color);
  HGDIOBJ oldPen = SelectObject(dc, pen);
  Polyline(dc, points.data(), static_cast<int>(points.size()));
  SelectObject(dc, oldPen);
  DeleteObject(pen);
  RestoreDC(dc, saved);
}

void ToneTraceWin32Editor::Impl::refresh() {
  if (traceBandCount() != builtResolution_) rebuildTraceTabs();
  refreshValues();
  refreshStatus();
  refreshDescription();
  refreshBandSliders();
  const RECT bounds = selectedPage_ == 0 ? canvasRect() : bandCanvasRect();
  InvalidateRect(window_, &bounds, FALSE);
}

void ToneTraceWin32Editor::Impl::refreshValues() {
  if (modeCombo_ == nullptr || resolutionCombo_ == nullptr) return;
  const int workflowStep = static_cast<int>(std::lround(
      paramValue(tonetrace::ParameterId::WorkflowAction)));
  if (workflowStep != lastWorkflowStep_) {
    lastWorkflowStep_ = workflowStep;
    for (HWND button : workflowButtons_) {
      InvalidateRect(button, nullptr, FALSE);
    }
  }
  const int modeValue =
      static_cast<int>(std::lround(paramValue(tonetrace::ParameterId::MatchMode)));
  if (SendMessageW(modeCombo_, CB_GETCURSEL, 0, 0) != modeValue) {
    SendMessageW(modeCombo_, CB_SETCURSEL, static_cast<WPARAM>(modeValue), 0);
  }

  const int resolutionValue = std::clamp(
      static_cast<int>(std::lround(
          paramValue(tonetrace::ParameterId::Resolution))),
      1, 120);
  const int resolutionSelection = resolutionValue - 1;
  if (SendMessageW(resolutionCombo_, CB_GETCURSEL, 0, 0) !=
      resolutionSelection) {
    SendMessageW(resolutionCombo_, CB_SETCURSEL,
                 static_cast<WPARAM>(resolutionSelection), 0);
  }

  for (std::size_t index = 0; index < editControls_.size(); ++index) {
    if (index >= std::size(kEditedParams)) break;
    const std::string text = paramText(kEditedParams[index]);
    if (!text.empty() && GetFocus() != editControls_[index]) {
      (void)setWindowTextIfChanged(editControls_[index], widen(text));
    }
  }
}

void ToneTraceWin32Editor::Impl::refreshStatus() {
  if (statusEdit_ == nullptr) return;
  const std::string status = paramText(tonetrace::ParameterId::Status);
  const std::string command = paramText(tonetrace::ParameterId::LastCommand);
  const std::string confidence = paramText(tonetrace::ParameterId::Confidence);
  const std::string captureTime = paramText(tonetrace::ParameterId::CaptureSeconds);
  const std::string drift = paramText(tonetrace::ParameterId::CurveDriftDb);
  const int workflowStep = static_cast<int>(std::lround(
      paramValue(tonetrace::ParameterId::WorkflowAction)));

  // Status/confidence/command transitions are meaningful and refresh
  // immediately. Continuously changing capture seconds and drift are visual
  // telemetry, so limit those text updates to once per second. The native
  // field stays reviewable/copyable without turning a screen reader into a
  // one-second talking clock.
  const std::wstring meaningfulKey = widen(status + "\n" + command + "\n" + confidence);
  const std::uint64_t now = GetTickCount64();
  const bool meaningfulChanged = meaningfulKey != lastStatusMeaningfulKey_;
  if (!meaningfulChanged && lastStatusTelemetryTicks_ != 0 &&
      now - lastStatusTelemetryTicks_ < 1000) {
    return;
  }

  std::wstring text = L"Status: " + widen(status.empty() ? "Ready" : status);
  const bool captureContext = workflowStep == 1 || workflowStep == 2 ||
                              paramValue(tonetrace::ParameterId::CaptureSeconds) > 0.0;
  if (captureContext) {
    text += L"\r\nCapture time: " + widen(captureTime) +
            L"  |  Confidence: " + widen(confidence) +
            L"  |  Curve drift: " + widen(drift);
  } else {
    text += L"\r\nNo capture in progress";
  }
  if (!command.empty() && command != "No action") {
    text += L"  |  Last action: " + widen(command);
  }
  if (traceMode_) {
    text += L"  |  Trace: On";
  }

  const bool changed = setWindowTextIfChanged(statusEdit_, text);
  lastStatusMeaningfulKey_ = meaningfulKey;
  lastStatusTelemetryTicks_ = now;
  if (changed && meaningfulChanged) {
    NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, statusEdit_, OBJID_CLIENT,
                   CHILDID_SELF);
  }
}

void ToneTraceWin32Editor::Impl::refreshDescription() {
  if (descriptionEdit_ == nullptr) return;
  const auto* snapshot = getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  std::wstring text = win32MultilineText(widen(tonetrace::curveDescriptionText(
      snapshot != nullptr ? *snapshot : tonetrace::ProfileSnapshot{})));
  if (text == lastDescriptionText_) return;
  lastDescriptionText_ = text;
  SetWindowTextW(descriptionEdit_, text.c_str());
  NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, descriptionEdit_, OBJID_CLIENT,
                 CHILDID_SELF);
}

void ToneTraceWin32Editor::Impl::copyDescriptionToClipboard() {
  if (descriptionEdit_ == nullptr) return;
  const int requested = GetWindowTextLengthW(descriptionEdit_);
  if (requested <= 0) return;
  std::vector<wchar_t> buffer(static_cast<std::size_t>(requested) + 1U, L'\0');
  const int length = GetWindowTextW(descriptionEdit_, buffer.data(),
                                    static_cast<int>(buffer.size()));
  if (length <= 0) return;
  if (!OpenClipboard(window_)) return;
  const bool emptied = EmptyClipboard() != 0;
  if (emptied) {
    const SIZE_T bytes = (static_cast<SIZE_T>(length) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
      void* data = GlobalLock(memory);
      if (data != nullptr) {
        std::memcpy(data, buffer.data(), bytes);
        GlobalUnlock(memory);
        if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
          GlobalFree(memory);
        }
      } else {
        GlobalFree(memory);
      }
    }
  }
  CloseClipboard();
}

void ToneTraceWin32Editor::Impl::announceMessage(const std::wstring& message) {
  if (readoutEdit_ != nullptr) SetWindowTextW(readoutEdit_, message.c_str());
  NotifyWinEvent(EVENT_SYSTEM_ALERT, window_, OBJID_CLIENT, CHILDID_SELF);
}

bool ToneTraceWin32Editor::Impl::chooseSavePath(const wchar_t* filter,
                                                const wchar_t* defaultExt,
                                                std::wstring& path) const {
  wchar_t buffer[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = window_;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buffer;
  ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
  ofn.lpstrDefExt = defaultExt;
  ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetSaveFileNameW(&ofn) == 0) return false;
  path = buffer;
  return true;
}

bool ToneTraceWin32Editor::Impl::chooseOpenPath(const wchar_t* filter,
                                                std::wstring& path) const {
  wchar_t buffer[MAX_PATH]{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = window_;
  ofn.lpstrFilter = filter;
  ofn.lpstrFile = buffer;
  ofn.nMaxFile = static_cast<DWORD>(std::size(buffer));
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
  if (GetOpenFileNameW(&ofn) == 0) return false;
  path = buffer;
  return true;
}

INT_PTR CALLBACK ToneTraceWin32Editor::Impl::optionsDialogProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  Impl* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, DWLP_USER));
  if (message == WM_INITDIALOG) {
    impl = reinterpret_cast<Impl*>(lParam);
    if (impl == nullptr) return FALSE;
    SetWindowLongPtrW(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(impl));
    CheckDlgButton(hwnd, IDC_OPTION_FULL_CORRECTION,
                   impl->paramValue(tonetrace::ParameterId::CompleteMatch) >= 0.5
                       ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_OPTION_TONE_NOTIFICATIONS,
                   impl->paramValue(tonetrace::ParameterId::ToneNotifications) >= 0.5
                       ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_OPTION_BYPASS,
                   impl->paramValue(tonetrace::ParameterId::Bypass) >= 0.5
                       ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(hwnd, IDC_OPTION_TONE_LEVEL,
                    widen(impl->paramText(tonetrace::ParameterId::ToneLevelDb)).c_str());
    if (impl->reset_ == nullptr) {
      EnableWindow(GetDlgItem(hwnd, IDC_OPTION_RESET), FALSE);
    }
    return TRUE;
  }
  if (impl == nullptr) return FALSE;
  if (message == WM_COMMAND) {
    const int id = LOWORD(wParam);
    if (id == IDOK) {
      if (impl->applyOptionsDialog(hwnd)) EndDialog(hwnd, IDOK);
      return TRUE;
    }
    if (id == IDCANCEL) {
      EndDialog(hwnd, IDCANCEL);
      return TRUE;
    }
    if (id == IDC_OPTION_RESET && HIWORD(wParam) == BN_CLICKED) {
      const int answer = MessageBoxW(
          hwnd,
          L"Reset Tone Trace?\n\nThis clears the learned Reference/Target match "
          L"and all manual band edits. This cannot be undone.",
          L"Reset Tone Trace", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
      if (answer == IDYES && impl->reset_ != nullptr) {
        impl->reset_(impl->context_);
        EndDialog(hwnd, IDOK);
      }
      return TRUE;
    }
  }
  return FALSE;
}

bool ToneTraceWin32Editor::Impl::applyOptionsDialog(HWND dialog) {
  if (dialog == nullptr || setParam_ == nullptr || params_ == nullptr ||
      params_->text_to_value == nullptr) {
    return false;
  }
  wchar_t toneText[128]{};
  GetDlgItemTextW(dialog, IDC_OPTION_TONE_LEVEL, toneText,
                  static_cast<int>(std::size(toneText)));
  double toneLevel = 0.0;
  const std::string narrowText = narrow(toneText);
  if (!params_->text_to_value(plugin_,
                              static_cast<clap_id>(tonetrace::ParameterId::ToneLevelDb),
                              narrowText.c_str(), &toneLevel)) {
    MessageBoxW(dialog,
                L"Enter a Confidence Tone Volume in dB, or type Off.",
                L"Tone Trace EQ Options", MB_OK | MB_ICONERROR);
    SetFocus(GetDlgItem(dialog, IDC_OPTION_TONE_LEVEL));
    return false;
  }
  const auto checked = [&](int id) {
    return IsDlgButtonChecked(dialog, id) == BST_CHECKED ? 1.0 : 0.0;
  };
  setParam_(context_, static_cast<std::uint32_t>(tonetrace::ParameterId::CompleteMatch),
            checked(IDC_OPTION_FULL_CORRECTION));
  setParam_(context_, static_cast<std::uint32_t>(tonetrace::ParameterId::ToneNotifications),
            checked(IDC_OPTION_TONE_NOTIFICATIONS));
  setParam_(context_, static_cast<std::uint32_t>(tonetrace::ParameterId::ToneLevelDb),
            toneLevel);
  setParam_(context_, static_cast<std::uint32_t>(tonetrace::ParameterId::Bypass),
            checked(IDC_OPTION_BYPASS));
  return true;
}

void ToneTraceWin32Editor::Impl::showOptionsDialog() {
  DialogBoxParamW(moduleInstance(), MAKEINTRESOURCEW(IDD_TONETRACE_OPTIONS),
                  window_, &Impl::optionsDialogProc,
                  reinterpret_cast<LPARAM>(this));
}

void ToneTraceWin32Editor::Impl::showTransferMenu(bool exportMenu) {
  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) return;
  if (exportMenu) {
    AppendMenuW(menu, MF_STRING, kExportIr,
                L"Export Impulse Response (WAV)...");
    AppendMenuW(menu, MF_STRING, kExportReference,
                L"Export Reference Curve (TTS)...");
    AppendMenuW(menu, MF_STRING, kExportTarget,
                L"Export Target Curve (TTS)...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExportModel,
                L"Export Correction Model (TTM)...");
  } else {
    AppendMenuW(menu, MF_STRING, kImportReference,
                L"Import Reference Curve (TTS)...");
    AppendMenuW(menu, MF_STRING, kImportTarget,
                L"Import Target Curve (TTS)...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kImportModel,
                L"Import Correction Model (TTM)...");
  }
  const HWND anchor =
      GetDlgItem(window_, exportMenu ? kExportId : kImportId);
  RECT rect{};
  if (anchor != nullptr && GetWindowRect(anchor, &rect)) {
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   rect.left, rect.bottom, 0, window_, nullptr);
  }
  DestroyMenu(menu);
}

tonetrace::IrRenderSettings
ToneTraceWin32Editor::Impl::currentManualIrSettings() const {
  tonetrace::IrRenderSettings settings;
  const double sampleRate =
      getSampleRate_ != nullptr ? getSampleRate_(context_) : 0.0;
  settings.sampleRate = static_cast<int>(std::lround(sampleRate));
  settings.correctionStrength =
      paramValue(tonetrace::ParameterId::CorrectionStrength);
  settings.correctionSharpness =
      paramValue(tonetrace::ParameterId::CorrectionSharpness);
  settings.correctionGainDb =
      paramValue(tonetrace::ParameterId::CorrectionGainDb);
  settings.rangeLowHz = paramValue(tonetrace::ParameterId::RangeLowHz);
  settings.rangeHighHz = paramValue(tonetrace::ParameterId::RangeHighHz);
  const std::size_t count =
      getBandCount_ != nullptr ? getBandCount_(context_) : 0U;
  settings.manualGains.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    settings.manualGains.push_back(
        getBandGain_ != nullptr ? getBandGain_(context_, index) : 0.0);
  }
  return settings;
}

void ToneTraceWin32Editor::Impl::exportCurve(int menuId) {
  const auto* snapshot =
      getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  tonetrace::IrRenderSettings manualSettings;
  bool exportManualIr = false;
  if (menuId == kExportIr && snapshot == nullptr) {
    manualSettings = currentManualIrSettings();
    if (!tonetrace::hasManualCorrection(manualSettings)) {
      MessageBoxW(window_, L"No matching curve is available to export yet.",
                  L"Tone Trace EQ", MB_OK | MB_ICONINFORMATION);
      return;
    }
    const int answer = confirmManualIrExport(window_);
    if (answer != IDOK) return;
    exportManualIr = true;
  }

  std::wstring path;
  switch (menuId) {
    case kExportIr:
      if (!chooseSavePath(kWavFilter, L"wav", path)) return;
      break;
    case kExportReference:
      if (!chooseSavePath(kSpectrumFilter, L"tts", path)) return;
      break;
    case kExportTarget:
      if (!chooseSavePath(kSpectrumFilter, L"tts", path)) return;
      break;
    case kExportModel:
      if (!chooseSavePath(kModelFilter, L"ttm", path)) return;
      break;
    default:
      return;
  }
  const auto stagedSpectrum = [&](int which)
      -> const tonetrace::SpectrumCapture* {
    return getStagedSpectrum_ != nullptr
               ? getStagedSpectrum_(context_, which)
               : nullptr;
  };
  if (snapshot == nullptr && !exportManualIr &&
      !((menuId == kExportReference && stagedSpectrum(1) != nullptr) ||
        (menuId == kExportTarget && stagedSpectrum(2) != nullptr))) {
    MessageBoxW(window_, L"No matching curve is available to export yet.",
                L"Tone Trace EQ", MB_OK | MB_ICONINFORMATION);
    return;
  }
  try {
    if (menuId == kExportIr) {
      const std::vector<double> kernel = exportManualIr
                                             ? tonetrace::renderManualCorrectionIr(
                                                   manualSettings)
                                             : tonetrace::renderProfileKernel(
                                                   *snapshot);
      tonetrace::AudioBuffer audio;
      audio.sampleRate = exportManualIr
                             ? manualSettings.sampleRate
                             : snapshot->renderSettings.sampleRate;
      audio.channels = {kernel};
      tonetrace::writeFloatWav(path, audio);
      announceMessage(L"Impulse response exported.");
    } else if (menuId == kExportReference) {
      const auto* reference = snapshot != nullptr
                                  ? &snapshot->reference
                                  : stagedSpectrum(1);
      tonetrace::saveSpectrumCapture(path, *reference);
      announceMessage(L"Reference curve exported.");
    } else if (menuId == kExportTarget) {
      const auto* target = snapshot != nullptr ? &snapshot->target
                                               : stagedSpectrum(2);
      tonetrace::saveSpectrumCapture(path, *target);
      announceMessage(L"Target curve exported.");
    } else if (menuId == kExportModel) {
      snapshot->uncappedModel.save(path);
      announceMessage(L"Correction model exported.");
    }
  } catch (const std::exception& error) {
    MessageBoxW(window_, widen(error.what()).c_str(),
                L"Tone Trace EQ Export Failed", MB_OK | MB_ICONERROR);
  }
}

void ToneTraceWin32Editor::Impl::importCurve(int menuId) {
  std::wstring path;
  switch (menuId) {
    case kImportReference:
      if (!chooseOpenPath(kSpectrumFilter, path)) return;
      break;
    case kImportTarget:
      if (!chooseOpenPath(kSpectrumFilter, path)) return;
      break;
    case kImportModel:
      if (!chooseOpenPath(kModelFilter, path)) return;
      break;
    default:
      return;
  }
  const double projectRate =
      getSampleRate_ != nullptr ? getSampleRate_(context_) : 0.0;
  try {
    if (menuId == kImportReference || menuId == kImportTarget) {
      const tonetrace::SpectrumCapture capture =
          tonetrace::loadSpectrumCapture(path);
      if (projectRate > 0.0 && setImportedSpectrum_ != nullptr) {
        // Curves store absolute frequencies, so a capture made at another
        // sample rate is usable as long as its frequencies overlap what this
        // session can analyze (the current range clamped to Nyquist). If the
        // overlap is empty the import cannot proceed, so explain why up front.
        const double rangeLow = paramValue(tonetrace::ParameterId::RangeLowHz);
        const double rangeHigh =
            paramValue(tonetrace::ParameterId::RangeHighHz);
        const tonetrace::ImportCompatibility check =
            tonetrace::assessCaptureImport(
                capture,
                rangeLow > 0.0 && rangeHigh > rangeLow ? rangeLow : 20.0,
                rangeHigh > rangeLow ? rangeHigh : 20000.0, projectRate);
        if (!check.usable) {
          MessageBoxW(window_, widen(check.reason).c_str(),
                      L"Tone Trace EQ Import Failed", MB_OK | MB_ICONERROR);
          return;
        }
        if (check.truncatedByNyquist) {
          announceMessage(
              L"The curve reaches above this project's Nyquist; content above "
              L"the sample-rate limit will not be applied.");
        }
      }
      if (setImportedSpectrum_ != nullptr) {
        setImportedSpectrum_(context_, menuId == kImportReference ? 1 : 2,
                             capture);
      }
      announceMessage(menuId == kImportReference
                          ? L"Reference curve imported. Capture or import the "
                            L"target."
                          : L"Target curve imported.");
    } else if (menuId == kImportModel) {
      const tonetrace::CorrectionModel model =
          tonetrace::CorrectionModel::load(path);
      if (projectRate > 0.0 && setImportedModel_ != nullptr) {
        const double rangeLow = paramValue(tonetrace::ParameterId::RangeLowHz);
        const double rangeHigh =
            paramValue(tonetrace::ParameterId::RangeHighHz);
        const tonetrace::ImportCompatibility check =
            tonetrace::assessModelImport(
                model,
                rangeLow > 0.0 && rangeHigh > rangeLow ? rangeLow : 10.0,
                rangeHigh > rangeLow ? rangeHigh : 30000.0, projectRate);
        if (!check.usable) {
          MessageBoxW(window_, widen(check.reason).c_str(),
                      L"Tone Trace EQ Import Failed", MB_OK | MB_ICONERROR);
          return;
        }
        if (check.truncatedByNyquist) {
          announceMessage(
              L"The model reaches above this project's Nyquist; correction "
              L"above the sample-rate limit will not be applied.");
        }
      }
      if (setImportedModel_ != nullptr) {
        setImportedModel_(context_, model);
      }
      announceMessage(L"Correction model imported.");
    }
  } catch (const std::exception& error) {
    MessageBoxW(window_, widen(error.what()).c_str(),
                L"Tone Trace EQ Import Failed", MB_OK | MB_ICONERROR);
  }
}

void ToneTraceWin32Editor::Impl::updateReadout() {
  if (readoutEdit_ == nullptr) return;
  const auto* snapshot = getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  if (traceMode_ || selectedPage_ > 0) {
    const int count = traceBandCount();
    const double frequency = traceFrequency();
    double match = 0.0;
    double trim = 0.0;
    double final = 0.0;
    if (snapshot != nullptr) {
      correctionAt(frequency, match, trim, final);
    } else {
      trim = getBandGain_ != nullptr && traceIndex_ >= 0 && traceIndex_ < count
                 ? getBandGain_(context_, static_cast<std::size_t>(traceIndex_))
                 : 0.0;
      final = trim;
    }
    wchar_t buffer[192]{};
    if (snapshot != nullptr) {
      const std::wstring finalText = tonetrace::formatBandValueDbWide(final);
      const std::wstring matchText = tonetrace::formatBandValueDbWide(match);
      const std::wstring trimText = tonetrace::formatBandValueDbWide(trim);
      std::swprintf(buffer, std::size(buffer),
                    L"Band %d of %d, %d Hz, Final %ls (match %ls, trim %ls)",
                    traceIndex_ + 1, count,
                    static_cast<int>(std::lround(frequency)), finalText.c_str(),
                    matchText.c_str(), trimText.c_str());
    } else {
      const std::wstring finalText = tonetrace::formatBandValueDbWide(final);
      std::swprintf(buffer, std::size(buffer),
                    L"Band %d of %d, %d Hz, %ls; manual graphic EQ, no learned correction",
                    traceIndex_ + 1, count,
                    static_cast<int>(std::lround(frequency)), finalText.c_str());
    }
    SetWindowTextW(readoutEdit_, buffer);
    return;
  }
  if (snapshot == nullptr) {
    SetWindowTextW(readoutEdit_, L"No tone trace loaded. Manual band EQ remains available.");
    return;
  }
  const RECT bounds = graphPlotRect(canvasRect());
  const int plotWidth =
      std::max(1, static_cast<int>(bounds.right - bounds.left));
  const int index = std::clamp(cursorIndex_, 0, plotWidth - 1);
  const double fraction = static_cast<double>(index) / plotWidth;
  const double frequency =
      kLogLowHz * std::pow(kLogHighHz / kLogLowHz, fraction);

  double reference = std::numeric_limits<double>::quiet_NaN();
  double target = std::numeric_limits<double>::quiet_NaN();
  const auto sampleCurve = [&](const std::vector<tonetrace::SpectrumPoint>& points,
                               double& output) {
    if (points.empty()) return;
    double nearest = std::numeric_limits<double>::max();
    for (const auto& point : points) {
      const double distance = std::abs(point.frequencyHz - frequency);
      if (distance < nearest) {
        nearest = distance;
        output = point.levelDb;
      }
    }
  };
  sampleCurve(snapshot->reference.points, reference);
  sampleCurve(snapshot->target.points, target);
  const double correction = finalCorrectionDb(frequency);

  wchar_t buffer[256]{};
  const std::wstring referenceText =
      tonetrace::formatBandValueDbWide(reference);
  const std::wstring targetText = tonetrace::formatBandValueDbWide(target);
  const std::wstring correctionText =
      tonetrace::formatBandValueDbWide(correction);
  std::swprintf(buffer, std::size(buffer),
                L"%d Hz  |  Reference %ls  |  Target %ls  |  Correction %ls",
                static_cast<int>(frequency), referenceText.c_str(),
                targetText.c_str(), correctionText.c_str());
  SetWindowTextW(readoutEdit_, buffer);
}

void ToneTraceWin32Editor::Impl::moveCursor(int step) {
  const RECT bounds = graphPlotRect(canvasRect());
  const int plotWidth =
      std::max(1, static_cast<int>(bounds.right - bounds.left));
  cursorIndex_ = std::clamp(
      (cursorIndex_ < 0 ? plotWidth / 2 : cursorIndex_) + step,
      0, plotWidth - 1);
  cursorVisible_ = true;
  updateReadout();
  const RECT canvas = canvasRect();
  InvalidateRect(window_, &canvas, FALSE);
}

int ToneTraceWin32Editor::Impl::traceBandCount() const {
  const double raw = paramValue(tonetrace::ParameterId::Resolution);
  return std::clamp(static_cast<int>(std::lround(raw)), 1, 120);
}

bool ToneTraceWin32Editor::Impl::traceRange(double& lowHz,
                                            double& highHz) const {
  const auto* snapshot =
      getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  if (snapshot == nullptr) return false;
  // The editable band grid belongs to the learned model, not to the current
  // render mask. Moving Correction Range Low/High must never move band
  // frequencies or renumber manual edits; it only masks their effective
  // contribution. This keeps projects, keyboard navigation and saved trims
  // stable while the user experiments with the active range.
  double low = std::max(snapshot->uncappedModel.analysisLowHz, kLogLowHz);
  double high = std::min(snapshot->uncappedModel.analysisHighHz, kLogHighHz);
  if (high <= low) {
    low = kLogLowHz;
    high = kLogHighHz;
  }
  lowHz = low;
  highHz = high;
  return true;
}

bool ToneTraceWin32Editor::Impl::frequencyInCorrectionRange(
    double frequencyHz) const {
  if (!std::isfinite(frequencyHz)) return false;
  const auto* snapshot =
      getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  if (snapshot == nullptr) {
    const double low = std::max(
        paramValue(tonetrace::ParameterId::RangeLowHz), kLogLowHz);
    const double high = std::min(
        paramValue(tonetrace::ParameterId::RangeHighHz), kLogHighHz);
    return high > low && frequencyHz >= low && frequencyHz <= high;
  }
  const double low = std::max(snapshot->renderSettings.rangeLowHz,
                              snapshot->uncappedModel.analysisLowHz);
  const double high = std::min(snapshot->renderSettings.rangeHighHz,
                               snapshot->uncappedModel.analysisHighHz);
  return high > low && frequencyHz >= low && frequencyHz <= high;
}

double ToneTraceWin32Editor::Impl::traceBandFrequency(int index) const {
  double low = kLogLowHz;
  double high = kLogHighHz;
  traceRange(low, high);
  const int count = traceBandCount();
  if (count <= 1) return std::sqrt(low * high);
  const double fraction = static_cast<double>(index) / (count - 1);
  return low * std::pow(high / low, fraction);
}

double ToneTraceWin32Editor::Impl::traceFrequency() const {
  const int count = traceBandCount();
  const int index = std::clamp(traceIndex_, 0, count - 1);
  return traceBandFrequency(index);
}

void ToneTraceWin32Editor::Impl::toggleTrace() {
  traceMode_ = !traceMode_;
  // The checkbox is a visual state only; traceMode_ is the source of truth.
  // Keeping them in sync also covers hosts that grab the Space key, which is
  // normally the only way to toggle a checkbox from the keyboard.
  if (traceButton_ != nullptr) {
    SendMessageW(traceButton_, BM_SETCHECK,
                 traceMode_ ? BST_CHECKED : BST_UNCHECKED, 0);
  }
  if (!traceMode_) {
    cancelTraceAnnounce();
  } else {
    traceIndex_ = traceBandCount() / 2;
    updateReadout();
    armTraceAnnounce();
    // Audible confirmation at the starting band (audible while processing).
    playTraceTone();
  }
  refreshStatus();
  InvalidateRect(window_, nullptr, FALSE);
}

void ToneTraceWin32Editor::Impl::setTraceBand(int index, bool withBeep) {
  traceIndex_ = std::clamp(index, 0, traceBandCount() - 1);
  updateReadout();
  if (traceMode_ && withBeep) playTraceTone();
  if (traceMode_) armTraceAnnounce();
  InvalidateRect(window_, nullptr, FALSE);
}

void ToneTraceWin32Editor::Impl::moveTrace(int step) {
  setTraceBand(traceIndex_ + step, true);
}

void ToneTraceWin32Editor::Impl::armTraceAnnounce() {
  if (traceAnnounceTimer_ != 0) {
    KillTimer(window_, kTraceAnnounceTimerId);
  }
  traceAnnounceTimer_ =
      SetTimer(window_, kTraceAnnounceTimerId, kTraceAnnounceDelayMs, nullptr);
}

void ToneTraceWin32Editor::Impl::cancelTraceAnnounce() {
  if (traceAnnounceTimer_ != 0) {
    KillTimer(window_, kTraceAnnounceTimerId);
    traceAnnounceTimer_ = 0;
  }
}

void ToneTraceWin32Editor::Impl::playTraceTone() const {
  if (playTone_ == nullptr || !traceMode_) return;
  const double frequency = traceFrequency();
  if (frequency <= 0.0) return;
  playTone_(context_, frequency);
}

std::wstring ToneTraceWin32Editor::Impl::bandFrequencyText(int band) const {
  const double frequency = traceBandFrequency(band);
  const std::wstring concise = formatFrequency(frequency);
  const auto conciseAt = [this](int index) {
    return formatFrequency(traceBandFrequency(index));
  };
  const int count = traceBandCount();
  const bool duplicateBefore = band > 0 && conciseAt(band - 1) == concise;
  const bool duplicateAfter = band + 1 < count && conciseAt(band + 1) == concise;
  if (!duplicateBefore && !duplicateAfter) return concise;

  // At very high resolutions two neighboring centers can round to the same
  // concise kHz caption. Escalate only that collision to whole Hz, then to
  // fractional Hz if an unusually narrow imported range still needs it.
  const auto hzText = [](double value, int decimals) {
    wchar_t buffer[32]{};
    std::swprintf(buffer, std::size(buffer), decimals == 0 ? L"%.0f Hz" :
                  decimals == 1 ? L"%.1f Hz" : L"%.2f Hz", value);
    return std::wstring(buffer);
  };
  for (int decimals = 0; decimals <= 2; ++decimals) {
    const std::wstring candidate = hzText(frequency, decimals);
    const bool sameBefore =
        band > 0 && hzText(traceBandFrequency(band - 1), decimals) == candidate;
    const bool sameAfter = band + 1 < count &&
        hzText(traceBandFrequency(band + 1), decimals) == candidate;
    if (!sameBefore && !sameAfter) return candidate;
  }
  return hzText(frequency, 2);
}

double ToneTraceWin32Editor::Impl::matchDbAtBand(int band) const {
  const auto* snapshot =
      getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  if (snapshot == nullptr) return 0.0;
  const double frequency = traceBandFrequency(band);
  if (!frequencyInCorrectionRange(frequency)) return 0.0;
  // Use the same ceiling -> sharpness -> Strength calculation as the FIR
  // renderer. The band editor calls this the Match contribution; manual trim
  // is kept separate so a focused edit can be reflected immediately.
  return tonetrace::evaluateCorrectionAt(
             snapshot->uncappedModel,
             snapshot->matchSettings.maximumCorrectionDb,
             snapshot->renderSettings, frequency)
      .automaticDb;
}

double ToneTraceWin32Editor::Impl::bandValueDb(int band) const {
  const double frequency = traceBandFrequency(band);
  if (!frequencyInCorrectionRange(frequency)) return 0.0;
  const double trim = getBandGain_ != nullptr
                          ? getBandGain_(context_, static_cast<std::size_t>(band))
                          : 0.0;
  return matchDbAtBand(band) + trim;
}

std::wstring ToneTraceWin32Editor::Impl::bandValueText(int band) const {
  return L"Band " + std::to_wstring(band + 1) + L", " +
         bandFrequencyText(band) + L": " +
         tonetrace::formatBandValueDbWide(bandValueDb(band));
}

void ToneTraceWin32Editor::Impl::playBandTone(int band) const {
  if (playTone_ == nullptr || band < 0 || band >= traceBandCount()) return;
  const double frequency = traceBandFrequency(band);
  if (frequency <= 0.0) return;
  playTone_(context_, frequency);
}

void ToneTraceWin32Editor::Impl::onBandFocus(int band) {
  playBandTone(band);
  announceBandValue(band);
}

void ToneTraceWin32Editor::Impl::announceBandValue(int band) {
  if (band < 0 || band >= traceBandCount()) return;
  // Do not write the readout or defer a value-change announcement from the
  // band focus/value path. A native Edit raises EVENT_OBJECT_VALUECHANGE
  // whenever SetWindowTextW changes its text, which made the background readout
  // a competing announcer that drowned the fader's own EVENT_OBJECT_FOCUS /
  // EVENT_OBJECT_VALUECHANGE. The readout is still updated (and still announces)
  // for page descriptions, status, and trace navigation elsewhere.
  traceIndex_ = band;
}

void ToneTraceWin32Editor::Impl::playPageSweep(const TracePage& page) const {
  if (playBandSweep_ == nullptr || page.bandCount <= 0) return;
  // Sweep the page's whole frequency window in one smooth log glide. The
  // window spans the band grid's EDGES (the geometric midpoints between this
  // page's first/last band and its neighbours), so consecutive pages meet
  // seamlessly: no frequency is skipped and no step can be heard.
  const int firstBand = page.firstBand;
  const int lastBand = page.firstBand + page.bandCount - 1;
  const double lowEdge =
      firstBand == 0
          ? page.lowHz
          : traceBandFrequency(firstBand) *
                std::sqrt(traceBandFrequency(firstBand - 1) /
                          traceBandFrequency(firstBand));
  const double highEdge =
      lastBand == traceBandCount() - 1
          ? page.highHz
          : traceBandFrequency(lastBand) *
                std::sqrt(traceBandFrequency(lastBand + 1) /
                          traceBandFrequency(lastBand));
  playBandSweep_(context_, lowEdge, highEdge, page.bandCount, 1200.0);
}

void ToneTraceWin32Editor::Impl::showPageDescription(const TracePage& page,
                                                      bool announce) {
  if (readoutEdit_ == nullptr) return;
  wchar_t buffer[256]{};
  std::swprintf(
      buffer, std::size(buffer),
      L"Bands %d-%d, %s to %s. Tab bands; Up/Down 1 dB; "
      L"PageUp/PageDown 6 dB; Home/End limits; 0 sets 0 dB.",
      page.firstBand + 1, page.firstBand + page.bandCount,
      bandFrequencyText(page.firstBand).c_str(),
      bandFrequencyText(page.firstBand + page.bandCount - 1).c_str());
  SetWindowTextW(readoutEdit_, buffer);
  if (announce) armTraceAnnounce();
}

void ToneTraceWin32Editor::Impl::correctionAt(double frequencyHz,
                                              double& matchDb,
                                              double& trimDb,
                                              double& finalDb) const {
  const auto* snapshot =
      getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  matchDb = 0.0;
  trimDb = 0.0;
  finalDb = 0.0;
  if (snapshot == nullptr) return;
  const auto breakdown = tonetrace::evaluateCorrectionAt(
      snapshot->uncappedModel, snapshot->matchSettings.maximumCorrectionDb,
      snapshot->renderSettings, frequencyHz);
  matchDb = breakdown.automaticDb;
  trimDb = breakdown.manualDb;
  finalDb = breakdown.tonalDb;
}

double ToneTraceWin32Editor::Impl::finalCorrectionDb(double frequencyHz) const {
  double match = 0.0;
  double trim = 0.0;
  double final = 0.0;
  correctionAt(frequencyHz, match, trim, final);
  return final;
}

// 0 (and legacy N) on a focused band sets its trim to exactly cancel the match at that
// band, leaving the band flat (0 dB) regardless of what the match produced.
void ToneTraceWin32Editor::Impl::neutralizeBand(int band) {
  if (setBandGain_ == nullptr || band < 0 || band >= traceBandCount()) return;
  const double match = matchDbAtBand(band);
  setBandGain_(context_, static_cast<std::size_t>(band), -match);
  // Update the focused box's text directly: the refresh pass skips the focused
  // control, so its value would otherwise stay stale after neutralizing.
  for (TracePage& page : tracePages_) {
    for (int offset = 0; offset < page.bandCount; ++offset) {
      if (page.firstBand + offset != band) continue;
      if (offset >= static_cast<int>(page.edits.size())) break;
      const HWND edit = page.edits[offset];
      if (edit == nullptr) break;
      SetWindowTextW(edit, bandValueText(band).c_str());
      InvalidateRect(edit, nullptr, TRUE);
      if (GetFocus() == edit) {
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, edit, OBJID_CLIENT,
                       CHILDID_SELF);
      }
      return;
    }
  }
}

// The loudest a band box may read: the correction ceiling the audible kernel
// clamps the match to, so Home can never push a band above what the curve is
// allowed to produce. Falls back to the engine's hard ceiling.
double ToneTraceWin32Editor::Impl::bandRangeDb(bool maximum) const {
  double limit = 60.0;
  const auto* snapshot =
      getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  if (snapshot != nullptr &&
      std::isfinite(snapshot->matchSettings.maximumCorrectionDb) &&
      snapshot->matchSettings.maximumCorrectionDb > 0.0 &&
      snapshot->matchSettings.maximumCorrectionDb <= 60.0) {
    limit = snapshot->matchSettings.maximumCorrectionDb;
  }
  return maximum ? limit : -limit;
}

void ToneTraceWin32Editor::Impl::adjustBandStep(int band, int step) {
  if (band < 0 || band >= traceBandCount()) return;
  setBandValueDb(band,
                 std::clamp(bandValueDb(band) + static_cast<double>(step),
                            bandRangeDb(false), bandRangeDb(true)));
}

void ToneTraceWin32Editor::Impl::bandToExtreme(int band, bool maximum) {
  if (band < 0 || band >= traceBandCount()) return;
  setBandValueDb(band, bandRangeDb(maximum));
}

void ToneTraceWin32Editor::Impl::destroyTracePages() {
  for (TracePage& page : tracePages_) {
    for (HWND control : page.edits) {
      if (control != nullptr) DestroyWindow(control);
    }
    for (HWND label : page.labels) {
      if (label != nullptr) DestroyWindow(label);
    }
  }
  tracePages_.clear();
}

void ToneTraceWin32Editor::Impl::rebuildTraceTabs() {
  const int resolution = traceBandCount();
  if (tabControl_ == nullptr) return;
  const HWND focusedBefore = GetFocus();
  const int focusedId = focusedBefore != nullptr ? GetDlgCtrlID(focusedBefore) : 0;
  const int oldResolution = builtResolution_;
  const bool restoreBandFocus =
      focusedBefore != nullptr && IsChild(window_, focusedBefore) != FALSE &&
      focusedId >= kBandSliderFirstId &&
      focusedId < kBandSliderFirstId + std::max(1, oldResolution);
  int restoredBand = -1;
  if (restoreBandFocus) {
    const int oldBand = focusedId - kBandSliderFirstId;
    restoredBand = oldResolution > 1 && resolution > 1
                       ? static_cast<int>(std::lround(
                             static_cast<double>(oldBand) * (resolution - 1) /
                             (oldResolution - 1)))
                       : 0;
    restoredBand = std::clamp(restoredBand, 0, resolution - 1);
  }
  destroyTracePages();
  builtResolution_ = resolution;

  // One column per band, each at least minBandWidth px wide. The width-derived
  // count is a maximum; pages are then balanced so a small remainder never
  // becomes a nearly empty final tab. Each page remains one contiguous
  // frequency window.
  const RECT canvas = bandCanvasRect();
  const int usableWidth =
      std::max(1, static_cast<int>(canvas.right - canvas.left));
  const int minBandWidth = px(48);
  const int widthCapacity = std::max(1, usableWidth / minBandWidth);
  // Ten bands is the preferred page size: it gives predictable ranges such as
  // 1-10 and 11-20 while leaving enough room for readable value boxes. For an
  // odd total, balancedPageSizes redistributes the remainder instead of
  // creating a one- or two-band final page.
  const std::vector<int> pageSizes =
      tonetrace::toneTraceBandPageSizes(resolution, widthCapacity);
  const int pageCount = static_cast<int>(pageSizes.size());
  int firstBand = 0;

  for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
    TracePage page;
    page.firstBand = firstBand;
    page.bandCount = pageSizes[static_cast<std::size_t>(pageIndex)];
    firstBand += page.bandCount;
    page.lowHz = traceBandFrequency(page.firstBand);
    page.highHz = traceBandFrequency(page.firstBand + page.bandCount - 1);
    for (int offset = 0; offset < page.bandCount; ++offset) {
      const int band = page.firstBand + offset;
      // One custom fader HWND provides both pointer editing and a unit-bearing
      // accessible value (e.g. "Band 3, 500 Hz: +12.0 dB"). It uses MSAA's
      // established dB value path and UIA's string Value pattern so neither
      // screen reader has to infer a percentage. Up/Down step 1 dB,
      // PageUp/PageDown step 6, Home/End go to the curve's extremes, 0 (or N) sets
      // the band to 0 dB.
      tonetrace::win32::AccessibleFaderCallbacks callbacks{};
      callbacks.context = this;
      callbacks.getValue = &Impl::faderGetValue;
      callbacks.getMinimum = &Impl::faderGetMinimum;
      callbacks.getMaximum = &Impl::faderGetMaximum;
      callbacks.setValue = &Impl::faderSetValue;
      callbacks.getName = &Impl::faderGetName;
      callbacks.onFocus = &Impl::faderOnFocus;
      callbacks.toggleTrace = &Impl::faderToggleTrace;
      callbacks.getScale = &Impl::faderGetScale;
      HWND edit = tonetrace::win32::createAccessibleFader(
          window_, moduleInstance(), kBandSliderFirstId + band, band, callbacks);
      if (edit == nullptr) continue;
      SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
      page.edits.push_back(edit);

      HWND label = CreateWindowExW(
          0, L"STATIC", bandFrequencyText(band).c_str(),
          WS_CHILD | WS_VISIBLE | SS_CENTER | SS_NOPREFIX,
          0, 0, 4, 4, window_, nullptr, moduleInstance(), nullptr);
      if (label != nullptr) {
        SendMessageW(label, WM_SETFONT,
                     reinterpret_cast<WPARAM>(smallFont_), FALSE);
        page.labels.push_back(label);
      }
    }
    tracePages_.push_back(std::move(page));
  }

  insertTraceTabs();
  int clamped = std::clamp(selectedPage_, 0, pageCount);
  if (restoredBand >= 0) {
    for (int pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
      const TracePage& page = tracePages_[static_cast<std::size_t>(pageIndex)];
      if (restoredBand >= page.firstBand &&
          restoredBand < page.firstBand + page.bandCount) {
        clamped = pageIndex + 1;
        break;
      }
    }
  }
  selectedPage_ = clamped;
  SendMessageW(tabControl_, TCM_SETCURSEL, static_cast<WPARAM>(clamped), 0);
  layoutBandSliders();
  showTracePage(clamped);
  stackCurrentPage();
  refreshBandSliders();
  if (clamped > 0 && static_cast<std::size_t>(clamped - 1) < tracePages_.size()) {
    const TracePage& page = tracePages_[static_cast<std::size_t>(clamped - 1)];
    showPageDescription(page, false);
    if (restoredBand >= page.firstBand &&
        restoredBand < page.firstBand + page.bandCount) {
      const int offset = restoredBand - page.firstBand;
      if (offset >= 0 && offset < static_cast<int>(page.edits.size())) {
        SetFocus(page.edits[static_cast<std::size_t>(offset)]);
      }
    }
  }
  InvalidateRect(window_, nullptr, FALSE);
}

void ToneTraceWin32Editor::Impl::layoutBandSliders() {
  const RECT canvas = bandCanvasRect();
  const int labelTop = canvas.top + px(18);
  const int labelHeight = px(18);
  const int faderTop = labelTop + labelHeight + px(8);
  const int faderBottom = canvas.bottom - px(14);
  const int faderHeight = std::max(1, faderBottom - faderTop);
  for (TracePage& page : tracePages_) {
    if (page.bandCount <= 0) continue;
    const int width = (canvas.right - canvas.left) / page.bandCount;
    const int faderWidth =
        std::min(px(72), std::max(px(42), width - px(14)));
    for (int index = 0; index < page.bandCount; ++index) {
      const int x = canvas.left + index * width;
      if (index < static_cast<int>(page.edits.size())) {
        SetWindowPos(page.edits[index], nullptr,
                     x + (width - faderWidth) / 2, faderTop,
                     faderWidth, faderHeight,
                     SWP_NOACTIVATE | SWP_NOZORDER);
      }
      if (index < static_cast<int>(page.labels.size())) {
        SetWindowPos(page.labels[index], nullptr, x, labelTop, width,
                     labelHeight, SWP_NOACTIVATE | SWP_NOZORDER);
      }
    }
  }
}

void ToneTraceWin32Editor::Impl::showTracePage(int page) {
  const bool match = page == 0;
  const auto setVisible = [](HWND control, bool visible) {
    if (control != nullptr) {
      ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }
  };
  setVisible(modeLabel_, match);
  setVisible(modeCombo_, match);
  setVisible(resolutionLabel_, !match);
  setVisible(resolutionCombo_, !match);
  // The Trace checkbox stays on every page so the user can switch between
  // hearing band positions and editing band values from any tab.
  setVisible(traceButton_, true);
  setVisible(optionsButton_, match);
  setVisible(statusLabel_, match);
  setVisible(statusEdit_, match);
  // The readout is shown on every page: it announces the trace cursor on the
  // match page and the focused band's value (and page description) on the
  // trace pages.
  setVisible(readoutEdit_, true);
  setVisible(descriptionLabel_, match);
  setVisible(descriptionEdit_, match);
  for (HWND control : workflowButtons_) setVisible(control, match);
  for (HWND control : editControls_) setVisible(control, match);
  for (HWND control : editLabels_) setVisible(control, match);
  for (std::size_t index = 0; index < tracePages_.size(); ++index) {
    const TracePage& tracePage = tracePages_[index];
    const bool visible = page == static_cast<int>(index) + 1;
    for (HWND control : tracePage.edits) setVisible(control, visible);
    for (HWND control : tracePage.labels) setVisible(control, visible);
  }
  layoutChildren();
}

void ToneTraceWin32Editor::Impl::stackCurrentPage() {
  if (selectedPage_ <= 0) {
    setTabOrder();
    return;
  }
  if (static_cast<std::size_t>(selectedPage_ - 1) >= tracePages_.size()) return;
  const TracePage& page = tracePages_[selectedPage_ - 1];
  // Band boxes are stacked in reverse so band order matches Z-order, directly
  // after the tab control in the dialog's tab order.
  for (int index = static_cast<int>(page.edits.size()) - 1; index >= 0;
       --index) {
    SetWindowPos(page.edits[index], HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
  // Bands-page tab order follows the visible layout: page selector,
  // Correction Resolution, Trace Curve, then the individual band faders.
  SetWindowPos(traceButton_, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetWindowPos(resolutionCombo_, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SetWindowPos(resolutionLabel_, HWND_TOP, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (tabControl_ != nullptr) {
    SetWindowPos(tabControl_, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

void ToneTraceWin32Editor::Impl::onTabChanged() {
  const LRESULT selection = SendMessageW(tabControl_, TCM_GETCURSEL, 0, 0);
  const int page = static_cast<int>(selection);
  if (page < 0) return;
  selectedPage_ = page;
  showTracePage(page);
  stackCurrentPage();
  if (page > 0 && static_cast<std::size_t>(page - 1) < tracePages_.size()) {
    const TracePage& tracePage = tracePages_[page - 1];
    // Play the page sweep once per user interaction, even when the dialog
    // re-selection machinery fires the change notification more than once
    // (clicking a tab can reach onTabChanged twice).
    const std::uint64_t now = GetTickCount64();
    if (lastSweepPage_ != page || now - lastSweepTicks_ > 1200) {
      lastSweepPage_ = page;
      lastSweepTicks_ = now;
      playPageSweep(tracePage);
    }
    showPageDescription(tracePage, true);
  }
  InvalidateRect(window_, nullptr, FALSE);
}

void ToneTraceWin32Editor::Impl::onTabFocus() {}

void ToneTraceWin32Editor::Impl::setBandValueDb(int band, double valueDb) {
  if (setBandGain_ == nullptr || !std::isfinite(valueDb)) return;
  if (band < 0 || band >= traceBandCount()) return;
  const double desired = std::clamp(valueDb, bandRangeDb(false), bandRangeDb(true));
  const double match = matchDbAtBand(band);
  const double trim = std::clamp(desired - match, -120.0, 120.0);
  setBandGain_(context_, static_cast<std::size_t>(band), trim);
  for (TracePage& page : tracePages_) {
    for (int offset = 0; offset < page.bandCount; ++offset) {
      if (page.firstBand + offset != band) continue;
      if (offset >= static_cast<int>(page.edits.size())) break;
      tonetrace::win32::syncAccessibleFader(page.edits[offset], true);
      break;
    }
  }
  announceBandValue(band);
}

double ToneTraceWin32Editor::Impl::faderGetValue(void* context, int band) {
  auto* impl = static_cast<Impl*>(context);
  return impl != nullptr ? impl->bandValueDb(band) : 0.0;
}

double ToneTraceWin32Editor::Impl::faderGetMinimum(void* context, int) {
  auto* impl = static_cast<Impl*>(context);
  return impl != nullptr ? impl->bandRangeDb(false) : -60.0;
}

double ToneTraceWin32Editor::Impl::faderGetMaximum(void* context, int) {
  auto* impl = static_cast<Impl*>(context);
  return impl != nullptr ? impl->bandRangeDb(true) : 60.0;
}

void ToneTraceWin32Editor::Impl::faderSetValue(void* context, int band, double value) {
  auto* impl = static_cast<Impl*>(context);
  if (impl != nullptr) impl->setBandValueDb(band, value);
}

std::wstring ToneTraceWin32Editor::Impl::faderGetName(void* context, int band) {
  auto* impl = static_cast<Impl*>(context);
  if (impl == nullptr) return L"Tone Trace band";
  return L"Band " + std::to_wstring(band + 1) + L", " +
         impl->bandFrequencyText(band);
}

void ToneTraceWin32Editor::Impl::faderOnFocus(void* context, int band) {
  auto* impl = static_cast<Impl*>(context);
  if (impl != nullptr) impl->onBandFocus(band);
}

void ToneTraceWin32Editor::Impl::faderToggleTrace(void* context) {
  auto* impl = static_cast<Impl*>(context);
  if (impl != nullptr) impl->toggleTrace();
}

double ToneTraceWin32Editor::Impl::faderGetScale(void* context) {
  auto* impl = static_cast<Impl*>(context);
  return impl != nullptr ? impl->scale_ : 1.0;
}

void ToneTraceWin32Editor::Impl::refreshBandSliders() {
  if (getBandGain_ == nullptr) return;
  // Only refresh the band page that is actually visible. The 33 ms refresh runs
  // every tick, and syncing every fader on every hidden page burns work and was
  // generating accessibility events for bands the user cannot see.
  if (selectedPage_ <= 0) return;
  if (static_cast<std::size_t>(selectedPage_ - 1) >= tracePages_.size()) return;
  const TracePage& page = tracePages_[static_cast<std::size_t>(selectedPage_ - 1)];
  for (HWND fader : page.edits) {
    tonetrace::win32::syncAccessibleFader(fader, false);
  }
}

void ToneTraceWin32Editor::Impl::onCommand(int id, int notificationCode,
                                               HWND source) {
  switch (id) {
    case kCaptureReferenceId:
      if (notificationCode != BN_CLICKED) return;
      setParam_(context_, static_cast<std::uint32_t>(
                              tonetrace::ParameterId::WorkflowAction),
                1.0);
      return;
    case kLearnTargetId:
      if (notificationCode != BN_CLICKED) return;
      setParam_(context_, static_cast<std::uint32_t>(
                              tonetrace::ParameterId::WorkflowAction),
                2.0);
      return;
    case kCorrectTargetId:
      if (notificationCode != BN_CLICKED) return;
      setParam_(context_, static_cast<std::uint32_t>(
                              tonetrace::ParameterId::WorkflowAction),
                3.0);
      return;
    case kFreezeId:
      if (notificationCode != BN_CLICKED) return;
      setParam_(context_, static_cast<std::uint32_t>(
                              tonetrace::ParameterId::WorkflowAction),
                4.0);
      return;
    case kDescribeId:
      if (notificationCode != BN_CLICKED) return;
      copyDescriptionToClipboard();
      return;
    case kTraceId:
      if (notificationCode != BN_CLICKED) return;
      toggleTrace();
      return;
    case kExportId:
      if (notificationCode != BN_CLICKED) return;
      showTransferMenu(true);
      return;
    case kImportId:
      if (notificationCode != BN_CLICKED) return;
      showTransferMenu(false);
      return;
    case kOptionsId:
      if (notificationCode != BN_CLICKED) return;
      showOptionsDialog();
      return;
    default:
      break;
  }
  if (id >= kExportMenuBase && id < kExportMenuBase + 4) {
    exportCurve(id);
    return;
  }
  if (id >= kImportMenuBase && id < kImportMenuBase + 3) {
    importCurve(id);
    return;
  }
  if (id == kModeComboId) {
    if (notificationCode != CBN_SELCHANGE) return;
    const LRESULT selection = SendMessageW(modeCombo_, CB_GETCURSEL, 0, 0);
    if (selection != CB_ERR) {
      setParam_(context_, static_cast<std::uint32_t>(
                              tonetrace::ParameterId::MatchMode),
                static_cast<double>(selection));
    }
    return;
  }
  if (id == kResolutionComboId) {
    if (notificationCode != CBN_SELCHANGE) return;
    const LRESULT selection =
        SendMessageW(resolutionCombo_, CB_GETCURSEL, 0, 0);
    if (selection != CB_ERR) {
      setParam_(context_, static_cast<std::uint32_t>(
                              tonetrace::ParameterId::Resolution),
                static_cast<double>(selection + 1));
      // The native editor parameter path updates the plug-in synchronously.
      // Rebuild here instead of waiting for the 33 ms timer so mouse and
      // keyboard users see the new page labels before focus leaves the combo.
      if (traceBandCount() != builtResolution_) rebuildTraceTabs();
      refreshValues();
      SetFocus(resolutionCombo_);
    }
    return;
  }
  if (id >= kEditFirstId &&
      id < kEditFirstId + static_cast<int>(editControls_.size())) {
    // SetWindowTextW emits EN_UPDATE/EN_CHANGE even when Tone Trace is merely
    // refreshing its own display. Treating those as user edits caused the
    // 33 ms UI timer to resubmit correction parameters indefinitely, which
    // kept the renderer rebuilding and could leave the status stuck on
    // "Correction update still completing" after Freeze. Commit only when
    // the user actually leaves the field.
    if (notificationCode == EN_KILLFOCUS && source != nullptr) {
      applyEdit(id - kEditFirstId);
    }
    return;
  }
}

void ToneTraceWin32Editor::Impl::applyEdit(int editIndex) {
  if (editIndex < 0 || editIndex >= static_cast<int>(std::size(kEditedParams))) {
    return;
  }
  if (editIndex >= static_cast<int>(editControls_.size())) return;
  const tonetrace::ParameterId id = kEditedParams[editIndex];
  if (params_ == nullptr || params_->text_to_value == nullptr) return;
  wchar_t wide[128]{};
  GetWindowTextW(editControls_[editIndex], wide, static_cast<int>(std::size(wide)));
  const std::string text = narrow(wide);
  double value = 0.0;
  if (!params_->text_to_value(plugin_, static_cast<clap_id>(id),
                              text.c_str(), &value)) {
    const std::string current = paramText(id);
    SetWindowTextW(editControls_[editIndex], widen(current).c_str());
    return;
  }
  setParam_(context_, static_cast<std::uint32_t>(id), value);
}

ToneTraceWin32Editor::ToneTraceWin32Editor(
    const clap_plugin_t* plugin, const clap_plugin_params_t* params,
    void* context, GetSnapshotFn getSnapshot,
    GetStagedSpectrumFn getStagedSpectrum, SetParamFn setParam, ResetFn reset,
    PlayToneFn playTone, PlayBandSweepFn playBandSweep,
    SetBandGainFn setBandGain, GetBandGainFn getBandGain,
    GetBandCountFn getBandCount, SetImportedSpectrumFn setImportedSpectrum,
    SetImportedModelFn setImportedModel, GetSampleRateFn getSampleRate,
    RefreshFn refresh)
    : impl_(std::make_unique<Impl>(
          plugin, params, context, getSnapshot, getStagedSpectrum, setParam,
          reset, playTone,
          playBandSweep, setBandGain, getBandGain, getBandCount,
          setImportedSpectrum, setImportedModel, getSampleRate, refresh)) {}

ToneTraceWin32Editor::~ToneTraceWin32Editor() = default;

bool ToneTraceWin32Editor::create() { return impl_->create(); }
void ToneTraceWin32Editor::destroy() { impl_->destroy(); }
bool ToneTraceWin32Editor::setParent(void* window) {
  return impl_->setParent(window);
}
bool ToneTraceWin32Editor::setScale(double scale) {
  return impl_->setScale(scale);
}
bool ToneTraceWin32Editor::getSize(std::uint32_t& width,
                                   std::uint32_t& height) const {
  return impl_->getSize(width, height);
}
bool ToneTraceWin32Editor::setSize(std::uint32_t width,
                                   std::uint32_t height) {
  return impl_->setSize(width, height);
}
bool ToneTraceWin32Editor::adjustSize(std::uint32_t& width,
                                      std::uint32_t& height) const {
  return impl_->adjustSize(width, height);
}
bool ToneTraceWin32Editor::show() { return impl_->show(); }
bool ToneTraceWin32Editor::hide() { return impl_->hide(); }
void ToneTraceWin32Editor::setOfflineRendering(bool offline) {
  impl_->setOfflineRendering(offline);
}
void ToneTraceWin32Editor::unregisterWindowClasses() {
  Impl::unregisterWindowClasses();
}
