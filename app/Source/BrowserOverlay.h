#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

#include "PluginRegistry.h"
#include "Theme.h"
#include "Widgets.h"

namespace nc
{
class BrowserOverlay final : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::Timer
{
public:
    explicit BrowserOverlay (const Registry& registryToUse);

    std::function<void()> onClose;
    std::function<void (const PluginInfo&)> onLoad;
    std::function<void()> onScan;
    std::function<void (const juce::String&)> onStatus;

    void setTarget (juce::String trackName, int slotIndex);
    void setExternalPlugins (const std::vector<PluginInfo>&);
    void setScanState (bool isScanning, float progress = 0.0f);
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void timerCallback() override;

    void rebuildFilter();
    void updateDetail();
    auto pluginAtRow (int row) const -> const PluginInfo*;
    void publishStatus (const juce::String& message);

    Registry registry;
    std::vector<int> filtered;

    juce::ListBox list { "plug-ins", this };
    juce::TextEditor search;
    SegmentedControl filter;
    HardwareButton closeButton { HardwareButton::Icon::close };
    HardwareButton loadButton { HardwareButton::Icon::plus, "LOAD" };
    HardwareButton scanButton { HardwareButton::Icon::none, "RESCAN VST3" };

    int selectedPlugin = -1;
    int filterIndex = 0;
    bool scanning = false;
    float scanProgress = 0.0f;
    juce::String targetTrack;
    int targetSlot = 0;

    juce::Rectangle<int> panelArea, headerArea, searchArea, filterArea, summaryArea,
                         listArea, detailArea, footerArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserOverlay)
};
} // namespace nc
