#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "BinaryData.h"

namespace nc::theme
{
// NCSound Music Hub brand tokens.
// One accent: burnt orange. Cream is text, never a large surface fill.
inline const juce::Colour ink       { 0xff131211 };
inline const juce::Colour well      { 0xff0a0a0a };
inline const juce::Colour wellLo    { 0xff060606 };
inline const juce::Colour panel     { 0xff1f1d1b };
inline const juce::Colour panelHi   { 0xff2a2724 };
inline const juce::Colour panelLo   { 0xff161514 };
inline const juce::Colour edge      { 0xff040404 };
inline const juce::Colour line      { 0x1cf8e8c8 };
inline const juce::Colour lineStrong{ 0x36f8e8c8 };
inline const juce::Colour paper     { 0xfff8e8c8 };
inline const juce::Colour paperDim  { 0xc7f8e8c8 };
inline const juce::Colour muted     { 0x94f8e8c8 };
inline const juce::Colour faint     { 0x70f8e8c8 };
inline const juce::Colour accent    { 0xffc25a2a };
inline const juce::Colour accentHot { 0xffe0824d };
inline const juce::Colour rust      { 0xff8e3a1e };
inline const juce::Colour mod       { 0xffe4a15c };
inline const juce::Colour danger    { 0xffe0503c };

// Meter ramp. Cream to burnt orange to danger red: a VU scale that stays
// inside the one-accent palette instead of importing a green traffic light.
inline const juce::Colour meterLow  { paper };
inline const juce::Colour meterMid  { accentHot };
inline const juce::Colour meterHigh { danger };

// Track identity tints. Bright enough to read across a dark arrange page at a
// glance, but still keyed to the one warm instrument rather than a rainbow.
inline const juce::Colour trackTints[8] {
    juce::Colour (0xffff8a3c), // drums  - signal orange
    juce::Colour (0xffff5f3a), // bass   - hot coral
    juce::Colour (0xffffc247), // keys   - amber
    juce::Colour (0xff8fe06a), // vox    - acid green
    juce::Colour (0xffe8a04c), // gtr    - gold
    juce::Colour (0xffd98b6a), // perc   - terracotta
    juce::Colour (0xff4fd6c0), // aux 1  - teal
    juce::Colour (0xffe07ab8)  // aux 2  - magenta
};

// Geometry: three corners, nothing else. Sharp enough to read as machined
// metal, round enough that a 1 px border never breaks at the corner.
namespace radius
{
inline constexpr float tight   = 2.0f;
inline constexpr float control = 4.0f;
inline constexpr float panel   = 6.0f;
}

// Faces. Display: Anton, a heavy condensed poster grotesque. UI: Space
// Grotesk regular + bold. Technical: Space Mono bold for every numeric readout.
inline juce::Typeface::Ptr anton()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::AntonRegular_ttf,
                                                                     BinaryData::AntonRegular_ttfSize);
    return face;
}

inline juce::Typeface::Ptr grotesk()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskRegular_ttf,
                                                                     BinaryData::SpaceGroteskRegular_ttfSize);
    return face;
}

inline juce::Typeface::Ptr groteskBold()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceGroteskBold_ttf,
                                                                     BinaryData::SpaceGroteskBold_ttfSize);
    return face;
}

inline juce::Typeface::Ptr mono()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::SpaceMonoBold_ttf,
                                                                     BinaryData::SpaceMonoBold_ttfSize);
    return face;
}

// One ramp, one floor. Everything below is real reading size on a 1x panel.
namespace size
{
inline constexpr float micro   = 11.0f;  // uppercase meta labels
inline constexpr float label   = 12.0f;  // section captions and chips
inline constexpr float body    = 13.5f;  // standard UI text
inline constexpr float strong  = 15.5f;  // track and plug-in names
inline constexpr float title   = 18.0f;  // panel titles
inline constexpr float hero    = 22.0f;  // one hero per surface
inline constexpr float readout = 30.0f;  // LCD digits
} // namespace size

// UI face. Caps get tracking so micro labels stay legible.
inline juce::Font ui (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (bold ? groteskBold() : grotesk())
                           .withHeight (juce::jmax (size::body, height)));
}

inline juce::Font label (float height = size::micro)
{
    return juce::Font (juce::FontOptions (groteskBold())
                           .withHeight (juce::jmax (size::micro, height)))
        .withExtraKerningFactor (0.06f);
}

// Display face: headings, lockup, track names. Never body copy.
inline juce::Font display (float height)
{
    return juce::Font (juce::FontOptions (anton())
                           .withHeight (juce::jmax (size::strong, height)))
        .withExtraKerningFactor (0.01f);
}

// Technical face: time, tempo, gain and frequency readouts only.
inline juce::Font clock (float height)
{
    return juce::Font (juce::FontOptions (mono())
                           .withHeight (juce::jmax (size::label, height)));
}

inline void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> area, float corner = radius::control)
{
    g.setColour (accentHot);
    g.drawRoundedRectangle (area.reduced (1.0f), corner, 2.0f);
}

inline const juce::Image& grainTexture()
{
    static const juce::Image image = []
    {
        juce::Image noise (juce::Image::ARGB, 64, 64, true);
        juce::Graphics g (noise);
        juce::Random random (0x4e43534f);

        for (int i = 0; i < 1200; ++i)
        {
            const auto alpha = static_cast<juce::uint8> (3 + random.nextInt (12));
            g.setColour (juce::Colours::white.withAlpha (static_cast<float> (alpha) / 255.0f));
            g.fillRect (random.nextInt (64), random.nextInt (64), 1, 1);
        }

        return noise;
    }();

    return image;
}

// A panel face: flat gradient, one hard 1 px edge, a single highlight line.
// No blurred drop shadow: on a tight grid the blur reads as dirt.
inline void face (juce::Graphics& g, juce::Rectangle<float> area, float corner = radius::panel, bool lift = true)
{
    if (lift)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (area.translated (0.0f, 1.0f), corner);
    }

    juce::ColourGradient gradient (panelHi, area.getX(), area.getY(),
                                   panelLo, area.getX(), area.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (area, corner);

    g.setColour (edge);
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);

    g.setColour (paper.withAlpha (0.09f));
    g.drawHorizontalLine (juce::roundToInt (area.getY() + 1.0f),
                          area.getX() + corner, area.getRight() - corner);

    g.setTiledImageFill (grainTexture(), 0, 0, 0.5f);
    g.fillRoundedRectangle (area.reduced (1.0f), juce::jmax (1.0f, corner - 1.0f));
}

inline void wellFace (juce::Graphics& g, juce::Rectangle<float> area, float corner = radius::control)
{
    g.setColour (wellLo);
    g.fillRoundedRectangle (area, corner);

    juce::ColourGradient gradient (juce::Colour (0xff050505), area.getX(), area.getY(),
                                   juce::Colour (0xff131313), area.getX(), area.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (area.reduced (1.0f), juce::jmax (1.0f, corner - 1.0f));

    g.setColour (edge);
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawHorizontalLine (juce::roundToInt (area.getY() + 1.5f), area.getX() + 2.0f, area.getRight() - 2.0f);
}

inline void hairline (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (line);
    g.drawHorizontalLine (area.getY(), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
}

inline void hairline (juce::Graphics& g, float x1, float y1, float x2, float y2)
{
    g.setColour (line);
    g.drawLine (x1, y1, x2, y2, 1.0f);
}

inline void chip (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float corner = radius::tight)
{
    g.setColour (colour);
    g.fillRoundedRectangle (area, corner);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 0.75f);
}

inline void led (juce::Graphics& g, juce::Point<float> centre, float radius, juce::Colour colour, bool on, bool glow = true)
{
    g.setColour (juce::Colour (0xff2d2a25));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

    if (! on)
        return;

    if (glow)
    {
        g.setColour (colour.withAlpha (0.18f));
        g.fillEllipse (juce::Rectangle<float> (radius * 4.6f, radius * 4.6f).withCentre (centre));
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.6f, radius * 2.6f).withCentre (centre));
    }

    g.setColour (colour);
    g.fillEllipse (juce::Rectangle<float> (radius * 1.7f, radius * 1.7f).withCentre (centre));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.6f, radius * 0.6f).withCentre (centre.translated (-radius * 0.35f, -radius * 0.35f)));
}

inline void lcd (juce::Graphics& g,
                 juce::Rectangle<float> area,
                 const juce::String& mainText,
                 const juce::String& smallText = {},
                 const juce::String& caption = {},
                 float mainHeight = 28.0f,
                 juce::Colour colour = accentHot)
{
    wellFace (g, area, radius::control);

    // Scanlines and a faint glass sweep.
    g.saveState();
    g.reduceClipRegion (area.toNearestInt());
    g.setColour (juce::Colours::black.withAlpha (0.20f));

    for (float y = area.getY() + 2.0f; y < area.getBottom() - 1.0f; y += 3.0f)
        g.drawHorizontalLine (juce::roundToInt (y), area.getX() + 1.0f, area.getRight() - 1.0f);

    juce::ColourGradient glass (juce::Colours::white.withAlpha (0.07f), area.getX(), area.getY(),
                                juce::Colours::transparentWhite, area.getX(), area.getY() + area.getHeight() * 0.48f, false);
    g.setGradientFill (glass);
    g.fillRect (area);
    g.restoreState();

    auto inner = area.reduced (8.0f, 4.0f);

    if (caption.isNotEmpty())
    {
        g.setColour (muted);
        g.setFont (label (10.0f));
        g.drawText (caption.toUpperCase(), inner.removeFromTop (13.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);
    }

    if (smallText.isNotEmpty())
    {
        g.setColour (colour.withAlpha (0.7f));
        g.setFont (clock (11.0f));
        g.drawText (smallText, inner.removeFromBottom (14.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);
    }

    const auto textArea = inner.toNearestInt();

    g.setColour (colour.withAlpha (0.25f));
    g.setFont (clock (mainHeight));
    g.drawText (mainText, textArea.translated (0, 1), juce::Justification::centred, false);
    g.setColour (colour);
    g.drawText (mainText, textArea, juce::Justification::centred, false);
}

inline juce::String dbString (float gain, int decimals = 1)
{
    if (gain <= 0.00001f)
        return "-inf";

    const auto db = juce::Decibels::gainToDecibels (gain, -100.0f);
    return (db > 0.0f ? "+" : "") + juce::String (db, decimals);
}

inline juce::String panString (float pan)
{
    const auto amount = juce::roundToInt (std::abs (pan) * 100.0f);

    if (amount == 0)
        return "C";

    return (pan < 0.0f ? "L" : "R") + juce::String (amount);
}

inline float levelFromGain (float gain, float floorDb = -60.0f)
{
    return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (gain, floorDb) - floorDb) / -floorDb);
}

} // namespace nc::theme
