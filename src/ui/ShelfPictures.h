#pragma once

// The shelf's pictures of the devices: each device's own panel, seen from above as a small object with rounded
// corners and a soft shadow, on a transparent background. They're rendered from the devices' real panels and
// kept as PNG files in assets/shelf (built into the app), so the shelf needn't make a device to show one.
// TABLETOP_RENDER_SHELF=<folder> renders them again (after a device's look changes) and quits.

#include "../studio/Registry.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace tabletop
{
/** Renders a device's picture (it makes the device and its panel to do so): at most maxW x maxH for the body,
    plus room round it for the shadow. */
juce::Image renderDevicePicture (const DeviceType& type, int maxW, int maxH);

/** Renders every device's picture into a folder as <id>.png. Returns the files written. */
juce::StringArray renderShelfPictures (const juce::File& folder);

/** The picture built into the app for a device type (an invalid image if there isn't one). */
juce::Image shelfPicture (const juce::String& typeId);
} // namespace tabletop
