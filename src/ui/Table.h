#pragma once

// The table: an endless wooden top you pan and zoom (in millimetres, so every device is life size). Devices sit
// on it with their jacks along the back; cables run between the jacks. Zoomed out, each device shows a picture
// of its panel; double-click one and the view flies in so you can play its real panel.

#include "../studio/Session.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace tabletop
{
class TableView;

/** One device on the table: the back strip with its jacks, and the front panel (the device's own editor, or a
    picture of it). Laid out in millimetres; the table places it with a transform. */
class DeviceNode : public juce::Component
{
public:
    DeviceNode (TableView& table, const juce::String& uid, const DeviceType& type, juce::AudioProcessor& processor);
    ~DeviceNode() override;

    const juce::String uid;
    const DeviceType& type;

    /** Live: the real panel, playable. Otherwise a picture of it (cheap to draw at any zoom). */
    void setLive (bool live);
    bool isLive() const { return live; }
    void refreshThumbnail (float scale);
    float getThumbnailScale() const { return thumbScale; }
    juce::uint32 getThumbnailTime() const { return thumbTime; }
    juce::AudioProcessorEditor* getEditor() const { return editor.get(); }

    juce::Rectangle<float> getBodyArea() const;
    juce::Point<float> getJackCentre (int jack) const; // in this node (mm)
    int jackAt (juce::Point<float> local) const;
    float jackRadius (int jack) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    struct Body : juce::Component
    {
        explicit Body (DeviceNode& n) : owner (n) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override { owner.mouseDown (e.getEventRelativeTo (&owner)); }
        void mouseDrag (const juce::MouseEvent& e) override { owner.mouseDrag (e.getEventRelativeTo (&owner)); }
        void mouseUp (const juce::MouseEvent& e) override { owner.mouseUp (e.getEventRelativeTo (&owner)); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { owner.mouseDoubleClick (e.getEventRelativeTo (&owner)); }
        DeviceNode& owner;
    };
    void showMenu();

    TableView& table;
    Body body;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    juce::Image thumb;
    float thumbScale = 0.0f;
    juce::uint32 thumbTime = 0;
    bool live = false;
    enum class Drag { None, Move, Cable } drag = Drag::None;
    juce::Point<float> grabOffset; // mm, from the node's position to the mouse
};

/** Cables, drawn from jack to jack in screen space. */
class CableLayer : public juce::Component
{
public:
    explicit CableLayer (TableView& t, bool forDragging) : table (t), dragging (forDragging) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics&) override;

private:
    TableView& table;
    const bool dragging;
};

class TableView : public juce::Component, public juce::DragAndDropTarget, private juce::Timer
{
public:
    explicit TableView (Session& session);
    ~TableView() override;

    Session& getSession() { return session; }

    /** Brings the devices on screen up to date with the session (after loading, adding, removing). */
    void rebuild();
    DeviceNode* findNode (const juce::String& uid) const;
    /** The benchmark: when recording, the time of every frame the table paints (seconds). */
    bool recordFrames = false;
    std::vector<double> frameTimes;
    bool isAnimating() const { return anim.active; }
    const std::vector<std::unique_ptr<DeviceNode>>& getNodes() const { return nodes; }

    // ---- the camera: screen = table (mm) * zoom + offset
    float getZoom() const { return zoom; }
    juce::Point<float> toScreen (juce::Point<float> mm) const { return mm * zoom + offset; }
    juce::Point<float> toTable (juce::Point<float> px) const { return (px - offset) / zoom; }
    void setCamera (float zoom, juce::Point<float> offset);
    void zoomAround (juce::Point<float> screenPoint, float factor);
    void fitAll (bool animate = true);
    /** The part of the view not covered by the shelf and the top bar (where fitting puts things). */
    juce::BorderSize<int> clearOf;
    juce::var getCameraJson() const;
    void setCameraJson (const juce::var&);

    // ---- playing a device up close
    void focusOn (const juce::String& uid);
    void backToTable();
    bool isFocused() const { return focused.isNotEmpty(); }
    std::function<void()> onFocusChanged;

    // ---- cables (dragged from a jack; the other end follows the mouse)
    void beginCableDrag (const juce::String& fixedUid, int fixedJack, juce::Colour colour);
    void updateCableDrag (juce::Point<float> screenPos);
    void endCableDrag (juce::Point<float> screenPos);
    bool isDraggingCable() const { return cableDrag.active; }
    struct CableDrag
    {
        bool active = false;
        juce::String uid;
        int jack = 0;
        juce::Colour colour;
        juce::Point<float> screenPos;
    };
    const CableDrag& getCableDrag() const { return cableDrag; }
    /** Where a jack is on screen. */
    juce::Point<float> jackOnScreen (const juce::String& uid, int jack) const;
    /** A jack under a screen point (and which device), for dropping a cable. */
    bool jackAtScreen (juce::Point<float> screenPos, juce::String& uid, int& jack) const;

    void moveNode (DeviceNode&, juce::Point<float> newPosMm);
    void removeDevice (const juce::String& uid);
    void cablesChanged();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    void timerCallback() override;
    void layoutNodes();
    void animateTo (float zoom, juce::Point<float> offset, std::function<void()> then = {});
    void stepAnimation();
    void cameraMoved();
    float displayScale() const;

    Session& session;
    std::vector<std::unique_ptr<DeviceNode>> nodes;
    CableLayer cablesUnder { *this, false }, cablesDragged { *this, true };
    juce::Image wood;
    float woodMm = 1000.0f;
    float zoom = 0.9f;
    juce::Point<float> offset { 600.0f, 400.0f };
    juce::String focused;
    CableDrag cableDrag;
    juce::Point<float> panStart;

    struct Animation
    {
        bool active = false;
        double start = 0.0, length = 0.42;
        float zoom0 = 1, zoom1 = 1;
        juce::Point<float> off0, off1;
        std::function<void()> then;
    } anim;
    juce::VBlankAttachment vblank;
    juce::uint32 liveResumeAt = 0; // a live panel goes back to a picture while the view moves
    size_t thumbCursor = 0;
};
} // namespace tabletop
