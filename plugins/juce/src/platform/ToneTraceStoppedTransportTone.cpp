#include "platform/ToneTraceStoppedTransportTone.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#if defined(_WIN32) && !defined(TONETRACE_DISABLE_STOPPED_TRANSPORT_BEEP)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace tonetrace::platform
{
struct StoppedTransportTone::Impl final
{
#if defined(_WIN32) && !defined(TONETRACE_DISABLE_STOPPED_TRANSPORT_BEEP)
    Impl() : thread([this] { run(); }) {}

    ~Impl()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            terminating.store(true, std::memory_order_release);
            ++requestSequence;
        }
        cv.notify_all();
        if (thread.joinable()) thread.join();
    }

    void requestTone(double frequencyHz) noexcept
    {
        if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0) return;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            commandType = 1;
            frequency = frequencyHz;
            ++requestSequence;
        }
        cv.notify_one();
    }

    void requestSweep(double fromHz, double toHz, int bandCountIn) noexcept
    {
        if (!std::isfinite(fromHz) || !std::isfinite(toHz)
            || fromHz <= 0.0 || toHz <= 0.0 || bandCountIn <= 0)
            return;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            commandType = 2;
            sweepFromHz = fromHz;
            sweepToHz = toHz;
            bandCount = bandCountIn;
            ++requestSequence;
        }
        cv.notify_one();
    }

    void cancel() noexcept
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            commandType = 0;
            ++requestSequence;
        }
        cv.notify_one();
    }

    void run() noexcept
    {
        while (true)
        {
            int type = 0;
            double toneFrequency = 0.0;
            double fromHz = 0.0;
            double toHz = 0.0;
            int count = 0;
            std::uint64_t sequence = 0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this] { return terminating.load(std::memory_order_acquire) || commandType != 0; });
                if (terminating.load(std::memory_order_acquire)) return;
                type = commandType;
                toneFrequency = frequency;
                fromHz = sweepFromHz;
                toHz = sweepToHz;
                count = bandCount;
                sequence = requestSequence.load(std::memory_order_relaxed);
                commandType = 0;
            }

            if (type == 1)
            {
                if (std::isfinite(toneFrequency) && toneFrequency > 0.0)
                    Beep(static_cast<DWORD>(std::clamp(toneFrequency, 37.0, 32767.0)), 125);
            }
            else if (type == 2 && std::isfinite(fromHz) && std::isfinite(toHz)
                     && fromHz > 0.0 && toHz > 0.0 && count > 0)
            {
                const double ratio = toHz / fromHz;
                const double octave = std::log2(ratio);
                const int totalBeeps = 2 * count - 1;
                for (int index = 0; index < totalBeeps; ++index)
                {
                    if (requestSequence.load(std::memory_order_relaxed) != sequence
                        || terminating.load(std::memory_order_acquire))
                        break;
                    const double frequencyHz = fromHz * std::pow(
                        2.0, (static_cast<double>(index) + 0.5) * octave
                            / static_cast<double>(totalBeeps));
                    if (std::isfinite(frequencyHz) && frequencyHz > 0.0)
                        Beep(static_cast<DWORD>(std::clamp(frequencyHz, 37.0, 32767.0)), 60);
                }
            }
        }
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> terminating { false };
    std::atomic<std::uint64_t> requestSequence { 0 };
    int commandType = 0;
    double frequency = 0.0;
    double sweepFromHz = 0.0;
    double sweepToHz = 0.0;
    int bandCount = 0;
    // Declare the thread last so every synchronization primitive/state field is
    // fully constructed before run() can observe this object.
    std::thread thread;
#else
    void requestTone(double) noexcept {}
    void requestSweep(double, double, int) noexcept {}
    void cancel() noexcept {}
#endif
};

StoppedTransportTone::StoppedTransportTone() : impl_(std::make_unique<Impl>()) {}
StoppedTransportTone::~StoppedTransportTone() = default;

void StoppedTransportTone::requestTone(double frequencyHz) noexcept
{
    impl_->requestTone(frequencyHz);
}

void StoppedTransportTone::requestSweep(double fromHz, double toHz, int bandCount) noexcept
{
    impl_->requestSweep(fromHz, toHz, bandCount);
}

void StoppedTransportTone::cancel() noexcept
{
    impl_->cancel();
}
}
