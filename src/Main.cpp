#include "ui/MainComponent.h"

namespace tabletop
{
class TabletopApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override { window = std::make_unique<MainWindow> (getApplicationName()); }

    void shutdown() override
    {
        if (window != nullptr)
            if (auto* mc = dynamic_cast<MainComponent*> (window->getContentComponent()))
            {
                mc->prepareToQuit();
                juce::MenuBarModel::setMacMainMenu (nullptr);
            }
        window.reset();
    }

    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name) : DocumentWindow (name, juce::Colour (0xff101113), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent();
            setContentOwned (content, true);
            juce::MenuBarModel::setMacMainMenu (content);
            addKeyListener (content->getCommands().getKeyMappings());
            setResizable (true, true);
            setResizeLimits (900, 600, 6000, 4000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<MainWindow> window;
};
} // namespace tabletop

START_JUCE_APPLICATION (tabletop::TabletopApplication)
