#pragma once

#include "ToneTraceReadOnlyField.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

class ToneTraceProcessor;

class ToneTraceCurveDescriptionField final : public ToneTraceReadOnlyField, private juce::Timer
{
public:
    ToneTraceCurveDescriptionField(const juce::String& name,
                                   const juce::String& description,
                                   bool multiLine = true,
                                   bool contextSensitive = true);
    ~ToneTraceCurveDescriptionField() override;

    void setContent(const juce::String&, bool announce = false,
                    juce::AccessibilityHandler::AnnouncementPriority priority =
                        juce::AccessibilityHandler::AnnouncementPriority::high);


private:
    struct ReportJob
    {
        ToneTraceProcessor* processor = nullptr;
        int stage = 0;
        std::uint64_t generation = 0;
    };

    void setContentImpl(const std::function<juce::String()>& normalDescription,
                        bool announce,
                        juce::AccessibilityHandler::AnnouncementPriority priority);
    void queueReport(ToneTraceProcessor&, int stage);
    void workerLoop();
    void timerCallback() override;

    const bool contextSensitive_ = true;
    int stage_ = -1;
    double reportedCaptureSeconds_ = -1.0;
    double lastCaptureReportMs_ = 0.0;
    std::uint64_t reportRevision_ = 0;
    std::uint64_t generation_ = 0;
    juce::String cachedDescription_;

    std::mutex workerMutex_;
    std::condition_variable workerCv_;
    std::optional<ReportJob> pendingJob_;
    bool workerExit_ = false;
    std::thread worker_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceCurveDescriptionField)
};
