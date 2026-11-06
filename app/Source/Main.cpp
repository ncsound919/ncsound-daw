#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"
#include "BinaryData.h"

namespace nc
{
namespace
{
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
        : DocumentWindow (name, brand::well, DocumentWindow::allButtons)
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
        centreWithSize (1280, 760);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
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
        auto& look = juce::LookAndFeel::getDefaultLookAndFeel();
        look.setDefaultSansSerifTypeface (juce::Typeface::createSystemTypefaceFor (BinaryData::Inter_ttf, BinaryData::Inter_ttfSize));
        look.setColour (juce::ResizableWindow::backgroundColourId, brand::well);
        mainWindow = std::make_unique<MainWindow> (getApplicationName(), commandLine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};
} // namespace nc

START_JUCE_APPLICATION (nc::NCSoundApplication)
// end of file
