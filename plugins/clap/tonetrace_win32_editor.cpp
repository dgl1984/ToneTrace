#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include "tonetrace_win32_editor.h"
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
constexpr int kModeComboId = 200;
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
    tonetrace::ParameterId::CorrectionSharpness,
    tonetrace::ParameterId::RangeLowHz,
    tonetrace::ParameterId::RangeHighHz,
    tonetrace::ParameterId::EmergencyClipGuardDb,
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
  tool.cbSize = sizeof(tool);
  tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
  tool.hwnd = owner;
  tool.uId = reinterpret_cast<UINT_PTR>(control);
  tool.lpszText = const_cast<LPWSTR>(text);
  SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
}

}  // namespace

class ToneTraceWin32Editor::Impl {
 public:
  Impl(const clap_plugin_t* plugin, const clap_plugin_params_t* params,
       void* context, GetSnapshotFn getSnapshot,
       GetStagedSpectrumFn getStagedSpectrum, SetParamFn setParam,
       PlayToneFn playTone, PlayBandSweepFn playBandSweep,
       SetBandGainFn setBandGain, GetBandGainFn getBandGain,
       GetBandCountFn getBandCount, SetImportedSpectrumFn setImportedSpectrum,
       SetImportedModelFn setImportedModel, GetSampleRateFn getSampleRate,
       RefreshFn refresh)
      : plugin_(plugin), params_(params), context_(context),
        getSnapshot_(getSnapshot), getStagedSpectrum_(getStagedSpectrum),
        setParam_(setParam), playTone_(playTone),
        playBandSweep_(playBandSweep), setBandGain_(setBandGain),
        getBandGain_(getBandGain), getBandCount_(getBandCount),
        setImportedSpectrum_(setImportedSpectrum),
        setImportedModel_(setImportedModel), getSampleRate_(getSampleRate),
        refresh_(refresh) {}

  ~Impl() { destroy(); }

  bool create() {
    // The dialog is created in setParent() once the real parent window is
    // known; the CLAP spec calls set_parent before show() for embedded GUIs.
    return true;
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
      SetWindowPos(window_, nullptr, 0, 0, kWidth, kHeight,
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
    scale_ = std::clamp(scale, 0.5, 4.0);
    return true;
  }

  bool getSize(std::uint32_t& width, std::uint32_t& height) const {
    width = static_cast<std::uint32_t>(kWidth);
    height = static_cast<std::uint32_t>(kHeight);
    return true;
  }

  bool setSize(std::uint32_t width, std::uint32_t height) {
    if (window_ == nullptr) return false;
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
    width = std::max<std::uint32_t>(width, 640);
    height = std::max<std::uint32_t>(height, 400);
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
    // No custom window class is registered; dialogs use the system dialog
    // class, so there is nothing to unregister.
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
  static LRESULT CALLBACK bandEditProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam);
  static INT_PTR CALLBACK dialogProc(HWND hwnd, UINT message, WPARAM wParam,
                                     LPARAM lParam);
  POINT editorOrigin() const;
  void focusFirstControl();
  void setTabOrder();
  void setSubclass(HWND control, WNDPROC proc);
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
    const int strip = px(168);
    // The plot starts below the tab strip and the readout row so curves never
    // run underneath them.
    RECT rect{margin, tabStripRect().bottom + margin + px(24) + margin,
              descriptionRect().left - px(8),
              clientHeight() - margin - strip};
    return rect;
  }

  RECT bandCanvasRect() const {
    const int margin = px(10);
    const int strip = px(168);
    // Band-page help is the same concise accessible readout as before, but the
    // native edit is allowed to wrap visually instead of clipping its tail.
    RECT rect{margin, tabStripRect().bottom + margin + px(42) + margin,
              clientWidth() - margin,
              clientHeight() - margin - strip};
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
    const int strip = px(168);
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
  void adjustBand(int band, int position);
  void playBandTone(int band) const;
  void onBandFocus(int band);
  void announceBandValue(int band);
  struct TracePage;
  void playPageSweep(const TracePage& page) const;
  void showPageDescription(const TracePage& page);
  void neutralizeBand(int band);
  void adjustBandStep(int band, int step);
  void bandToExtreme(int band, bool maximum);
  [[nodiscard]] double bandRangeDb(bool maximum) const;
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
  void showTransferMenu(bool exportMenu);
  void exportCurve(int menuId);
  void importCurve(int menuId);
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
  HWND statusEdit_ = nullptr;
  HWND readoutEdit_ = nullptr;
  HWND descriptionEdit_ = nullptr;
  HWND modeCombo_ = nullptr;
  HWND traceButton_ = nullptr;
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
  UINT_PTR traceAnnounceTimer_ = 0;
  double scale_ = 1.0;
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
        RECT panelLabel{descriptionRect().left, px(46), descriptionRect().right,
                        px(62)};
        DrawTextW(dc, L"Curve Description", -1, &panelLabel,
                  DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
      }
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
      if (wParam == kTimerId && !offline_) {
        if (refresh_ != nullptr) refresh_(context_);
        refresh();
      } else if (wParam == kTraceAnnounceTimerId) {
        traceAnnounceTimer_ = 0;
        KillTimer(window_, kTraceAnnounceTimerId);
        if (readoutEdit_ != nullptr && (traceMode_ || selectedPage_ > 0)) {
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
      control == modeCombo_) {
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
        (hwnd == impl->descriptionEdit_ || hwnd == impl->readoutEdit_)) {
      return (code & ~static_cast<LRESULT>(DLGC_WANTALLKEYS)) |
             DLGC_WANTARROWS;
    }
    return code;
  }
  if (message == WM_KEYDOWN) {
    Impl* impl = fromWindow(GetParent(hwnd));
    if (impl != nullptr) {
      const int key = static_cast<int>(wParam);
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
  const bool utility = id == kDescribeId || id == kExportId || id == kImportId;
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

LRESULT CALLBACK ToneTraceWin32Editor::Impl::bandEditProc(
    HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  const WNDPROC original =
      reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  Impl* impl = fromWindow(GetParent(hwnd));
  const int band = static_cast<int>(GetDlgCtrlID(hwnd)) - kBandSliderFirstId;

  // The native control remains a readonly EDIT so MSAA/NVDA sees exactly the
  // same concise band/frequency/value text. Sighted users get a conventional
  // vertical EQ-fader drawing and mouse interaction layered onto that same
  // control; there is no second focus stop or second parameter surface.
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const bool focused = GetFocus() == hwnd;
    const bool hovered = cursorInsideWindow(hwnd);

    HBRUSH background = CreateSolidBrush(focused ? RGB(34, 34, 42)
                                      : hovered ? RGB(30, 33, 40)
                                                : RGB(25, 25, 31));
    FillRect(dc, &rect, background);
    DeleteObject(background);

    const COLORREF outline = focused ? RGB(255, 224, 160)
                             : hovered ? RGB(104, 210, 188)
                                       : RGB(92, 92, 104);
    HPEN outlinePen = CreatePen(PS_SOLID, 1, outline);
    HGDIOBJ oldPen = SelectObject(dc, outlinePen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(outlinePen);

    const auto scaled = [&](int value) { return impl != nullptr ? impl->px(value) : value; };
    const int valueHeight = scaled(24);
    const int trackTop = rect.top + scaled(10);
    const int rectBottom = static_cast<int>(rect.bottom);
    const int trackBottom = std::max(trackTop + 1,
                                     rectBottom - valueHeight - scaled(8));
    const int trackX = (rect.left + rect.right) / 2;

    HPEN trackPen = CreatePen(PS_SOLID, std::max(1, scaled(2)),
                              RGB(112, 112, 124));
    oldPen = SelectObject(dc, trackPen);
    MoveToEx(dc, trackX, trackTop, nullptr);
    LineTo(dc, trackX, trackBottom);
    SelectObject(dc, oldPen);
    DeleteObject(trackPen);

    // A clear center marker makes 0 dB visually obvious, as on a graphic EQ.
    const int zeroY = (trackTop + trackBottom) / 2;
    HPEN zeroPen = CreatePen(PS_SOLID, 1, RGB(150, 150, 164));
    oldPen = SelectObject(dc, zeroPen);
    MoveToEx(dc, rect.left + scaled(6), zeroY, nullptr);
    LineTo(dc, rect.right - scaled(6), zeroY);
    SelectObject(dc, oldPen);
    DeleteObject(zeroPen);

    const double lo = impl != nullptr ? impl->bandRangeDb(false) : -60.0;
    const double hi = impl != nullptr ? impl->bandRangeDb(true) : 60.0;
    const double db = (impl != nullptr && band >= 0)
                          ? impl->bandValueDb(band)
                          : 0.0;
    const double normalized =
        hi > lo ? std::clamp((db - lo) / (hi - lo), 0.0, 1.0) : 0.5;
    const int thumbY = static_cast<int>(std::lround(
        trackBottom - normalized * static_cast<double>(trackBottom - trackTop)));
    HPEN levelPen = CreatePen(PS_SOLID, std::max(1, scaled(3)),
                              db >= 0.0 ? RGB(104, 210, 188)
                                        : RGB(102, 166, 224));
    oldPen = SelectObject(dc, levelPen);
    MoveToEx(dc, trackX, zeroY, nullptr);
    LineTo(dc, trackX, thumbY);
    SelectObject(dc, oldPen);
    DeleteObject(levelPen);

    const int thumbHalfWidth = scaled(13);
    const int thumbHalfHeight = scaled(5);
    RECT thumb{trackX - thumbHalfWidth, thumbY - thumbHalfHeight,
               trackX + thumbHalfWidth, thumbY + thumbHalfHeight};
    HBRUSH thumbBrush = CreateSolidBrush(
        focused ? RGB(255, 240, 210)
                : hovered ? RGB(190, 245, 232) : RGB(226, 226, 232));
    const int thumbSaved = SaveDC(dc);
    SelectObject(dc, thumbBrush);
    SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, thumb.left, thumb.top, thumb.right, thumb.bottom,
              scaled(4), scaled(4));
    RestoreDC(dc, thumbSaved);
    DeleteObject(thumbBrush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, focused ? RGB(255, 240, 210) : RGB(232, 232, 238));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    wchar_t value[32]{};
    std::swprintf(value, std::size(value), L"%+.1f dB", db);
    RECT valueRect{rect.left + 2, rect.bottom - valueHeight, rect.right - 2,
                   rect.bottom - 2};
    DrawTextW(dc, value, -1, &valueRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont != nullptr) SelectObject(dc, oldFont);
    EndPaint(hwnd, &paint);
    return 0;
  }
  if (message == WM_ERASEBKGND) return 1;
  if (message == WM_MOUSEMOVE) {
    TRACKMOUSEEVENT tracking{};
    tracking.cbSize = sizeof(tracking);
    tracking.dwFlags = TME_LEAVE;
    tracking.hwndTrack = hwnd;
    TrackMouseEvent(&tracking);
    if (GetCapture() != hwnd) InvalidateRect(hwnd, nullptr, FALSE);
  } else if (message == WM_MOUSELEAVE) {
    InvalidateRect(hwnd, nullptr, FALSE);
  }
  if (message == WM_SETCURSOR) {
    // IDC_SIZENS follows the build's ANSI/Unicode macro setting. Use the
    // matching generic Win32 entry point rather than forcing LoadCursorW.
    SetCursor(LoadCursor(nullptr, IDC_SIZENS));
    return TRUE;
  }

  auto setFromMouseY = [&](int y) {
    if (impl == nullptr || band < 0 || band >= impl->traceBandCount()) return;
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int valueHeight = impl->px(24);
    const int trackTop = rect.top + impl->px(10);
    const int rectBottom = static_cast<int>(rect.bottom);
    const int trackBottom = std::max(trackTop + 1,
                                     rectBottom - valueHeight - impl->px(8));
    const double lo = impl->bandRangeDb(false);
    const double hi = impl->bandRangeDb(true);
    const double normalized = std::clamp(
        static_cast<double>(trackBottom - y) /
            static_cast<double>(trackBottom - trackTop),
        0.0, 1.0);
    // Mouse editing follows the existing whole-dB keyboard granularity rather
    // than introducing a second precision model just for pointer users.
    const int desired = static_cast<int>(
        std::lround(lo + normalized * (hi - lo)));
    impl->adjustBand(band, desired);
  };

  if (message == WM_LBUTTONDOWN) {
    SetFocus(hwnd);
    SetCapture(hwnd);
    setFromMouseY(GET_Y_LPARAM(lParam));
    return 0;
  }
  if (message == WM_MOUSEMOVE && GetCapture() == hwnd &&
      (wParam & MK_LBUTTON) != 0) {
    setFromMouseY(GET_Y_LPARAM(lParam));
    return 0;
  }
  if (message == WM_LBUTTONUP && GetCapture() == hwnd) {
    setFromMouseY(GET_Y_LPARAM(lParam));
    ReleaseCapture();
    return 0;
  }
  if (message == WM_LBUTTONDBLCLK) {
    if (impl != nullptr && band >= 0) impl->neutralizeBand(band);
    return 0;
  }
  if (message == WM_MOUSEWHEEL && impl != nullptr && band >= 0) {
    SetFocus(hwnd);
    const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
    if (steps != 0) impl->adjustBandStep(band, steps);
    return 0;
  }

  if (message == WM_SETFOCUS) {
    if (impl != nullptr && band >= 0) impl->onBandFocus(band);
    InvalidateRect(hwnd, nullptr, TRUE);
  } else if (message == WM_KILLFOCUS) {
    if (GetCapture() == hwnd) ReleaseCapture();
    InvalidateRect(hwnd, nullptr, TRUE);
  }
  if (message == WM_GETDLGCODE) {
    const LRESULT code =
        CallWindowProcW(original, hwnd, message, wParam, lParam);
    return code | DLGC_WANTARROWS;
  }
  if (message == WM_KEYDOWN && impl != nullptr) {
    const int key = static_cast<int>(wParam);
    if (key == 'T' || key == VK_F2) {
      impl->toggleTrace();
      return 0;
    }
    if (band >= 0) {
      if (key == '0' || key == VK_NUMPAD0 || key == 'N') {
        impl->neutralizeBand(band);
        return 0;
      }
      int step = 0;
      if (key == VK_UP || key == VK_RIGHT) {
        step = 1;
      } else if (key == VK_DOWN || key == VK_LEFT) {
        step = -1;
      } else if (key == VK_PRIOR) {
        step = 6;
      } else if (key == VK_NEXT) {
        step = -6;
      }
      if (step != 0) {
        impl->adjustBandStep(band, step);
        return 0;
      }
      if (key == VK_HOME) {
        impl->bandToExtreme(band, true);
        return 0;
      }
      if (key == VK_END) {
        impl->bandToExtreme(band, false);
        return 0;
      }
    }
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
  // Desired tab order (first to last):
  //   Capture Reference, Learn Target, Correct Target, Freeze Correction,
  //   Match Mode, Trace Curve, labeled edits, Status, Readout, Description,
  //   Copy Curve Description.
  // The copy button sits directly after the box it copies, so a keyboard user
  // can read the description then copy it without hunting.
  // workflowButtons_ creation order is 0=Capture, 1=Learn, 2=Correct,
  // 3=Freeze, 4=Copy, 5=Export, 6=Import. Each label is placed directly above
  // its edit so screen readers associate it with the control.
  auto topWorkflow = [&](std::size_t index) {
    if (index < workflowButtons_.size()) moveTop(workflowButtons_[index]);
  };
  topWorkflow(6);  // Import Curve (last stop)
  topWorkflow(5);  // Export Curve
  topWorkflow(4);  // Copy Curve Description
  moveTop(descriptionEdit_);
  moveTop(readoutEdit_);
  moveTop(statusEdit_);
  for (int index = static_cast<int>(editControls_.size()) - 1; index >= 0;
       --index) {
    moveTop(editControls_[index]);
    if (index < static_cast<int>(editLabels_.size())) {
      moveTop(editLabels_[index]);
    }
  }
  moveTop(traceButton_);
  moveTop(modeCombo_);
  topWorkflow(3);  // Freeze
  topWorkflow(2);  // Correct
  topWorkflow(1);  // Learn
  topWorkflow(0);  // Capture Reference (first stop)
  // The tab strip comes before every page control so a keyboard user reaches
  // the page selector first, then the page's controls.
  moveTop(tabControl_);
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
  font_ = CreateFontW(-px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  titleFont_ = CreateFontW(-px(22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
  smallFont_ = CreateFontW(-px(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

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
      tool.cbSize = sizeof(tool);
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

  statusEdit_ = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL,
      0, 0, 4, 4, window_, nullptr, instance, nullptr);
  if (statusEdit_ != nullptr) {
    SendMessageW(statusEdit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font_), FALSE);
    setSubclass(statusEdit_, &Impl::keyForwardProc);
  }

  readoutEdit_ = CreateWindowExW(
      0, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_MULTILINE,
      0, 0, 4, 4, window_, nullptr, instance, nullptr);
  if (readoutEdit_ != nullptr) {
    SendMessageW(readoutEdit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(font_), FALSE);
    setSubclass(readoutEdit_, &Impl::keyForwardProc);
  }

  descriptionEdit_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_READONLY | ES_MULTILINE |
          ES_AUTOVSCROLL | WS_VSCROLL,
      0, 0, 4, 4, window_, nullptr, instance, nullptr);
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
  const bool compactLabels = tracePages_.size() >= 9;
  for (std::size_t index = 0; index < tracePages_.size(); ++index) {
    const TracePage& page = tracePages_[index];
    // The full "Bands N-M, F-F Hz" range is announced in the readout when the
    // page is selected. At very high resolutions the strip uses just N-M so
    // all tabs remain readable without scroll arrows.
    std::wstring label = compactLabels ? L"" : L"Bands ";
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
    // The copy caption is materially longer than the four workflow captions;
    // Export/Import are much shorter. Give the labels the space they actually
    // need instead of clipping Copy Curve Description in an equal-width row.
    if (id == kDescribeId) {
      buttonWeights[static_cast<std::size_t>(index)] = 1.25;
    } else if (id == kExportId || id == kImportId) {
      buttonWeights[static_cast<std::size_t>(index)] = 0.85;
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
  SetWindowPos(modeCombo_, nullptr, margin, comboY, px(220), px(220),
               SWP_NOACTIVATE | SWP_NOZORDER);

  const int editStartX = margin + px(236);
  const int editWidth = std::max(
      px(64), (width - editStartX - margin - (static_cast<int>(editControls_.size()) - 1) * px(6)) /
                  static_cast<int>(editControls_.size()));
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

  const int statusY = bottom - px(156);
  SetWindowPos(statusEdit_, nullptr, margin, statusY,
               std::max(px(200), width - margin * 2), px(24),
               SWP_NOACTIVATE | SWP_NOZORDER);

  const RECT description = descriptionRect();
  SetWindowPos(descriptionEdit_, nullptr, description.left, description.top,
               description.right - description.left,
               description.bottom - description.top,
               SWP_NOACTIVATE | SWP_NOZORDER);

  // Keep Trace Curve beside the always-visible readout rather than leaving a
  // checkbox floating in the lower-right corner of band pages. BS_PUSHLIKE
  // gives the toggle the same readable themed treatment as the other buttons.
  const int readoutY = tabStrip.bottom + margin;
  const int traceWidth = px(126);
  const int readoutRight = width - margin;
  const int readoutWidth = std::max(
      px(200), readoutRight - margin - px(8) - traceWidth);
  const int readoutHeight = selectedPage_ > 0 ? px(42) : px(24);
  SetWindowPos(readoutEdit_, nullptr, margin, readoutY, readoutWidth,
               readoutHeight, SWP_NOACTIVATE | SWP_NOZORDER);
  SetWindowPos(traceButton_, nullptr, margin + readoutWidth + px(8), readoutY,
               traceWidth, px(24), SWP_NOACTIVATE | SWP_NOZORDER);

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
    RECT text{x + px(3), bounds.top + px(2), x + px(44),
              bounds.top + px(16)};
    DrawTextW(dc, label, -1, &text, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
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
  RECT rangeText{plot.right - px(72), plot.top + px(3), plot.right - px(6),
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
  if (modeCombo_ == nullptr) return;
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
  std::wstring text = widen(status);
  if (traceMode_) {
    if (!text.empty()) text += L"  |  ";
    text += L"Trace: ON - use Left/Right arrows to move bands, Home/End for the edges";
  }
  if (!command.empty() && command != "No action") {
    text += L"  |  Last: ";
    text += widen(command);
  }
  (void)setWindowTextIfChanged(statusEdit_, text);
}

void ToneTraceWin32Editor::Impl::refreshDescription() {
  if (descriptionEdit_ == nullptr) return;
  const auto* snapshot = getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  std::wstring text;
  if (snapshot != nullptr) {
    text = win32MultilineText(widen(tonetrace::curveDescriptionText(*snapshot)));
  } else {
    text = L"Tone Trace summary\r\n\r\nNo captures yet. Capture Reference to begin.";
  }
  if (text == lastDescriptionText_) return;
  lastDescriptionText_ = text;
  SetWindowTextW(descriptionEdit_, text.c_str());
  NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, descriptionEdit_, OBJID_CLIENT,
                 CHILDID_SELF);
}

void ToneTraceWin32Editor::Impl::copyDescriptionToClipboard() {
  if (descriptionEdit_ == nullptr) return;
  wchar_t buffer[4096]{};
  const int length = GetWindowTextW(
      descriptionEdit_, buffer, static_cast<int>(std::size(buffer)));
  if (length <= 0) return;
  if (!OpenClipboard(window_)) return;
  const bool emptied = EmptyClipboard() != 0;
  if (emptied) {
    const SIZE_T bytes = (static_cast<SIZE_T>(length) + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
      void* data = GlobalLock(memory);
      if (data != nullptr) {
        std::memcpy(data, buffer, bytes);
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
  const std::size_t anchorIndex = exportMenu ? 5 : 6;
  if (anchorIndex >= workflowButtons_.size()) {
    DestroyMenu(menu);
    return;
  }
  const HWND anchor = workflowButtons_[anchorIndex];
  RECT rect{};
  if (anchor != nullptr && GetWindowRect(anchor, &rect)) {
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   rect.left, rect.bottom, 0, window_, nullptr);
  }
  DestroyMenu(menu);
}

void ToneTraceWin32Editor::Impl::exportCurve(int menuId) {
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
  const auto* snapshot =
      getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  const auto stagedSpectrum = [&](int which)
      -> const tonetrace::SpectrumCapture* {
    return getStagedSpectrum_ != nullptr
               ? getStagedSpectrum_(context_, which)
               : nullptr;
  };
  if (snapshot == nullptr &&
      !((menuId == kExportReference && stagedSpectrum(1) != nullptr) ||
        (menuId == kExportTarget && stagedSpectrum(2) != nullptr))) {
    MessageBoxW(window_, L"No matching curve is available to export yet.",
                L"Tone Trace EQ", MB_OK | MB_ICONINFORMATION);
    return;
  }
  try {
    if (menuId == kExportIr) {
      const std::vector<double> kernel =
          tonetrace::renderProfileKernel(*snapshot);
      tonetrace::AudioBuffer audio;
      audio.sampleRate = snapshot->renderSettings.sampleRate;
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
  if (snapshot == nullptr) {
    SetWindowTextW(readoutEdit_, L"No tone trace loaded.");
    return;
  }
  if (traceMode_ || selectedPage_ > 0) {
    const int count = traceBandCount();
    const double frequency = traceFrequency();
    double match = 0.0;
    double trim = 0.0;
    double final = 0.0;
    correctionAt(frequency, match, trim, final);
    wchar_t buffer[160]{};
    std::swprintf(buffer, std::size(buffer),
                  L"Band %d of %d, %d Hz, Final %+.1f dB (match %+.1f, trim %+.1f)",
                  traceIndex_ + 1, count,
                  static_cast<int>(std::lround(frequency)), final, match, trim);
    SetWindowTextW(readoutEdit_, buffer);
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
  std::swprintf(buffer, std::size(buffer),
                L"%d Hz  |  Reference %.1f dB  |  Target %.1f dB  |  Correction %+.1f dB",
                static_cast<int>(frequency), reference, target, correction);
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
  const auto* snapshot =
      getSnapshot_ != nullptr ? getSnapshot_(context_) : nullptr;
  if (snapshot == nullptr || !std::isfinite(frequencyHz)) return false;
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
  return formatFrequency(traceBandFrequency(band));
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
  wchar_t buffer[96]{};
  std::swprintf(buffer, std::size(buffer),
                L"Band %d, %s: %+.1f dB", band + 1,
                bandFrequencyText(band).c_str(), bandValueDb(band));
  return buffer;
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
  traceIndex_ = band;
  updateReadout();
  armTraceAnnounce();
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

void ToneTraceWin32Editor::Impl::showPageDescription(const TracePage& page) {
  if (readoutEdit_ == nullptr) return;
  wchar_t buffer[256]{};
  std::swprintf(
      buffer, std::size(buffer),
      L"Bands %d-%d, %s to %s. Tab bands; Up/Down 1 dB; "
      L"PageUp/PageDown 6 dB; Home/End limits; 0 sets 0 dB.",
      page.firstBand + 1, page.firstBand + page.bandCount,
      formatFrequency(page.lowHz).c_str(), formatFrequency(page.highHz).c_str());
  SetWindowTextW(readoutEdit_, buffer);
  armTraceAnnounce();
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
  const int position = static_cast<int>(std::lround(bandValueDb(band)));
  const int lo = static_cast<int>(bandRangeDb(false));
  const int hi = static_cast<int>(bandRangeDb(true));
  adjustBand(band, std::clamp(position + step, lo, hi));
}

void ToneTraceWin32Editor::Impl::bandToExtreme(int band, bool maximum) {
  if (band < 0 || band >= traceBandCount()) return;
  adjustBand(band, static_cast<int>(bandRangeDb(maximum)));
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
      // A read-only edit whose text IS the band's dB value. Unlike a trackbar
      // (which NVDA reports as a percentage of its -120..120 range), the edit
      // announces the final level (e.g. "Band 3, 500 Hz: +12.0 dB") on focus,
      // and the subclass makes it act like a slider: Up/Down step 1 dB,
      // PageUp/PageDown step 6, Home/End go to the curve's extremes, 0 (or N) sets
      // the band to 0 dB.
      HWND edit = CreateWindowExW(
          0, L"EDIT", bandValueText(band).c_str(),
          WS_CHILD | WS_TABSTOP | ES_READONLY | WS_BORDER,
          0, 0, 4, 4, window_,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(
              kBandSliderFirstId + band)),
          moduleInstance(), nullptr);
      if (edit == nullptr) continue;
      SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
      setSubclass(edit, &Impl::bandEditProc);
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
  const int clamped = std::clamp(selectedPage_, 0, pageCount);
  selectedPage_ = clamped;
  SendMessageW(tabControl_, TCM_SETCURSEL, static_cast<WPARAM>(clamped), 0);
  layoutBandSliders();
  showTracePage(clamped);
  stackCurrentPage();
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
  setVisible(modeCombo_, match);
  // The Trace checkbox stays on every page so the user can switch between
  // hearing band positions and editing band values from any tab.
  setVisible(traceButton_, true);
  setVisible(statusEdit_, match);
  // The readout is shown on every page: it announces the trace cursor on the
  // match page and the focused band's value (and page description) on the
  // trace pages.
  setVisible(readoutEdit_, true);
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
    showPageDescription(tracePage);
  }
  InvalidateRect(window_, nullptr, FALSE);
}

void ToneTraceWin32Editor::Impl::onTabFocus() {}

void ToneTraceWin32Editor::Impl::adjustBand(int band, int position) {
  if (setBandGain_ == nullptr) return;
  if (band < 0 || band >= traceBandCount()) return;
  // position is the desired FINAL band value (what the box shows). The engine
  // stores the trim — the delta on top of the auto match — so convert before
  // storing; the box then reads back exactly what the user set.
  const double match = matchDbAtBand(band);
  const double trim =
      std::clamp(static_cast<double>(position) - match, -120.0, 120.0);
  setBandGain_(context_, static_cast<std::size_t>(band), trim);
  // Refresh the focused box's text and announce the new value so a screen
  // reader hears the real dB change, not a trackbar percentage.
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
      break;
    }
  }
  announceBandValue(band);
}

void ToneTraceWin32Editor::Impl::refreshBandSliders() {
  if (getBandGain_ == nullptr) return;
  for (TracePage& page : tracePages_) {
    for (int offset = 0; offset < page.bandCount; ++offset) {
      if (offset >= static_cast<int>(page.edits.size())) break;
      const int band = page.firstBand + offset;
      const HWND edit = page.edits[offset];
      // Never fight the user while a box is focused; the subclass refreshes it
      // itself after each edit.
      if (GetFocus() == edit) continue;
      if (setWindowTextIfChanged(edit, bandValueText(band))) {
        InvalidateRect(edit, nullptr, TRUE);
      }
    }
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
    GetStagedSpectrumFn getStagedSpectrum, SetParamFn setParam,
    PlayToneFn playTone, PlayBandSweepFn playBandSweep,
    SetBandGainFn setBandGain, GetBandGainFn getBandGain,
    GetBandCountFn getBandCount, SetImportedSpectrumFn setImportedSpectrum,
    SetImportedModelFn setImportedModel, GetSampleRateFn getSampleRate,
    RefreshFn refresh)
    : impl_(std::make_unique<Impl>(
          plugin, params, context, getSnapshot, getStagedSpectrum, setParam,
          playTone,
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
