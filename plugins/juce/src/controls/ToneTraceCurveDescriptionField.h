#pragma once

#include "ToneTraceReadOnlyField.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

class ToneTraceProcessor;

class ToneTraceCurveDescriptionField final : public ToneTraceReadOnlyField
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

    void queueReport(ToneTraceProcessor&, int stage);
    void workerLoop();

    const bool contextSensitive_ = true;
    int stage_ = -1;
    int lastStatus_ = -1;
    std::uint64_t generation_ = 0;
    juce::String cachedDescription_;

    std::mutex workerMutex_;
    std::condition_variable workerCv_;
    std::optional<ReportJob> pendingJob_;
    bool workerExit_ = false;
    std::thread worker_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneTraceCurveDescriptionField)
};
