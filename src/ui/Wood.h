#pragma once

// The table top: oiled oak planks, generated once as a seamless tile.

#include <juce_graphics/juce_graphics.h>

namespace tabletop
{
/** A seamless tile of the table top: `sizePx` square, covering `sizeMm` of table. */
juce::Image woodTile (int sizePx, float sizeMm, juce::uint32 seed = 11);
} // namespace tabletop
