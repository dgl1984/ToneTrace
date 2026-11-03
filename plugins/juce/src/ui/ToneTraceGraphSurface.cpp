#include "ToneTraceGraphSurface.h"
#include "ToneTraceDesignSystem.h"
#include <array>
#include <cmath>

namespace
{
constexpr double lowHz = 10.0, highHz = 30000.0;
const juce::Colour targetColour = juce::Colour::fromRGB(100, 170, 240);
double meanOf(const auto& points)
{
    double sum = 0.0;
    for (const auto& point : points) sum += point.valueDb;
    return points.empty() ? 0.0 : sum / static_cast<double>(points.size());
}
}

ToneTraceGraphSurface::ToneTraceGraphSurface()
{
    setAccessible(false);
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(true, false);
}

juce::Rectangle<float> ToneTraceGraphSurface::plotBounds() const
{
    return getLocalBounds().toFloat().withTrimmedLeft(48).withTrimmedRight(26)
        .withTrimmedTop(34).withTrimmedBottom(36);
}

float ToneTraceGraphSurface::xFor(double hz) const
{
    const auto plot = plotBounds();
    return plot.getX() + static_cast<float>(std::log(std::clamp(hz, lowHz, highHz) / lowHz)
        / std::log(highHz / lowHz)) * plot.getWidth();
}

void ToneTraceGraphSurface::mouseMove(const juce::MouseEvent& event)
{
    const auto plot = plotBounds();
    if (plot.getWidth() <= 0) return;
    const double fraction = std::clamp(static_cast<double>((event.position.x - plot.getX()) / plot.getWidth()), 0.0, 1.0);
    cursorHz_ = lowHz * std::pow(highHz / lowHz, fraction);
    if (onInspectFrequency) onInspectFrequency(cursorHz_);
    repaint();
}

void ToneTraceGraphSurface::mouseExit(const juce::MouseEvent&) { setTraceFrequency(0.0); }
void ToneTraceGraphSurface::setTraceFrequency(double hz) { cursorHz_ = hz; repaint(); }
void ToneTraceGraphSurface::resized() { rebuildPaths(); }

void ToneTraceGraphSurface::updateFromState(const tonetrace::ProfileSnapshot* snapshot,
    const tonetrace::IrRenderSettings& settings, double maximumCorrectionDb)
{
    reference_.clear(); target_.clear(); correction_.clear();
    if (snapshot != nullptr)
    {
        for (const auto& p : snapshot->reference.points)
            if (std::isfinite(p.frequencyHz) && std::isfinite(p.levelDb)) reference_.push_back({p.frequencyHz, p.levelDb});
        for (const auto& p : snapshot->target.points)
            if (std::isfinite(p.frequencyHz) && std::isfinite(p.levelDb)) target_.push_back({p.frequencyHz, p.levelDb});
    }
    constexpr int samples = 320;
    if (snapshot != nullptr || tonetrace::hasManualCorrection(settings))
        for (int i = 0; i < samples; ++i)
        {
            const double hz = lowHz * std::pow(highHz / lowHz, static_cast<double>(i) / (samples - 1));
            correction_.push_back({hz, tonetrace::ui::displayedCorrectionDb(snapshot, settings, maximumCorrectionDb, hz)});
        }
    rangeDb_ = 12.0;
    for (const auto* curve : { &reference_, &target_ })
    {
        const double mean = meanOf(*curve);
        for (const auto& p : *curve) rangeDb_ = std::max(rangeDb_, std::abs(p.valueDb - mean));
    }
    for (const auto& p : correction_) rangeDb_ = std::max(rangeDb_, std::abs(p.valueDb));
    rangeDb_ = std::ceil(rangeDb_ / 6.0) * 6.0;
    rebuildPaths();
    repaint();
}

void ToneTraceGraphSurface::rebuildPaths()
{
    const auto plot = plotBounds();
    const auto build = [&](const auto& points, juce::Path& path, double offset)
    {
        path.clear();
        bool started = false;
        for (const auto& p : points)
        {
            if (p.frequencyHz < lowHz || p.frequencyHz > highHz) continue;
            const auto x = xFor(p.frequencyHz);
            const auto y = plot.getCentreY() - static_cast<float>((p.valueDb - offset) / (2.0 * rangeDb_)) * plot.getHeight();
            if (!started) { path.startNewSubPath(x, y); started = true; }
            else path.lineTo(x, y);
        }
    };
    build(reference_, referencePath_, meanOf(reference_));
    build(target_, targetPath_, meanOf(target_));
    const float dashes[] {4, 3};
    juce::PathStrokeType(1.4f).createDashedStroke(dashedTargetPath_, targetPath_, dashes, 2);
    build(correction_, correctionPath_, 0.0);
}

void ToneTraceGraphSurface::paint(juce::Graphics& g)
{
    using namespace tonetrace::ui;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(colour::panel);
    g.fillRoundedRectangle(bounds, spacing::cornerRadius);
    g.setColour(colour::border);
    g.drawRoundedRectangle(bounds.reduced(0.5f), spacing::cornerRadius, 1.0f);
    const auto plot = plotBounds();
    if (plot.getWidth() < 80 || plot.getHeight() < 30) return;

    // Label the three traces individually; width and dash pattern also distinguish them.
    auto legend = bounds.reduced(14, 5).removeFromTop(24);
    const auto item = [&](const juce::String& label, juce::Colour c, bool dashed, float width)
    {
        auto cell = legend.removeFromLeft(width);
        const auto y = cell.getCentreY();
        g.setColour(c);
        if (dashed) { const float dashes[] { 4, 3 }; g.drawDashedLine({cell.getX(), y, cell.getX()+20, y}, dashes, 2); }
        else g.drawLine(cell.getX(), y, cell.getX()+20, y, 2);
        g.setFont(type::small());
        g.drawText(label, cell.withTrimmedLeft(26), juce::Justification::centredLeft);
    };
    item("Reference", colour::warning, false, 115);
    item("Target", targetColour, true, 95);
    item("Correction + gain", colour::accentStrong, false, 165);
    g.setColour(colour::mutedText);
    g.drawText("Auto scale (dB)", legend, juce::Justification::centredRight);

    constexpr std::array<double, 9> frequencies {10, 30, 100, 300, 1000, 3000, 10000, 20000, 30000};
    for (double hz : frequencies)
    {
        const float x = xFor(hz);
        g.setColour(colour::grid);
        g.drawVerticalLine(static_cast<int>(std::lround(x)), plot.getY(), plot.getBottom());
        // Avoid colliding 20k/30k labels in compact layouts.
        if (hz == 20000 && plot.getWidth() < 900) continue;
        const auto label = hz >= 1000 ? juce::String(hz / 1000, 0) + "k" : juce::String(hz, 0);
        g.setColour(colour::mutedText);
        g.drawText(label, juce::Rectangle<float>(x-19, plot.getBottom()+1, 38, 16), juce::Justification::centred);
    }
    const int divisions = plot.getHeight() < 110 ? 2 : 4;
    for (int i = 0; i <= divisions; ++i)
    {
        const float y = plot.getY() + plot.getHeight() * static_cast<float>(i) / divisions;
        const double db = rangeDb_ * (1.0 - 2.0 * i / divisions);
        g.setColour(i == divisions/2 ? colour::border : colour::grid);
        g.drawHorizontalLine(static_cast<int>(std::lround(y)), plot.getX(), plot.getRight());
        g.setColour(colour::mutedText);
        g.drawText((db > 0 ? "+" : "") + juce::String(db, 0),
            juce::Rectangle<float>(2, y-8, 40, 16), juce::Justification::centredRight);
    }
    g.setColour(colour::warning); g.strokePath(referencePath_, juce::PathStrokeType(1.4f));
    g.setColour(targetColour);
    const float dashes[] {4, 3};
    g.fillPath(dashedTargetPath_);
    g.setColour(colour::accentStrong); g.strokePath(correctionPath_, juce::PathStrokeType(2.2f));
    if (cursorHz_ > 0)
    {
        const float x = xFor(cursorHz_);
        g.setColour(colour::focus);
        g.drawDashedLine({x, plot.getY(), x, plot.getBottom()}, dashes, 2);
    }
    g.setColour(colour::mutedText);
    g.drawText(correction_.empty() ? "Hz | Capture a Reference to begin. Manual EQ is available on Bands."
        : "Hz | Spectra: relative to each mean. Correction: output dB including gain.",
        bounds.reduced(14, 1).removeFromBottom(16), juce::Justification::centredLeft);
}
