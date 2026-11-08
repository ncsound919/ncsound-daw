#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"
#include "PluginScanner.h"
#include "BinaryData.h"

namespace nc
{
namespace
{
// Development affordance: --ui-scale=1.5 reproduces a 150% Windows display
// without touching the operator's display settings.
double parseUiScale (const juce::String& commandLine)
{
    juce::StringArray tokens;
    tokens.addTokens (commandLine, " ", "\"");

    for (const auto& token : tokens)
        if (token.startsWith ("--ui-scale="))
            return juce::jlimit (0.75, 2.5,
                                 token.fromFirstOccurrenceOf ("=", false, false).getDoubleValue());

    return 1.0;
}

juce::File searchUpwards (juce::File directory)
{
    for (int depth = 0; depth < 8 && directory.isDirectory(); ++depth)
    {
        const auto candidate = directory.getChildFile ("plugins").getChildFile ("registry.json");

        if (candidate.existsAsFile())
            return candidate;

        directory = directory.getParentDirectory();
    }

    return {};
}

juce::File locateRegistry()
{
    const auto besideCwd = searchUpwards (juce::File::getCurrentWorkingDirectory());

    if (besideCwd.existsAsFile())
        return besideCwd;

    return searchUpwards (juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory());
}
} // namespace

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, const juce::String& commandLine)
        : DocumentWindow (name, theme::ink, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);

        auto registryFile = locateRegistry();

        if (commandLine.isNotEmpty())
        {
            const auto candidate = juce::File::getCurrentWorkingDirectory().getChildFile (commandLine.unquoted());

            if (candidate.existsAsFile())
                registryFile = candidate;
        }

        setContentOwned (new MainComponent (registryFile), true);
        setResizable (true, true);
        // Fit the window to the display at the current scale: a 1440 x 900
        // logical window does not fit a 1080p panel at 150%.
        auto usable = juce::Rectangle<int> (0, 0, 1440, 900);

        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            usable = display->userBounds.toNearestInt();

        const auto scale = juce::jmax (1.0f, juce::Desktop::getInstance().getGlobalScaleFactor());
        const auto available = usable.toFloat() / scale;

        // Leave room for the native frame: a 1366 px panel cannot host a
        // 1366 px client area without the window running off-screen.
        const auto width = juce::jmin (1440, juce::roundToInt (available.getWidth()) - 24);
        const auto height = juce::jmin (900, juce::roundToInt (available.getHeight()) - 64);

        setResizeLimits (juce::jmin (1180, width), juce::jmin (720, height), 3840, 2160);
        centreWithSize (width, height);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (auto* component = dynamic_cast<MainComponent*> (getContentComponent()))
        {
            if (! component->confirmClose())
                return;
        }

        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class NCSoundApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "NCSound Music Hub"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        // The same executable is the scanner worker. Initialise it before any
        // window or audio object so the child process never builds the editor.
        auto scanner = std::make_unique<PluginScannerSubprocess>();

        if (scanner->initialiseFromCommandLine (commandLine, scannerProcessUID))
        {
            scannerSubprocess = std::move (scanner);
            return;
        }

        auto& look = juce::LookAndFeel::getDefaultLookAndFeel();
        look.setDefaultSansSerifTypeface (theme::grotesk());
        look.setColour (juce::ResizableWindow::backgroundColourId, theme::ink);
        const auto uiScale = parseUiScale (commandLine);

        if (uiScale != 1.0)
            juce::Desktop::getInstance().setGlobalScaleFactor (static_cast<float> (uiScale));

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), commandLine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        scannerSubprocess = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PluginScannerSubprocess> scannerSubprocess;
};
} // namespace nc

START_JUCE_APPLICATION (nc::NCSoundApplication)
// end of file
