#include "ShelfPictures.h"
#include <map>

#if TABLETOP_SHELF_ASSETS
 #include "TabletopAssets.h"
#endif

namespace tabletop
{
juce::Image renderDevicePicture (const DeviceType& type, int maxW, int maxH)
{
    if (! type.create)
        return {};
    auto processor = type.create();
    processor->setRateAndBufferSizeDetails (48000.0, 512);
    processor->prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditorAndMakeActive());
    if (editor == nullptr)
        return {};
    editor->setSize (type.panelWidth, type.panelHeight);
    // the device itself, without whatever its panel draws round it
    const auto crop = type.panelCrop.isEmpty() ? editor->getLocalBounds() : type.panelCrop;
    const float scale = std::min ((float) maxW / (float) crop.getWidth(), (float) maxH / (float) crop.getHeight());
    const auto snap = editor->createComponentSnapshot (crop, true, scale);
    processor->editorBeingDeleted (editor.get()); // (as a host does before deleting an editor)
    editor.reset();
    processor->releaseResources();

    // the object: rounded corners, an edge catching the light, a soft shadow falling down-right
    const int margin = 18;
    juce::Image out (juce::Image::ARGB, snap.getWidth() + margin * 2, snap.getHeight() + margin * 2, true);
    juce::Graphics g (out);
    const auto body = juce::Rectangle<float> ((float) margin, (float) margin, (float) snap.getWidth(), (float) snap.getHeight());
    const float corner = type.pictureCorner * body.getWidth();
    juce::Path outline;
    outline.addRoundedRectangle (body, corner);
    juce::DropShadow (juce::Colours::black.withAlpha (0.55f), 14, { 4, 8 }).drawForPath (g, outline);
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 3, { 1, 2 }).drawForPath (g, outline);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (outline);
        g.setOpacity (1.0f); // (images take the current colour's opacity, and the shadows left theirs)
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (snap, body, juce::RectanglePlacement::stretchToFit);
    }
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.drawRoundedRectangle (body.reduced (0.5f), corner, 1.0f);
    return out;
}

juce::StringArray renderShelfPictures (const juce::File& folder)
{
    juce::StringArray written;
    folder.createDirectory();
    for (auto& t : deviceTypes())
    {
        const auto img = renderDevicePicture (t, 360, 220);
        if (! img.isValid())
            continue;
        const auto f = folder.getChildFile (t.id + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        if (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os))
            written.add (f.getFullPathName());
    }
    return written;
}

juce::Image shelfPicture (const juce::String& typeId)
{
    static std::map<juce::String, juce::Image> cache;
    if (auto it = cache.find (typeId); it != cache.end())
        return it->second;
    juce::Image img;
#if TABLETOP_SHELF_ASSETS
    int size = 0;
    if (const auto* data = TabletopAssets::getNamedResource ((typeId + "_png").toRawUTF8(), size))
        img = juce::ImageFileFormat::loadFrom (data, (size_t) size);
#endif
    cache[typeId] = img;
    return img;
}
} // namespace tabletop
