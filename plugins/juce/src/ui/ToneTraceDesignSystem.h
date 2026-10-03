#pragma once

#include <JuceHeader.h>

namespace tonetrace::ui
{
namespace colour
{
inline const juce::Colour background = juce::Colour::fromRGB(18, 22, 27);
inline const juce::Colour panel = juce::Colour::fromRGB(27, 33, 40);
inline const juce::Colour panelRaised = juce::Colour::fromRGB(34, 41, 49);
inline const juce::Colour border = juce::Colour::fromRGB(72, 84, 96);
inline const juce::Colour grid = juce::Colour::fromRGB(52, 62, 73);
inline const juce::Colour text = juce::Colour::fromRGB(242, 245, 248);
inline const juce::Colour mutedText = juce::Colour::fromRGB(181, 191, 201);
inline const juce::Colour accent = juce::Colour::fromRGB(93, 205, 184);
inline const juce::Colour accentStrong = juce::Colour::fromRGB(150, 239, 220);
inline const juce::Colour focus = juce::Colour::fromRGB(255, 225, 145);
inline const juce::Colour warning = juce::Colour::fromRGB(255, 205, 122);
}

namespace spacing
{
inline constexpr int outer = 20;
inline constexpr int panelInset = 14;
inline constexpr int sectionGap = 14;
inline constexpr int rowGap = 7;
inline constexpr int rowHeight = 42;
inline constexpr int labelWidth = 210;
inline constexpr int valueWidth = 112;
inline constexpr float cornerRadius = 8.0f;
}

namespace type
{
inline juce::Font title() { return juce::Font(juce::FontOptions(27.0f, juce::Font::bold)); }
inline juce::Font subtitle() { return juce::Font(juce::FontOptions(14.0f)); }
inline juce::Font section() { return juce::Font(juce::FontOptions(15.0f, juce::Font::bold)); }
inline juce::Font control() { return juce::Font(juce::FontOptions(14.0f)); }
inline juce::Font value() { return juce::Font(juce::FontOptions(14.0f, juce::Font::bold)); }
inline juce::Font small() { return juce::Font(juce::FontOptions(12.5f)); }
}

inline constexpr int defaultWidth = 960;
inline constexpr int defaultHeight = 920;
inline constexpr int minimumWidth = 720;
inline constexpr int minimumHeight = 780;
inline constexpr int maximumWidth = 1500;
inline constexpr int maximumHeight = 1000;
}
