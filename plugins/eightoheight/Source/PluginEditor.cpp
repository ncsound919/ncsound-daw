#include "PluginEditor.h"

namespace nc
{
namespace
{
const juce::Colour background { 0xff111014 };
const juce::Colour panel { 0xff1b1a1f };
const juce::Colour accent { 0xffe8b04b };
const juce::Colour text { 0xffe6e1d8 };

void styleSlider (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a373f));
    slider.setColour (juce::Slider::thumbColourId, text);
    slider.setColour (juce::Slider::textBoxTextColourId, text);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}
} // namespace

EightOHeightEditor::EightOHeightEditor (EightOHeightProcessor& processorToUse)
    : AudioProcessorEditor (&processorToUse), processor (processorToUse)
{
    title.setText ("808 DESIGNER", juce::dontSendNotification);
    title.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    title.setColour (juce::Label::textColourId, accent);
    addAndMakeVisible (title);

    using namespace eightoheight;

    addRotary (pitchAttack, "PITCH ATK", 0);
    addRotary (pitchDecay, "PITCH DEC", 1);
    addRotary (pitchCurve, "CURVE", 2);
    addRotary (pitchTime, "PITCH TIME", 3);
    addRotary (ampDecay, "DECAY", 4);
    addRotary (sustain, "SUSTAIN", 5);

    addRotary (level, "LEVEL", 0);
    addRotary (subLevel, "SUB", 1);
    addRotary (subDamping, "DAMPING", 2);
    addRotary (subTilt, "TILT", 3);
    addRotary (glideTime, "GLIDE", 4);
    addRotary (monoCrossover, "XOVER", 5);

    addRotary (subsonicFilter, "SUBSONIC", 0);
    addRotary (clipCeiling, "CLIPPER", 1);
    addRotary (outputLevel, "OUTPUT", 2);

    addRotary (characterDrive, "DRIVE", 3);
    addRotary (characterIron, "IRON", 4);
    addRotary (characterTone, "TONE", 5);

    waveBox.addItemList (juce::StringArray { "Sine", "Triangle", "Saw", "Square", "Supersaw" }, 1);
    addAndMakeVisible (waveBox);
    waveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, wave, waveBox);

    glideModeBox.addItemList (juce::StringArray { "Const Time", "Const Rate" }, 1);
    addAndMakeVisible (glideModeBox);
    glideModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, glideMode, glideModeBox);

    juce::StringArray circuits;

    for (int i = 0; i < engine::numCharacterCircuits; ++i)
        circuits.add (engine::characterCircuitName (static_cast<engine::CharacterCircuit> (i)));

    circuitBox.addItemList (circuits, 1);
    addAndMakeVisible (circuitBox);
    circuitAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, characterCircuit, circuitBox);

    characterToggle.setColour (juce::ToggleButton::tickColourId, accent);
    characterToggle.setColour (juce::ToggleButton::textColourId, text);
    addAndMakeVisible (characterToggle);
    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, characterOn, characterToggle);

    monoLockToggle.setColour (juce::ToggleButton::tickColourId, accent);
    monoLockToggle.setColour (juce::ToggleButton::textColourId, text);
    addAndMakeVisible (monoLockToggle);
    monoLockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.apvts, monoLock, monoLockToggle);

    setSize (760, 460);
    setResizable (true, true);
    setResizeLimits (620, 380, 1200, 760);
}

EightOHeightEditor::Rotary& EightOHeightEditor::addRotary (const char* parameterId,
                                                           const juce::String& name,
                                                           int column)
{
    auto rotary = std::make_unique<Rotary>();
    rotary->column = column;
    rotary->label.setText (name, juce::dontSendNotification);
    rotary->label.setJustificationType (juce::Justification::centred);
    rotary->label.setFont (juce::Font (juce::FontOptions (10.0f)));
    rotary->label.setColour (juce::Label::textColourId, text.withAlpha (0.72f));
    addAndMakeVisible (rotary->label);

    styleSlider (rotary->slider);
    addAndMakeVisible (rotary->slider);
    rotary->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, parameterId, rotary->slider);

    auto& reference = rotaries.emplace_back (std::move (rotary));
    return *reference;
}

void EightOHeightEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f), 10.0f);

    g.setColour (accent.withAlpha (0.30f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (8.0f), 10.0f, 1.0f);
}

void EightOHeightEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    title.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);

    auto header = area.removeFromTop (26);
    waveBox.setBounds (header.removeFromLeft (140));
    header.removeFromLeft (10);
    glideModeBox.setBounds (header.removeFromLeft (130));
    header.removeFromLeft (10);
    monoLockToggle.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (10);
    characterToggle.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (10);
    circuitBox.setBounds (header.removeFromLeft (150));

    area.removeFromTop (10);

    const auto rows = rotaries.empty() ? 1 : static_cast<int> ((rotaries.size() + 5) / 6);
    const auto rowHeight = area.getHeight() / juce::jmax (1, rows);
    const auto columnWidth = area.getWidth() / 6;

    for (auto& rotary : rotaries)
    {
        const auto row = rotary->column / 6;
        auto cell = juce::Rectangle<int> (area.getX() + rotary->column % 6 * columnWidth,
                                          area.getY() + row * rowHeight,
                                          columnWidth, rowHeight).reduced (4);

        rotary->label.setBounds (cell.removeFromTop (14));
        rotary->slider.setBounds (cell);
    }
}
} // namespace nc
