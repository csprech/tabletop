#include "MainComponent.h"
#include "../studio/BuiltIn.h"
#include "fourpole/src/plugin/FourpoleProcessor.h"
#include "padlab/src/plugin/PadlabProcessor.h"
#include "valvebox/src/plugin/ValveboxProcessor.h"

namespace tabletop
{
namespace
{
    juce::Font uiFont (float h, const char* style = "Bold")
    {
        static const bool have = juce::Font::findAllTypefaceNames().contains ("Avenir Next Condensed");
        return have ? juce::Font (juce::FontOptions ("Avenir Next Condensed", style, h)) : juce::Font (juce::FontOptions (h, juce::Font::bold));
    }
    constexpr int kBarHeight = 44, kShelfWidth = 210;

    struct BarLook : juce::LookAndFeel_V4
    {
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (juce::Colours::white.withAlpha (down ? 0.28f : over ? 0.16f : 0.08f));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.setFont (uiFont (15.0f));
            g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
        }
    };
    BarLook& barLook()
    {
        static BarLook l;
        return l;
    }
} // namespace

// ---------------------------------------------------------------- the shelf

Shelf::Shelf()
{
    for (auto& t : deviceTypes())
    {
        auto c = std::make_unique<Card> (t);
        addAndMakeVisible (*c);
        cards.push_back (std::move (c));
    }
}

void Shelf::refresh (const Session& s)
{
    for (auto& c : cards)
    {
        c->enabled = ! (c->type.unique && s.hasDeviceOfType (c->type.id));
        c->repaint();
    }
}

void Shelf::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xe0141518));
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (uiFont (14.0f));
    g.drawText ("DRAG ONTO THE TABLE", r.removeFromTop (34.0f).reduced (14.0f, 0.0f), juce::Justification::centredLeft);
}

void Shelf::resized()
{
    auto r = getLocalBounds().reduced (10);
    r.removeFromTop (26);
    for (auto& c : cards)
    {
        c->setBounds (r.removeFromTop (78));
        r.removeFromTop (8);
    }
}

void Shelf::Card::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::white.withAlpha (enabled ? 0.07f : 0.03f));
    g.fillRoundedRectangle (r, 7.0f);
    // a strip in the device's back-panel colour, like its rear edge
    g.setColour (type.rear.brighter (0.2f).withAlpha (enabled ? 1.0f : 0.4f));
    g.fillRoundedRectangle (r.removeFromLeft (8.0f).reduced (0.0f, 4.0f), 3.0f);
    r.removeFromLeft (8.0f);
    const float a = enabled ? 1.0f : 0.4f;
    g.setColour (juce::Colours::white.withAlpha (0.95f * a));
    g.setFont (uiFont (17.0f));
    g.drawText (type.name, r.removeFromTop (28.0f).withTrimmedTop (6.0f), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.62f * a));
    g.setFont (uiFont (13.0f, "Medium"));
    g.drawFittedText (type.blurb, r.removeFromTop (30.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
    g.setColour (juce::Colours::white.withAlpha (0.4f * a));
    g.drawText (juce::String (juce::roundToInt (type.widthMm)) + " x " + juce::String (juce::roundToInt (type.depthMm + kStripDepthMm)) + " mm",
                r.toNearestInt(), juce::Justification::centredLeft);
}

void Shelf::Card::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! enabled || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragging = true;
        container->startDragging ("device:" + type.id, this, juce::ScaledImage (createComponentSnapshot (getLocalBounds())));
    }
}

// ---------------------------------------------------------------- the window

MainComponent::MainComponent()
{
    addAndMakeVisible (table);
    addAndMakeVisible (shelf);
    for (auto* b : { &backBtn, &fitBtn, &audioBtn, &helpBtn })
    {
        b->setLookAndFeel (&barLook());
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }
    backBtn.onClick = [this] { table.backToTable(); };
    fitBtn.onClick = [this] { table.isFocused() ? table.backToTable() : table.fitAll(); };
    audioBtn.onClick = [this] { showAudioSettings(); };
    helpBtn.onClick = [this] { showHelp(); };
    table.onFocusChanged = [this] { resized(); };
    session.onChange = [this] { shelf.refresh (session); };

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);
    setApplicationCommandManagerToWatch (&commands);

    auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto area = display != nullptr ? display->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1440, 900);
    setSize ((int) (area.getWidth() * 0.92f), (int) (area.getHeight() * 0.9f));
    if (auto* forced = std::getenv ("TABLETOP_WINDOW_SIZE")) // testing aid, e.g. "1600x1000"
    {
        const auto wh = juce::StringArray::fromTokens (forced, "x", {});
        if (wh.size() == 2)
            setSize (wh[0].getIntValue(), wh[1].getIntValue());
    }

    // the table as you left it, or a first table already cabled up (the self-test always starts from a fresh one)
    const auto testFile = std::getenv ("TABLETOP_OPEN");
    if (std::getenv ("TABLETOP_SELFTEST") != nullptr || ! loadTable (testFile != nullptr ? juce::File (testFile) : autosaveFile()))
    {
        session.createDefaultTable();
        table.rebuild();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] {
            if (safe != nullptr)
                safe->table.fitAll (false);
        });
    }
    currentFile = juce::File();
    updateTitle();
    shelf.refresh (session);
    startTimer (60 * 1000);
    if (auto* report = std::getenv ("TABLETOP_SELFTEST"))
        juce::Timer::callAfterDelay (1500, [safe = juce::Component::SafePointer<MainComponent> (this), f = juce::File (report)] {
            if (safe != nullptr)
                safe->runSelfTest (f);
        });
}

MainComponent::~MainComponent()
{
    setApplicationCommandManagerToWatch (nullptr);
    for (auto* b : { &backBtn, &fitBtn, &audioBtn, &helpBtn })
        b->setLookAndFeel (nullptr);
}

juce::File MainComponent::autosaveFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Tabletop").getChildFile ("Last Table.tabletop");
}

void MainComponent::prepareToQuit()
{
    if (std::getenv ("TABLETOP_NO_AUTOSAVE") == nullptr && std::getenv ("TABLETOP_SELFTEST") == nullptr)
        saveTable (autosaveFile());
}

void MainComponent::timerCallback() { prepareToQuit(); } // (a crash doesn't lose the table)

void MainComponent::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff101113)); }

void MainComponent::resized()
{
    table.setBounds (getLocalBounds());
    const bool focused = table.isFocused();
    table.clearOf = juce::BorderSize<int> (kBarHeight + 6, focused ? 0 : kShelfWidth + 20, 10, 10);
    shelf.setVisible (! focused);
    shelf.setBounds (12, kBarHeight + 8, kShelfWidth, juce::jmin (getHeight() - kBarHeight - 20, 36 + (int) deviceTypes().size() * 86 + 8));
    auto bar = getLocalBounds().removeFromTop (kBarHeight).reduced (12, 7);
    helpBtn.setBounds (bar.removeFromRight (70));
    bar.removeFromRight (8);
    audioBtn.setBounds (bar.removeFromRight (110));
    bar.removeFromRight (8);
    fitBtn.setBounds (bar.removeFromRight (60));
    backBtn.setVisible (focused);
    backBtn.setBounds (bar.removeFromLeft (190));
}

void MainComponent::updateTitle()
{
    if (auto* w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        w->setName (currentFile.existsAsFile() ? "Tabletop - " + currentFile.getFileNameWithoutExtension() : juce::String ("Tabletop"));
}

// ---------------------------------------------------------------- files

void MainComponent::newTable()
{
    table.backToTable();
    session.createDefaultTable();
    table.rebuild();
    table.fitAll (false);
    currentFile = juce::File();
    updateTitle();
}

bool MainComponent::saveTable (const juce::File& f)
{
    auto json = session.toJson();
    if (auto* o = json.getDynamicObject())
        o->setProperty ("camera", table.getCameraJson());
    f.getParentDirectory().createDirectory();
    return f.replaceWithText (juce::JSON::toString (json, true));
}

bool MainComponent::loadTable (const juce::File& f)
{
    if (! f.existsAsFile())
        return false;
    const auto json = juce::JSON::parse (f);
    if (! session.fromJson (json))
        return false;
    table.rebuild();
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), cam = json["camera"]] {
        if (safe != nullptr)
            safe->table.setCameraJson (cam);
    });
    return true;
}

void MainComponent::openTable()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a table", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.tabletop");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (f == juce::File())
            return;
        table.backToTable();
        if (loadTable (f))
        {
            currentFile = f;
            updateTitle();
        }
        else
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Tabletop", "That isn't a Tabletop table: " + f.getFileName());
    });
}

void MainComponent::saveAs()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the table", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("My Table.tabletop"),
                                                   "*.tabletop");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc) {
        auto f = fc.getResult();
        if (f == juce::File())
            return;
        f = f.withFileExtension ("tabletop");
        if (saveTable (f))
        {
            currentFile = f;
            updateTitle();
        }
    });
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (session.getDeviceManager(), 0, 2, 2, 2, true, false, true, false);
    selector->setSize (560, 480);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio / MIDI";
    o.dialogBackgroundColour = juce::Colour (0xff1c1d20);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showHelp()
{
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::NoIcon, "Tabletop",
        "THE TABLE: drag the wood to move around; pinch, or Cmd + scroll, to zoom. Right-click the wood to put a device down, or drag one "
        "in from the shelf.\n\n"
        "DEVICES: drag one to move it. Double-click it to zoom in and play it; \"Back to the table\" (or double-click the wood, or "
        "Cmd + 0) to step back. Right-click a device to take it off the table.\n\n"
        "CABLES: drag from a jack on a device's back panel to a jack on another; the jacks it can go to light up. Outputs go to "
        "inputs, MIDI to MIDI. Drag a plug out to move it or unplug it.\n\n"
        "THE AUDIO INTERFACE is your computer: cable to its SPEAKERS to hear something; its IN jacks are your computer's inputs and "
        "its MIDI jack carries every MIDI controller you plug in. The MIXER sums up to six stereo devices.\n\n"
        "The table is saved when you quit and opens again next time; File > Save As keeps a copy.");
}

// ---------------------------------------------------------------- menus and commands

juce::StringArray MainComponent::getMenuBarNames() { return { "File", "View", "Audio" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    if (index == 0)
    {
        m.addCommandItem (&commands, cmdNew);
        m.addCommandItem (&commands, cmdOpen);
        m.addSeparator();
        m.addCommandItem (&commands, cmdSave);
        m.addCommandItem (&commands, cmdSaveAs);
    }
    else if (index == 1)
    {
        m.addCommandItem (&commands, cmdFit);
        m.addCommandItem (&commands, cmdBack);
        m.addSeparator();
        m.addCommandItem (&commands, cmdHelp);
    }
    else
        m.addCommandItem (&commands, cmdAudio);
    return m;
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& c) { c.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdFit, cmdBack, cmdAudio, cmdHelp }); }

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto cmd = juce::ModifierKeys::commandModifier;
    switch (id)
    {
        case cmdNew: info.setInfo ("New Table", "A fresh table, cabled up", "File", 0); info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen: info.setInfo ("Open...", "Open a saved table", "File", 0); info.addDefaultKeypress ('o', cmd); break;
        case cmdSave: info.setInfo ("Save", "Save the table", "File", 0); info.addDefaultKeypress ('s', cmd); break;
        case cmdSaveAs: info.setInfo ("Save As...", "Save the table as a new file", "File", 0); info.addDefaultKeypress ('s', cmd | juce::ModifierKeys::shiftModifier); break;
        case cmdFit: info.setInfo ("Show the Whole Table", "Zoom out to see every device", "View", 0); info.addDefaultKeypress ('0', cmd); break;
        case cmdBack:
            info.setInfo ("Back to the Table", "Step back from the device you're playing", "View", 0);
            info.setActive (table.isFocused());
            info.addDefaultKeypress (KP::upKey, cmd);
            break;
        case cmdAudio: info.setInfo ("Audio / MIDI Settings...", "The computer's audio device and MIDI inputs", "Audio", 0); info.addDefaultKeypress (',', cmd); break;
        case cmdHelp: info.setInfo ("How Tabletop Works", "A quick guide", "View", 0); break;
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdNew: newTable(); return true;
        case cmdOpen: openTable(); return true;
        case cmdSave:
            if (currentFile.existsAsFile())
                saveTable (currentFile);
            else
                saveAs();
            return true;
        case cmdSaveAs: saveAs(); return true;
        case cmdFit:
            if (table.isFocused())
                table.backToTable();
            else
                table.fitAll();
            return true;
        case cmdBack: table.backToTable(); return true;
        case cmdAudio: showAudioSettings(); return true;
        case cmdHelp: showHelp(); return true;
        default: return false;
    }
}
} // namespace tabletop

namespace tabletop
{
// ---------------------------------------------------------------- self-test

void MainComponent::runSelfTest (const juce::File& reportFile)
{
    auto lines = std::make_shared<juce::StringArray>();
    auto failures = std::make_shared<int> (0);
    auto check = [lines, failures] (bool ok, const juce::String& what) {
        lines->add ((ok ? "ok    " : "FAIL  ") + what);
        if (! ok)
            ++*failures;
    };
    auto find = [this] (const juce::String& type) -> juce::String {
        for (auto& d : session.getDevices())
            if (d.type->id == type)
                return d.uid;
        return {};
    };
    auto jack = [this] (const juce::String& uid, const juce::String& label) {
        const auto* d = session.findDevice (uid);
        for (size_t i = 0; d != nullptr && i < d->type->jacks.size(); ++i)
            if (d->type->jacks[i].label == label)
                return (int) i;
        return -1;
    };

    lines->add ("-- the first table: five devices, cabled up");
    check (session.getDevices().size() == 5, "5 devices on the table (" + juce::String ((int) session.getDevices().size()) + ")");
    check (session.getCables().size() == 8, "8 cables (" + juce::String ((int) session.getCables().size()) + ")");
    check (table.getNodes().size() == 5, "every device has its place on the table");
    bool thumbs = true;
    for (auto& n : table.getNodes())
        thumbs = thumbs && n->getThumbnailScale() > 0.0f && n->getEditor() != nullptr;
    check (thumbs, "every device shows its panel");
    const auto fp = find ("fourpole"), vb = find ("valvebox"), pl = find ("padlab"), mx = find ("mixer"), ai = find ("interface");

    lines->add ("-- cables: outputs to inputs, MIDI to MIDI, one per jack");
    check (session.canConnect (fp, jack (fp, "OUTPUT"), mx, jack (mx, "5 L")) == false, "a jack in use takes no second cable");
    check (session.canConnect (vb, jack (vb, "OUT 3"), mx, jack (mx, "5 L")), "an output to a free input: yes");
    check (session.canConnect (mx, jack (mx, "5 L"), vb, jack (vb, "OUT 3")), "either way round");
    check (! session.canConnect (vb, jack (vb, "OUT 3"), pl, jack (pl, "OUT L")), "an output to an output: no");
    check (! session.canConnect (ai, jack (ai, "MIDI"), vb, jack (vb, "AUDIO IN")), "MIDI to audio: no");
    check (! session.canConnect (vb, jack (vb, "MIDI OUT"), vb, jack (vb, "MIDI IN")), "a device to itself: no");
    check (session.connect (vb, jack (vb, "MIDI OUT"), pl, jack (pl, "MIDI IN")), "Valvebox MIDI OUT to Padlab MIDI IN");
    check (session.findCable (pl, jack (pl, "MIDI IN")) >= 0, "the cable is there at both ends");
    session.disconnect (session.findCable (pl, jack (pl, "MIDI IN")));
    check (session.findCable (vb, jack (vb, "MIDI OUT")) < 0, "and pulled out again");
    if (auto* mixer = dynamic_cast<MixerProcessor*> (session.getProcessor (mx)))
    {
        check (mixer->channels[0].stereo.load(), "mixer channel 1 (two cables) is stereo");
        check (! mixer->channels[2].stereo.load(), "mixer channel 3 (Fourpole, one cable) is mono");
    }

    lines->add ("-- every device keeps its state");
    if (auto* p = dynamic_cast<fourpole::FourpoleProcessor*> (session.getProcessor (fp)))
    {
        auto patch = p->getEngine().getPatch();
        patch.name = "Selftest";
        patch.glide = 3.25f;
        p->getEngine().setPatch (patch);
        juce::MemoryBlock m;
        p->getStateInformation (m);
        fourpole::FourpoleProcessor fresh;
        fresh.setStateInformation (m.getData(), (int) m.getSize());
        check (std::abs (fresh.getEngine().getPatch().glide - 3.25f) < 1.0e-4f && fresh.getEngine().getPatch().name == "Selftest", "Fourpole: the patch");
    }
    if (auto* p = dynamic_cast<valvebox::ValveboxProcessor*> (session.getProcessor (vb)))
    {
        juce::String before;
        p->getEngine().withMachine ([&] (valvebox::Machine& m) {
            auto& step = m.getMemory().patterns[3].parts[0].steps[5];
            step.on = ! step.on;
            m.getMemory().patterns[3].setName ("SELFTEST");
            before = juce::JSON::toString (valvebox::memoryToJson (m.getMemory()));
        });
        juce::MemoryBlock m;
        p->getStateInformation (m);
        valvebox::ValveboxProcessor fresh;
        fresh.setStateInformation (m.getData(), (int) m.getSize());
        juce::String after;
        fresh.getEngine().withMachine ([&] (valvebox::Machine& mc) { after = juce::JSON::toString (valvebox::memoryToJson (mc.getMemory())); });
        check (before == after, "Valvebox: the whole memory (" + juce::String ((int) (m.getSize() / 1024)) + " KB compressed)");
    }
    if (auto* p = dynamic_cast<padlab::PadlabProcessor*> (session.getProcessor (pl)))
    {
        p->getEngine().withProject ([] (padlab::Project& pr) { pr.masterLevelDb = -3.5f; });
        juce::MemoryBlock m;
        p->getStateInformation (m);
        padlab::PadlabProcessor fresh;
        fresh.setStateInformation (m.getData(), (int) m.getSize());
        float level = 0.0f;
        int samples = 0;
        fresh.getEngine().withProject ([&] (padlab::Project& pr) {
            level = pr.masterLevelDb;
            samples = (int) pr.samplePool.size();
        });
        int samplesBefore = 0;
        p->getEngine().withProject ([&] (padlab::Project& pr) { samplesBefore = (int) pr.samplePool.size(); });
        check (std::abs (level + 3.5f) < 1.0e-4f && samples == samplesBefore,
               "Padlab: the project, samples and all (" + juce::String (samples) + " samples, " + juce::String ((int) (m.getSize() / 1024)) + " KB)");
    }

    lines->add ("-- the table survives saving and opening");
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("tabletop-selftest.tabletop");
        check (saveTable (tmp), "saved");
        std::vector<std::pair<juce::String, juce::Point<float>>> devicesBefore;
        for (auto& d : session.getDevices())
            devicesBefore.push_back ({ d.type->id, d.pos });
        const auto cablesBefore = session.getCables().size();
        session.clear();
        table.rebuild();
        check (session.getDevices().empty(), "cleared");
        check (loadTable (tmp), "opened");
        bool same = session.getDevices().size() == devicesBefore.size();
        for (size_t i = 0; same && i < devicesBefore.size(); ++i)
            same = session.getDevices()[i].type->id == devicesBefore[i].first && session.getDevices()[i].pos.getDistanceFrom (devicesBefore[i].second) < 0.01f;
        check (same, "the same devices in the same places");
        check (session.getCables().size() == cablesBefore, "the same cables");
        tmp.deleteFile();
    }

    lines->add ("-- sound: Valvebox through the mixer to the speakers");
    const auto vbNow = find ("valvebox");
    if (auto* p = dynamic_cast<valvebox::ValveboxProcessor*> (session.getProcessor (vbNow)))
        p->getEngine().withMachine ([] (valvebox::Machine& m) {
            m.press (valvebox::Key::PlayPause);
            m.release (valvebox::Key::PlayPause);
        });
    juce::Timer::callAfterDelay (1500, [this, lines, failures, check, find, reportFile, vbNow] {
        float speakers = 0.0f, channel1 = 0.0f;
        if (auto* iface = dynamic_cast<InterfaceProcessor*> (session.getProcessor (find ("interface"))))
            speakers = std::max (iface->peaks[0].load(), iface->peaks[1].load());
        if (auto* mixer = dynamic_cast<MixerProcessor*> (session.getProcessor (find ("mixer"))))
            channel1 = mixer->channels[0].peak.load();
        check (channel1 > 0.01f, "mixer channel 1 hears the Valvebox (" + juce::String (channel1, 3) + ")");
        check (speakers > 0.01f, "the speakers hear the mix (" + juce::String (speakers, 3) + ")");
        if (auto* p = dynamic_cast<valvebox::ValveboxProcessor*> (session.getProcessor (vbNow)))
            p->getEngine().withMachine ([] (valvebox::Machine& m) {
                m.press (valvebox::Key::StopCancel);
                m.release (valvebox::Key::StopCancel);
            });
        lines->add ("-- playing a device up close");
        table.focusOn (vbNow);
        juce::Timer::callAfterDelay (900, [this, lines, failures, check, reportFile, vbNow, find] {
            auto* n = table.findNode (vbNow);
            check (n != nullptr && n->isLive() && table.isFocused(), "double-click: the view flies in and the panel is live");
            table.backToTable();
            check (n != nullptr && ! n->isLive() && ! table.isFocused(), "back to the table");
            const auto cables = session.getCables().size();
            table.removeDevice (find ("padlab"));
            check (session.getDevices().size() == 4 && session.getCables().size() == cables - 2 && table.getNodes().size() == 4,
                   "taking Padlab off the table takes its cables with it");
            lines->add ("");
            lines->add (*failures == 0 ? juce::String ("ALL PASSED") : juce::String (*failures) + " FAILED");
            reportFile.replaceWithText (lines->joinIntoString ("\n") + "\n");
            juce::JUCEApplicationBase::quit();
        });
    });
}
} // namespace tabletop
