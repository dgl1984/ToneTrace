#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <clap/clap.h>
#include <windows.h>
#include <commctrl.h>
#include <oleacc.h>
#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int kTabControlId = 50;
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
constexpr int kBandSliderFirstId = 2000;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct DynamicLibrary {
  HMODULE handle = nullptr;
  explicit DynamicLibrary(const char* path) {
    handle = LoadLibraryA(path);
    require(handle != nullptr, "could not load CLAP module");
  }
  ~DynamicLibrary() {
    if (handle != nullptr) FreeLibrary(handle);
  }
  template <typename T>
  T symbol(const char* name) const {
    return reinterpret_cast<T>(GetProcAddress(handle, name));
  }
};

struct HostState {
  clap_host_t host{};
  HostState() {
    host.clap_version = CLAP_VERSION;
    host.host_data = this;
    host.name = "Tone Trace neutral Win32 UI harness";
    host.vendor = "LanesAudio tests";
    host.url = "https://lanesaudio.com";
    host.version = "1";
    host.get_extension = getExtension;
    host.request_restart = requestRestart;
    host.request_process = requestProcess;
    host.request_callback = requestCallback;
  }
  static const void* CLAP_ABI getExtension(const clap_host_t*, const char*) {
    return nullptr;
  }
  static void CLAP_ABI requestRestart(const clap_host_t*) {}
  static void CLAP_ABI requestProcess(const clap_host_t*) {}
  static void CLAP_ABI requestCallback(const clap_host_t*) {}
};

struct PluginInstance {
  const clap_plugin_t* plugin = nullptr;
  explicit PluginInstance(const clap_plugin_t* p) : plugin(p) {
    require(plugin != nullptr && plugin->init(plugin), "plugin init failed");
  }
  ~PluginInstance() {
    if (plugin != nullptr) plugin->destroy(plugin);
  }
};

void pumpMessages() {
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

HWND findEditor(HWND root) {
  HWND result = nullptr;
  EnumChildWindows(
      root,
      [](HWND child, LPARAM context) -> BOOL {
        if (GetDlgItem(child, kTabControlId) != nullptr &&
            GetDlgItem(child, kCaptureReferenceId) != nullptr) {
          *reinterpret_cast<HWND*>(context) = child;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
  return result;
}

std::wstring textOf(HWND window) {
  const int length = window ? GetWindowTextLengthW(window) : 0;
  std::wstring text(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
  const int copied = window ? GetWindowTextW(
      window, text.data(), static_cast<int>(text.size())) : 0;
  text.resize(static_cast<std::size_t>(std::max(0, copied)));
  return text;
}

bool neutralText(const std::wstring& text) {
  return text.find(L": +0.0 dB") != std::wstring::npos ||
         text.find(L": -0.0 dB") != std::wstring::npos;
}

// Band text ends with ": +N.N dB". Parse that signed value so a test can pin
// the exact step a gesture produced instead of only "something changed".
double parseBandDb(const std::wstring& text) {
  const std::size_t db = text.rfind(L" dB");
  require(db != std::wstring::npos, "band text carries no dB value");
  std::size_t start = db;
  while (start > 0) {
    const wchar_t ch = text[start - 1];
    if (std::iswdigit(ch) || ch == L'.' || ch == L'+' || ch == L'-') {
      --start;
    } else {
      break;
    }
  }
  require(start < db, "band text has an empty dB number");
  return std::stod(text.substr(start, db - start));
}

void selectPage(HWND editor, int index) {
  HWND tabs = GetDlgItem(editor, kTabControlId);
  require(tabs != nullptr, "tab control missing");
  require(SendMessageW(tabs, TCM_SETCURSEL, index, 0) >= 0,
          "could not select tab");

  NMHDR header{};
  header.hwndFrom = tabs;
  header.idFrom = kTabControlId;
  header.code = TCN_SELCHANGE;
  SendMessageW(editor, WM_NOTIFY, kTabControlId,
               reinterpret_cast<LPARAM>(&header));
  pumpMessages();
}

void requireNextTab(HWND dialog, HWND from, bool previous,
                    int expectedId, const char* description) {
  HWND next = GetNextDlgTabItem(dialog, from, previous ? TRUE : FALSE);
  require(next != nullptr, std::string(description) + ": no next tab item");
  require(GetDlgCtrlID(next) == expectedId,
          std::string(description) + ": expected id " +
          std::to_string(expectedId) + ", got " +
          std::to_string(GetDlgCtrlID(next)));
}

// Bootstrap a committed correction the same way a real host session does, so
// band fader tests exercise the actual match+trim display path.
struct HostInputEvents {
  clap_input_events_t iface{};
  std::vector<clap_event_param_value_t> events;
  HostInputEvents() {
    iface.ctx = this;
    iface.size = [](const clap_input_events_t* list) -> uint32_t {
      return static_cast<uint32_t>(
          static_cast<const HostInputEvents*>(list->ctx)->events.size());
    };
    iface.get = [](const clap_input_events_t* list,
                   uint32_t index) -> const clap_event_header_t* {
      const auto& events =
          static_cast<const HostInputEvents*>(list->ctx)->events;
      return index < events.size() ? &events[index].header : nullptr;
    };
  }
  void add(clap_id id, double value) {
    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = CLAP_EVENT_PARAM_VALUE;
    event.param_id = id;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = value;
    events.push_back(event);
  }
};

void feedSeconds(const clap_plugin_t* plugin, double seconds,
                 int command, double seed) {
  const uint32_t frames = 256;
  const uint32_t total = static_cast<uint32_t>(48000.0 * seconds);
  uint32_t position = 0;
  std::vector<float> left(frames, 0.0F);
  std::vector<float> right(frames, 0.0F);
  std::vector<float> outLeft(frames, 0.0F);
  std::vector<float> outRight(frames, 0.0F);
  float* inPtrs[2]{left.data(), right.data()};
  float* outPtrs[2]{outLeft.data(), outRight.data()};
  clap_audio_buffer_t inBuf{};
  inBuf.channel_count = 2;
  inBuf.data32 = inPtrs;
  clap_audio_buffer_t outBuf{};
  outBuf.channel_count = 2;
  outBuf.data32 = outPtrs;
  HostInputEvents input;
  while (position < total) {
    const uint32_t count = std::min(frames, total - position);
    if (position == 0 && command != 0) {
      input.events.clear();
      input.add(100, command);
    }
    for (uint32_t frame = 0; frame < count; ++frame) {
      const double t = static_cast<double>(position + frame) / 48000.0;
      const double value = 0.18 * (std::sin(6.2831853 * 220.0 * t + seed) +
                                   0.4 * std::sin(6.2831853 * 1700.0 * t) +
                                   0.05 * std::sin(6.2831853 * 7300.0 * t));
      left[frame] = static_cast<float>(value);
      right[frame] = static_cast<float>(value * 0.7 +
                                        0.05 * std::sin(6.2831853 * 55.0 * t));
    }
    clap_process_t process{};
    process.frames_count = count;
    process.audio_inputs = &inBuf;
    process.audio_outputs = &outBuf;
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;
    process.in_events = input.events.empty() ? nullptr : &input.iface;
    require(plugin->process(plugin, &process) == CLAP_PROCESS_CONTINUE,
            "ui harness audio process failed");
    input.events.clear();
    position += count;
  }
}

void commitCorrectionProfile(const clap_plugin_t* plugin) {
  require(plugin->start_processing(plugin), "ui harness start failed");
  feedSeconds(plugin, 2.4, 1, 0.0);   // Capture Reference
  feedSeconds(plugin, 2.4, 2, 1.7);   // Learn Target
  feedSeconds(plugin, 0.2, 3, 0.0);   // Correct Target schedules analysis
  plugin->on_main_thread(plugin);
  pumpMessages();
  const auto* params = static_cast<const clap_plugin_params_t*>(
      plugin->get_extension(plugin, CLAP_EXT_PARAMS));
  double status = -1.0;
  params->get_value(plugin, 230, &status);
  require(status == 4.0 || status == 5.0,
          "ui harness could not reach a committed correction (status=" +
              std::to_string(static_cast<int>(status)) + ")");
}

void verifyKnownGoodKeyboardBaseline(HWND editor) {
  selectPage(editor, 0);

  HWND tabs = GetDlgItem(editor, kTabControlId);
  HWND capture = GetDlgItem(editor, kCaptureReferenceId);
  HWND learn = GetDlgItem(editor, kLearnTargetId);
  HWND correct = GetDlgItem(editor, kCorrectTargetId);
  HWND freeze = GetDlgItem(editor, kFreezeId);
  HWND mode = GetDlgItem(editor, kModeComboId);
  HWND trace = GetDlgItem(editor, kTraceId);

  require(tabs && capture && learn && correct && freeze && mode && trace,
          "Match-page keyboard controls are incomplete");

  requireNextTab(editor, tabs, false, kCaptureReferenceId, "tabs -> Capture");
  requireNextTab(editor, capture, false, kLearnTargetId, "Capture -> Learn");
  requireNextTab(editor, learn, false, kCorrectTargetId, "Learn -> Correct");
  requireNextTab(editor, correct, false, kFreezeId, "Correct -> Freeze");
  requireNextTab(editor, freeze, false, kModeComboId, "Freeze -> Match Mode");
  requireNextTab(editor, mode, false, kTraceId, "Match Mode -> Trace");
  requireNextTab(editor, trace, false, kEditFirstId, "Trace -> first value");

  requireNextTab(editor, capture, true, kTabControlId,
                 "Shift+Tab Capture -> tabs");
  requireNextTab(editor, mode, true, kFreezeId,
                 "Shift+Tab Match Mode -> Freeze");

  for (int id = kEditFirstId; id < kEditFirstId + 7; ++id) {
    HWND edit = GetDlgItem(editor, id);
    require(edit != nullptr, "Match exact-value edit missing");
    if (id < kEditFirstId + 6) {
      requireNextTab(editor, edit, false, id + 1, "value edit ordering");
    }
  }

  std::vector<HWND> readonlyMultiline;
  EnumChildWindows(
      editor,
      [](HWND child, LPARAM context) -> BOOL {
        wchar_t cls[32]{};
        GetClassNameW(child, cls, static_cast<int>(std::size(cls)));
        const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
        if (_wcsicmp(cls, L"Edit") == 0 &&
            (style & ES_READONLY) != 0 &&
            (style & ES_MULTILINE) != 0) {
          reinterpret_cast<std::vector<HWND>*>(context)->push_back(child);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&readonlyMultiline));

  for (HWND edit : readonlyMultiline) {
    const LRESULT code = SendMessageW(edit, WM_GETDLGCODE, 0, 0);
    require((code & DLGC_WANTALLKEYS) == 0,
            "readonly multiline edit can trap Tab");
  }
}

void verifyBandAccessibility(HWND fader) {
  IAccessible* accessible = nullptr;
  HRESULT hr = AccessibleObjectFromWindow(
      fader, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
      reinterpret_cast<void**>(&accessible));
  require(SUCCEEDED(hr) && accessible != nullptr,
          "custom band fader did not expose MSAA/IAccessible");

  VARIANT self{};
  self.vt = VT_I4;
  self.lVal = CHILDID_SELF;
  VARIANT role{};
  require(SUCCEEDED(accessible->get_accRole(self, &role)) &&
              role.vt == VT_I4 && role.lVal == ROLE_SYSTEM_SLIDER,
          "MSAA band role is not Slider");
  VariantClear(&role);

  BSTR name = nullptr;
  require(SUCCEEDED(accessible->get_accName(self, &name)) && name != nullptr &&
              std::wstring(name).find(L"Band 1") != std::wstring::npos,
          "MSAA band name does not identify the band");
  SysFreeString(name);

  BSTR value = nullptr;
  require(SUCCEEDED(accessible->get_accValue(self, &value)) && value != nullptr &&
              std::wstring(value).find(L"dB") != std::wstring::npos,
          "MSAA band value does not expose dB");
  SysFreeString(value);
  accessible->Release();

  IUIAutomation* automation = nullptr;
  hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                        __uuidof(IUIAutomation),
                        reinterpret_cast<void**>(&automation));
  require(SUCCEEDED(hr) && automation != nullptr,
          "UI Automation client could not be created");

  IUIAutomationElement* element = nullptr;
  hr = automation->ElementFromHandle(fader, &element);
  require(SUCCEEDED(hr) && element != nullptr,
          "UI Automation could not retrieve the band fader element");

  CONTROLTYPEID controlType = 0;
  require(SUCCEEDED(element->get_CurrentControlType(&controlType)) &&
              controlType == UIA_SliderControlTypeId,
          "UI Automation band control type is not Slider");

  IUnknown* unknownPattern = nullptr;
  require(SUCCEEDED(element->GetCurrentPattern(UIA_RangeValuePatternId,
                                               &unknownPattern)) &&
              unknownPattern != nullptr,
          "UI Automation band does not expose RangeValue");
  IUIAutomationRangeValuePattern* range = nullptr;
  require(SUCCEEDED(unknownPattern->QueryInterface(
              __uuidof(IUIAutomationRangeValuePattern),
              reinterpret_cast<void**>(&range))) && range != nullptr,
          "UI Automation RangeValue pattern could not be queried");
  double current = 0.0;
  double smallChange = 0.0;
  double largeChange = 0.0;
  require(SUCCEEDED(range->get_CurrentValue(&current)) &&
              SUCCEEDED(range->get_CurrentSmallChange(&smallChange)) &&
              SUCCEEDED(range->get_CurrentLargeChange(&largeChange)) &&
              std::abs(smallChange - 1.0) < 1.0e-9 &&
              std::abs(largeChange - 6.0) < 1.0e-9,
          "UI Automation RangeValue step metadata is wrong");
  range->Release();
  unknownPattern->Release();
  element->Release();
  automation->Release();
}

void verifyManualBandBeforeProfile(HWND editor) {
  selectPage(editor, 1);
  HWND first = GetDlgItem(editor, kBandSliderFirstId);
  require(first != nullptr && IsWindowVisible(first) != FALSE,
          "manual-EQ pre-profile band is unavailable");

  SetFocus(first);
  require(neutralText(textOf(first)),
          "fresh manual-EQ band is not neutral before matching");
  SendMessageW(first, WM_KEYDOWN, VK_UP, 0);
  require(std::abs(parseBandDb(textOf(first)) - 1.0) < 1.0e-9,
          "band cannot be edited before a learned correction exists");

  IAccessible* accessible = nullptr;
  const HRESULT hr = AccessibleObjectFromWindow(
      first, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
      reinterpret_cast<void**>(&accessible));
  require(SUCCEEDED(hr) && accessible != nullptr,
          "pre-profile fader accessibility object is unavailable");
  VARIANT self{};
  self.vt = VT_I4;
  self.lVal = CHILDID_SELF;
  BSTR value = nullptr;
  require(SUCCEEDED(accessible->get_accValue(self, &value)) && value != nullptr &&
              std::wstring(value).find(L"+1.0 dB") != std::wstring::npos,
          "pre-profile accessible band value did not follow the edit");
  SysFreeString(value);
  accessible->Release();

  SendMessageW(first, WM_KEYDOWN, '0', 0);
  require(neutralText(textOf(first)),
          "pre-profile band did not return to neutral");
}

void verifyBandMouseAndKeyboard(HWND editor) {
  selectPage(editor, 1);

  HWND tabs = GetDlgItem(editor, kTabControlId);
  HWND first = GetDlgItem(editor, kBandSliderFirstId);
  HWND second = GetDlgItem(editor, kBandSliderFirstId + 1);
  require(tabs && first && second, "first Bands-page controls missing");
  require(IsWindowVisible(first) != FALSE, "first band is not visible");

  wchar_t cls[64]{};
  GetClassNameW(first, cls, static_cast<int>(std::size(cls)));
  require(_wcsicmp(cls, L"ToneTraceAccessibleFader") == 0,
          "band control is not the dedicated accessible fader class");
  verifyBandAccessibility(first);

  requireNextTab(editor, tabs, false, kBandSliderFirstId,
                 "Bands tab -> first band");
  requireNextTab(editor, first, false, kBandSliderFirstId + 1,
                 "first band -> second band");
  requireNextTab(editor, first, true, kTabControlId,
                 "Shift+Tab first band -> tabs");

  SetFocus(first);
  std::wstring before = textOf(first);
  SendMessageW(first, WM_KEYDOWN, VK_UP, 0);
  require(textOf(first) != before, "Up did not change the band");

  before = textOf(first);
  SendMessageW(first, WM_KEYDOWN, VK_DOWN, 0);
  require(textOf(first) != before, "Down did not change the band");

  before = textOf(first);
  SendMessageW(first, WM_KEYDOWN, VK_RIGHT, 0);
  require(textOf(first) != before, "Right did not change the band");

  before = textOf(first);
  SendMessageW(first, WM_KEYDOWN, VK_LEFT, 0);
  require(textOf(first) != before, "Left did not change the band");

  SendMessageW(first, WM_KEYDOWN, VK_HOME, 0);
  require(!neutralText(textOf(first)), "Home did not reach a non-neutral value");
  SendMessageW(first, WM_KEYDOWN, '0', 0);
  require(neutralText(textOf(first)), "0 did not neutralize the band");

  RECT r{};
  GetClientRect(first, &r);
  require(r.right - r.left > 10 && r.bottom - r.top > 20,
          "band HWND has unusable dimensions");

  const int x = (r.left + r.right) / 2;
  const int yBottom = std::max(r.top, r.bottom - 30);
  const int yTop = std::min(r.bottom - 1, r.top + 10);

  SendMessageW(first, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, yBottom));
  require(GetCapture() == first, "band did not capture pointer");
  const std::wstring pressed = textOf(first);

  SendMessageW(first, WM_MOUSEMOVE, 0, MAKELPARAM(x, yTop));
  require(textOf(first) != pressed,
          "captured move without MK_LBUTTON did not continue drag");

  SendMessageW(first, WM_LBUTTONUP, 0, MAKELPARAM(x, yTop));
  require(GetCapture() != first, "pointer capture not released");

  SendMessageW(first, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(x, yTop));
  require(neutralText(textOf(first)), "double-click did not neutralize");

  // Plain wheel is the fine whole-dB step; Shift+wheel mirrors the keyboard
  // PageUp/PageDown coarse step. Parse the announced value so the test pins
  // the step sizes rather than only "something changed".
  const double neutralDb = parseBandDb(textOf(first));
  SendMessageW(first, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
  const double fineDb = parseBandDb(textOf(first));
  require(std::abs((fineDb - neutralDb) - 1.0) < 0.05,
          "mouse wheel did not step the band by exactly 1 dB");
  SendMessageW(first, WM_MOUSEWHEEL, MAKEWPARAM(MK_SHIFT, WHEEL_DELTA), 0);
  const double coarseDb = parseBandDb(textOf(first));
  require(std::abs((coarseDb - fineDb) - 6.0) < 0.05,
          "Shift+mouse wheel did not step the band by the coarse 6 dB");

  SendMessageW(first, WM_KEYDOWN, '0', 0);
  const double beforeHighResolutionWheel = parseBandDb(textOf(first));
  for (int index = 0; index < 3; ++index) {
    SendMessageW(first, WM_MOUSEWHEEL, MAKEWPARAM(0, 30), 0);
  }
  require(std::abs(parseBandDb(textOf(first)) - beforeHighResolutionWheel) < 0.05,
          "partial high-resolution wheel deltas changed the value too early");
  SendMessageW(first, WM_MOUSEWHEEL, MAKEWPARAM(0, 30), 0);
  require(std::abs((parseBandDb(textOf(first)) - beforeHighResolutionWheel) - 1.0) < 0.05,
          "high-resolution wheel deltas were not accumulated to one notch");

  SetFocus(first);
  SendMessageW(first, WM_KEYDOWN, VK_RETURN, 0);
  pumpMessages();
  HWND exactEdit = GetFocus();
  wchar_t exactClass[32]{};
  GetClassNameW(exactEdit, exactClass, static_cast<int>(std::size(exactClass)));
  require(exactEdit != nullptr && exactEdit != first &&
              _wcsicmp(exactClass, L"Edit") == 0,
          "Enter did not open the native exact-value edit");
  SetWindowTextW(exactEdit, L"-3.5");
  SendMessageW(exactEdit, WM_KEYDOWN, VK_RETURN, 0);
  pumpMessages();
  require(GetFocus() == first,
          "exact-value Enter did not return focus to the same band");
  require(std::abs(parseBandDb(textOf(first)) + 3.5) < 0.05,
          "exact-value editor did not commit a decimal dB value");

  // Mouse use must leave the same control usable from the keyboard.
  before = textOf(first);
  SendMessageW(first, WM_KEYDOWN, VK_DOWN, 0);
  require(textOf(first) != before,
          "keyboard edit failed immediately after mouse use");

  int trackbars = 0;
  EnumChildWindows(
      editor,
      [](HWND child, LPARAM context) -> BOOL {
        wchar_t c[64]{};
        GetClassNameW(child, c, static_cast<int>(std::size(c)));
        if (_wcsicmp(c, TRACKBAR_CLASSW) == 0) {
          ++(*reinterpret_cast<int*>(context));
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&trackbars));
  require(trackbars == 0,
          "visual faders introduced separate trackbar controls");
}

struct RectFinding {
  HWND window = nullptr;
  int id = 0;
  RECT rect{};
};

std::vector<RectFinding> visibleChildRects(HWND editor) {
  struct EnumContext {
    HWND editor = nullptr;
    std::vector<RectFinding>* result = nullptr;
  };
  std::vector<RectFinding> result;
  EnumContext context{editor, &result};
  EnumChildWindows(
      editor,
      [](HWND child, LPARAM rawContext) -> BOOL {
        auto* context = reinterpret_cast<EnumContext*>(rawContext);
        if (context == nullptr || context->editor == nullptr ||
            context->result == nullptr || IsWindowVisible(child) == FALSE) {
          return TRUE;
        }
        RECT rect{};
        if (!GetWindowRect(child, &rect)) return TRUE;
        POINT tl{rect.left, rect.top};
        POINT br{rect.right, rect.bottom};
        ScreenToClient(context->editor, &tl);
        ScreenToClient(context->editor, &br);
        rect = {tl.x, tl.y, br.x, br.y};
        context->result->push_back({child, GetDlgCtrlID(child), rect});
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&context));
  return result;
}

int paintedFaderTrackLength(HWND fader, double scale) {
  // Mirrors the control's own paint geometry:
  // trackTop = top + 10*s; trackBottom = bottom - 24*s - 8*s;
  // with the max(top+1, ...) clamp and the zero line at the midpoint.
  RECT r{};
  GetClientRect(fader, &r);
  const int top = r.top + static_cast<int>(std::lround(10 * scale));
  const int bottomRaw = static_cast<int>(r.bottom) -
      static_cast<int>(std::lround(24 * scale)) -
      static_cast<int>(std::lround(8 * scale));
  const int bottom = std::max(top + 1, bottomRaw);
  return bottom - top;
}

void reportLayoutMeasurements(HWND editor, const RECT& client, double scale,
                              const char* label) {
  const auto rects = visibleChildRects(editor);
  std::cout << "--- measurements at " << label << " (client "
            << client.right << "x" << client.bottom << ", scale " << scale
            << ") ---\n";
  int worstOverflowRight = 0;
  int worstOverflowBottom = 0;
  for (const auto& item : rects) {
    const int overX = static_cast<int>(item.rect.right - client.right);
    const int overY = static_cast<int>(item.rect.bottom - client.bottom);
    if (overX > 0 || overY > 0) {
      std::cout << "OUT-OF-BOUNDS id=" << item.id
                << " rect=(" << item.rect.left << "," << item.rect.top << ")-("
                << item.rect.right << "," << item.rect.bottom
                << ") overflow " << overX << "x" << overY << "\n";
      worstOverflowRight = std::max(worstOverflowRight, overX);
      worstOverflowBottom = std::max(worstOverflowBottom, overY);
    }
  }
  std::cout << "overflow right=" << worstOverflowRight
            << " bottom=" << worstOverflowBottom << "\n";
  HWND fader = GetDlgItem(editor, kBandSliderFirstId);
  if (fader != nullptr && IsWindowVisible(fader)) {
    RECT r{};
    GetWindowRect(fader, &r);
    std::cout << "first band fader window " << (r.right - r.left) << "x"
              << (r.bottom - r.top) << ", painted track length "
              << paintedFaderTrackLength(fader, scale) << " px\n";
  }
}

void verifyDeclaredMinimumLayout(const clap_plugin_t* plugin,
                                 const clap_plugin_gui_t* gui,
                                 HWND host,
                                 HWND editor,
                                 double scale) {
  uint32_t w = 1;
  uint32_t h = 1;
  (void)host;
  require(gui->adjust_size(plugin, &w, &h),
          "adjust_size failed for declared minimum");
  require(w > 0 && h > 0, "declared minimum is zero");

  require(gui->set_size(plugin, w, h),
          "editor rejected its own declared minimum");
  pumpMessages();

  RECT client{};
  GetClientRect(editor, &client);
  require(client.right == static_cast<LONG>(w) &&
              client.bottom == static_cast<LONG>(h),
          "actual editor client size disagrees with declared minimum");

  selectPage(editor, 0);
  reportLayoutMeasurements(editor, client, scale, "Match page");
  auto matchRects = visibleChildRects(editor);
  const int minimumEditWidth = static_cast<int>(std::lround(64 * scale));
  for (const auto& item : matchRects) {
    require(item.rect.left >= client.left &&
                item.rect.top >= client.top &&
                item.rect.right <= client.right &&
                item.rect.bottom <= client.bottom,
            "visible Match control extends outside editor at declared minimum; "
            "control id=" + std::to_string(item.id));
    if (item.id >= kEditFirstId && item.id < kEditFirstId + 7) {
      require(item.rect.right - item.rect.left >= minimumEditWidth,
              "Match value field fell below its 64 px design minimum; control id=" +
                  std::to_string(item.id));
    }
  }

  selectPage(editor, 1);
  reportLayoutMeasurements(editor, client, scale, "Bands page");
  auto bandRects = visibleChildRects(editor);
  HWND firstBand = GetDlgItem(editor, kBandSliderFirstId);
  require(firstBand != nullptr && IsWindowVisible(firstBand),
          "first band not visible at declared minimum");

  RECT band{};
  GetClientRect(firstBand, &band);
  const int bandHeight = band.bottom - band.top;

  // The fader's own paint geometry (trackTop = +10, trackBottom = -24-8,
  // thumb 26x10) defines the usable track. Practical pointer use needs at
  // least a thumb-length of travel; require >= 60 px at every scale.
  const int track = paintedFaderTrackLength(firstBand, scale);
  std::cout << "Declared minimum: " << w << " x " << h << "\n";
  std::cout << "First band control height at minimum: "
            << bandHeight << " px, painted track: " << track << " px\n";
  require(track >= 60, "declared minimum leaves an unusable fader track");

  for (const auto& item : bandRects) {
    require(item.rect.left >= client.left &&
                item.rect.top >= client.top &&
                item.rect.right <= client.right &&
                item.rect.bottom <= client.bottom,
            "visible Bands control extends outside editor at declared minimum; "
            "control id=" + std::to_string(item.id));
  }
}

void verifyScale(const clap_plugin_t* plugin,
                 const clap_plugin_gui_t* gui,
                 double scale) {
  require(gui->set_scale(plugin, scale), "set_scale rejected test scale");

  uint32_t w = 0;
  uint32_t h = 0;
  require(gui->get_size(plugin, &w, &h) && w > 0 && h > 0,
          "get_size failed after scale change");
}

void run(const char* path) {
  const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitializeCom = SUCCEEDED(comResult);
  require(SUCCEEDED(comResult) || comResult == RPC_E_CHANGED_MODE,
          "COM initialization failed for UI Automation verification");

  DynamicLibrary module(path);
  const auto* entry = module.symbol<const clap_plugin_entry_t*>("clap_entry");
  require(entry != nullptr, "clap_entry missing");
  require(clap_version_is_compatible(entry->clap_version),
          "incompatible CLAP version");
  require(entry->init(path), "CLAP entry init failed");

  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  require(factory && factory->get_plugin_count(factory) >= 1,
          "plugin factory invalid");
  const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
  require(descriptor != nullptr, "descriptor missing");

  HostState hostState;
  {
    PluginInstance instance(
        factory->create_plugin(factory, &hostState.host, descriptor->id));
    const auto* gui = static_cast<const clap_plugin_gui_t*>(
        instance.plugin->get_extension(instance.plugin, CLAP_EXT_GUI));
    require(gui != nullptr &&
                gui->is_api_supported(instance.plugin,
                                      CLAP_WINDOW_API_WIN32, false) &&
                gui->create(instance.plugin, CLAP_WINDOW_API_WIN32, false),
            "Win32 GUI unavailable");
    require(instance.plugin->activate(instance.plugin, 48000.0, 1, 512),
            "plugin activation failed for UI harness");

    verifyScale(instance.plugin, gui, 1.0);

    uint32_t preferredW = 0;
    uint32_t preferredH = 0;
    require(gui->get_size(instance.plugin, &preferredW, &preferredH),
            "preferred size unavailable");

    HWND host = CreateWindowExW(
        0, L"STATIC", L"Tone Trace neutral UI harness",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        static_cast<int>(preferredW + 100),
        static_cast<int>(preferredH + 100),
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(host != nullptr, "host HWND creation failed");

    clap_window_t parent{};
    parent.api = CLAP_WINDOW_API_WIN32;
    parent.win32 = host;
    require(gui->set_size(instance.plugin, preferredW, preferredH),
            "preferred size rejected");
    require(gui->set_parent(instance.plugin, &parent),
            "set_parent failed");
    require(gui->show(instance.plugin), "show failed");
    pumpMessages();

    HWND editor = findEditor(host);
    require(editor != nullptr, "could not locate editor HWND");

    verifyKnownGoodKeyboardBaseline(editor);
    verifyManualBandBeforeProfile(editor);
    commitCorrectionProfile(instance.plugin);
    verifyBandMouseAndKeyboard(editor);
    reportLayoutMeasurements(editor, [&]{ RECT c{}; GetClientRect(editor, &c); return c; }(), 1.0, "default size, Bands page 1");
    verifyDeclaredMinimumLayout(instance.plugin, gui, host, editor, 1.0);

    verifyScale(instance.plugin, gui, 1.25);
    require(gui->set_size(instance.plugin, 1225, 800),
            "scaled preferred size rejected at 125%");
    pumpMessages();
    verifyKnownGoodKeyboardBaseline(editor);
    verifyBandMouseAndKeyboard(editor);
    verifyDeclaredMinimumLayout(instance.plugin, gui, host, editor, 1.25);

    verifyScale(instance.plugin, gui, 1.50);
    require(gui->set_size(instance.plugin, 1470, 960),
            "scaled preferred size rejected at 150%");
    pumpMessages();
    verifyKnownGoodKeyboardBaseline(editor);
    verifyBandMouseAndKeyboard(editor);
    verifyDeclaredMinimumLayout(instance.plugin, gui, host, editor, 1.50);

    verifyScale(instance.plugin, gui, 1.0);
    gui->hide(instance.plugin);
    gui->destroy(instance.plugin);
    instance.plugin->deactivate(instance.plugin);
    DestroyWindow(host);
  }

  entry->deinit();
  if (uninitializeCom) CoUninitialize();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 2,
            "usage: tonetrace-win32-ui-regression <Tone Trace EQ.clap>");
    run(argv[1]);
    std::cout << "Tone Trace neutral Win32 UI regression harness passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Tone Trace neutral Win32 UI regression harness failed: "
              << error.what() << "\n";
    return 1;
  }
}
