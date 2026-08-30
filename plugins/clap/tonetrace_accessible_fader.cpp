#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "tonetrace_accessible_fader.h"
#include "tonetrace_band_value.h"

#include <oleacc.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <functional>
#include <limits>
#include <iterator>
#include <new>
#include <string>

namespace tonetrace::win32 {
namespace {

constexpr wchar_t kFaderClassName[] = L"ToneTraceAccessibleFader";
constexpr wchar_t kProviderProperty[] = L"ToneTraceAccessibleFaderProvider";

// Accessibility clients (NVDA/Narrator) may invoke the provider on a UIA worker
// thread, not the fader's GUI thread. All editor/plugin state access must stay
// on the GUI thread where that state is owned. This marshals a callable to the
// fader's GUI thread synchronously (SendMessage), so getters never traverse
// mutable plugin state off-thread and SetValue never mutates it off-thread.
constexpr UINT kFaderMarshalMessage = WM_APP + 0x52;

struct FaderMarshalJob {
  std::function<void()> fn;
};

template <typename F>
void RunOnGuiThread(HWND window, F&& fn) {
  if (window == nullptr) return;
  const DWORD caller = GetCurrentThreadId();
  const DWORD gui = GetWindowThreadProcessId(window, nullptr);
  if (caller == gui) {
    fn();
    return;
  }
  FaderMarshalJob job{std::forward<F>(fn)};
  SendMessageW(window, kFaderMarshalMessage, 0, reinterpret_cast<LPARAM>(&job));
}

// UI Automation identifier values are documented by Microsoft as part of
// UIAutomationClient.h. This provider intentionally does not include that
// client header because Windows SDK 10.0.26100 can collide when client and
// provider IDL declarations are pulled into the same translation unit.
constexpr PATTERNID kUiaValuePatternId = 10002;
constexpr PROPERTYID kUiaControlTypePropertyId = 30003;
constexpr PROPERTYID kUiaNamePropertyId = 30005;
constexpr PROPERTYID kUiaHasKeyboardFocusPropertyId = 30008;
constexpr PROPERTYID kUiaIsKeyboardFocusablePropertyId = 30009;
constexpr PROPERTYID kUiaIsEnabledPropertyId = 30010;
constexpr PROPERTYID kUiaAutomationIdPropertyId = 30011;
constexpr PROPERTYID kUiaHelpTextPropertyId = 30013;
constexpr PROPERTYID kUiaIsControlElementPropertyId = 30016;
constexpr PROPERTYID kUiaIsContentElementPropertyId = 30017;
constexpr PROPERTYID kUiaValueValuePropertyId = 30045;
constexpr CONTROLTYPEID kUiaEditControlTypeId = 50004;
constexpr EVENTID kUiaAutomationFocusChangedEventId = 20005;

struct FaderState {
  int band = -1;
  AccessibleFaderCallbacks callbacks{};
  double lastValue = std::numeric_limits<double>::quiet_NaN();
  int wheelRemainder = 0;
  bool wheelShift = false;
  HFONT font = nullptr;  // Borrowed from the editor; never owned here.
  // Thread-relative GetFocus() is unreliable here: UIA queries the provider
  // from a UIA worker thread, so GetFocus() would report the wrong thread's
  // focus. Track focus on the GUI thread in WM_SETFOCUS/WM_KILLFOCUS and read
  // this flag from the provider instead.
  std::atomic<bool> focused_{false};
  std::wstring lastText;  // Avoid SetWindowTextW/InvalidateRect when unchanged.
};

FaderState* stateFor(HWND window) {
  return reinterpret_cast<FaderState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

double scaleFor(const FaderState* state) {
  if (state == nullptr || state->callbacks.getScale == nullptr) return 1.0;
  const double scale = state->callbacks.getScale(state->callbacks.context);
  return std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
}

int scaled(const FaderState* state, int value) {
  return static_cast<int>(std::lround(static_cast<double>(value) * scaleFor(state)));
}

double valueFor(const FaderState* state) {
  if (state == nullptr || state->callbacks.getValue == nullptr) return 0.0;
  const double value = state->callbacks.getValue(state->callbacks.context, state->band);
  return std::isfinite(value) ? value : 0.0;
}

double minimumFor(const FaderState* state) {
  if (state == nullptr || state->callbacks.getMinimum == nullptr) return -60.0;
  const double value = state->callbacks.getMinimum(state->callbacks.context, state->band);
  return std::isfinite(value) ? value : -60.0;
}

double maximumFor(const FaderState* state) {
  if (state == nullptr || state->callbacks.getMaximum == nullptr) return 60.0;
  const double value = state->callbacks.getMaximum(state->callbacks.context, state->band);
  return std::isfinite(value) ? value : 60.0;
}

std::wstring nameFor(const FaderState* state) {
  if (state == nullptr) return L"Tone Trace band";
  if (state->callbacks.getName != nullptr) {
    std::wstring name = state->callbacks.getName(state->callbacks.context, state->band);
    if (!name.empty()) return name;
  }
  return L"Tone Trace band " + std::to_wstring(state->band + 1);
}

std::wstring valueText(double value) {
  return tonetrace::formatBandValueDbWide(value);
}

std::wstring fullText(const FaderState* state) {
  return nameFor(state) + L": " + valueText(valueFor(state));
}

bool parseExactValue(const wchar_t* text, double& value) {
  if (text == nullptr) return false;
  wchar_t* end = nullptr;
  value = std::wcstod(text, &end);
  if (end == text || !std::isfinite(value)) return false;
  while (*end == L' ' || *end == L'\t') ++end;
  if ((end[0] == L'd' || end[0] == L'D') &&
      (end[1] == L'b' || end[1] == L'B')) {
    end += 2;
    while (*end == L' ' || *end == L'\t') ++end;
  }
  return *end == L'\0';
}

class FaderAccessibleProvider final : public IAccessible,
                                      public IRawElementProviderSimple,
                                      public IValueProvider {
 public:
  explicit FaderAccessibleProvider(HWND window) : window_(window) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
      *object = static_cast<IAccessible*>(this);
    } else if (riid == __uuidof(IRawElementProviderSimple)) {
      *object = static_cast<IRawElementProviderSimple*>(this);
    } else if (riid == __uuidof(IValueProvider)) {
      *object = static_cast<IValueProvider*>(this);
    } else {
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG value = --references_;
    if (value == 0) delete this;
    return value;
  }

  void disconnect() { window_ = nullptr; }

  HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
    if (count == nullptr) return E_POINTER;
    *count = 0;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID,
                                          DISPID*) override {
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*,
                                   VARIANT*, EXCEPINFO*, UINT*) override {
    return E_NOTIMPL;
  }

  HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) override {
    if (parent == nullptr) return E_POINTER;
    *parent = nullptr;
    if (!valid()) return S_FALSE;
    HWND parentWindow = GetParent(window_);
    if (parentWindow == nullptr) return S_FALSE;
    IAccessible* accessible = nullptr;
    const HRESULT result = AccessibleObjectFromWindow(
        parentWindow, OBJID_WINDOW, IID_IAccessible,
        reinterpret_cast<void**>(&accessible));
    if (FAILED(result) || accessible == nullptr) return S_FALSE;
    *parent = static_cast<IDispatch*>(accessible);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) override {
    if (count == nullptr) return E_POINTER;
    *count = 0;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE get_accChild(VARIANT, IDispatch** child) override {
    if (child == nullptr) return E_POINTER;
    *child = nullptr;
    return S_FALSE;
  }

  HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR* name) override {
    if (name == nullptr) return E_POINTER;
    *name = nullptr;
    if (!self(child)) return E_INVALIDARG;
    std::wstring text;
    RunOnGuiThread(window_, [&] { text = nameFor(state()); });
    *name = SysAllocString(text.c_str());
    return *name != nullptr ? S_OK : E_OUTOFMEMORY;
  }

  HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR* value) override {
    if (value == nullptr) return E_POINTER;
    *value = nullptr;
    if (!self(child)) return E_INVALIDARG;
    std::wstring text;
    RunOnGuiThread(window_, [&] { text = valueText(valueFor(state())); });
    *value = SysAllocString(text.c_str());
    return *value != nullptr ? S_OK : E_OUTOFMEMORY;
  }

  HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child,
                                                BSTR* description) override {
    if (description == nullptr) return E_POINTER;
    *description = nullptr;
    if (!self(child)) return E_INVALIDARG;
    const std::wstring text =
        L"Tone Trace correction fader. Arrow keys change 1 dB; Page Up and "
        L"Page Down change 6 dB; Home and End go to limits; 0 sets 0 dB; "
        L"Enter opens exact value entry.";
    *description = SysAllocString(text.c_str());
    return *description != nullptr ? S_OK : E_OUTOFMEMORY;
  }

  HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT* role) override {
    if (role == nullptr) return E_POINTER;
    VariantInit(role);
    if (!self(child)) return E_INVALIDARG;
    role->vt = VT_I4;
    role->lVal = ROLE_SYSTEM_SLIDER;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT* stateValue) override {
    if (stateValue == nullptr) return E_POINTER;
    VariantInit(stateValue);
    if (!self(child)) return E_INVALIDARG;
    stateValue->vt = VT_I4;
    stateValue->lVal = STATE_SYSTEM_FOCUSABLE;
    if (!valid() || IsWindowEnabled(window_) == FALSE) {
      stateValue->lVal |= STATE_SYSTEM_UNAVAILABLE;
    }
    if (keyboardFocused()) stateValue->lVal |= STATE_SYSTEM_FOCUSED;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT child, BSTR* help) override {
    return get_accDescription(child, help);
  }

  HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* helpFile, VARIANT child,
                                              long* topicId) override {
    if (helpFile != nullptr) *helpFile = nullptr;
    if (topicId != nullptr) *topicId = 0;
    return self(child) ? S_FALSE : E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child,
                                                     BSTR* shortcut) override {
    if (shortcut == nullptr) return E_POINTER;
    *shortcut = nullptr;
    if (!self(child)) return E_INVALIDARG;
    *shortcut = SysAllocString(L"Arrow keys");
    return *shortcut != nullptr ? S_OK : E_OUTOFMEMORY;
  }

  HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* focus) override {
    if (focus == nullptr) return E_POINTER;
    VariantInit(focus);
    if (keyboardFocused()) {
      focus->vt = VT_I4;
      focus->lVal = CHILDID_SELF;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* selection) override {
    if (selection == nullptr) return E_POINTER;
    VariantInit(selection);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child,
                                                  BSTR* action) override {
    if (action == nullptr) return E_POINTER;
    *action = nullptr;
    return self(child) ? S_FALSE : E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT child) override {
    if (!self(child)) return E_INVALIDARG;
    if (!valid()) return E_FAIL;
    if ((flags & SELFLAG_TAKEFOCUS) != 0) SetFocus(window_);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE accLocation(long* left, long* top, long* width,
                                        long* height, VARIANT child) override {
    if (left == nullptr || top == nullptr || width == nullptr || height == nullptr) {
      return E_POINTER;
    }
    if (!self(child) || !valid()) return E_INVALIDARG;
    RECT bounds{};
    if (!GetWindowRect(window_, &bounds)) return E_FAIL;
    *left = bounds.left;
    *top = bounds.top;
    *width = bounds.right - bounds.left;
    *height = bounds.bottom - bounds.top;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE accNavigate(long, VARIANT child,
                                        VARIANT* target) override {
    if (target == nullptr) return E_POINTER;
    VariantInit(target);
    return self(child) ? S_FALSE : E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* child) override {
    if (child == nullptr) return E_POINTER;
    VariantInit(child);
    if (!valid()) return S_FALSE;
    RECT bounds{};
    if (GetWindowRect(window_, &bounds) && PtInRect(&bounds, POINT{x, y})) {
      child->vt = VT_I4;
      child->lVal = CHILDID_SELF;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
    return self(child) ? S_FALSE : E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE put_accName(VARIANT child, BSTR) override {
    return self(child) ? E_NOTIMPL : E_INVALIDARG;
  }

  HRESULT STDMETHODCALLTYPE put_accValue(VARIANT child, BSTR value) override {
    if (!self(child) || value == nullptr) return E_INVALIDARG;
    double parsed = 0.0;
    if (!parseExactValue(value, parsed)) return E_INVALIDARG;
    bool ok = false;
    RunOnGuiThread(window_, [&] { ok = setExactValue(parsed); });
    return ok ? S_OK : E_FAIL;
  }

  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) override {
    if (options == nullptr) return E_POINTER;
    *options = static_cast<ProviderOptions>(
        ProviderOptions_ServerSideProvider | ProviderOptions_HasNativeIAccessible);
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId,
                                                IUnknown** provider) override {
    if (provider == nullptr) return E_POINTER;
    *provider = nullptr;
    if (patternId != kUiaValuePatternId) return S_OK;
    *provider = static_cast<IValueProvider*>(this);
    AddRef();
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId,
                                              VARIANT* value) override {
    if (value == nullptr) return E_POINTER;
    VariantInit(value);
    if (propertyId == kUiaControlTypePropertyId) {
      value->vt = VT_I4;
      // UIA RangeValue has no unit-bearing string, so Narrator normalizes a
      // Slider to a percentage. Value exposes the actual editable dB text.
      value->lVal = kUiaEditControlTypeId;
    } else if (propertyId == kUiaNamePropertyId) {
      std::wstring text;
      RunOnGuiThread(window_, [&] { text = nameFor(state()); });
      value->vt = VT_BSTR;
      value->bstrVal = SysAllocString(text.c_str());
      if (value->bstrVal == nullptr) return E_OUTOFMEMORY;
    } else if (propertyId == kUiaAutomationIdPropertyId) {
      wchar_t buffer[48]{};
      const FaderState* current = state();
      std::swprintf(buffer, std::size(buffer), L"ToneTraceBand%d",
                    current != nullptr ? current->band + 1 : 0);
      value->vt = VT_BSTR;
      value->bstrVal = SysAllocString(buffer);
      if (value->bstrVal == nullptr) return E_OUTOFMEMORY;
    } else if (propertyId == kUiaIsKeyboardFocusablePropertyId ||
               propertyId == kUiaIsControlElementPropertyId ||
               propertyId == kUiaIsContentElementPropertyId) {
      value->vt = VT_BOOL;
      value->boolVal = VARIANT_TRUE;
    } else if (propertyId == kUiaHasKeyboardFocusPropertyId) {
      value->vt = VT_BOOL;
      value->boolVal = keyboardFocused() ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (propertyId == kUiaIsEnabledPropertyId) {
      value->vt = VT_BOOL;
      value->boolVal = valid() && IsWindowEnabled(window_) != FALSE
                           ? VARIANT_TRUE
                           : VARIANT_FALSE;
    } else if (propertyId == kUiaHelpTextPropertyId) {
      value->vt = VT_BSTR;
      value->bstrVal = SysAllocString(
          L"Arrow keys 1 dB; Page Up/Down 6 dB; Home/End limits; 0 neutral; "
          L"Enter or right-click for exact value.");
      if (value->bstrVal == nullptr) return E_OUTOFMEMORY;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(
      IRawElementProviderSimple** provider) override {
    if (provider == nullptr) return E_POINTER;
    *provider = nullptr;
    return valid() ? UiaHostProviderFromHwnd(window_, provider) : S_OK;
  }

  HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
    double parsed = 0.0;
    if (!parseExactValue(value, parsed)) return E_INVALIDARG;
    bool ok = false;
    RunOnGuiThread(window_, [&] { ok = setExactValue(parsed); });
    return ok ? S_OK : E_FAIL;
  }

  HRESULT STDMETHODCALLTYPE get_Value(BSTR* value) override {
    if (value == nullptr) return E_POINTER;
    *value = nullptr;
    std::wstring text;
    RunOnGuiThread(window_, [&] { text = valueText(valueFor(state())); });
    *value = SysAllocString(text.c_str());
    return *value != nullptr ? S_OK : E_OUTOFMEMORY;
  }

  HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* readOnly) override {
    if (readOnly == nullptr) return E_POINTER;
    *readOnly = FALSE;
    return S_OK;
  }

 private:
  ~FaderAccessibleProvider() = default;

  bool valid() const { return window_ != nullptr && IsWindow(window_) != FALSE; }
  FaderState* state() const { return valid() ? stateFor(window_) : nullptr; }

  // Thread-safe keyboard-focus state. The provider is queried from a UIA worker
  // thread, where GetFocus() is thread-relative and would report the wrong
  // thread's focus. Use the flag set by WM_SETFOCUS/WM_KILLFOCUS instead.
  bool keyboardFocused() const {
    FaderState* current = state();
    return current != nullptr && current->focused_.load(std::memory_order_relaxed);
  }

  static bool self(const VARIANT& child) {
    return child.vt == VT_I4 && child.lVal == CHILDID_SELF;
  }

  bool setExactValue(double requested) {
    FaderState* current = state();
    if (current == nullptr || current->callbacks.setValue == nullptr ||
        !std::isfinite(requested)) {
      return false;
    }
    const double minimum = minimumFor(current);
    const double maximum = maximumFor(current);
    current->callbacks.setValue(current->callbacks.context, current->band,
                                std::clamp(requested, minimum, maximum));
    syncAccessibleFader(window_, true);
    return true;
  }

  std::atomic<ULONG> references_{1};
  HWND window_ = nullptr;
};

FaderAccessibleProvider* providerFor(HWND window, bool create) {
  auto* provider = reinterpret_cast<FaderAccessibleProvider*>(
      GetPropW(window, kProviderProperty));
  if (provider == nullptr && create) {
    provider = new (std::nothrow) FaderAccessibleProvider(window);
    if (provider != nullptr) SetPropW(window, kProviderProperty, provider);
  }
  return provider;
}

void raiseValueChanged(HWND window, double oldValue, double newValue) {
  NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, window, OBJID_CLIENT, CHILDID_SELF);
  FaderAccessibleProvider* provider = providerFor(window, true);
  if (provider == nullptr) return;
  VARIANT oldVariant{};
  oldVariant.vt = VT_BSTR;
  oldVariant.bstrVal = SysAllocString(valueText(oldValue).c_str());
  VARIANT newVariant{};
  newVariant.vt = VT_BSTR;
  newVariant.bstrVal = SysAllocString(valueText(newValue).c_str());
  if (oldVariant.bstrVal != nullptr && newVariant.bstrVal != nullptr) {
    UiaRaiseAutomationPropertyChangedEvent(
        static_cast<IRawElementProviderSimple*>(provider),
        kUiaValueValuePropertyId, oldVariant, newVariant);

    // Narrator reads the unit-bearing Value on focus, but does not announce a
    // custom Edit provider's Value-property change after Up/Down. A focused
    // UIA notification supplies that same canonical dB string without changing
    // NVDA's established MSAA VALUECHANGE path. MostRecent coalesces rapid key
    // repeats instead of building an announcement backlog.
    FaderState* current = stateFor(window);
    if (current != nullptr &&
        current->focused_.load(std::memory_order_relaxed)) {
      BSTR activity = SysAllocString(L"ToneTraceBandValue");
      if (activity != nullptr) {
        UiaRaiseNotificationEvent(
            static_cast<IRawElementProviderSimple*>(provider),
            NotificationKind_ActionCompleted, NotificationProcessing_MostRecent,
            newVariant.bstrVal, activity);
        SysFreeString(activity);
      }
    }
  }
  VariantClear(&oldVariant);
  VariantClear(&newVariant);
}

void raiseFocus(HWND window) {
  NotifyWinEvent(EVENT_OBJECT_FOCUS, window, OBJID_CLIENT, CHILDID_SELF);
  FaderAccessibleProvider* provider = providerFor(window, true);
  if (provider != nullptr) {
    UiaRaiseAutomationEvent(static_cast<IRawElementProviderSimple*>(provider),
                            kUiaAutomationFocusChangedEventId);
  }
}

void setValue(HWND window, double requested) {
  FaderState* state = stateFor(window);
  if (state == nullptr || state->callbacks.setValue == nullptr) return;
  const double minimum = minimumFor(state);
  const double maximum = maximumFor(state);
  state->callbacks.setValue(state->callbacks.context, state->band,
                            std::clamp(requested, minimum, maximum));
  syncAccessibleFader(window, true);
}

void stepValue(HWND window, double delta) {
  FaderState* state = stateFor(window);
  if (state == nullptr) return;
  setValue(window, valueFor(state) + delta);
}

struct ExactEditState {
  HWND fader = nullptr;
  WNDPROC original = nullptr;
  bool closing = false;
};

bool commitExactEditor(HWND edit, bool restoreFocus) {
  auto* state = reinterpret_cast<ExactEditState*>(
      GetWindowLongPtrW(edit, GWLP_USERDATA));
  if (state == nullptr || state->closing || state->fader == nullptr) return false;
  wchar_t text[128]{};
  GetWindowTextW(edit, text, static_cast<int>(std::size(text)));
  double value = 0.0;
  if (!parseExactValue(text, value)) {
    MessageBeep(MB_ICONWARNING);
    return false;
  }
  HWND fader = state->fader;
  setValue(fader, value);
  state->closing = true;
  DestroyWindow(edit);
  if (restoreFocus && IsWindow(fader)) SetFocus(fader);
  return true;
}

LRESULT CALLBACK exactEditProc(HWND window, UINT message, WPARAM wParam,
                               LPARAM lParam) {
  auto* state = reinterpret_cast<ExactEditState*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  WNDPROC original = state != nullptr ? state->original : DefWindowProcW;

  if (message == WM_KEYDOWN && state != nullptr) {
    if (wParam == VK_RETURN) {
      if (commitExactEditor(window, true)) return 0;
    } else if (wParam == VK_ESCAPE) {
      HWND fader = state->fader;
      state->closing = true;
      DestroyWindow(window);
      if (IsWindow(fader)) SetFocus(fader);
      return 0;
    }
  } else if (message == WM_KILLFOCUS && state != nullptr && !state->closing) {
    wchar_t text[128]{};
    GetWindowTextW(window, text, static_cast<int>(std::size(text)));
    double value = 0.0;
    if (parseExactValue(text, value) && IsWindow(state->fader)) {
      setValue(state->fader, value);
    }
    state->closing = true;
    DestroyWindow(window);
    return 0;
  }

  const LRESULT result = CallWindowProcW(original, window, message, wParam, lParam);
  if (message == WM_NCDESTROY && state != nullptr) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    delete state;
  }
  return result;
}

void openExactEditor(HWND fader, POINT anchor) {
  FaderState* state = stateFor(fader);
  if (state == nullptr) return;

  RECT faderRect{};
  GetWindowRect(fader, &faderRect);
  if (anchor.x == -1 && anchor.y == -1) {
    anchor.x = (faderRect.left + faderRect.right) / 2;
    anchor.y = (faderRect.top + faderRect.bottom) / 2;
  }

  const int width = std::max(90, scaled(state, 112));
  const int height = std::max(24, scaled(state, 30));
  int x = anchor.x + scaled(state, 8);
  int y = anchor.y - height / 2;

  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  if (x + width > work.right) x = anchor.x - width - scaled(state, 8);
  x = std::clamp(x, static_cast<int>(work.left),
                 std::max(static_cast<int>(work.left),
                          static_cast<int>(work.right) - width));
  y = std::clamp(y, static_cast<int>(work.top),
                 std::max(static_cast<int>(work.top),
                          static_cast<int>(work.bottom) - height));

  const std::wstring initial =
      tonetrace::formatBandValueDbWide(valueFor(state), false);
  HWND owner = GetAncestor(fader, GA_ROOT);
  if (owner == nullptr) owner = GetParent(fader);
  HWND edit = CreateWindowExW(
      WS_EX_TOOLWINDOW, L"EDIT", initial.c_str(),
      WS_POPUP | WS_BORDER | ES_AUTOHSCROLL,
      x, y, width, height, owner, nullptr,
      reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(fader, GWLP_HINSTANCE)), nullptr);
  if (edit == nullptr) return;

  auto* editState = new (std::nothrow) ExactEditState;
  if (editState == nullptr) {
    DestroyWindow(edit);
    return;
  }
  editState->fader = fader;
  editState->original = reinterpret_cast<WNDPROC>(
      GetWindowLongPtrW(edit, GWLP_WNDPROC));
  SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editState));
  SetWindowLongPtrW(edit, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&exactEditProc));

  HFONT font = reinterpret_cast<HFONT>(SendMessageW(fader, WM_GETFONT, 0, 0));
  if (font != nullptr) SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  ShowWindow(edit, SW_SHOWNOACTIVATE);
  SetWindowPos(edit, HWND_TOP, x, y, width, height, SWP_SHOWWINDOW);
  SetFocus(edit);
  SendMessageW(edit, EM_SETSEL, 0, -1);
}

bool cursorInsideWindow(HWND window) {
  POINT point{};
  RECT bounds{};
  return GetCursorPos(&point) && ScreenToClient(window, &point) &&
         GetClientRect(window, &bounds) && PtInRect(&bounds, point) != FALSE;
}

LRESULT CALLBACK faderProc(HWND window, UINT message, WPARAM wParam,
                           LPARAM lParam) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
    auto* state = static_cast<FaderState*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
  }

  if (message == kFaderMarshalMessage) {
    auto* job = reinterpret_cast<FaderMarshalJob*>(lParam);
    if (job != nullptr && job->fn) job->fn();
    return 0;
  }

  FaderState* state = stateFor(window);

  if (message == WM_SETFONT && state != nullptr) {
    state->font = reinterpret_cast<HFONT>(wParam);
    if (LOWORD(lParam) != 0) InvalidateRect(window, nullptr, TRUE);
    return 0;
  }
  if (message == WM_GETFONT && state != nullptr) {
    return reinterpret_cast<LRESULT>(state->font);
  }

  if (message == WM_GETOBJECT) {
    FaderAccessibleProvider* provider = providerFor(window, true);
    if (provider != nullptr) {
      const DWORD objectId = static_cast<DWORD>(lParam);
      if (objectId == static_cast<DWORD>(UiaRootObjectId)) {
        return UiaReturnRawElementProvider(
            window, wParam, lParam,
            static_cast<IRawElementProviderSimple*>(provider));
      }
      if (objectId == static_cast<DWORD>(OBJID_CLIENT)) {
        return LresultFromObject(IID_IAccessible, wParam,
                                 static_cast<IAccessible*>(provider));
      }
    }
  }

  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window, &paint);
    RECT rect{};
    GetClientRect(window, &rect);
    const bool focused = GetFocus() == window;
    const bool hovered = cursorInsideWindow(window);

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

    const int valueHeight = scaled(state, 24);
    const int trackTop = rect.top + scaled(state, 10);
    const int trackBottom = std::max(trackTop + 1,
                                     static_cast<int>(rect.bottom) - valueHeight -
                                         scaled(state, 8));
    const int trackX = (rect.left + rect.right) / 2;

    HPEN trackPen = CreatePen(PS_SOLID, std::max(1, scaled(state, 2)),
                              RGB(112, 112, 124));
    oldPen = SelectObject(dc, trackPen);
    MoveToEx(dc, trackX, trackTop, nullptr);
    LineTo(dc, trackX, trackBottom);
    SelectObject(dc, oldPen);
    DeleteObject(trackPen);

    const int zeroY = (trackTop + trackBottom) / 2;
    HPEN zeroPen = CreatePen(PS_SOLID, 1, RGB(150, 150, 164));
    oldPen = SelectObject(dc, zeroPen);
    MoveToEx(dc, rect.left + scaled(state, 6), zeroY, nullptr);
    LineTo(dc, rect.right - scaled(state, 6), zeroY);
    SelectObject(dc, oldPen);
    DeleteObject(zeroPen);

    const double lo = minimumFor(state);
    const double hi = maximumFor(state);
    const double db = valueFor(state);
    const double normalized = hi > lo
                                  ? std::clamp((db - lo) / (hi - lo), 0.0, 1.0)
                                  : 0.5;
    const int thumbY = static_cast<int>(std::lround(
        trackBottom - normalized * static_cast<double>(trackBottom - trackTop)));
    HPEN levelPen = CreatePen(PS_SOLID, std::max(1, scaled(state, 3)),
                              db >= 0.0 ? RGB(104, 210, 188)
                                        : RGB(102, 166, 224));
    oldPen = SelectObject(dc, levelPen);
    MoveToEx(dc, trackX, zeroY, nullptr);
    LineTo(dc, trackX, thumbY);
    SelectObject(dc, oldPen);
    DeleteObject(levelPen);

    RECT thumb{trackX - scaled(state, 13), thumbY - scaled(state, 5),
               trackX + scaled(state, 13), thumbY + scaled(state, 5)};
    HBRUSH thumbBrush = CreateSolidBrush(
        focused ? RGB(255, 240, 210)
                : hovered ? RGB(190, 245, 232) : RGB(226, 226, 232));
    const int thumbSaved = SaveDC(dc);
    SelectObject(dc, thumbBrush);
    SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, thumb.left, thumb.top, thumb.right, thumb.bottom,
              scaled(state, 4), scaled(state, 4));
    RestoreDC(dc, thumbSaved);
    DeleteObject(thumbBrush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, focused ? RGB(255, 240, 210) : RGB(232, 232, 238));
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    const std::wstring value = valueText(db);
    RECT valueRect{rect.left + 2, rect.bottom - valueHeight, rect.right - 2,
                   rect.bottom - 2};
    DrawTextW(dc, value.c_str(), -1, &valueRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (oldFont != nullptr) SelectObject(dc, oldFont);
    EndPaint(window, &paint);
    return 0;
  }

  if (message == WM_ERASEBKGND) return 1;

  if (message == WM_MOUSEMOVE) {
    TRACKMOUSEEVENT tracking{};
    tracking.cbSize = sizeof(tracking);
    tracking.dwFlags = TME_LEAVE;
    tracking.hwndTrack = window;
    TrackMouseEvent(&tracking);
    if (GetCapture() != window) InvalidateRect(window, nullptr, FALSE);
  } else if (message == WM_MOUSELEAVE) {
    InvalidateRect(window, nullptr, FALSE);
  }

  if (message == WM_SETCURSOR) {
    SetCursor(LoadCursor(nullptr, IDC_SIZENS));
    return TRUE;
  }

  const auto setFromY = [&](int y) {
    if (state == nullptr) return;
    RECT rect{};
    GetClientRect(window, &rect);
    const int valueHeight = scaled(state, 24);
    const int trackTop = rect.top + scaled(state, 10);
    const int trackBottom = std::max(trackTop + 1,
                                     static_cast<int>(rect.bottom) - valueHeight -
                                         scaled(state, 8));
    const double lo = minimumFor(state);
    const double hi = maximumFor(state);
    const double normalized = std::clamp(
        static_cast<double>(trackBottom - y) /
            static_cast<double>(trackBottom - trackTop),
        0.0, 1.0);
    setValue(window, std::lround(lo + normalized * (hi - lo)));
  };

  if (message == WM_LBUTTONDOWN) {
    SetFocus(window);
    SetCapture(window);
    setFromY(GET_Y_LPARAM(lParam));
    return 0;
  }
  if (message == WM_MOUSEMOVE && GetCapture() == window) {
    setFromY(GET_Y_LPARAM(lParam));
    return 0;
  }
  if (message == WM_LBUTTONUP && GetCapture() == window) {
    setFromY(GET_Y_LPARAM(lParam));
    ReleaseCapture();
    return 0;
  }
  if (message == WM_LBUTTONDBLCLK) {
    setValue(window, 0.0);
    return 0;
  }
  if (message == WM_CONTEXTMENU) {
    POINT anchor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    openExactEditor(window, anchor);
    return 0;
  }
  if (message == WM_MOUSEWHEEL && state != nullptr) {
    SetFocus(window);
    const bool shift = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;
    if (shift != state->wheelShift) {
      state->wheelRemainder = 0;
      state->wheelShift = shift;
    }
    state->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
    const int notches = state->wheelRemainder / WHEEL_DELTA;
    state->wheelRemainder %= WHEEL_DELTA;
    if (notches != 0) stepValue(window, static_cast<double>(notches * (shift ? 6 : 1)));
    return 0;
  }

  if (message == WM_SETFOCUS) {
    if (state != nullptr) state->focused_.store(true, std::memory_order_relaxed);
    syncAccessibleFader(window, false);
    if (state != nullptr && state->callbacks.onFocus != nullptr) {
      state->callbacks.onFocus(state->callbacks.context, state->band);
    }
    raiseFocus(window);
    InvalidateRect(window, nullptr, TRUE);
    return 0;
  }
  if (message == WM_KILLFOCUS) {
    if (state != nullptr) state->focused_.store(false, std::memory_order_relaxed);
    if (GetCapture() == window) ReleaseCapture();
    InvalidateRect(window, nullptr, TRUE);
    return 0;
  }

  if (message == WM_GETDLGCODE) {
    LRESULT code = DLGC_WANTARROWS;
    if (lParam != 0) {
      const auto* msg = reinterpret_cast<const MSG*>(lParam);
      if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN) {
        code |= DLGC_WANTMESSAGE;
      }
    }
    return code;
  }

  if (message == WM_KEYDOWN && state != nullptr) {
    switch (static_cast<int>(wParam)) {
      case VK_UP:
      case VK_RIGHT:
        stepValue(window, 1.0);
        return 0;
      case VK_DOWN:
      case VK_LEFT:
        stepValue(window, -1.0);
        return 0;
      case VK_PRIOR:
        stepValue(window, 6.0);
        return 0;
      case VK_NEXT:
        stepValue(window, -6.0);
        return 0;
      case VK_HOME:
        setValue(window, maximumFor(state));
        return 0;
      case VK_END:
        setValue(window, minimumFor(state));
        return 0;
      case VK_RETURN: {
        RECT bounds{};
        GetWindowRect(window, &bounds);
        POINT anchor{(bounds.left + bounds.right) / 2,
                     (bounds.top + bounds.bottom) / 2};
        openExactEditor(window, anchor);
        return 0;
      }
      case '0':
      case VK_NUMPAD0:
      case 'N':
        setValue(window, 0.0);
        return 0;
      case 'T':
      case VK_F2:
        if (state->callbacks.toggleTrace != nullptr) {
          state->callbacks.toggleTrace(state->callbacks.context);
        }
        return 0;
      default:
        break;
    }
  }

  if (message == WM_DESTROY) {
    UiaReturnRawElementProvider(window, 0, 0, nullptr);
  }

  if (message == WM_NCDESTROY) {
    if (auto* provider = reinterpret_cast<FaderAccessibleProvider*>(
            RemovePropW(window, kProviderProperty))) {
      provider->disconnect();
      provider->Release();
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    delete state;
    return DefWindowProcW(window, message, wParam, lParam);
  }

  return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

bool registerAccessibleFaderClass(HINSTANCE instance) {
  WNDCLASSEXW existing{};
  existing.cbSize = sizeof(existing);
  if (GetClassInfoExW(instance, kFaderClassName, &existing) != FALSE) return true;

  WNDCLASSEXW klass{};
  klass.cbSize = sizeof(klass);
  klass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  klass.lpfnWndProc = faderProc;
  klass.hInstance = instance;
  klass.hCursor = LoadCursor(nullptr, IDC_SIZENS);
  klass.hbrBackground = nullptr;
  klass.lpszClassName = kFaderClassName;
  return RegisterClassExW(&klass) != 0;
}

void unregisterAccessibleFaderClass(HINSTANCE instance) {
  UnregisterClassW(kFaderClassName, instance);
}

HWND createAccessibleFader(HWND parent, HINSTANCE instance, int controlId,
                           int band, const AccessibleFaderCallbacks& callbacks) {
  if (parent == nullptr || !registerAccessibleFaderClass(instance)) return nullptr;

  auto* state = new (std::nothrow) FaderState;
  if (state == nullptr) return nullptr;
  state->band = band;
  state->callbacks = callbacks;
  state->wheelShift = false;

  HWND window = CreateWindowExW(
      0, kFaderClassName, L"",
      WS_CHILD | WS_TABSTOP | WS_BORDER,
      0, 0, 4, 4, parent,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
      instance, state);
  if (window == nullptr) {
    delete state;
    return nullptr;
  }
  syncAccessibleFader(window, false);
  return window;
}

void syncAccessibleFader(HWND window, bool announceChange) {
  FaderState* state = stateFor(window);
  if (state == nullptr) return;
  const double value = valueFor(state);
  const double oldValue = state->lastValue;
  const std::wstring text = fullText(state);
  // SetWindowTextW raises EVENT_OBJECT_NAMECHANGE every time it is called, even
  // when the text is unchanged. Only touch the window when the text really
  // changed; this is the only place the editor's 33 ms refresh writes to the
  // faders, so it is what was producing a continuous NAMECHANGE stream.
  if (text != state->lastText) {
    state->lastText = text;
    SetWindowTextW(window, text.c_str());
    InvalidateRect(window, nullptr, TRUE);
  }
  state->lastValue = value;
  if (announceChange && std::isfinite(oldValue) &&
      std::abs(oldValue - value) > 1.0e-9) {
    raiseValueChanged(window, oldValue, value);
  }
}

}  // namespace tonetrace::win32
