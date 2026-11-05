#include "Wood.h"
#include <cmath>

namespace tabletop
{
namespace
{
    float hash (int x, int y, juce::uint32 seed)
    {
        juce::uint32 h = (juce::uint32) x * 374761393u + (juce::uint32) y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (float) (h & 0xffffff) / 16777215.0f;
    }

    /** Value noise that repeats every `px` lattice cells across and `py` down (so the tile is seamless). */
    float noise (float x, float y, juce::uint32 seed, int px, int py)
    {
        const int xi = (int) std::floor (x), yi = (int) std::floor (y);
        const float fx = x - (float) xi, fy = y - (float) yi;
        auto w = [] (int v, int p) { return ((v % p) + p) % p; };
        const float a = hash (w (xi, px), w (yi, py), seed), b = hash (w (xi + 1, px), w (yi, py), seed);
        const float c = hash (w (xi, px), w (yi + 1, py), seed), d = hash (w (xi + 1, px), w (yi + 1, py), seed);
        const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
        return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
    }
} // namespace

juce::Image woodTile (int size, float sizeMm, juce::uint32 seed)
{
    juce::Image img (juce::Image::ARGB, size, size, false); // (ARGB: the native layout, so the pixels go in as written)
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    const float pxPerMm = (float) size / sizeMm;
    const int planks = juce::jmax (1, juce::roundToInt (sizeMm / 150.0f)); // ~150 mm boards, running across
    const float plankH = (float) size / (float) planks;
    for (int y = 0; y < size; ++y)
    {
        const int plank = juce::jmin (planks - 1, (int) ((float) y / plankH));
        const float inPlank = (float) y - plank * plankH; // px from the board's top edge
        // each board: its own tone and its own butt joint (boards are shorter than the tile)
        const float tone = 0.88f + 0.22f * hash (plank, 3, seed);
        const int jointX = (int) (hash (plank, 7, seed) * (float) size);
        for (int x = 0; x < size; ++x)
        {
            // growth rings: long stripes along the board, wandering slowly (the lattice periods divide the tile)
            const float u = (float) x / (float) size, v = (float) y / (float) size;
            const float warp = 1.6f * noise (u * 3.0f, v * 24.0f + plank * 5.0f, seed + 1, 3, 24)
                               + 0.5f * noise (u * 12.0f, v * 48.0f, seed + 2, 12, 48);
            const float rings = 0.5f + 0.5f * std::sin ((inPlank / pxPerMm) * 0.9f + warp * 9.0f + plank * 2.1f);
            const float figure = noise (u * 6.0f, v * 30.0f, seed + 3, 6, 30);
            float t = std::pow (rings, 2.2f) * 0.55f + figure * 0.35f;
            // fine fibres and open pores (short dark dashes along the grain)
            t += (hash (x, y / 3, seed + 4) - 0.5f) * 0.10f;
            if (hash (x / 4, y, seed + 5) > 0.992f)
                t -= 0.28f;
            t = juce::jlimit (0.0f, 1.0f, t);
            // oiled oak: honey to a deeper amber in the latewood
            float r = 0.62f - 0.20f * t, g = 0.43f - 0.17f * t, b = 0.25f - 0.11f * t;
            r *= tone;
            g *= tone;
            b *= tone;
            // the seams between boards and the butt joint: a dark line with a slight bevel either side
            const float edge = std::min (inPlank, plankH - inPlank);
            const int dj = std::abs (x - jointX);
            const float seam = std::min (edge, (float) std::min (dj, size - dj));
            if (seam < 1.0f)
            {
                r *= 0.35f;
                g *= 0.35f;
                b *= 0.35f;
            }
            else if (seam < 2.5f)
            {
                const float k = inPlank < plankH * 0.5f ? 1.10f : 0.86f; // light on the top bevel, shade on the bottom
                r *= k;
                g *= k;
                b *= k;
            }
            auto* p = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
            p->setARGB (255, (juce::uint8) juce::jlimit (0, 255, (int) (r * 255.0f)), (juce::uint8) juce::jlimit (0, 255, (int) (g * 255.0f)),
                        (juce::uint8) juce::jlimit (0, 255, (int) (b * 255.0f)));
        }
    }
    return img;
}
} // namespace tabletop
