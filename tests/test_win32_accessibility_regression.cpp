// Tone Trace Win32 accessibility regression test (band fader focus/value).
//
// Universal accessibility is verified on BOTH the MSAA and UI Automation paths
// for the custom-drawn ToneTraceAccessibleFader, under a REAPER-style wrapper
// parent:
//   - MSAA: the focus WinEvent resolves to a Slider on the same fader HWND,
//     carrying a band/frequency name, a dB value, and STATE_SYSTEM_FOCUSED +
//     STATE_SYSTEM_FOCUSABLE.
//   - UIA: the SAME control is discovered from the editor tree as an Edit and
//     exposes a unit-bearing string Value. It deliberately does not expose
//     RangeValue, which makes Narrator normalize sliders to percentages.
//     Focused value changes also emit the canonical dB text as a notification,
//     which gives Narrator a spoken Up/Down result.
//   - Focus/value events do not duplicate or compete: the background read-only
//     readout edit must NOT emit a competing EVENT_OBJECT_VALUECHANGE merely
//     because band focus or value changed.
//
// Run with the built plugin path as the single argument:
//   tonetrace-win32-a11y-tests "Tone Trace EQ.clap"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wchar.h>
#include <tchar.h>
#include <commctrl.h>
#include <oleacc.h>
#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>

#include <clap/clap.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kTabControlId = 50;
constexpr int kCaptureReferenceId = 100;
constexpr int kDescriptionLabelId = 202;
constexpr int kDescriptionEditId = 203;
constexpr int kBandSliderFirstId = 2000;
constexpr wchar_t kFaderClass[] = L"ToneTraceAccessibleFader";

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct WinEventRec {
  DWORD event = 0;
  HWND hwnd = nullptr;
  LONG idObject = 0;
  LONG idChild = 0;
};

std::vector<WinEventRec> g_events;
CRITICAL_SECTION g_lock;

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                           LONG idObject, LONG idChild,
                           DWORD, DWORD) {
  EnterCriticalSection(&g_lock);
  g_events.push_back({event, hwnd, idObject, idChild});
  LeaveCriticalSection(&g_lock);
}

void ClearEvents() {
  EnterCriticalSection(&g_lock);
  g_events.clear();
  LeaveCriticalSection(&g_lock);
}

bool HasWinEvent(DWORD event, HWND hwnd, LONG idObject,
                 const std::vector<WinEventRec>& events) {
  for (const auto& e : events) {
    if (e.event == event && e.hwnd == hwnd && e.idObject == idObject) return true;
  }
  return false;
}
bool HasWinEventOnHwnd(DWORD event, HWND hwnd,
                       const std::vector<WinEventRec>& events) {
  for (const auto& e : events) {
    if (e.event == event && e.hwnd == hwnd) return true;
  }
  return false;
}

void PumpFor(DWORD ms) {
  DWORD start = GetTickCount();
  while (true) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    DWORD now = GetTickCount();
    if (now - start >= ms) break;
    Sleep(std::min<DWORD>(20, ms - (now - start)));
  }
}
void PumpAll() {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

HWND FindEditor(HWND root) {
  HWND result = nullptr;
  EnumChildWindows(root,
      [](HWND child, LPARAM p) -> BOOL {
        if (GetDlgItem(child, kTabControlId) != nullptr &&
            GetDlgItem(child, kCaptureReferenceId) != nullptr) {
          *reinterpret_cast<HWND*>(p) = child;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
  return result;
}

void SelectPage(HWND editor, int page) {
  HWND tabs = GetDlgItem(editor, kTabControlId);
  require(tabs != nullptr, "tab control missing");
  SendMessageW(tabs, TCM_SETCURSEL, page, 0);
  NMHDR h{};
  h.hwndFrom = tabs;
  h.idFrom = kTabControlId;
  h.code = TCN_SELCHANGE;
  SendMessageW(editor, WM_NOTIFY, kTabControlId,
               reinterpret_cast<LPARAM>(&h));
  PumpAll();
}

std::wstring WinText(HWND w) {
  if (!w) return L"";
  int len = GetWindowTextLengthW(w);
  std::wstring s(static_cast<std::size_t>(std::max(0, len)) + 1, L'\0');
  int c = GetWindowTextW(w, s.data(), static_cast<int>(s.size()));
  s.resize(static_cast<std::size_t>(std::max(0, c)));
  return s;
}

// The readout is the only visible read-only multiline edit on a Bands page.
HWND FindReadoutEdit(HWND editor) {
  HWND result = nullptr;
  EnumChildWindows(editor,
      [](HWND child, LPARAM p) -> BOOL {
        wchar_t cls[64]{};
        GetClassNameW(child, cls, 64);
        if (_wcsicmp(cls, L"Edit") == 0 && IsWindowVisible(child)) {
          LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
          if ((style & ES_READONLY) && (style & ES_MULTILINE)) {
            if (!*reinterpret_cast<HWND*>(p)) {
              *reinterpret_cast<HWND*>(p) = child;
            }
          }
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
  return result;
}

std::wstring accGetName(IAccessible* a, const VARIANT& v) {
  BSTR b = nullptr;
  HRESULT hr = a->get_accName(v, &b);
  std::wstring out = (FAILED(hr) || !b) ? L"" : std::wstring(b);
  if (b) SysFreeString(b);
  return out;
}
std::wstring accGetValue(IAccessible* a, const VARIANT& v) {
  BSTR b = nullptr;
  HRESULT hr = a->get_accValue(v, &b);
  std::wstring out = (FAILED(hr) || !b) ? L"" : std::wstring(b);
  if (b) SysFreeString(b);
  return out;
}
long accGetRole(IAccessible* a, const VARIANT& v) {
  VARIANT val{};
  VariantInit(&val);
  long role = -1;
  if (SUCCEEDED(a->get_accRole(v, &val)) && val.vt == VT_I4) role = val.lVal;
  VariantClear(&val);
  return role;
}
long accGetState(IAccessible* a, const VARIANT& v) {
  VARIANT val{};
  VariantInit(&val);
  long state = 0;
  if (SUCCEEDED(a->get_accState(v, &val)) && val.vt == VT_I4) state = val.lVal;
  VariantClear(&val);
  return state;
}

VARIANT SelfChild() {
  VARIANT v{};
  v.vt = VT_I4;
  v.lVal = CHILDID_SELF;
  return v;
}

double ParseDb(const std::wstring& text) {
  const std::size_t db = text.rfind(L" dB");
  require(db != std::wstring::npos, "value carries no dB text");
  std::size_t start = db;
  while (start > 0) {
    const wchar_t ch = text[start - 1];
    if (std::iswdigit(ch) || ch == L'.' || ch == L'+' || ch == L'-') --start;
    else break;
  }
  require(start < db, "value has an empty dB number");
  return std::stod(text.substr(start, db - start));
}

bool IsBandFrequencyName(const std::wstring& name) {
  if (name.empty()) return false;
  const bool band = name.find(L"Band") != std::wstring::npos;
  const bool freq = name.find(L"Hz") != std::wstring::npos ||
                    name.find(L"kHz") != std::wstring::npos;
  return band && freq;
}

// Query the fader's focus state from a DEDICATED worker thread (which does not
// hold the REAPER GUI thread's focus). This reproduces the real defect where UIA
// invokes the provider on a worker thread and thread-relative GetFocus() would
// have reported the wrong focus state. A focused fader must still report
// STATE_SYSTEM_FOCUSED / get_accFocus == CHILDID_SELF / UIA HasKeyboardFocus ==
// true regardless of the querying thread.
IUIAutomation* CreateAutomation();

struct FocusFromWorkerResult {
  DWORD workerThread = 0;
  DWORD guiThread = 0;
  bool workerFocusIsNotFader = false;
  bool msaaFocused = false;
  bool msaaFocusSelf = false;
  bool uiaHasFocus = false;
};

FocusFromWorkerResult QueryFocusFromWorker(HWND fader) {
  FocusFromWorkerResult r{};
  std::atomic<bool> done{false};
  std::thread worker([&] {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    r.workerThread = GetCurrentThreadId();
    r.guiThread = GetWindowThreadProcessId(fader, nullptr);
    r.workerFocusIsNotFader = (GetFocus() != fader);

    IAccessible* acc = nullptr;
    if (SUCCEEDED(AccessibleObjectFromWindow(fader,
                                             static_cast<DWORD>(OBJID_CLIENT),
                                             IID_IAccessible,
                                             reinterpret_cast<void**>(&acc))) &&
        acc != nullptr) {
      const VARIANT self = SelfChild();
      VARIANT state{};
      if (SUCCEEDED(acc->get_accState(self, &state)) && state.vt == VT_I4) {
        r.msaaFocused = (state.lVal & STATE_SYSTEM_FOCUSED) != 0;
      }
      VARIANT focus{};
      if (SUCCEEDED(acc->get_accFocus(&focus))) {
        r.msaaFocusSelf = (focus.vt == VT_I4 && focus.lVal == CHILDID_SELF);
      }
      acc->Release();
    }

    IUIAutomation* uia = CreateAutomation();
    if (uia != nullptr) {
      IUIAutomationElement* element = nullptr;
      if (SUCCEEDED(uia->ElementFromHandle(fader, &element)) && element != nullptr) {
        VARIANT v{};
        if (SUCCEEDED(element->GetCurrentPropertyValue(
                UIA_HasKeyboardFocusPropertyId, &v))) {
          r.uiaHasFocus = (v.vt == VT_BOOL && v.boolVal == VARIANT_TRUE);
        }
        VariantClear(&v);
        element->Release();
      }
      uia->Release();
    }

    if (SUCCEEDED(hr)) CoUninitialize();
    done.store(true, std::memory_order_release);
  });
  // The MSAA/UIA calls on the worker may be marshaled to the fader's GUI thread
  // (STA), so the main thread MUST pump messages while waiting or the worker's
  // in-flight provider call deadlocks. Pumping also ensures WM_SETFOCUS focused_
  // state has propagated before we read providers.
  while (!done.load(std::memory_order_acquire)) {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Sleep(5);
  }
  worker.join();
  return r;
}

// ---- UI Automation helpers (used to prove the SAME control is discoverable via
// ---- UIA and exposes the exact dB string, independent of MSAA).
IUIAutomation* CreateAutomation() {
  IUIAutomation* u = nullptr;
  CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                   __uuidof(IUIAutomation), reinterpret_cast<void**>(&u));
  return u;
}
HWND UiaNative(IUIAutomationElement* e) {
  UIA_HWND h = nullptr; if (e) e->get_CurrentNativeWindowHandle(&h);
  return reinterpret_cast<HWND>(h);
}
long UiaType(IUIAutomationElement* e) {
  CONTROLTYPEID t = 0; if (e) e->get_CurrentControlType(&t);
  return static_cast<long>(t);
}
std::wstring UiaAutoId(IUIAutomationElement* e) {
  VARIANT v{}; std::wstring o = L"";
  if (e && SUCCEEDED(e->GetCurrentPropertyValue(UIA_AutomationIdPropertyId, &v)) &&
      v.vt == VT_BSTR && v.bstrVal) o = v.bstrVal;
  VariantClear(&v); return o;
}
bool UiaValue(IUIAutomationElement* e, std::wstring& value) {
  IUnknown* pattern = nullptr;
  IUIAutomationValuePattern* provider = nullptr;
  if (!e || FAILED(e->GetCurrentPattern(UIA_ValuePatternId, &pattern)) ||
      pattern == nullptr) {
    return false;
  }
  if (FAILED(pattern->QueryInterface(__uuidof(IUIAutomationValuePattern),
                                     reinterpret_cast<void**>(&provider))) ||
      provider == nullptr) {
    pattern->Release();
    return false;
  }
  BSTR text = nullptr;
  const bool ok = SUCCEEDED(provider->get_CurrentValue(&text)) && text != nullptr;
  if (ok) value = text;
  if (text != nullptr) SysFreeString(text);
  provider->Release();
  pattern->Release();
  return ok;
}

bool UiaSetValue(IUIAutomationElement* e, const wchar_t* value) {
  IUnknown* pattern = nullptr;
  IUIAutomationValuePattern* provider = nullptr;
  if (!e || FAILED(e->GetCurrentPattern(UIA_ValuePatternId, &pattern)) ||
      pattern == nullptr) {
    return false;
  }
  const bool queried = SUCCEEDED(pattern->QueryInterface(
      __uuidof(IUIAutomationValuePattern), reinterpret_cast<void**>(&provider)));
  BSTR text = SysAllocString(value);
  const bool ok = queried && provider != nullptr && text != nullptr &&
                  SUCCEEDED(provider->SetValue(text));
  if (text != nullptr) SysFreeString(text);
  if (provider != nullptr) provider->Release();
  pattern->Release();
  return ok;
}

class NotificationRecorder final
    : public IUIAutomationNotificationEventHandler {
 public:
  NotificationRecorder() { InitializeCriticalSection(&lock_); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                            void** object) override {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (riid == IID_IUnknown ||
        riid == __uuidof(IUIAutomationNotificationEventHandler)) {
      *object = static_cast<IUIAutomationNotificationEventHandler*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE HandleNotificationEvent(
      IUIAutomationElement*, NotificationKind, NotificationProcessing,
      BSTR displayString, BSTR) override {
    EnterCriticalSection(&lock_);
    ++count_;
    lastText_ = displayString != nullptr ? displayString : L"";
    LeaveCriticalSection(&lock_);
    return S_OK;
  }

  int count() {
    EnterCriticalSection(&lock_);
    const int result = count_;
    LeaveCriticalSection(&lock_);
    return result;
  }

  std::wstring lastText() {
    EnterCriticalSection(&lock_);
    const std::wstring result = lastText_;
    LeaveCriticalSection(&lock_);
    return result;
  }

 private:
  ~NotificationRecorder() { DeleteCriticalSection(&lock_); }

  std::atomic<ULONG> references_{1};
  CRITICAL_SECTION lock_{};
  int count_ = 0;
  std::wstring lastText_;
};

bool NotificationApiAvailable() {
  HMODULE module = GetModuleHandleW(L"UIAutomationCore.dll");
  if (module == nullptr) module = LoadLibraryW(L"UIAutomationCore.dll");
  return module != nullptr &&
         GetProcAddress(module, "UiaRaiseNotificationEvent") != nullptr;
}

// Resolve a captured event the way NVDA resolves a WinEvent and validate it.
IAccessible* ResolveEventToFader(const WinEventRec& e, HWND fader,
                                 LONG expectedClass) {
  IAccessible* acc = nullptr;
  VARIANT child{};
  VariantInit(&child);
  HRESULT hr = AccessibleObjectFromEvent(e.hwnd, static_cast<DWORD>(e.idObject),
                                         static_cast<DWORD>(e.idChild), &acc,
                                         &child);
  VariantClear(&child);
  require(SUCCEEDED(hr) && acc != nullptr,
          "AccessibleObjectFromEvent failed for the event");
  HWND wnd = nullptr;
  WindowFromAccessibleObject(acc, &wnd);
  require(wnd == fader, "event-resolved object is not the fader HWND");
  const VARIANT self = SelfChild();
  const long role = accGetRole(acc, self);
  require(role == expectedClass, "event-resolved object is not the expected class");
  return acc;
}

struct DynamicLibrary {
  HMODULE handle = nullptr;
  explicit DynamicLibrary(const char* path) { handle = LoadLibraryA(path); }
  ~DynamicLibrary() { if (handle) FreeLibrary(handle); }
  template <typename T> T symbol(const char* name) const {
    return reinterpret_cast<T>(GetProcAddress(handle, name));
  }
};

struct HostState {
  clap_host_t host{};
  HostState() {
    host.clap_version = CLAP_VERSION;
    host.host_data = this;
    host.name = "Tone Trace a11y test host";
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

const wchar_t kReaperWrapClass[] = L"reaperPluginHostWrapProc";

LRESULT CALLBACK DummyProc(HWND w, UINT m, WPARAM p, LPARAM l) {
  return DefWindowProcW(w, m, p, l);
}

void Run(const char* clapPath) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

  InitializeCriticalSection(&g_lock);

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
  InitCommonControlsEx(&icc);

  // REAPER-style parenting: an FX top-level window that owns a wrapper window
  // of class reaperPluginHostWrapProc, which is what the editor detects in
  // setParent() to place itself as a sibling of the wrapper.
  const HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSEXW hostCls{};
  hostCls.cbSize = sizeof(hostCls);
  hostCls.lpfnWndProc = DummyProc;
  hostCls.hInstance = inst;
  hostCls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  hostCls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  hostCls.lpszClassName = L"TtA11yFxHost";
  RegisterClassExW(&hostCls);
  WNDCLASSEXW wrapCls{};
  wrapCls.cbSize = sizeof(wrapCls);
  wrapCls.lpfnWndProc = DummyProc;
  wrapCls.hInstance = inst;
  wrapCls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wrapCls.lpszClassName = kReaperWrapClass;
  RegisterClassExW(&wrapCls);

  HWND host = CreateWindowExW(WS_EX_CONTROLPARENT, L"TtA11yFxHost",
                              L"TtA11y FX Host", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              40, 40, 1200, 900, nullptr, nullptr, inst, nullptr);
  HWND wrapper = CreateWindowExW(0, kReaperWrapClass, L"",
                                 WS_CHILD | WS_VISIBLE, 10, 40, 1040, 700,
                                 host, nullptr, inst, nullptr);
  require(GetParent(wrapper) == host, "wrapper was not parented to the FX host");

  DynamicLibrary module(clapPath);
  const auto* entry = module.symbol<const clap_plugin_entry_t*>("clap_entry");
  require(entry && clap_version_is_compatible(entry->clap_version),
          "clap_entry invalid");
  require(entry->init(clapPath), "entry init failed");
  const auto* factory = static_cast<const clap_plugin_factory_t*>(
      entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
  HostState hostState;
  const clap_plugin_t* plugin = factory->create_plugin(factory, &hostState.host,
                                                       descriptor->id);
  require(plugin && plugin->init(plugin), "plugin init failed");
  const auto* gui = static_cast<const clap_plugin_gui_t*>(
      plugin->get_extension(plugin, CLAP_EXT_GUI));
  require(gui && gui->is_api_supported(plugin, CLAP_WINDOW_API_WIN32, false),
          "Win32 GUI unavailable");
  gui->create(plugin, CLAP_WINDOW_API_WIN32, false);
  require(plugin->activate(plugin, 48000.0, 1, 512), "plugin activation failed");

  std::uint32_t pw = 0, ph = 0;
  require(gui->get_size(plugin, &pw, &ph) && pw > 0 && ph > 0,
          "preferred size unavailable");
  require(gui->set_size(plugin, pw, ph), "set_size failed");
  clap_window_t win{};
  win.api = CLAP_WINDOW_API_WIN32;
  win.win32 = wrapper;
  require(gui->set_parent(plugin, &win), "set_parent(wrapper) failed");
  require(gui->show(plugin), "show failed");
  PumpAll();
  PumpFor(300);

  HWND editor = FindEditor(host);
  require(editor != nullptr, "editor HWND not found");
  require(GetParent(editor) == host,
          "editor was not parented as a sibling of the wrapper");

  // The curve summary is an EDIT so a screen-reader user can review and copy
  // it. A real adjacent STATIC must provide its accessible name; painted text
  // cannot label a native control.
  HWND descriptionLabel = GetDlgItem(editor, kDescriptionLabelId);
  HWND descriptionEdit = GetDlgItem(editor, kDescriptionEditId);
  require(descriptionLabel != nullptr && descriptionEdit != nullptr,
          "Curve Description label/edit pair is missing");
  require(WinText(descriptionLabel) == L"Curve Description",
          "Curve Description visible label text changed");
  require(GetWindow(descriptionEdit, GW_HWNDPREV) == descriptionLabel,
          "Curve Description label is not adjacent to its edit in Z-order");
  IAccessible* descriptionAccessible = nullptr;
  require(SUCCEEDED(AccessibleObjectFromWindow(
              descriptionEdit, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
              reinterpret_cast<void**>(&descriptionAccessible))) &&
              descriptionAccessible != nullptr,
          "Curve Description edit has no MSAA object");
  const VARIANT descriptionSelf = SelfChild();
  require(accGetName(descriptionAccessible, descriptionSelf) ==
              L"Curve Description",
          "Curve Description edit exposes the wrong accessible name");
  descriptionAccessible->Release();

  // Jump to the first Bands page so the faders are visible, and flush the
  // page-description fallback announce that runs on tab change (190 ms timer).
  SelectPage(editor, 1);
  PumpFor(400);

  HWND fader = GetDlgItem(editor, kBandSliderFirstId);
  require(fader != nullptr && IsWindowVisible(fader) != FALSE,
          "first band fader is not visible");
  wchar_t cls[64]{};
  GetClassNameW(fader, cls, 64);
  require(_wcsicmp(cls, kFaderClass) == 0, "band control is not the fader class");

  HWND readout = FindReadoutEdit(editor);
  require(readout != nullptr, "readout edit not found");
  require(WinText(readout).find(L"Band") != std::wstring::npos,
          "readout does not carry band/status text");

  HWINEVENTHOOK hkF = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS,
                                      nullptr, WinEventProc, GetCurrentProcessId(),
                                      0, WINEVENT_OUTOFCONTEXT);
  HWINEVENTHOOK hkV = SetWinEventHook(EVENT_OBJECT_VALUECHANGE,
                                      EVENT_OBJECT_VALUECHANGE, nullptr,
                                      WinEventProc, GetCurrentProcessId(), 0,
                                      WINEVENT_OUTOFCONTEXT);

  // ---- Requirement 1 & 2: focus the band and resolve the FOCUS event ----
  ClearEvents();
  SetFocus(fader);
  PumpFor(400);
  {
    std::vector<WinEventRec> evs;
    EnterCriticalSection(&g_lock);
    evs = g_events;
    LeaveCriticalSection(&g_lock);
    require(HasWinEvent(EVENT_OBJECT_FOCUS, fader, OBJID_CLIENT, evs),
            "focusing a band produced no EVENT_OBJECT_FOCUS on the fader "
            "with OBJID_CLIENT");
  }
  // Ensure the captured focus event used OBJID_CLIENT and CHILDID_SELF.
  {
    EnterCriticalSection(&g_lock);
    std::vector<WinEventRec> evs;
    evs = g_events;
    LeaveCriticalSection(&g_lock);
    bool foundSelf = false;
    for (const auto& e : evs) {
      if (e.event == EVENT_OBJECT_FOCUS && e.hwnd == fader &&
          e.idObject == OBJID_CLIENT && e.idChild == CHILDID_SELF) {
        foundSelf = true;
      }
    }
    require(foundSelf, "focus event did not use OBJID_CLIENT/CHILDID_SELF");
  }

  // Resolve the focus event object and validate identity + content.
  IAccessible* focusAcc = nullptr;
  {
    std::vector<WinEventRec> evs;
    EnterCriticalSection(&g_lock);
    evs = g_events;
    LeaveCriticalSection(&g_lock);
    for (const auto& e : evs) {
      if (e.event == EVENT_OBJECT_FOCUS && e.hwnd == fader &&
          e.idObject == OBJID_CLIENT) {
        IAccessible* acc = ResolveEventToFader(e, fader, ROLE_SYSTEM_SLIDER);
        const VARIANT self = SelfChild();
        require(accGetRole(acc, self) == ROLE_SYSTEM_SLIDER,
                "focus object role is not ROLE_SYSTEM_SLIDER");
        const std::wstring name = accGetName(acc, self);
        require(IsBandFrequencyName(name), "focus name is not a band/frequency name");
        const std::wstring value = accGetValue(acc, self);
        require(value.find(L"dB") != std::wstring::npos,
                "focus value does not carry dB");
        const long state = accGetState(acc, self);
        require((state & STATE_SYSTEM_FOCUSABLE) != 0,
                "STATE_SYSTEM_FOCUSABLE is not set");
        require((state & STATE_SYSTEM_FOCUSED) != 0,
                "STATE_SYSTEM_FOCUSED is not set on the focused fader");
        focusAcc = acc;
        break;
      }
    }
    require(focusAcc != nullptr, "could not resolve the focus object");
  }

  // ---- Requirement 4 (focus): readout must not emit a competing VALUECHANGE ----
  {
    std::vector<WinEventRec> evs;
    EnterCriticalSection(&g_lock);
    evs = g_events;
    LeaveCriticalSection(&g_lock);
    require(!HasWinEventOnHwnd(EVENT_OBJECT_VALUECHANGE, readout, evs),
            "background readout edit emitted EVENT_OBJECT_VALUECHANGE on band "
            "focus; it is competing with the fader announcement");
  }

  double beforeDb = ParseDb(accGetValue(focusAcc, SelfChild()));
  focusAcc->Release();

  // ---- UIA discovers the SAME fader with a unit-bearing Value, not the
  // ---- percentage-producing Slider/RangeValue combination ----
  {
    IUIAutomation* uia = CreateAutomation();
    require(uia != nullptr, "UI Automation client could not be created");
    IUIAutomationElement* elFader = nullptr;
    require(SUCCEEDED(uia->ElementFromHandle(fader, &elFader)) && elFader != nullptr,
            "UI Automation could not retrieve the band fader element");
    require(UiaType(elFader) == UIA_EditControlTypeId,
            "UI Automation band control type is not Edit");
    require(UiaNative(elFader) == fader,
            "UI Automation band element is not the fader HWND");
    require(UiaAutoId(elFader).find(L"ToneTraceBand") != std::wstring::npos,
            "UI Automation band automation id does not identify the band");
    std::wstring curVal;
    require(UiaValue(elFader, curVal) &&
                curVal.find(L"dB") != std::wstring::npos,
            "UI Automation band does not expose a dB Value");
    IUnknown* rangePattern = nullptr;
    elFader->GetCurrentPattern(UIA_RangeValuePatternId, &rangePattern);
    require(rangePattern == nullptr,
            "UI Automation still exposes percentage-producing RangeValue");

    // Pin a value finer than one decimal through the public UIA edit contract.
    require(UiaSetValue(elFader, L"-6.234 dB"),
            "UI Automation could not set an exact dB value");
    PumpFor(100);
    require(UiaValue(elFader, curVal) && curVal == L"-6.234 dB",
            "UI Automation hid digits beyond one decimal place");
    IAccessible* exactAcc = nullptr;
    require(SUCCEEDED(AccessibleObjectFromWindow(
                fader, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
                reinterpret_cast<void**>(&exactAcc))) && exactAcc != nullptr,
            "MSAA object unavailable after UIA exact entry");
    require(accGetValue(exactAcc, SelfChild()) == curVal,
            "MSAA and UIA expose different dB strings");
    exactAcc->Release();
    beforeDb = ParseDb(curVal);

    // The SAME control is discovered by walking the editor tree (not only via
    // ElementFromHandle), proving it is a composed descendant.
    IUIAutomationElement* elEditor = nullptr;
    require(SUCCEEDED(uia->ElementFromHandle(editor, &elEditor)) && elEditor != nullptr,
            "UI Automation could not retrieve the editor element");
    IUIAutomationCondition* cond = nullptr;
    VARIANT v{};
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(L"ToneTraceBand1");
    require(v.bstrVal != nullptr &&
                SUCCEEDED(uia->CreatePropertyCondition(
                    UIA_AutomationIdPropertyId, v, &cond)),
            "could not build UI Automation band-id condition");
    IUIAutomationElement* found = nullptr;
    const HRESULT fr = elEditor->FindFirst(TreeScope_Descendants, cond, &found);
    require(SUCCEEDED(fr) && found != nullptr && UiaNative(found) == fader,
            "UI Automation did not discover the band fader as an Edit "
            "descendant of the editor");
    if (found) found->Release();
    if (cond) cond->Release();
    VariantClear(&v);
    elEditor->Release();
    elFader->Release();
    uia->Release();
  }

  // ---- Regression: focus state must be correct when a focused fader is queried
  // ---- from a different (worker) thread, not the fader's GUI thread.
  {
    const FocusFromWorkerResult fr = QueryFocusFromWorker(fader);
    require(fr.workerThread != fr.guiThread,
            "worker thread unexpectedly equaled the fader GUI thread; "
            "the cross-thread reproduction was not exercised");
    require(fr.workerFocusIsNotFader,
            "worker-thread GetFocus() unexpectedly equaled the fader; "
            "thread-relative focus could not be distinguished");
    require(fr.msaaFocused,
            "MSAA get_accState did not report STATE_SYSTEM_FOCUSED when the "
            "focused fader was queried from a worker thread");
    require(fr.msaaFocusSelf,
            "MSAA get_accFocus did not identify the fader when queried from a "
            "worker thread");
    require(fr.uiaHasFocus,
            "UIA HasKeyboardFocus was false when the focused fader was queried "
            "from a worker thread");
  }

  // ---- Requirement 3 & 4 (value): Up, then resolve the VALUECHANGE ----
  IUIAutomation* notificationBase = nullptr;
  IUIAutomation5* notificationUia = nullptr;
  IUIAutomationElement* notificationElement = nullptr;
  NotificationRecorder* notificationRecorder = nullptr;
  bool notificationTestAvailable = NotificationApiAvailable();
  if (notificationTestAvailable) {
    notificationBase = CreateAutomation();
    require(notificationBase != nullptr,
            "UI Automation client unavailable for notification test");
    if (FAILED(notificationBase->QueryInterface(
            __uuidof(IUIAutomation5),
            reinterpret_cast<void**>(&notificationUia))) ||
        notificationUia == nullptr) {
      notificationBase->Release();
      notificationBase = nullptr;
      notificationTestAvailable = false;
    } else {
      require(SUCCEEDED(notificationUia->ElementFromHandle(
                  fader, &notificationElement)) && notificationElement != nullptr,
              "UI Automation could not retrieve the notification source");
      notificationRecorder = new NotificationRecorder;
      require(SUCCEEDED(notificationUia->AddNotificationEventHandler(
                  notificationElement, TreeScope_Element, nullptr,
                  notificationRecorder)),
              "could not register the UIA notification handler");
    }
  }

  ClearEvents();
  SendMessageW(fader, WM_KEYDOWN, VK_UP, 0);
  PumpFor(400);
  {
    std::vector<WinEventRec> evs;
    EnterCriticalSection(&g_lock);
    evs = g_events;
    LeaveCriticalSection(&g_lock);
    require(HasWinEvent(EVENT_OBJECT_VALUECHANGE, fader, OBJID_CLIENT, evs),
            "pressing Up produced no EVENT_OBJECT_VALUECHANGE on the fader");

    bool validated = false;
    for (const auto& e : evs) {
      if (e.event == EVENT_OBJECT_VALUECHANGE && e.hwnd == fader &&
          e.idObject == OBJID_CLIENT) {
        IAccessible* acc = ResolveEventToFader(e, fader, ROLE_SYSTEM_SLIDER);
        const VARIANT self = SelfChild();
        const std::wstring name = accGetName(acc, self);
        require(IsBandFrequencyName(name),
                "value-change object name is not the band/frequency name");
        const std::wstring value = accGetValue(acc, self);
        const double afterDb = ParseDb(value);
        require(std::abs((afterDb - beforeDb) - 1.0) < 1.0e-6,
                "value-change did not report the 1 dB step (before=" +
                    std::to_string(beforeDb) + " after=" + std::to_string(afterDb) +
                    ")");
        acc->Release();
        validated = true;
        break;
      }
    }
    require(validated, "the fader value-change event could not be validated");

    require(!HasWinEventOnHwnd(EVENT_OBJECT_VALUECHANGE, readout, evs),
            "background readout edit emitted EVENT_OBJECT_VALUECHANGE on band "
            "value change; it is competing with the fader announcement");
  }
  if (notificationTestAvailable) {
    require(notificationRecorder->count() > 0,
            "pressing Up produced no UIA value notification for Narrator");
    require(notificationRecorder->lastText() == L"-5.234 dB",
            "Narrator notification did not carry the canonical stepped dB value");
    notificationUia->RemoveNotificationEventHandler(notificationElement,
                                                      notificationRecorder);
    notificationRecorder->Release();
    notificationElement->Release();
    notificationUia->Release();
    notificationBase->Release();
  } else {
    std::puts("UIA notification client support unavailable; assertion skipped");
  }

  // ---- UIA Value reflects the stepped value without losing the fraction ----
  {
    IUIAutomation* uia = CreateAutomation();
    require(uia != nullptr, "UI Automation client could not be created");
    IUIAutomationElement* elFader = nullptr;
    require(SUCCEEDED(uia->ElementFromHandle(fader, &elFader)) && elFader != nullptr,
            "UI Automation could not retrieve the band fader element");
    std::wstring curVal;
    require(UiaValue(elFader, curVal),
            "UI Automation band does not expose Value after value change");
    require(curVal == L"-5.234 dB" &&
                std::abs(ParseDb(curVal) - (beforeDb + 1.0)) < 1.0e-9,
            "UI Automation Value did not preserve the fractional 1 dB step");
    elFader->Release();
    uia->Release();
  }

  // ---- Requirement 4 (steady state): while the band stays focused across
  // several refresh cycles, the readout must not emit a competing VALUECHANGE.
  ClearEvents();
  PumpFor(500);
  {
    std::vector<WinEventRec> evs;
    EnterCriticalSection(&g_lock);
    evs = g_events;
    LeaveCriticalSection(&g_lock);
    require(!HasWinEventOnHwnd(EVENT_OBJECT_VALUECHANGE, readout, evs),
            "background readout edit emitted a competing VALUECHANGE while a "
            "band was focused");
  }

  if (hkF) UnhookWinEvent(hkF);
  if (hkV) UnhookWinEvent(hkV);

  gui->hide(plugin);
  gui->destroy(plugin);
  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  DestroyWindow(wrapper);
  DestroyWindow(host);
  DeleteCriticalSection(&g_lock);
  if (uninit) CoUninitialize();
}

}  // namespace

int main(int argc, char** argv) {
  try {
    require(argc == 2,
            "usage: tonetrace-win32-a11y-tests <Tone Trace EQ.clap>");
    Run(argv[1]);
    std::printf("Tone Trace Win32 accessibility regression passed\n");
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Tone Trace Win32 accessibility regression FAILED: %s\n",
                 e.what());
    return 1;
  }
}
