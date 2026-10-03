#include "ToneTraceGraphSurface.h"
#include "ToneTraceDesignSystem.h"

#include <array>
#include <cmath>

ToneTraceGraphSurface::ToneTraceGraphSurface()
{
    setAccessible(false);
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(true, false);
}

void ToneTraceGraphSurface::mouseMove(const juce::MouseEvent& event)
{
    const auto plot = getLocalBounds().toFloat().reduced(18.0f, 30.0f);
    if (plot.getWidth() <= 0.0f) return;
    const double fraction = juce::jlimit(0.0, 1.0,
        static_cast<double>((event.position.x - plot.getX()) / plot.getWidth()));
    cursorHz_ = 20.0 * std::pow(1000.0, fraction);
    if (onInspectFrequency) onInspectFrequency(cursorHz_);
    repaint();
}

void ToneTraceGraphSurface::mouseExit(const juce::MouseEvent&)
{
    cursorHz_ = 0.0;
    repaint();
}

void ToneTraceGraphSurface::setTraceFrequency(double frequencyHz)
{
    cursorHz_ = frequencyHz;
    repaint();
}

void ToneTraceGraphSurface::updateFromState(
    const tonetrace::ProfileSnapshot* snapshot,
    const tonetrace::IrRenderSettings& renderSettings,
    double maximumCorrectionDb)
{
    reference_.clear(); target_.clear(); correction_.clear();
    if (snapshot != nullptr)
    {
        reference_.reserve(snapshot->reference.points.size());
        target_.reserve(snapshot->target.points.size());
        for (const auto& p : snapshot->reference.points)
            if (std::isfinite(p.frequencyHz) && std::isfinite(p.levelDb)) reference_.push_back({p.frequencyHz, p.levelDb});
        for (const auto& p : snapshot->target.points)
            if (std::isfinite(p.frequencyHz) && std::isfinite(p.levelDb)) target_.push_back({p.frequencyHz, p.levelDb});
    }

    constexpr int samples = 240;
    correction_.reserve(samples);
    const double low = snapshot != nullptr
        ? std::max(20.0, snapshot->uncappedModel.analysisLowHz) : 20.0;
    const double high = snapshot != nullptr
        ? std::min(20000.0, snapshot->uncappedModel.analysisHighHz) : 20000.0;
    const bool manualOnly = snapshot == nullptr && tonetrace::hasManualCorrection(renderSettings);
    if (high > low && (snapshot != nullptr || manualOnly))
    {
        for (int i = 0; i < samples; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(samples - 1);
            const double hz = low * std::pow(high / low, t);
            double value = renderSettings.correctionGainDb;
            if (snapshot != nullptr)
            {
                value = tonetrace::evaluateCorrectionAt(
                    snapshot->uncappedModel, maximumCorrectionDb, renderSettings, hz).outputDb;
            }
            else if (hz >= renderSettings.rangeLowHz && hz <= renderSettings.rangeHighHz)
            {
                value += tonetrace::manualGainDbAt(
                    hz, renderSettings.manualGains, 20.0, 20000.0);
            }
            correction_.push_back({hz, value});
        }
    }
    repaint();
}

void ToneTraceGraphSurface::paint(juce::Graphics& g)
{
    using namespace tonetrace::ui;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::panel);
    g.fillRoundedRectangle(bounds, spacing::cornerRadius);
    g.setColour(colour::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), spacing::cornerRadius, 1.0f);

    auto plot = bounds.reduced(18.0f, 30.0f);
    if (plot.getWidth() < 80.0f || plot.getHeight() < 80.0f) return;

    g.setColour(colour::grid);
    constexpr std::array<double, 10> frequencies {{ 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 }};
    for (const auto frequency : frequencies)
    {
        const auto normalised = std::log(frequency / 20.0) / std::log(20000.0 / 20.0);
        const auto x = plot.getX() + static_cast<float>(normalised) * plot.getWidth();
        g.drawVerticalLine(static_cast<int>(std::lround(x)), plot.getY(), plot.getBottom());
    }
    for (int i = 0; i <= 6; ++i)
    {
        const auto y = plot.getY() + plot.getHeight() * static_cast<float>(i) / 6.0f;
        g.drawHorizontalLine(static_cast<int>(std::lround(y)), plot.getX(), plot.getRight());
    }

    const auto xFor = [&plot](double hz)
    {
        const double f = std::clamp(hz, 20.0, 20000.0);
        return plot.getX() + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * plot.getWidth();
    };
    const auto meanOf = [](const std::vector<Point>& points)
    {
        double mean = 0.0;
        for (const auto& point : points) mean += point.valueDb;
        return points.empty() ? 0.0 : mean / static_cast<double>(points.size());
    };
    double rangeDb = 12.0;
    for (const auto* curve : { &reference_, &target_ })
    {
        const double mean = meanOf(*curve);
        for (const auto& point : *curve) rangeDb = std::max(rangeDb, std::abs(point.valueDb - mean));
    }
    for (const auto& point : correction_) rangeDb = std::max(rangeDb, std::abs(point.valueDb));
    rangeDb = std::ceil(rangeDb / 6.0) * 6.0;
    // Spectrum traces are normalized independently to show shape rather than absolute capture level.
    const auto drawSpectrum = [&](const std::vector<Point>& points, juce::Colour c)
    {
        if (points.size() < 2) return;
        double mean = 0.0; for (const auto& p : points) mean += p.valueDb; mean /= static_cast<double>(points.size());
        juce::Path path; bool started = false;
        for (const auto& p : points)
        {
            if (p.frequencyHz < 20.0 || p.frequencyHz > 20000.0) continue;
            const float y = plot.getCentreY() - static_cast<float>((p.valueDb - mean) / (2.0 * rangeDb)) * plot.getHeight();
            const float x = xFor(p.frequencyHz);
            if (!started) { path.startNewSubPath(x, y); started = true; } else path.lineTo(x, y);
        }
        g.setColour(c); g.strokePath(path, juce::PathStrokeType(1.4f));
    };
    const auto drawCorrection = [&]
    {
        if (correction_.size() < 2) return;
        juce::Path path; bool started = false;
        for (const auto& p : correction_)
        {
            const float y = plot.getCentreY() - static_cast<float>(p.valueDb / (2.0 * rangeDb)) * plot.getHeight();
            const float x = xFor(p.frequencyHz);
            if (!started) { path.startNewSubPath(x, y); started = true; } else path.lineTo(x, y);
        }
        g.setColour(colour::accentStrong); g.strokePath(path, juce::PathStrokeType(2.2f));
    };

    drawSpectrum(reference_, colour::warning);
    drawSpectrum(target_, juce::Colour::fromRGB(100, 170, 240));
    drawCorrection();

    if (cursorHz_ > 0.0)
    {
        const float x = xFor(cursorHz_);
        g.setColour(colour::focus);
        const float dashes[] { 3.0f, 3.0f };
        g.drawDashedLine({ x, plot.getY(), x, plot.getBottom() }, dashes, 2);
        g.setFont(type::small());
        g.drawText(juce::String(cursorHz_, 0) + " Hz",
            juce::Rectangle<float>(juce::jlimit(plot.getX(), plot.getRight() - 90.0f, x + 5.0f), plot.getY(), 90.0f, 20.0f),
            juce::Justification::centredLeft);
    }

    g.setColour(colour::accent);
    g.drawHorizontalLine(static_cast<int>(std::lround(plot.getCentreY())), plot.getX(), plot.getRight());
    g.setFont(type::section()); g.setColour(colour::text);
    g.drawText("Reference / Target / Correction", bounds.reduced(16.0f).removeFromTop(20.0f), juce::Justification::centredLeft);
    g.setFont(type::small());
    g.drawText("+/- " + juce::String(rangeDb, 0) + " dB", bounds.reduced(16.0f).removeFromTop(20.0f), juce::Justification::centredRight);
    g.setFont(type::small()); g.setColour(colour::mutedText);
    g.drawText(correction_.empty() ? "Capture and correct to display learned curves." : "Reference and Target show spectral shape; Correction is centered on 0 dB.",
               bounds.reduced(16.0f).removeFromBottom(20.0f), juce::Justification::centredLeft);
}
