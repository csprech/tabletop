// The GPUI front end's way into the studio: the session (graph, devices, cables), each device's panel (its editor,
// kept off-screen and drawn on request) and the mouse and keyboard for the device being played.

#include "TableBridge.h"
#include "../src/studio/BuiltIn.h"
#include "../src/studio/Session.h"
#include "../src/ui/ShelfPictures.h"
#include "../src/ui/Wood.h"
#include "fourpole/src/ui/MainComponent.h"
#include "keytape/src/ui/Panel.h"
#include "padlab/src/ui/MainComponent.h"
#include "trackdesk/src/ui/Panel.h"
#include "valvebox/src/plugin/ValveboxProcessor.h"
#include "valvebox/src/ui/MainComponent.h"
#include "valvebox/src/ui/Widgets.h"
#include <set>

namespace tabletop
{
namespace
{
    uint64_t mix (uint64_t h, uint64_t v) { return (h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2))) * 0x100000001b3ull; }
    uint64_t mixFloat (uint64_t h, float v, float q = 1.0e-3f) { return mix (h, (uint64_t) (int64_t) std::llround (v / q)); }
    uint64_t mixString (uint64_t h, const juce::String& s) { return mix (h, (uint64_t) s.hashCode64()); }

    void copyOut (const juce::Image& img, uint8_t* bgra, int w, int h)
    {
        // JUCE's pixels are premultiplied; GPUI's images aren't
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h && y < img.getHeight(); ++y)
        {
            const auto* src = reinterpret_cast<const juce::PixelARGB*> (bd.getLinePointer (y));
            auto* dst = bgra + (size_t) y * (size_t) w * 4;
            for (int x = 0; x < w && x < img.getWidth(); ++x)
            {
                auto px = src[x];
                px.unpremultiply();
                dst[x * 4 + 0] = px.getBlue();
                dst[x * 4 + 1] = px.getGreen();
                dst[x * 4 + 2] = px.getRed();
                dst[x * 4 + 3] = px.getAlpha();
            }
        }
    }

    juce::ModifierKeys modsFrom (int bits, bool button)
    {
        int f = 0;
        if (bits & 1) f |= juce::ModifierKeys::shiftModifier;
        if (bits & 2) f |= juce::ModifierKeys::commandModifier;
        if (bits & 4) f |= juce::ModifierKeys::altModifier;
        if (bits & 8) f |= juce::ModifierKeys::ctrlModifier;
        if (button) f |= juce::ModifierKeys::leftButtonModifier;
        return juce::ModifierKeys (f);
    }

    /** A window that isn't one: the peer JUCE gives a device's panel. It never draws (GPUI shows the panel), but it
        sits where the panel is on screen, so JUCE delivers the mouse the normal way (with mouse listeners, hover,
        double-clicks and buttons as on screen) and opens menus and pop-ups over the panel, at its size. */
    struct HeadlessPeer final : juce::ComponentPeer
    {
        HeadlessPeer (juce::Component& c, int flags, const bool& focusedFlag) : juce::ComponentPeer (c, flags), focused (focusedFlag) {}
        const bool& focused;
        juce::Rectangle<int> bounds;

        void* getNativeHandle() const override { return nullptr; }
        void setVisible (bool) override {}
        void setTitle (const juce::String&) override {}
        void setBounds (const juce::Rectangle<int>& r, bool) override { bounds = r; }
        juce::Rectangle<int> getBounds() const override { return bounds; }
        juce::Point<float> localToGlobal (juce::Point<float> p) override { return p + bounds.getPosition().toFloat(); }
        juce::Point<float> globalToLocal (juce::Point<float> p) override { return p - bounds.getPosition().toFloat(); }
        void setMinimised (bool) override {}
        bool isMinimised() const override { return false; }
        bool isShowing() const override { return true; }
        void setFullScreen (bool) override {}
        bool isFullScreen() const override { return false; }
        void setIcon (const juce::Image&) override {}
        bool contains (juce::Point<int> p, bool) const override { return bounds.withZeroOrigin().contains (p); }
        OptionalBorderSize getFrameSizeIfPresent() const override { return {}; }
        juce::BorderSize<int> getFrameSize() const override { return {}; }
        bool setAlwaysOnTop (bool) override { return true; }
        void toFront (bool) override {}
        void toBehind (juce::ComponentPeer*) override {}
        bool isFocused() const override { return focused; }
        void grabFocus() override {}
        void repaint (const juce::Rectangle<int>&) override {}
        void performAnyPendingRepaintsNow() override {}
        void setAlpha (float) override {}
        juce::StringArray getAvailableRenderingEngines() override { return { "Software Renderer" }; }
        void textInputRequired (juce::Point<int>, juce::TextInputTarget&) override {}
    };

    /** What holds a device's panel on the (pretend) desktop: in screen points, with the panel scaled into it. */
    struct Mount final : juce::Component
    {
        bool focused = false;
        juce::Point<float> origin;
        float scale = 1.0f;

        Mount() { setAccessible (false); }
        juce::ComponentPeer* createNewPeer (int flags, void*) override { return new HeadlessPeer (*this, flags, focused); }
    };

    /** The mouse sent into a panel through its peer (so JUCE handles it as if it were on screen). */
    struct Forwarder
    {
        uint64_t inputs = 0;
        bool buttonDown = false;

        static juce::Point<float> toPeer (Mount& m, juce::Component& panel, juce::Point<float> p) { return m.getLocalPoint (&panel, p); }

        void mouse (Mount& m, juce::Component& panel, int kind, juce::Point<float> p, int bits)
        {
            if (kind <= 2)
                ++inputs; // (hovering doesn't redraw the panel by itself: the steady tick does)
            auto* peer = m.getPeer();
            if (peer == nullptr)
                return;
            // kind: 0 down, 1 drag, 2 up, 3 moved (no button), 4 left the panel
            buttonDown = kind == 0 || (kind == 1 && buttonDown);
            juce::ModifierKeys::currentModifiers = modsFrom (bits, buttonDown);
            const auto pos = kind == 4 ? juce::Point<float> (-100000.0f, -100000.0f) : toPeer (m, panel, p);
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, pos, juce::ModifierKeys::currentModifiers,
                                    juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::Time::currentTimeMillis());
        }

        void wheel (Mount& m, juce::Component& panel, juce::Point<float> p, float dx, float dy, int bits)
        {
            ++inputs;
            auto* peer = m.getPeer();
            if (peer == nullptr)
                return;
            juce::ModifierKeys::currentModifiers = modsFrom (bits, buttonDown);
            juce::MouseWheelDetails d;
            d.deltaX = dx;
            d.deltaY = dy;
            d.isReversed = false;
            d.isSmooth = true;
            d.isInertial = false;
            peer->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, toPeer (m, panel, p), juce::Time::currentTimeMillis(), d);
        }
    };

    int keyCodeFor (const juce::String& key, juce::juce_wchar ch)
    {
        static const std::pair<const char*, int> named[] = {
            { "space", juce::KeyPress::spaceKey },   { "escape", juce::KeyPress::escapeKey },     { "enter", juce::KeyPress::returnKey },
            { "backspace", juce::KeyPress::backspaceKey }, { "delete", juce::KeyPress::deleteKey }, { "tab", juce::KeyPress::tabKey },
            { "left", juce::KeyPress::leftKey },     { "right", juce::KeyPress::rightKey },       { "up", juce::KeyPress::upKey },
            { "down", juce::KeyPress::downKey },     { "home", juce::KeyPress::homeKey },         { "end", juce::KeyPress::endKey },
            { "f1", juce::KeyPress::F1Key },         { "f2", juce::KeyPress::F2Key },             { "f3", juce::KeyPress::F3Key },
            { "f4", juce::KeyPress::F4Key },         { "f5", juce::KeyPress::F5Key },             { "f6", juce::KeyPress::F6Key },
            { "f7", juce::KeyPress::F7Key },
        };
        for (auto& [n, c] : named)
            if (key == n)
                return c;
        return (int) juce::CharacterFunctions::toUpperCase (ch);
    }
} // namespace

struct Bridge
{
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
    Session session;
    std::map<juce::String, std::unique_ptr<juce::AudioProcessorEditor>> editors;
    std::map<juce::String, std::unique_ptr<Mount>> mounts;
    std::map<juce::String, Forwarder> forwarders;
    juce::String focusedUid;
    juce::Point<float> windowOrigin, originFix; // (the GPUI window's content as GPUI says, and the correction to JUCE's screen)
    uint64_t revision = 1;
    std::string typesJson, sessionJson, lastUid;
    juce::var camera;
    std::set<int> held;
    int modBits = 0;

    Bridge()
    {
        session.onChange = [this] {
            ++revision;
            syncEditors();
        };
        // every device's panel asks us whether a key is held (it isn't on screen, so the computer can't tell it)
        auto hook = [this] (int c) { return held.count ((int) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) c)) != 0; };
        fourpole::MainComponent::keyHook = hook;
        valvebox::ui::MainComponent::keyHook = hook;
        padlab::MainComponent::keyHook = hook;
        trackdesk::ui::Panel::keyHook = hook;
        keytape::ui::Panel::keyHook = hook;
        const auto* open = std::getenv ("TABLETOP_OPEN"); // (a table to open instead: for pictures and the benchmark)
        if (open != nullptr ? ! load (juce::File (juce::String::fromUTF8 (open)))
                            : (std::getenv ("TABLETOP_GPUI_SELFTEST") != nullptr || ! load (autosaveFile())))
            session.createDefaultTable();
        syncEditors();
    }

    ~Bridge()
    {
        for (auto* h : { &fourpole::MainComponent::keyHook, &valvebox::ui::MainComponent::keyHook, &padlab::MainComponent::keyHook,
                         &trackdesk::ui::Panel::keyHook, &keytape::ui::Panel::keyHook })
            *h = nullptr;
        session.onChange = nullptr;
        dropEditors();
        session.clear();
    }

    static juce::File autosaveFile()
    {
        // (the GPUI version keeps its own table, so the app's stays as it was)
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Tabletop").getChildFile ("Last Table (GPUI).tabletop");
    }

    // ---------------------------------------------------------------- the devices' panels

    void dropEditor (const juce::String& uid)
    {
        auto it = editors.find (uid);
        if (it == editors.end())
            return;
        if (auto m = mounts.find (uid); m != mounts.end())
        {
            m->second->removeChildComponent (it->second.get());
            mounts.erase (m);
        }
        if (auto* p = session.getProcessor (uid))
            p->editorBeingDeleted (it->second.get());
        editors.erase (it);
        forwarders.erase (uid);
        if (focusedUid == uid)
            focusedUid = {};
    }

    void dropEditors()
    {
        std::vector<juce::String> uids;
        for (auto& [u, e] : editors)
            uids.push_back (u);
        for (auto& u : uids)
            dropEditor (u);
    }

    void syncEditors()
    {
        std::vector<juce::String> gone;
        for (auto& [u, e] : editors)
            if (session.findDevice (u) == nullptr)
                gone.push_back (u);
        for (auto& u : gone)
            dropEditor (u);
        for (auto& d : session.getDevices())
            if (editors.count (d.uid) == 0)
                if (auto* p = session.getProcessor (d.uid))
                    if (auto* e = p->createEditorAndMakeActive())
                    {
                        editors[d.uid].reset (e);
                        auto m = std::make_unique<Mount>();
                        m->setSize (e->getWidth(), e->getHeight());
                        m->addAndMakeVisible (e);
                        m->setVisible (true);
                        m->addToDesktop (0); // (a peer that never draws: see HeadlessPeer)
                        mounts[d.uid] = std::move (m);
                    }
    }

    juce::AudioProcessorEditor* editor (const char* uid)
    {
        auto it = editors.find (juce::String::fromUTF8 (uid));
        return it != editors.end() ? it->second.get() : nullptr;
    }

    Mount* mount (const char* uid)
    {
        auto it = mounts.find (juce::String::fromUTF8 (uid));
        return it != mounts.end() ? it->second.get() : nullptr;
    }

    /** Where the device's panel is in the window now (its top left, in window points, and points per panel pixel). */
    void place (const char* uid, float x, float y, float scale)
    {
        auto* m = mount (uid);
        auto* e = editor (uid);
        if (m == nullptr || e == nullptr || scale <= 0.0f)
            return;
        const auto origin = windowOrigin + originFix + juce::Point<float> (x, y);
        if (origin == m->origin && scale == m->scale)
            return;
        m->origin = origin;
        m->scale = scale;
        // (whole points for the peer; the fraction goes into the panel's transform)
        const auto topLeft = origin.toInt();
        const auto frac = origin - topLeft.toFloat();
        e->setTransform (juce::AffineTransform::scale (scale).translated (frac));
        m->setBounds (topLeft.x, topLeft.y, (int) std::ceil ((float) e->getWidth() * scale + 1.0f), (int) std::ceil ((float) e->getHeight() * scale + 1.0f));
    }

    void setFocused (const char* uid)
    {
        const auto u = juce::String::fromUTF8 (uid);
        if (u == focusedUid)
            return;
        if (auto* old = mount (focusedUid.toRawUTF8()))
        {
            if (auto* e = editor (focusedUid.toRawUTF8()))
                forwarders[focusedUid].mouse (*old, *e, 4, {}, 0); // (the mouse leaves it)
            old->focused = false;
        }
        juce::PopupMenu::dismissAllActiveMenus();
        focusedUid = u;
        if (auto* m = mount (uid))
            m->focused = true;
    }

    // ---------------------------------------------------------------- tooltips

    /** The caption that names a part: a label just above or below it (as on the devices' screens and knobs). */
    static juce::String captionFor (juce::Component& c)
    {
        auto* parent = c.getParentComponent();
        if (parent == nullptr)
            return {};
        const auto b = c.getBounds();
        juce::String best;
        int bestGap = 15; // (only a caption right against it)
        for (auto* sib : parent->getChildren())
            if (auto* l = dynamic_cast<juce::Label*> (sib); l != nullptr && sib != &c && l->isVisible() && l->getText().trim().isNotEmpty())
            {
                const auto lb = l->getBounds();
                if (lb.getRight() <= b.getX() || lb.getX() >= b.getRight())
                    continue;
                const int gap = lb.getBottom() <= b.getCentreY() ? b.getY() - lb.getBottom() : lb.getY() - b.getBottom();
                if (gap >= -4 && gap < bestGap)
                {
                    bestGap = gap;
                    best = l->getText().trim();
                }
            }
        return best;
    }

    static juce::String describe (juce::Component& c)
    {
        if (auto* tc = dynamic_cast<juce::TooltipClient*> (&c))
            if (auto t = tc->getTooltip(); t.isNotEmpty())
                return t;
        auto name = c.getName().replace ("\n", " ").trim();
        if (name.isEmpty())
            name = c.getTitle().trim();
        if (auto* k = dynamic_cast<valvebox::ui::KnobWidget*> (&c); k != nullptr && name.isEmpty())
            name = k->getLabel();
        if (name.isEmpty())
            name = captionFor (c);
        if (auto* sl = dynamic_cast<juce::Slider*> (&c))
        {
            const auto v = sl->getTextFromValue (sl->getValue()).trim();
            return name.isNotEmpty() ? name + ": " + v : v;
        }
        if (auto* cb = dynamic_cast<juce::ComboBox*> (&c))
            return (name.isNotEmpty() ? name + ": " : juce::String()) + cb->getText() + "  (click to choose)";
        if (auto* bt = dynamic_cast<juce::Button*> (&c); bt != nullptr && name.isEmpty())
            name = bt->getButtonText().trim();
        return name;
    }

    std::string tip;

    const char* tooltip (const char* uid, juce::Point<float> p)
    {
        tip.clear();
        if (auto* e = editor (uid))
            for (auto* c = e->getComponentAt (p.roundToInt()); c != nullptr && c != e; c = c->getParentComponent())
                if (auto t = describe (*c); t.isNotEmpty())
                {
                    tip = t.toStdString();
                    break;
                }
        return tip.c_str();
    }

    int renderDevice (const char* uid, float scale, uint8_t* bgra, int w, int h)
    {
        auto* e = editor (uid);
        if (e == nullptr)
            return 1;
        juce::Image img (juce::Image::ARGB, std::max (1, w), std::max (1, h), true, juce::SoftwareImageType());
        {
            juce::Graphics g (img);
            g.addTransform (juce::AffineTransform::scale (scale));
            e->paintEntireComponent (g, true);
        }
        copyOut (img, bgra, w, h);
        return 0;
    }

    static uint64_t walk (juce::Component& c, uint64_t h)
    {
        h = mix (h, c.isVisible() ? 1 : 2);
        if (! c.isVisible())
            return h;
        const auto b = c.getBounds();
        h = mix (mix (mix (mix (h, (uint64_t) b.getX()), (uint64_t) b.getY()), (uint64_t) b.getWidth()), (uint64_t) b.getHeight());
        if (auto* l = dynamic_cast<juce::Label*> (&c))
            h = mixString (h, l->getText());
        if (auto* s = dynamic_cast<juce::Slider*> (&c))
            h = mixFloat (h, (float) s->getValue(), 1.0e-4f);
        if (auto* bt = dynamic_cast<juce::Button*> (&c))
            h = mixString (mix (mix (h, bt->getToggleState()), bt->isDown()), bt->getButtonText());
        for (auto* child : c.getChildren())
            h = walk (*child, h);
        return h;
    }

    uint64_t signature (const char* uid, bool live)
    {
        auto* e = editor (uid);
        if (e == nullptr)
            return 0;
        uint64_t h = walk (*e, 1469598103934665603ull);
        h = mix (h, forwarders[juce::String::fromUTF8 (uid)].inputs);
        // (its lights, screens and meters: drawn again 15 times a second while it's played, every half second otherwise)
        h = mix (h, juce::Time::getMillisecondCounter() / (live ? 66u : 500u));
        return h;
    }

    // ---------------------------------------------------------------- the keyboard

    /** Where a device's panel takes keys: the first part of it that wants the keyboard (as focus would give it). */
    static juce::Component* keyTargetIn (juce::Component& c)
    {
        if (c.getWantsKeyboardFocus() && c.isVisible())
            return &c;
        for (auto* child : c.getChildren())
            if (auto* t = keyTargetIn (*child))
                return t;
        return nullptr;
    }

    int key (const char* uid, const char* keyName, const char* chars, bool down, int bits)
    {
        auto* e = editor (uid);
        if (e == nullptr)
            return 0;
        modifiers (uid, bits);
        ++forwarders[juce::String::fromUTF8 (uid)].inputs;
        const juce::String name (keyName), typed = juce::String::fromUTF8 (chars);
        const juce::juce_wchar ch = typed.isNotEmpty() ? typed[0] : (name.length() == 1 ? name[0] : 0);
        const int code = keyCodeFor (name, ch);
        const int heldKey = name.length() == 1 ? (int) juce::CharacterFunctions::toLowerCase (name[0]) : code;
        auto* target = juce::Component::getCurrentlyFocusedComponent();
        if (target == nullptr || ! e->isParentOf (target))
            target = keyTargetIn (*e);
        if (target == nullptr)
            target = e;
        bool handled = false;
        if (down)
        {
            const bool repeat = held.count (heldKey) != 0;
            held.insert (heldKey);
            if (! repeat)
                for (auto* c = target; c != nullptr && ! handled; c = c == e ? nullptr : c->getParentComponent())
                    handled = c->keyPressed (juce::KeyPress (code, juce::ModifierKeys::currentModifiers, ch));
        }
        else
            held.erase (heldKey);
        for (auto* c = target; c != nullptr; c = c == e ? nullptr : c->getParentComponent())
            if (c->keyStateChanged (down))
            {
                handled = true;
                break;
            }
        return handled ? 1 : 0;
    }

    void modifiers (const char* uid, int bits)
    {
        juce::ModifierKeys::currentModifiers = modsFrom (bits, false);
        if (bits == modBits)
            return;
        modBits = bits;
        if (auto* e = editor (uid))
        {
            auto* target = keyTargetIn (*e);
            for (auto* c = target != nullptr ? target : e; c != nullptr; c = c == e ? nullptr : c->getParentComponent())
                c->modifierKeysChanged (juce::ModifierKeys::currentModifiers);
        }
    }

    // ---------------------------------------------------------------- the session as data

    static juce::String hexColour (juce::Colour c) { return "#" + c.toDisplayString (true); }

    const char* types()
    {
        juce::Array<juce::var> list;
        for (auto& t : deviceTypes())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", t.id);
            o->setProperty ("name", t.name);
            o->setProperty ("kind", t.kind);
            o->setProperty ("blurb", t.blurb);
            o->setProperty ("widthMm", t.widthMm);
            o->setProperty ("depthMm", t.depthMm);
            o->setProperty ("panelW", t.panelWidth);
            o->setProperty ("panelH", t.panelHeight);
            o->setProperty ("rear", hexColour (t.rear));
            o->setProperty ("unique", t.unique);
            juce::Array<juce::var> jacks;
            for (auto& j : t.jacks)
            {
                auto* jo = new juce::DynamicObject();
                jo->setProperty ("label", j.label);
                jo->setProperty ("midi", j.isMidi());
                jo->setProperty ("input", j.isInput());
                jacks.add (juce::var (jo));
            }
            o->setProperty ("jacks", jacks);
            list.add (juce::var (o));
        }
        typesJson = juce::JSON::toString (list, true).toStdString();
        return typesJson.c_str();
    }

    const char* sessionData()
    {
        juce::Array<juce::var> ds, cs;
        for (auto& d : session.getDevices())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("uid", d.uid);
            o->setProperty ("type", d.type->id);
            o->setProperty ("x", d.pos.x);
            o->setProperty ("y", d.pos.y);
            ds.add (juce::var (o));
        }
        for (auto& c : session.getCables())
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("from", c.from);
            o->setProperty ("fromJack", c.fromJack);
            o->setProperty ("to", c.to);
            o->setProperty ("toJack", c.toJack);
            o->setProperty ("colour", hexColour (c.colour));
            const auto* d = session.findDevice (c.from);
            o->setProperty ("midi", d != nullptr && d->type->jacks[(size_t) c.fromJack].isMidi());
            cs.add (juce::var (o));
        }
        auto* root = new juce::DynamicObject();
        root->setProperty ("revision", (juce::int64) revision);
        root->setProperty ("devices", ds);
        root->setProperty ("cables", cs);
        root->setProperty ("camera", camera);
        sessionJson = juce::JSON::toString (juce::var (root), true).toStdString();
        return sessionJson.c_str();
    }

    // ---------------------------------------------------------------- files

    bool load (const juce::File& f)
    {
        if (! f.existsAsFile())
            return false;
        const auto json = juce::JSON::parse (f);
        dropEditors();
        if (! session.fromJson (json))
            return false;
        camera = json["camera"];
        syncEditors();
        ++revision;
        return true;
    }

    bool save (const juce::File& f, const juce::String& cameraJson)
    {
        auto json = session.toJson();
        if (auto* o = json.getDynamicObject())
            o->setProperty ("camera", juce::JSON::parse (cameraJson));
        camera = json["camera"];
        f.getParentDirectory().createDirectory();
        return f.replaceWithText (juce::JSON::toString (json, true));
    }

    void showAudioSettings()
    {
        auto* selector = new juce::AudioDeviceSelectorComponent (session.getDeviceManager(), 0, 2, 2, 2, true, false, true, false);
        selector->setSize (560, 470);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (selector);
        o.dialogTitle = "Audio / MIDI settings";
        o.dialogBackgroundColour = juce::Colour (0xff1d1f24);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }


    // ---------------------------------------------------------------- the probe (diagnostics: which parts answer the mouse)

    static uint64_t stateHash (juce::AudioProcessor& p)
    {
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        return (uint64_t) juce::String::toHexString (mb.getData(), (int) mb.getSize(), 0).hashCode64();
    }

    static void collect (juce::Component& c, std::vector<juce::Component*>& out)
    {
        for (auto* child : c.getChildren())
            if (child->isVisible() && child->getWidth() > 2 && child->getHeight() > 2)
            {
                out.push_back (child);
                collect (*child, out);
            }
    }

    static uint64_t pixels (juce::Component& c)
    {
        juce::Image img (juce::Image::ARGB, std::max (1, c.getWidth()), std::max (1, c.getHeight()), true, juce::SoftwareImageType());
        {
            juce::Graphics g (img);
            c.paintEntireComponent (g, true);
        }
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        uint64_t h = 1469598103934665603ull;
        for (int y = 0; y < img.getHeight(); y += 2)
            h = mix (h, (uint64_t) juce::String::toHexString (bd.getLinePointer (y), img.getWidth() * 4, 0).hashCode64());
        return h;
    }

    struct Probe : juce::Timer
    {
        struct Item { juce::String uid, type; juce::Component::SafePointer<juce::Component> c; juce::Point<float> at; juce::String what; };
        Bridge& b;
        juce::String path;
        std::vector<Item> items;
        size_t i = 0;
        int phase = 0;
        juce::MemoryBlock before;
        uint64_t s0 = 0, p0 = 0, pDown = 0, sClick = 0, p1 = 0, s1 = 0, pDrag = 0, pDrag2 = 0;
        bool opened = false; // (a menu or a window came up)

        void closeWindows()
        {
            juce::PopupMenu::dismissAllActiveMenus();
            if (auto* mcm = juce::ModalComponentManager::getInstanceWithoutCreating())
            {
                opened = opened || mcm->getNumModalComponents() > 0;
                mcm->cancelAllModalComponents();
            }
        }
        juce::StringArray lines;
        std::map<juce::String, std::pair<int, int>> counts;
        bool done = false;

        Probe (Bridge& br, const char* p) : b (br), path (juce::String::fromUTF8 (p))
        {
            const auto only = juce::SystemStats::getEnvironmentVariable ("TABLETOP_PROBE_ONLY", {});
            for (auto& d : b.session.getDevices())
                if (auto* e = b.editor (d.uid.toRawUTF8()); e != nullptr && (only.isEmpty() || only == d.type->id))
                {
                    std::vector<juce::Component*> all;
                    collect (*e, all);
                    std::set<juce::Component*> seen;
                    for (auto* c : all)
                    {
                        const auto centre = e->getLocalArea (c, c->getLocalBounds()).toFloat().getCentre();
                        if (e->getComponentAt (centre.roundToInt()) != c || ! seen.insert (c).second)
                            continue;
                        auto name = juce::String (typeid (*c).name());
                        items.push_back ({ d.uid, d.type->id, c, centre,
                                           "[" + describe (*c) + "] " + name + " '" + c->getName().replace ("\n", " ") + "' in " + juce::String (typeid (*c->getParentComponent()).name()) + " at "
                                               + centre.toInt().toString() });
                    }
                }
            startTimer (45);
        }

        juce::AudioProcessor* proc() { return b.session.getProcessor (items[i].uid); }
        juce::Component* ed() { return b.editor (items[i].uid.toRawUTF8()); }

        void timerCallback() override
        {
            if (i >= items.size())
            {
                stopTimer();
                for (auto& [t, c] : counts)
                    lines.add ("-- " + t + ": " + juce::String (c.first) + " answer, " + juce::String (c.second) + " don't");
                juce::File (path).replaceWithText (lines.joinIntoString ("\n") + "\n");
                done = true;
                return;
            }
            auto& it = items[i];
            auto* p = proc();
            auto* e = ed();
            auto* c = it.c.getComponent();
            if (p == nullptr || e == nullptr || c == nullptr)
            {
                ++i;
                phase = 0;
                return;
            }
            auto& fw = b.forwarders[it.uid];
            auto* m = b.mount (it.uid.toRawUTF8());
            if (m == nullptr)
            {
                ++i;
                phase = 0;
                return;
            }
            switch (phase++)
            {
                case 0:
                    p->getStateInformation (before);
                    s0 = stateHash (*p);
                    p0 = pixels (*c);
                    fw.mouse (*m, *e, 0, it.at, 0);
                    break;
                case 1:
                    pDown = pixels (*c);
                    fw.mouse (*m, *e, 2, it.at, 0);
                    break;
                case 2: break;
                case 3:
                    sClick = stateHash (*p);
                    p->setStateInformation (before.getData(), (int) before.getSize());
                    closeWindows();
                    break;
                case 4: closeWindows(); break;
                case 5:
                    s1 = stateHash (*p);
                    p1 = pixels (*c);
                    fw.mouse (*m, *e, 0, it.at, 0);
                    for (int k = 1; k <= 6; ++k)
                        fw.mouse (*m, *e, 1, it.at.translated (0.0f, -10.0f * (float) k), 0);
                    break;
                case 6:
                    pDrag = pixels (*c);
                    for (int k = 1; k <= 12; ++k) // (and back down past where it started: a knob at its top only goes down)
                        fw.mouse (*m, *e, 1, it.at.translated (0.0f, -60.0f + 10.0f * (float) k), 0);
                    break;
                case 7:
                    pDrag2 = pixels (*c);
                    fw.mouse (*m, *e, 2, it.at.translated (0.0f, 60.0f), 0);
                    break;
                case 8: break;
                case 9: closeWindows(); break;
                default:
                {
                    const bool click = pDown != p0 || sClick != s0 || opened, drag = pDrag != p1 || pDrag2 != p1 || stateHash (*p) != s1;
                    p->setStateInformation (before.getData(), (int) before.getSize());
                    closeWindows();
                    opened = false;
                    auto& n = counts[it.type];
                    (click || drag ? n.first : n.second)++;
                    lines.add (it.type.paddedRight (' ', 10) + (click || drag ? juce::String ("ok   ") + (click ? "click " : "      ") + (drag ? "drag  " : "      ") : "DEAD              ") + it.what);
                    ++i;
                    phase = 0;
                }
            }
        }
    };
    std::unique_ptr<Probe> probing;

    void probe (const char* path) { probing = std::make_unique<Probe> (*this, path); }
    bool probeDone() const { return probing != nullptr && probing->done; }

    // ---------------------------------------------------------------- the self-test's ears

    template <typename P>
    P* first()
    {
        for (auto& d : session.getDevices())
            if (auto* p = dynamic_cast<P*> (session.getProcessor (d.uid)))
                return p;
        return nullptr;
    }
};
} // namespace tabletop

struct TtTable
{
    tabletop::Bridge b;
};

using tabletop::Bridge;

extern "C" {
TtTable* tt_create (void) { return new TtTable(); }
void tt_destroy (TtTable* t) { delete t; }
const char* tt_types_json (TtTable* t) { return t->b.types(); }
const char* tt_session_json (TtTable* t) { return t->b.sessionData(); }
uint64_t tt_revision (TtTable* t) { return t->b.revision; }

int tt_shelf_picture_size (TtTable*, const char* type, int* w, int* h)
{
    const auto img = tabletop::shelfPicture (juce::String::fromUTF8 (type));
    *w = img.getWidth();
    *h = img.getHeight();
    return img.isValid() ? 0 : 1;
}

int tt_shelf_picture (TtTable*, const char* type, uint8_t* bgra, int w, int h)
{
    const auto img = tabletop::shelfPicture (juce::String::fromUTF8 (type)).convertedToFormat (juce::Image::ARGB);
    if (! img.isValid())
        return 1;
    tabletop::copyOut (img, bgra, w, h);
    return 0;
}

int tt_wood_tile (TtTable*, int sizePx, float sizeMm, uint8_t* bgra)
{
    tabletop::copyOut (tabletop::woodTile (sizePx, sizeMm).convertedToFormat (juce::Image::ARGB), bgra, sizePx, sizePx);
    return 0;
}

int tt_render_device (TtTable* t, const char* uid, float scale, uint8_t* bgra, int w, int h) { return t->b.renderDevice (uid, scale, bgra, w, h); }
uint64_t tt_device_signature (TtTable* t, const char* uid, int live) { return t->b.signature (uid, live != 0); }

const char* tt_add_device (TtTable* t, const char* type, float x, float y)
{
    t->b.lastUid = t->b.session.addDevice (juce::String::fromUTF8 (type), { x, y }).toStdString();
    return t->b.lastUid.c_str();
}
void tt_remove_device (TtTable* t, const char* uid) { t->b.session.removeDevice (juce::String::fromUTF8 (uid)); }
void tt_move_device (TtTable* t, const char* uid, float x, float y)
{
    t->b.session.moveDevice (juce::String::fromUTF8 (uid), { x, y });
    ++t->b.revision;
}
void tt_bring_to_front (TtTable* t, const char* uid)
{
    t->b.session.bringToFront (juce::String::fromUTF8 (uid));
    ++t->b.revision;
}
int tt_can_connect (TtTable* t, const char* a, int aj, const char* b, int bj) { return t->b.session.canConnect (juce::String::fromUTF8 (a), aj, juce::String::fromUTF8 (b), bj) ? 1 : 0; }
int tt_connect (TtTable* t, const char* a, int aj, const char* b, int bj) { return t->b.session.connect (juce::String::fromUTF8 (a), aj, juce::String::fromUTF8 (b), bj) ? 1 : 0; }
int tt_find_cable (TtTable* t, const char* uid, int jack) { return t->b.session.findCable (juce::String::fromUTF8 (uid), jack); }
void tt_disconnect (TtTable* t, int index) { t->b.session.disconnect (index); }

void tt_device_mouse (TtTable* t, const char* uid, int kind, float x, float y, int mods, int)
{
    auto* e = t->b.editor (uid);
    auto* m = t->b.mount (uid);
    if (e != nullptr && m != nullptr)
        t->b.forwarders[juce::String::fromUTF8 (uid)].mouse (*m, *e, kind, { x, y }, mods);
}
void tt_device_wheel (TtTable* t, const char* uid, float x, float y, float dx, float dy, int mods)
{
    auto* e = t->b.editor (uid);
    auto* m = t->b.mount (uid);
    if (e != nullptr && m != nullptr)
        t->b.forwarders[juce::String::fromUTF8 (uid)].wheel (*m, *e, { x, y }, dx, dy, mods);
}
void tt_window_origin (TtTable* t, float x, float y) { t->b.windowOrigin = { x, y }; }
void tt_window_origin_from_pointer (TtTable* t, float x, float y)
{
    // (from a real mouse event: where the pointer is on screen, less where it is in the window; GPUI's own idea of the
    // window's position is only right on the main display)
    t->b.originFix = juce::Desktop::getInstance().getMainMouseSource().getRawScreenPosition() - juce::Point<float> (x, y) - t->b.windowOrigin;
}
void tt_place_device (TtTable* t, const char* uid, float x, float y, float scale) { t->b.place (uid, x, y, scale); }
void tt_set_focused (TtTable* t, const char* uid) { t->b.setFocused (uid); }
const char* tt_device_tooltip (TtTable* t, const char* uid, float x, float y) { return t->b.tooltip (uid, { x, y }); }
int tt_menu_check (TtTable* t, const char* uid)
{
    // (the self-test) 0: nothing open; 1: a menu or window is open over the device's panel; 2: open somewhere else
    auto* mcm = juce::ModalComponentManager::getInstanceWithoutCreating();
    auto* m = t->b.mount (uid);
    if (mcm == nullptr || mcm->getNumModalComponents() == 0 || m == nullptr)
        return 0;
    auto* c = mcm->getModalComponent (0);
    return m->getScreenBounds().intersects (c->getScreenBounds()) ? 1 : 2;
}
void tt_close_menus (TtTable*)
{
    juce::PopupMenu::dismissAllActiveMenus();
    if (auto* mcm = juce::ModalComponentManager::getInstanceWithoutCreating())
        mcm->cancelAllModalComponents();
}
int tt_device_key (TtTable* t, const char* uid, const char* key, const char* chars, int down, int mods) { return t->b.key (uid, key, chars, down != 0, mods); }
void tt_device_modifiers (TtTable* t, const char* uid, int mods) { t->b.modifiers (uid, mods); }

void tt_new_table (TtTable* t)
{
    t->b.dropEditors();
    t->b.session.createDefaultTable();
    t->b.camera = juce::var();
    t->b.syncEditors();
}
int tt_save (TtTable* t, const char* path, const char* cameraJson) { return t->b.save (juce::File (juce::String::fromUTF8 (path)), juce::String::fromUTF8 (cameraJson)) ? 0 : 1; }
int tt_load (TtTable* t, const char* path) { return t->b.load (juce::File (juce::String::fromUTF8 (path))) ? 0 : 1; }
void tt_autosave (TtTable* t, const char* cameraJson)
{
    if (std::getenv ("TABLETOP_GPUI_SELFTEST") == nullptr && std::getenv ("TABLETOP_NO_AUTOSAVE") == nullptr)
        t->b.save (Bridge::autosaveFile(), juce::String::fromUTF8 (cameraJson));
}
void tt_show_audio_settings (TtTable* t) { t->b.showAudioSettings(); }
void tt_show_help (TtTable*)
{
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::NoIcon, "Tabletop (GPUI)",
        "A studio on a table, drawn with GPUI.\n\n"
        "Drag a device from the shelf onto the table. Drag a device to move it; drag from a jack to another to cable them, "
        "or from a cabled jack to pull its cable out. Scroll to move around; pinch, or hold command and scroll, to zoom. "
        "Double-click a device to fly in and play it; Escape or the button goes back.\n\n"
        "The table is saved when you quit (separately from the JUCE app's).");
}

float tt_speaker_level (TtTable* t)
{
    if (auto* i = t->b.first<tabletop::InterfaceProcessor>())
        return std::max (i->peaks[0].load(), i->peaks[1].load());
    return 0.0f;
}
float tt_mixer_channel_level (TtTable* t, int ch)
{
    if (auto* m = t->b.first<tabletop::MixerProcessor>(); m != nullptr && ch >= 0 && ch < tabletop::MixerProcessor::kChannels)
        return m->channels[(size_t) ch].peak.load();
    return 0.0f;
}
void tt_test_sound (TtTable* t, int start)
{
    if (auto* p = t->b.first<valvebox::ValveboxProcessor>())
        p->getEngine().withMachine ([start] (valvebox::Machine& m) {
            m.press (start ? valvebox::Key::PlayPause : valvebox::Key::StopCancel);
            m.release (start ? valvebox::Key::PlayPause : valvebox::Key::StopCancel);
        });
}
int tt_device_count (TtTable* t) { return (int) t->b.session.getDevices().size(); }
void tt_probe (TtTable* t, const char* path) { t->b.probe (path); }
int tt_probe_done (TtTable* t) { return t->b.probeDone() ? 1 : 0; }
}
