#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace tonetrace::win32 {

struct AccessibleFaderCallbacks {
  void* context = nullptr;
  double (*getValue)(void* context, int band) = nullptr;
  double (*getMinimum)(void* context, int band) = nullptr;
  double (*getMaximum)(void* context, int band) = nullptr;
  void (*setValue)(void* context, int band, double value) = nullptr;
  std::wstring (*getName)(void* context, int band) = nullptr;
  void (*onFocus)(void* context, int band) = nullptr;
  void (*toggleTrace)(void* context) = nullptr;
  double (*getScale)(void* context) = nullptr;
};

bool registerAccessibleFaderClass(HINSTANCE instance);
void unregisterAccessibleFaderClass(HINSTANCE instance);

HWND createAccessibleFader(HWND parent, HINSTANCE instance, int controlId,
                           int band, const AccessibleFaderCallbacks& callbacks);

void syncAccessibleFader(HWND window, bool announceChange = false);

}  // namespace tonetrace::win32
