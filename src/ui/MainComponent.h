#pragma once

#include "Table.h"

namespace tabletop
{
/** The shelf of devices down the left: drag one onto the table. */
class Shelf : public juce::Component
{
public:
    Shelf();
    void paint (juce::Graphics&) override;
    void resized() override;
    /** Greys out what can't go on the table again (a second audio interface). */
    void refresh (const Session&);

private:
    struct Card : juce::Component
    {
        explicit Card (const DeviceType& t) : type (t) {}
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { dragging = false; }
        const DeviceType& type;
        bool enabled = true, dragging = false;
    };
    std::vector<std::unique_ptr<Card>> cards;
};

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::MenuBarModel,
                      public juce::ApplicationCommandTarget,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    /** Before the window closes: the table is saved, to open again next time. */
    void prepareToQuit();

    juce::ApplicationCommandManager& getCommands() { return commands; }

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

private:
    enum Command
    {
        cmdNew = 1,
        cmdOpen,
        cmdSave,
        cmdSaveAs,
        cmdFit,
        cmdBack,
        cmdAudio,
        cmdHelp
    };
    void timerCallback() override;
    void newTable();
    void openTable();
    bool saveTable (const juce::File& f);
    bool loadTable (const juce::File& f);
    void saveAs();
    void showAudioSettings();
    void showHelp();
    void updateTitle();
    juce::File autosaveFile() const;
    /** TABLETOP_SELFTEST=/path: checks the table, the cables, saving, every device's state and the sound, then quits. */
    void runSelfTest (const juce::File& report);

    juce::ApplicationCommandManager commands;
    Session session;
    TableView table { session };
    Shelf shelf;
    juce::TextButton backBtn { "<  Back to the table" }, fitBtn { "Fit" }, audioBtn { "Audio/MIDI" }, helpBtn { "Help" };
    juce::File currentFile;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace tabletop
