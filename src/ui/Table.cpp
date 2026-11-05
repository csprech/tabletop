#include "Table.h"
#include "Wood.h"

namespace tabletop
{
namespace
{
    juce::Font labelFont (float h)
    {
        static const bool have = juce::Font::findAllTypefaceNames().contains ("Avenir Next Condensed");
        return have ? juce::Font (juce::FontOptions ("Avenir Next Condensed", "Bold", h)) : juce::Font (juce::FontOptions (h, juce::Font::bold));
    }

    /** A cable lying on the table between two points (screen px): it leaves each jack away from its device and
        curves round, with a shadow, a body and a highlight. */
    void drawCable (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, juce::Colour colour, float width, float zoom)
    {
        const float lift = std::max (45.0f * zoom, a.getDistanceFrom (b) * 0.32f);
        juce::Path p;
        p.startNewSubPath (a);
        p.cubicTo (a.translated (0.0f, -lift), b.translated (0.0f, -lift), b);
        const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.strokePath (p, juce::PathStrokeType (width * 1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (width * 0.5f, width * 0.9f));
        g.setColour (colour.darker (0.45f));
        g.strokePath (p, stroke);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (width * 0.74f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (colour.getPerceivedBrightness() > 0.5f ? 0.35f : 0.22f));
        g.strokePath (p, juce::PathStrokeType (width * 0.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (-width * 0.16f, -width * 0.18f));
    }

    /** A plug seen from above: its coloured boot and the metal collar. */
    void drawPlug (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, bool midi)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.3f, r * 2.3f).withCentre (c.translated (r * 0.25f, r * 0.45f)));
        const auto boot = juce::Rectangle<float> (r * 2.2f, r * 2.2f).withCentre (c);
        const auto col = midi ? juce::Colour (0xff1e1f22) : colour;
        g.setGradientFill (juce::ColourGradient (col.brighter (0.35f), boot.getX(), boot.getY(), col.darker (0.4f), boot.getRight(), boot.getBottom(), false));
        g.fillEllipse (boot);
        const auto collar = boot.reduced (r * 0.45f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe8eaee), collar.getX(), collar.getY(), juce::Colour (0xff70757e), collar.getRight(),
                                                 collar.getBottom(), false));
        g.fillEllipse (collar);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillEllipse (collar.reduced (collar.getWidth() * 0.32f));
    }

    constexpr float kJackY = 15.5f; // jack centres, mm from the back edge
} // namespace

// ---------------------------------------------------------------- a device on the table

DeviceNode::DeviceNode (TableView& t, const juce::String& u, const DeviceType& ty, juce::AudioProcessor& processor)
    : uid (u), type (ty), table (t), body (*this)
{
    setSize (juce::roundToInt (type.widthMm), juce::roundToInt (kStripDepthMm + type.depthMm));
    body.setBounds (getBodyArea().getSmallestIntegerContainer());
    addAndMakeVisible (body);
    editor.reset (processor.createEditorAndMakeActive());
    if (editor != nullptr)
    {
        body.addChildComponent (*editor); // shown when you zoom in to play it
        editor->setTopLeftPosition (0, 0);
        editor->setTransform (juce::AffineTransform::scale ((float) body.getWidth() / (float) editor->getWidth(),
                                                            (float) body.getHeight() / (float) editor->getHeight()));
    }
}

DeviceNode::~DeviceNode() { editor.reset(); }

juce::Rectangle<float> DeviceNode::getBodyArea() const { return { 0.0f, kStripDepthMm, type.widthMm, type.depthMm }; }

float DeviceNode::jackRadius (int jack) const { return type.jacks[(size_t) jack].isMidi() ? 6.4f : 4.8f; }

juce::Point<float> DeviceNode::getJackCentre (int jack) const
{
    const int n = (int) type.jacks.size();
    const float spacing = std::min (30.0f, (type.widthMm - 30.0f) / (float) juce::jmax (1, n));
    const float x0 = (type.widthMm - spacing * (float) (n - 1)) * 0.5f;
    return { x0 + spacing * (float) jack, kJackY };
}

int DeviceNode::jackAt (juce::Point<float> p) const
{
    for (int j = 0; j < (int) type.jacks.size(); ++j)
        if (p.getDistanceFrom (getJackCentre (j)) <= jackRadius (j) + 3.0f)
            return j;
    return -1;
}

void DeviceNode::setLive (bool on)
{
    if (live == on || editor == nullptr)
        return;
    live = on;
    editor->setVisible (on);
    if (on)
        editor->grabKeyboardFocus();
    body.repaint();
}

void DeviceNode::refreshThumbnail (float scale)
{
    if (editor == nullptr)
        return;
    thumb = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    thumbScale = scale;
    thumbTime = juce::Time::getMillisecondCounter();
    body.repaint();
}

void DeviceNode::Body::paint (juce::Graphics& g)
{
    if (owner.live)
        return;
    const auto r = getLocalBounds().toFloat();
    if (owner.thumb.isValid())
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (owner.thumb, r, juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        g.setColour (juce::Colour (0xff2a2c31));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.setFont (labelFont (12.0f));
        g.drawText (owner.type.name, r, juce::Justification::centred);
    }
}

void DeviceNode::paint (juce::Graphics& g)
{
    // the back panel: a strip of folded metal in the device's colour, with its jacks
    const auto strip = juce::Rectangle<float> (0.0f, 0.0f, type.widthMm, kStripDepthMm + 3.0f);
    g.setGradientFill (juce::ColourGradient (type.rear.brighter (0.28f), 0.0f, 0.0f, type.rear.darker (0.35f), 0.0f, strip.getBottom(), false));
    g.fillRoundedRectangle (strip, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 2.0f, type.widthMm - 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (0.0f, kStripDepthMm - 1.0f, type.widthMm, 1.2f);
    for (float x : { 6.0f, type.widthMm - 6.0f })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd8dbe0), x - 2.0f, 9.0f, juce::Colour (0xff6d727b), x + 2.0f, 13.0f, false));
        g.fillEllipse (x - 2.2f, 8.8f, 4.4f, 4.4f);
    }
    const auto& cableDrag = table.getCableDrag();
    for (int j = 0; j < (int) type.jacks.size(); ++j)
    {
        const auto& spec = type.jacks[(size_t) j];
        const auto c = getJackCentre (j);
        const float r = jackRadius (j);
        g.setColour (juce::Colours::white.withAlpha (0.88f));
        g.setFont (labelFont (3.2f));
        g.drawText (spec.label, juce::Rectangle<float> (c.x - 15.0f, 1.2f, 30.0f, 4.2f), juce::Justification::centred, false);
        // a cable being dragged lights the jacks it could go into
        if (cableDrag.active && table.getSession().canConnect (cableDrag.uid, cableDrag.jack, uid, j))
        {
            g.setColour (juce::Colour (0xff6dff8a).withAlpha (0.55f));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 5.0f, r * 2.0f + 5.0f).withCentre (c));
        }
        // the socket: a nut round a dark hole (a DIN socket for MIDI)
        const auto nut = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe3e6ea), nut.getX(), nut.getY(), juce::Colour (0xff60656e), nut.getRight(), nut.getBottom(), false));
        g.fillEllipse (nut);
        g.setColour (juce::Colour (0xff0d0e10));
        if (spec.isMidi())
        {
            g.fillEllipse (nut.reduced (r * 0.18f));
            g.setColour (juce::Colour (0xffb8bcc3));
            for (int pin = 0; pin < 5; ++pin)
            {
                const float a = juce::MathConstants<float>::pi * (0.75f + 0.25f * (float) pin) + juce::MathConstants<float>::halfPi;
                g.fillEllipse (juce::Rectangle<float> (1.0f, 1.0f).withCentre ({ c.x + std::cos (a) * r * 0.5f, c.y + std::sin (a) * r * 0.5f }));
            }
        }
        else
            g.fillEllipse (nut.reduced (r * 0.42f));
        // a cable plugged in
        if (const int ci = table.getSession().findCable (uid, j); ci >= 0)
            drawPlug (g, c, r, table.getSession().getCables()[(size_t) ci].colour, spec.isMidi());
    }
}

void DeviceNode::showMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (type.name);
    m.addItem ("Play it (zoom in)", ! live, false, [this] { table.focusOn (uid); });
    m.addItem ("Unplug every cable", [u = uid, t = &table] {
        auto& s = t->getSession();
        for (int i = (int) s.getCables().size(); --i >= 0;)
            if (s.getCables()[(size_t) i].from == u || s.getCables()[(size_t) i].to == u)
                s.disconnect (i);
        t->cablesChanged();
    });
    m.addSeparator();
    m.addItem ("Take off the table", [t = &table, u = uid] { juce::MessageManager::callAsync ([t, u] { t->removeDevice (u); }); });
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void DeviceNode::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    toFront (false);
    table.getSession().bringToFront (uid);
    const auto screen = e.getEventRelativeTo (&table).position;
    if (const int j = e.position.y < kStripDepthMm ? jackAt (e.position) : -1; j >= 0)
    {
        auto& s = table.getSession();
        if (const int ci = s.findCable (uid, j); ci >= 0)
        {
            // pulling a plug out: the cable stays in its other end, this end follows the mouse
            const auto c = s.getCables()[(size_t) ci];
            const bool thisIsFrom = c.from == uid && c.fromJack == j;
            s.disconnect (ci);
            table.beginCableDrag (thisIsFrom ? c.to : c.from, thisIsFrom ? c.toJack : c.fromJack, c.colour);
        }
        else
            table.beginCableDrag (uid, j, s.nextCableColour (type.jacks[(size_t) j].isMidi()));
        table.updateCableDrag (screen);
        drag = Drag::Cable;
        return;
    }
    drag = Drag::Move;
    const auto* d = table.getSession().findDevice (uid);
    grabOffset = table.toTable (screen) - (d != nullptr ? d->pos : juce::Point<float>());
}

void DeviceNode::mouseDrag (const juce::MouseEvent& e)
{
    const auto screen = e.getEventRelativeTo (&table).position;
    if (drag == Drag::Cable)
        table.updateCableDrag (screen);
    else if (drag == Drag::Move)
        table.moveNode (*this, table.toTable (screen) - grabOffset);
}

void DeviceNode::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::Cable)
        table.endCableDrag (e.getEventRelativeTo (&table).position);
    drag = Drag::None;
}

void DeviceNode::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! live && e.position.y >= kStripDepthMm)
        table.focusOn (uid);
}

// ---------------------------------------------------------------- cables

void CableLayer::paint (juce::Graphics& g)
{
    const float z = table.getZoom();
    auto& s = table.getSession();
    auto widthFor = [&] (bool midi) { return std::max (2.0f, (midi ? 6.4f : 5.2f) * z); };
    if (! dragging)
    {
        for (auto& c : s.getCables())
        {
            const auto* d = s.findDevice (c.from);
            const bool midi = d != nullptr && d->type->jacks[(size_t) c.fromJack].isMidi();
            drawCable (g, table.jackOnScreen (c.from, c.fromJack), table.jackOnScreen (c.to, c.toJack), midi ? juce::Colour (0xff1e1f22) : c.colour,
                       widthFor (midi), z);
        }
        return;
    }
    const auto& cd = table.getCableDrag();
    if (! cd.active)
        return;
    const auto* d = s.findDevice (cd.uid);
    const bool midi = d != nullptr && d->type->jacks[(size_t) cd.jack].isMidi();
    const auto a = table.jackOnScreen (cd.uid, cd.jack);
    drawCable (g, a, cd.screenPos, midi ? juce::Colour (0xff1e1f22) : cd.colour, widthFor (midi), z);
    drawPlug (g, a, (midi ? 6.4f : 4.8f) * z, cd.colour, midi);
    drawPlug (g, cd.screenPos, (midi ? 6.4f : 4.8f) * z, cd.colour, midi);
}

// ---------------------------------------------------------------- the table

TableView::TableView (Session& s) : session (s), vblank (this, [this] { stepAnimation(); })
{
    wood = woodTile (1536, woodMm);
    addAndMakeVisible (cablesUnder);
    addAndMakeVisible (cablesDragged);
    setWantsKeyboardFocus (true);
    startTimerHz (15);
}

TableView::~TableView()
{
    stopTimer();
    nodes.clear();
}

float TableView::displayScale() const
{
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
        return (float) d->scale;
    return 1.0f;
}

DeviceNode* TableView::findNode (const juce::String& uid) const
{
    for (auto& n : nodes)
        if (n->uid == uid)
            return n.get();
    return nullptr;
}

void TableView::rebuild()
{
    // gone from the session: off the table
    for (auto it = nodes.begin(); it != nodes.end();)
        it = session.findDevice ((*it)->uid) == nullptr ? nodes.erase (it) : it + 1;
    // new in the session: onto the table
    for (auto& d : session.getDevices())
        if (findNode (d.uid) == nullptr)
            if (auto* p = session.getProcessor (d.uid))
            {
                auto node = std::make_unique<DeviceNode> (*this, d.uid, *d.type, *p);
                addAndMakeVisible (*node);
                nodes.push_back (std::move (node));
            }
    // stacked as the session has them (the last one touched on top), the dragged cable over everything
    for (auto& d : session.getDevices())
        if (auto* n = findNode (d.uid))
            n->toFront (false);
    cablesDragged.toFront (false);
    layoutNodes();
    for (auto& n : nodes)
        if (n->getThumbnailScale() <= 0.0f)
            n->refreshThumbnail (0.5f);
    repaint();
}

void TableView::layoutNodes()
{
    for (auto& n : nodes)
        if (const auto* d = session.findDevice (n->uid))
        {
            const auto topLeft = toScreen ({ d->pos.x, d->pos.y - kStripDepthMm });
            n->setTransform (juce::AffineTransform::scale (zoom).translated (topLeft));
        }
    cablesUnder.repaint();
    cablesDragged.repaint();
}

void TableView::setCamera (float z, juce::Point<float> off)
{
    zoom = juce::jlimit (0.08f, 8.0f, z);
    offset = off;
    cameraMoved();
}

void TableView::cameraMoved()
{
    layoutNodes();
    repaint();
    // a live panel would re-render at every step of the move: show its picture until the view settles
    if (! anim.active)
        if (auto* n = findNode (focused); n != nullptr && n->isLive())
        {
            n->setLive (false);
            liveResumeAt = juce::Time::getMillisecondCounter() + 350;
        }
}

void TableView::zoomAround (juce::Point<float> p, float factor)
{
    const auto mm = toTable (p);
    const float z = juce::jlimit (0.08f, 8.0f, zoom * factor);
    setCamera (z, p - mm * z);
}

void TableView::fitAll (bool animate)
{
    if (nodes.empty())
        return;
    juce::Rectangle<float> all;
    for (auto& d : session.getDevices())
    {
        const auto r = juce::Rectangle<float> (d.pos.x, d.pos.y - kStripDepthMm, d.type->widthMm, d.type->depthMm + kStripDepthMm);
        all = all.isEmpty() ? r : all.getUnion (r);
    }
    all = all.expanded (60.0f);
    const auto view = clearOf.subtractedFrom (getLocalBounds()).toFloat();
    const float z = juce::jlimit (0.08f, 8.0f, std::min (view.getWidth() / all.getWidth(), view.getHeight() / all.getHeight()));
    const auto off = view.getCentre() - all.getCentre() * z;
    if (animate)
        animateTo (z, off);
    else
        setCamera (z, off);
}

juce::var TableView::getCameraJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("zoom", zoom);
    // where the table's centre of view is, so a different window size shows the same place
    const auto centre = toTable (getLocalBounds().toFloat().getCentre());
    o->setProperty ("x", centre.x);
    o->setProperty ("y", centre.y);
    return juce::var (o);
}

void TableView::setCameraJson (const juce::var& v)
{
    if (! v.isObject())
        return fitAll (false);
    const float z = (float) (double) v.getProperty ("zoom", 0.9);
    const juce::Point<float> centre ((float) (double) v.getProperty ("x", 0.0), (float) (double) v.getProperty ("y", 0.0));
    setCamera (z, getLocalBounds().toFloat().getCentre() - centre * z);
}

void TableView::animateTo (float z, juce::Point<float> off, std::function<void()> then)
{
    anim.active = true;
    anim.start = juce::Time::getMillisecondCounterHiRes() * 0.001;
    anim.zoom0 = zoom;
    anim.zoom1 = juce::jlimit (0.08f, 8.0f, z);
    anim.off0 = offset;
    anim.off1 = off;
    anim.then = std::move (then);
}

void TableView::stepAnimation()
{
    if (! anim.active)
        return;
    const double t = juce::jlimit (0.0, 1.0, (juce::Time::getMillisecondCounterHiRes() * 0.001 - anim.start) / anim.length);
    const float e = (float) (1.0 - std::pow (1.0 - t, 3.0));
    // zoom geometrically, and move so the point being flown to stays on its path
    const float z = anim.zoom0 * std::pow (anim.zoom1 / anim.zoom0, e);
    const auto c0 = (getLocalBounds().toFloat().getCentre() - anim.off0) / anim.zoom0;
    const auto c1 = (getLocalBounds().toFloat().getCentre() - anim.off1) / anim.zoom1;
    const auto c = c0 + (c1 - c0) * e;
    zoom = z;
    offset = getLocalBounds().toFloat().getCentre() - c * z;
    layoutNodes();
    repaint();
    if (t >= 1.0)
    {
        anim.active = false;
        if (auto then = std::move (anim.then))
            then();
    }
}

void TableView::focusOn (const juce::String& uid)
{
    auto* n = findNode (uid);
    if (n == nullptr)
        return;
    if (auto* old = findNode (focused); old != nullptr && old != n)
    {
        old->setLive (false);
        old->refreshThumbnail (old->getThumbnailScale() > 0.0f ? old->getThumbnailScale() : 0.5f);
    }
    focused = uid;
    if (onFocusChanged)
        onFocusChanged();
    const auto* d = session.findDevice (uid);
    const auto r = juce::Rectangle<float> (d->pos.x, d->pos.y - kStripDepthMm, d->type->widthMm, d->type->depthMm + kStripDepthMm);
    const auto view = juce::BorderSize<int> (clearOf.getTop(), 0, 0, 0).subtractedFrom (getLocalBounds()).toFloat().reduced (24.0f, 12.0f);
    const float z = std::min (view.getWidth() / r.getWidth(), view.getHeight() / r.getHeight());
    const auto off = view.getCentre() - r.getCentre() * z;
    n->toFront (false);
    session.bringToFront (uid);
    cablesDragged.toFront (false);
    animateTo (z, off, [this, uid] {
        if (auto* node = findNode (uid); node != nullptr && focused == uid)
            node->setLive (true);
    });
}

void TableView::backToTable()
{
    if (auto* n = findNode (focused))
    {
        n->setLive (false);
        n->refreshThumbnail (0.5f);
    }
    focused = {};
    liveResumeAt = 0;
    if (onFocusChanged)
        onFocusChanged();
    fitAll (true);
    grabKeyboardFocus();
}

juce::Point<float> TableView::jackOnScreen (const juce::String& uid, int jack) const
{
    if (auto* n = findNode (uid))
        return getLocalPoint (n, n->getJackCentre (jack));
    return {};
}

bool TableView::jackAtScreen (juce::Point<float> p, juce::String& uid, int& jack) const
{
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
    {
        const auto local = (*it)->getLocalPoint (this, p);
        if (local.y < kStripDepthMm + 4.0f)
            if (const int j = (*it)->jackAt (local); j >= 0)
            {
                uid = (*it)->uid;
                jack = j;
                return true;
            }
    }
    return false;
}

void TableView::beginCableDrag (const juce::String& fixedUid, int fixedJack, juce::Colour colour)
{
    cableDrag = { true, fixedUid, fixedJack, colour, jackOnScreen (fixedUid, fixedJack) };
    cablesChanged();
}

void TableView::updateCableDrag (juce::Point<float> p)
{
    cableDrag.screenPos = p;
    cablesDragged.repaint();
}

void TableView::endCableDrag (juce::Point<float> p)
{
    juce::String uid;
    int jack = -1;
    if (jackAtScreen (p, uid, jack))
        session.connect (cableDrag.uid, cableDrag.jack, uid, jack, cableDrag.colour);
    cableDrag = {};
    cablesChanged();
}

void TableView::cablesChanged()
{
    cablesUnder.repaint();
    cablesDragged.repaint();
    for (auto& n : nodes)
        n->repaint (0, 0, n->getWidth(), juce::roundToInt (kStripDepthMm) + 4);
}

void TableView::moveNode (DeviceNode& n, juce::Point<float> pos)
{
    session.moveDevice (n.uid, pos);
    layoutNodes();
    repaint();
}

void TableView::removeDevice (const juce::String& uid)
{
    if (focused == uid)
        backToTable();
    session.removeDevice (uid);
    rebuild();
}

void TableView::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    // the view has settled: the panel being played is live again
    if (liveResumeAt != 0 && now >= liveResumeAt && ! anim.active)
    {
        liveResumeAt = 0;
        if (auto* n = findNode (focused))
            n->setLive (true);
    }
    // keep the pictures fresh (lights blinking, displays changing), one device per tick, sharp enough for the zoom
    if (anim.active || nodes.empty())
        return;
    for (size_t tries = 0; tries < nodes.size(); ++tries)
    {
        auto& n = nodes[thumbCursor++ % nodes.size()];
        if (n->isLive())
            continue;
        const auto onScreen = getLocalArea (n.get(), n->getLocalBounds()).toFloat();
        if (! onScreen.intersects (getLocalBounds().toFloat()))
            continue;
        const float wanted = zoom * n->type.widthMm / (float) n->type.panelWidth * displayScale();
        float q = 1.5f;
        for (float step : { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f })
            if (step >= wanted)
            {
                q = step;
                break;
            }
        if (std::abs (q - n->getThumbnailScale()) > 0.01f || now - n->getThumbnailTime() > 300)
        {
            n->refreshThumbnail (q);
            break;
        }
    }
}

void TableView::paint (juce::Graphics& g)
{
    if (recordFrames)
        frameTimes.push_back (juce::Time::getMillisecondCounterHiRes() * 0.001);
    // the table top
    g.setFillType (juce::FillType (wood, juce::AffineTransform::scale (zoom * woodMm / (float) wood.getWidth()).translated (offset)));
    g.fillAll();
    // a soft pool of light in the middle of the room, the edges a little darker
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY() * 0.8f, juce::Colours::black.withAlpha (0.35f),
                                             r.getX(), r.getBottom(), true));
    g.fillRect (r);
    // what stands on it casts a shadow
    for (auto& d : session.getDevices())
    {
        const auto box = juce::Rectangle<float> (d.pos.x, d.pos.y - kStripDepthMm, d.type->widthMm, d.type->depthMm + kStripDepthMm);
        auto sr = juce::Rectangle<float> (toScreen (box.getTopLeft()), toScreen (box.getBottomRight())).translated (5.0f * zoom, 10.0f * zoom);
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (juce::Colours::black.withAlpha (0.07f));
            g.fillRoundedRectangle (sr.expanded ((float) i * 3.5f * zoom), (6.0f + (float) i * 3.5f) * zoom);
        }
    }
}

void TableView::resized()
{
    cablesUnder.setBounds (getLocalBounds());
    cablesDragged.setBounds (getLocalBounds());
    layoutNodes();
}

void TableView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.mods.isPopupMenu())
    {
        const auto at = toTable (e.position);
        juce::PopupMenu add;
        for (auto& t : deviceTypes())
            add.addItem (t.name, ! (t.unique && session.hasDeviceOfType (t.id)), false, [this, id = t.id, at, w = t.widthMm, dep = t.depthMm] {
                if (session.addDevice (id, at - juce::Point<float> (w * 0.5f, dep * 0.5f)).isNotEmpty())
                    rebuild();
            });
        juce::PopupMenu m;
        m.addSubMenu ("Put on the table", add);
        m.addItem ("Fit everything in view", [this] { fitAll(); });
        m.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        return;
    }
    panStart = offset;
}

void TableView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    setCamera (zoom, panStart + e.getOffsetFromDragStart().toFloat());
}

void TableView::mouseUp (const juce::MouseEvent&) {}

void TableView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isFocused())
        backToTable();
    else
        zoomAround (e.position, 1.6f);
}

void TableView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        zoomAround (e.getEventRelativeTo (this).position, std::pow (1.0025f, w.deltaY * 600.0f));
    else
        setCamera (zoom, offset + juce::Point<float> (w.deltaX, w.deltaY) * (w.isReversed ? -420.0f : 420.0f));
}

void TableView::mouseMagnify (const juce::MouseEvent& e, float scale) { zoomAround (e.getEventRelativeTo (this).position, scale); }

bool TableView::isInterestedInDragSource (const SourceDetails& d) { return d.description.toString().startsWith ("device:"); }

void TableView::itemDropped (const SourceDetails& d)
{
    const auto id = d.description.toString().fromFirstOccurrenceOf ("device:", false, false);
    if (const auto* t = findDeviceType (id))
    {
        const auto at = toTable (d.localPosition.toFloat()) - juce::Point<float> (t->widthMm * 0.5f, t->depthMm * 0.5f);
        if (session.addDevice (id, at).isNotEmpty())
            rebuild();
    }
}
} // namespace tabletop
