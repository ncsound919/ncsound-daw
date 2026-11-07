#include "BrowserOverlay.h"

#include <algorithm>

namespace nc
{
namespace
{
juce::Colour statusColour (PluginStatus status)
{
    switch (status)
    {
        case PluginStatus::native:   return theme::accentHot;
        case PluginStatus::external: return theme::paperDim;
        case PluginStatus::port:     return theme::mod;
        case PluginStatus::archived: return theme::muted;
    }

    return theme::muted;
}

juce::String statusLabel (PluginStatus status)
{
    switch (status)
    {
        case PluginStatus::native:   return "NATIVE";
        case PluginStatus::external: return "VST3";
        case PluginStatus::port:     return "TO PORT";
        case PluginStatus::archived: return "ARCHIVE";
    }

    return "UNKNOWN";
}

void drawStatusChip (juce::Graphics& g, juce::Rectangle<int> area, PluginStatus status)
{
    const auto colour = statusColour (status);
    g.setColour (colour.withAlpha (0.13f));
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (colour.withAlpha (0.45f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f, 0.8f);
    g.setColour (colour);
    g.setFont (theme::label (9.0f));
    g.drawText (statusLabel (status), area, juce::Justification::centred, false);
}

void drawChainIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto centreY = area.getCentreY();
    const auto left = area.getX() + area.getWidth() * 0.16f;
    const auto right = area.getRight() - area.getWidth() * 0.16f;

    g.setColour (colour.withAlpha (0.45f));
    g.drawLine (left, centreY, right, centreY, 1.2f);

    for (int i = 0; i < 3; ++i)
    {
        const auto x = left + (right - left) * static_cast<float> (i) / 2.0f;
        g.setColour (i == 1 ? colour : theme::paperDim);
        g.fillEllipse (juce::Rectangle<float> (i == 1 ? 7.0f : 5.0f,
                                              i == 1 ? 7.0f : 5.0f).withCentre ({ x, centreY }));
    }
}
} // namespace

BrowserOverlay::BrowserOverlay (const Registry& registryToUse)
    : registry (registryToUse)
{
    addAndMakeVisible (list);
    addAndMakeVisible (search);
    addAndMakeVisible (filter);
    addAndMakeVisible (closeButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (scanButton);

    list.setRowHeight (54);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    list.setMultipleSelectionEnabled (false);

    search.setMultiLine (false);
    search.setReturnKeyStartsNewLine (false);
    search.setFont (theme::ui (12.0f));
    search.setIndents (4, 0);
    search.setTextToShowWhenEmpty ("Search plug-ins, makers, folders...", theme::muted);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, theme::paper);
    search.setColour (juce::TextEditor::highlightColourId, theme::accent.withAlpha (0.35f));
    search.setColour (juce::TextEditor::highlightedTextColourId, theme::paper);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
    search.setColour (juce::CaretComponent::caretColourId, theme::accentHot);
    search.onTextChange = [this] { rebuildFilter(); };
    search.onEscapeKey = [this]
    {
        if (onClose)
            onClose();
    };
    search.onReturnKey = [this]
    {
        if (selectedPlugin >= 0 && selectedPlugin < static_cast<int> (registry.plugins.size()) && onLoad)
            onLoad (registry.plugins[static_cast<size_t> (selectedPlugin)]);
    };

    filter.setItems ({ "ALL", "INSTRUMENTS", "EFFECTS", "VST3" });
    filter.setCompact (true);
    filter.setSelected (0);
    filter.onChange = [this] (int index)
    {
        filterIndex = index;
        rebuildFilter();
    };

    closeButton.setCompact (true);
    closeButton.onClick = [this]
    {
        if (onClose)
            onClose();
    };

    loadButton.setCompact (true);
    loadButton.setButtonText ("LOAD ON TRACK");
    loadButton.setEnabled (false);
    loadButton.onClick = [this]
    {
        if (selectedPlugin >= 0 && selectedPlugin < static_cast<int> (registry.plugins.size()) && onLoad)
            onLoad (registry.plugins[static_cast<size_t> (selectedPlugin)]);
    };

    scanButton.setCompact (true);
    scanButton.setTooltip ("Rescan VST3 folders");
    scanButton.onClick = [this]
    {
        if (onScan)
            onScan();
        else
            publishStatus ("VST3 scanner is unavailable");
    };

    rebuildFilter();
}

void BrowserOverlay::setTarget (juce::String trackName, int slotIndex)
{
    targetTrack = std::move (trackName);
    targetSlot = juce::jmax (0, slotIndex);
    repaint (headerArea);
}

void BrowserOverlay::setExternalPlugins (const std::vector<PluginInfo>& plugins)
{
    bool changed = false;

    for (const auto& plugin : plugins)
    {
        const auto exists = std::any_of (registry.plugins.begin(), registry.plugins.end(),
                                         [&plugin] (const PluginInfo& existing)
                                         {
                                             return existing.id == plugin.id;
                                         });

        if (! exists)
        {
            registry.plugins.push_back (plugin);
            changed = true;
        }
    }

    if (changed)
        refresh();
}

void BrowserOverlay::setStatusLine (juce::String message)
{
    statusLine = std::move (message);
    repaint (footerArea);
}

void BrowserOverlay::setScanState (bool isScanning, float progressValue)
{
    scanning = isScanning;
    scanProgress = juce::jlimit (0.0f, 1.0f, progressValue);
    scanButton.setEnabled (! scanning);
    scanButton.setButtonText (scanning ? "SCANNING..." : "RESCAN VST3");
    repaint (footerArea);
}

void BrowserOverlay::refresh()
{
    rebuildFilter();
}

auto BrowserOverlay::pluginAtRow (int row) const -> const PluginInfo*
{
    if (row < 0 || row >= static_cast<int> (filtered.size()))
        return nullptr;

    const auto index = filtered[static_cast<size_t> (row)];

    if (index < 0 || index >= static_cast<int> (registry.plugins.size()))
        return nullptr;

    return &registry.plugins[static_cast<size_t> (index)];
}

void BrowserOverlay::rebuildFilter()
{
    const auto previous = selectedPlugin;
    const auto query = search.getText().trim().toLowerCase();

    filtered.clear();

    for (int i = 0; i < static_cast<int> (registry.plugins.size()); ++i)
    {
        const auto& plugin = registry.plugins[static_cast<size_t> (i)];

        if (filterIndex == 1 && ! plugin.isInstrument())
            continue;

        if (filterIndex == 2 && plugin.isInstrument())
            continue;

        if (filterIndex == 3 && plugin.status != PluginStatus::external)
            continue;

        if (query.isNotEmpty()
            && ! plugin.name.toLowerCase().contains (query)
            && ! plugin.id.toLowerCase().contains (query)
            && ! plugin.source.toLowerCase().contains (query)
            && ! plugin.kind.toLowerCase().contains (query))
            continue;

        filtered.push_back (i);
    }

    list.updateContent();
    selectedPlugin = -1;

    if (filtered.empty())
    {
        list.deselectAllRows();
    }
    else
    {
        auto row = 0;

        if (previous >= 0)
        {
            for (int i = 0; i < static_cast<int> (filtered.size()); ++i)
            {
                if (filtered[static_cast<size_t> (i)] == previous)
                {
                    row = i;
                    break;
                }
            }
        }

        list.selectRow (row, true);
        selectedPlugin = filtered[static_cast<size_t> (row)];
    }

    updateDetail();
    repaint();
}

void BrowserOverlay::updateDetail()
{
    loadButton.setEnabled (selectedPlugin >= 0);
    loadButton.repaint();
    repaint (detailArea);
}

void BrowserOverlay::publishStatus (const juce::String& message)
{
    if (onStatus)
        onStatus (message);
}

void BrowserOverlay::timerCallback()
{
    if (! scanning)
        return;

    scanProgress = juce::jmin (1.0f, scanProgress + 0.023f);

    if (scanProgress >= 1.0f)
    {
        scanning = false;
        stopTimer();
        publishStatus ("Scan complete / " + juce::String (registry.plugins.size()) + " plug-ins indexed");
        rebuildFilter();
    }

    repaint (footerArea);
}

//==============================================================================
void BrowserOverlay::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();

    // Backdrop.
    g.setColour (juce::Colours::black.withAlpha (0.58f));
    g.fillRect (full);

    juce::ColourGradient vignette (juce::Colours::transparentBlack, full.getCentreX(), full.getCentreY(),
                                   juce::Colours::black.withAlpha (0.34f), full.getX(), full.getY(), true);
    g.setGradientFill (vignette);
    g.fillRect (full);

    // Drawer shadow.
    for (int i = 1; i <= 4; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (static_cast<float> (panelArea.getX() - i * 5),
                                            static_cast<float> (panelArea.getY()),
                                            5.0f, static_cast<float> (panelArea.getHeight())));
    }

    const auto panel = panelArea.toFloat();
    juce::ColourGradient background (juce::Colour (0xff282523), panel.getX(), panel.getY(),
                                     juce::Colour (0xff151413), panel.getX(), panel.getBottom(), false);
    g.setGradientFill (background);
    g.fillRect (panel);
    g.setTiledImageFill (theme::grainTexture(), 0, 0, 0.45f);
    g.fillRect (panel);

    g.setColour (theme::accent.withAlpha (0.85f));
    g.fillRect (juce::Rectangle<float> (panel.getX(), panel.getY(), 2.0f, panel.getHeight()));
    g.setColour (theme::edge);
    g.drawVerticalLine (panelArea.getX(), panel.getY(), panel.getBottom());
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.drawVerticalLine (panelArea.getX() + 1, panel.getY(), panel.getBottom());

    // Header.
    auto header = headerArea;
    g.setColour (theme::accent);
    g.setFont (theme::label (9.5f));
    g.drawText ("NCSOUND MUSIC HUB  /  PLUG-IN LIBRARY", header.removeFromTop (15),
                juce::Justification::centredLeft, false);

    g.setColour (theme::paper);
    g.setFont (theme::display (16.0f));
    g.drawText ("PLUG-IN BROWSER", header.removeFromTop (23), juce::Justification::centredLeft, false);

    g.setColour (theme::muted);
    g.setFont (theme::label (9.5f));
    g.drawText ("STOCK SUITE + THIRD-PARTY VST3  /  DOUBLE-CLICK TO LOAD", header.removeFromTop (13),
                juce::Justification::centredLeft, false);

    if (targetTrack.isNotEmpty())
    {
        auto target = header;
        const auto chip = juce::Rectangle<int> (target.getX(), target.getY() + 2, juce::jmin (270, target.getWidth()), 18);
        g.setColour (theme::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (chip.toFloat(), 2.5f);
        g.setColour (theme::accent.withAlpha (0.4f));
        g.drawRoundedRectangle (chip.toFloat().reduced (0.5f), 2.5f, 0.7f);
        g.setColour (theme::accentHot);
        g.setFont (theme::label (9.0f));
        g.drawText ("TARGET  /  " + targetTrack + "  /  INS " + juce::String (targetSlot + 1),
                    chip.reduced (8, 0), juce::Justification::centredLeft, true);
    }

    // Search well and magnifier.
    theme::wellFace (g, searchArea.toFloat(), 4.0f);
    const auto glass = juce::Rectangle<float> (static_cast<float> (searchArea.getX() + 8),
                                               static_cast<float> (searchArea.getCentreY() - 5),
                                               9.0f, 9.0f);
    g.setColour (theme::muted);
    g.drawEllipse (glass, 1.0f);
    g.drawLine (glass.getRight() - 1.0f, glass.getBottom() - 1.0f,
                glass.getRight() + 3.5f, glass.getBottom() + 3.5f, 1.0f);

    // Summary.
    auto summary = summaryArea;
    g.setColour (theme::muted);
    g.setFont (theme::label (9.5f));
    g.drawText (juce::String (filtered.size()) + " SHOWN  /  "
                    + juce::String (registry.plugins.size()) + " INDEXED",
                summary, juce::Justification::centredLeft, false);
    g.drawText (juce::String (registry.countInstruments()) + " INSTRUMENTS  /  "
                    + juce::String (registry.countEffects()) + " EFFECTS",
                summary, juce::Justification::centredRight, false);

    // List well.
    theme::wellFace (g, listArea.toFloat().expanded (1.0f), 5.0f);

    // Detail card.
    const auto detail = detailArea.toFloat();
    theme::wellFace (g, detail, 5.0f);

    if (selectedPlugin >= 0 && selectedPlugin < static_cast<int> (registry.plugins.size()))
    {
        const auto& plugin = registry.plugins[static_cast<size_t> (selectedPlugin)];
        auto inner = detailArea.reduced (12, 9);

        auto top = inner.removeFromTop (40);
        auto icon = top.removeFromLeft (36).withSizeKeepingCentre (32, 32);
        theme::wellFace (g, icon.toFloat(), 4.0f);

        if (plugin.isInstrument())
            paintMonogram (g, icon.toFloat().reduced (4.0f), theme::accentHot);
        else
            drawChainIcon (g, icon.toFloat().reduced (5.0f), theme::accentHot);

        top.removeFromLeft (8);
        auto status = top.removeFromRight (70).withSizeKeepingCentre (68, 18);
        drawStatusChip (g, status, plugin.status);

        g.setColour (theme::paper);
        g.setFont (theme::display (15.0f));
        g.drawText (plugin.name, top.removeFromTop (21), juce::Justification::centredLeft, true);

        g.setColour (theme::muted);
        g.setFont (theme::label (9.5f));
        g.drawText (plugin.kind.toUpperCase() + "  /  " + plugin.id.toUpperCase(),
                    top, juce::Justification::centredLeft, true);

        inner.removeFromTop (4);
        g.setColour (theme::line);
        g.drawHorizontalLine (inner.getY(), static_cast<float> (inner.getX()), static_cast<float> (inner.getRight()));
        inner.removeFromTop (5);

        g.setColour (theme::paperDim);
        g.setFont (theme::ui (11.0f));
        g.drawFittedText (plugin.source, inner.removeFromTop (26), juce::Justification::topLeft, 2, 0.85f);

        if (plugin.note.isNotEmpty())
        {
            g.setColour (theme::muted);
            g.setFont (theme::ui (10.5f));
            g.drawFittedText (plugin.note, inner.removeFromTop (28), juce::Justification::topLeft, 2, 0.85f);
        }

        g.setColour (theme::accentHot);
        g.setFont (theme::label (9.0f));
        g.drawText ("LOADS INTO THE SELECTED CHANNEL",
                    detailArea.reduced (12, 8).removeFromBottom (11), juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (theme::muted);
        g.setFont (theme::ui (12.0f));
        g.drawText ("Select a plug-in to inspect it.", detailArea.reduced (12, 0),
                    juce::Justification::centred, false);
    }

    // Footer.
    if (scanning)
    {
        auto progress = footerArea.toFloat().withY (static_cast<float> (footerArea.getY() - 4)).withHeight (2.0f);
        g.setColour (theme::accent.withAlpha (0.18f));
        g.fillRect (progress);
        g.setColour (theme::accentHot);
        g.fillRect (progress.withWidth (progress.getWidth() * scanProgress));

        g.setColour (theme::muted);
        g.setFont (theme::label (9.0f));
        g.drawText ("SCANNING  " + juce::String (juce::roundToInt (scanProgress * 100.0f)) + "%",
                    footerArea, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (theme::muted);
        g.setFont (theme::label (9.0f));
        g.drawText (statusLine.isNotEmpty() ? statusLine : juce::String (filtered.size()) + " PLUG-INS / VST3 READY",
                    footerArea, juce::Justification::centred, false);
    }

    theme::hairline (g, panel.getX() + 16.0f, panel.getBottom() - 1.0f,
                     panel.getRight() - 16.0f, panel.getBottom() - 1.0f);
}

void BrowserOverlay::resized()
{
    auto area = getLocalBounds();
    panelArea = area.removeFromRight (juce::jmin (470, juce::jmax (390, area.getWidth() - 90)));

    auto inner = panelArea.reduced (16, 12);
    headerArea = inner.removeFromTop (70);
    inner.removeFromTop (4);
    searchArea = inner.removeFromTop (36);
    inner.removeFromTop (8);
    filterArea = inner.removeFromTop (26);
    inner.removeFromTop (8);
    summaryArea = inner.removeFromTop (16);
    inner.removeFromTop (8);
    detailArea = inner.removeFromBottom (188);
    inner.removeFromBottom (10);
    footerArea = inner.removeFromBottom (34);
    inner.removeFromBottom (8);
    listArea = inner;

    closeButton.setBounds (panelArea.getRight() - 48, panelArea.getY() + 14, 32, 32);
    search.setBounds (searchArea.withTrimmedLeft (23).reduced (2, 3));
    filter.setBounds (filterArea);
    list.setBounds (listArea.reduced (1));

    auto footer = footerArea;
    scanButton.setBounds (footer.removeFromLeft (132));
    loadButton.setBounds (footer.removeFromRight (132));
}

void BrowserOverlay::mouseDown (const juce::MouseEvent& event)
{
    if (! panelArea.contains (event.getPosition()) && onClose)
        onClose();
}

//==============================================================================
int BrowserOverlay::getNumRows()
{
    return static_cast<int> (filtered.size());
}

void BrowserOverlay::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    const auto* plugin = pluginAtRow (row);

    if (plugin == nullptr)
        return;

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (2, 1);

    if (rowIsSelected)
    {
        g.setColour (theme::accent.withAlpha (0.13f));
        g.fillRoundedRectangle (area.toFloat(), 3.0f);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (static_cast<float> (area.getX()), static_cast<float> (area.getY() + 4),
                                                        2.0f, static_cast<float> (area.getHeight() - 8)), 1.0f);
        g.setColour (theme::accent.withAlpha (0.35f));
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 3.0f, 0.8f);
    }
    else if (row % 2 == 1)
    {
        g.setColour (juce::Colours::white.withAlpha (0.016f));
        g.fillRect (area);
    }

    auto inner = area.reduced (12, 7);
    auto icon = inner.removeFromLeft (32).withSizeKeepingCentre (30, 30);

    theme::wellFace (g, icon.toFloat(), 3.5f);

    if (plugin->isInstrument())
        paintMonogram (g, icon.toFloat().reduced (4.0f), theme::accentHot);
    else
        drawChainIcon (g, icon.toFloat().reduced (5.0f), theme::accentHot);

    inner.removeFromLeft (9);

    auto status = inner.removeFromRight (68).withSizeKeepingCentre (66, 18);
    drawStatusChip (g, status, plugin->status);

    g.setColour (rowIsSelected ? theme::paper : theme::paperDim);
    g.setFont (theme::ui (12.0f, true));
    g.drawText (plugin->name, inner.removeFromTop (19), juce::Justification::centredLeft, true);

    g.setColour (theme::muted);
    g.setFont (theme::label (9.5f));
    g.drawText (plugin->kind.toUpperCase() + "  /  " + plugin->source,
                inner.removeFromTop (14), juce::Justification::centredLeft, true);

    g.setColour (row % 2 == 0 ? theme::line : theme::lineStrong.withAlpha (0.06f));
    g.drawHorizontalLine (area.getBottom(), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
}

void BrowserOverlay::selectedRowsChanged (int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < static_cast<int> (filtered.size()))
        selectedPlugin = filtered[static_cast<size_t> (lastRowSelected)];
    else
        selectedPlugin = -1;

    updateDetail();
}

void BrowserOverlay::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (const auto* plugin = pluginAtRow (row))
    {
        selectedPlugin = filtered[static_cast<size_t> (row)];

        if (onLoad)
            onLoad (*plugin);
    }
}
} // namespace nc
