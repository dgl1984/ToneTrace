#pragma once

#include <memory>

namespace tonetrace::platform
{
// Windows-only parity backend for released 1.0.5 trace navigation when the
// host is not calling the audio processor. The implementation owns all Win32
// details so the JUCE processor remains format/platform neutral.
class StoppedTransportTone final
{
public:
    StoppedTransportTone();
    ~StoppedTransportTone();

    StoppedTransportTone(const StoppedTransportTone&) = delete;
    StoppedTransportTone& operator=(const StoppedTransportTone&) = delete;

    void requestTone(double frequencyHz) noexcept;
    void requestSweep(double fromHz, double toHz, int bandCount) noexcept;
    void cancel() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
