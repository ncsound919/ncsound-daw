#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BinaryData.h"

namespace nc::theme
{
// marketing-studio tokens (og-glass), applied to the editor.
inline const juce::Colour bg        { 0xff0a0d14 };
inline const juce::Colour panel     { 0xff10141c };
inline const juce::Colour raised    { 0x0affffff };
inline const juce::Colour border    { 0x14ffffff };
inline const juce::Colour accent    { 0xff6366f1 };
inline const juce::Colour accentHi  { 0xff818cf8 };
inline const juce::Colour sky       { 0xff0ea5e9 };
inline const juce::Colour gold      { 0xfff59e0b };
inline const juce::Colour red       { 0xffef4444 };
inline const juce::Colour text      { 0xebffffff };
inline const juce::Colour textDim   { 0x99ffffff };
inline const juce::Colour textMuted { 0x59ffffff };

inline juce::Typeface::Ptr inter()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::Inter_ttf, BinaryData::Inter_ttfSize);
    return face;
}

inline juce::Typeface::Ptr bank()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::BankGothic_ttf, BinaryData::BankGothic_ttfSize);
    return face;
}

inline juce::Typeface::Ptr bremen()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::BremenBlack_ttf, BinaryData::BremenBlack_ttfSize);
    return face;
}

inline juce::Typeface::Ptr digital()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::DigitalReadout_ttf, BinaryData::DigitalReadout_ttfSize);
    return face;
}

inline juce::Font ui (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (inter()).withHeight (height).withStyle (bold ? "Bold" : "Regular"));
}

inline juce::Font label (float height = 9.5f)
{
    return juce::Font (juce::FontOptions (bank()).withHeight (height));
}

inline juce::Font display (float height)
{
    return juce::Font (juce::FontOptions (bremen()).withHeight (height));
}

inline juce::Font clock (float height)
{
    return juce::Font (juce::FontOptions (digital()).withHeight (height));
}

inline void hairline (juce::Graphics& g, float x1, float y1, float x2, float y2)
{
    g.setColour (border);
    g.drawLine (x1, y1, x2, y2, 1.0f);
}
} // namespace nc::theme
