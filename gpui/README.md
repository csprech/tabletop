# Tabletop with a GPUI front end (experimental)

This branch (`gpui`) draws the table with [GPUI](https://gpui.rs). `main` keeps the JUCE app as it was, with its own saved table.

How it's put together:
- **The studio is unchanged.** The session, its audio graph, every device and the devices' panels sit behind a C interface (`bridge/TableBridge.h`).
- **GPUI draws the table itself.** That includes the camera (panning and zooming are GPU transforms, not redraws), the wood, each device's jack strip and shadow, the cables (stroked curves with their plugs), the shelf and the top bar.
- **Each device's panel is a picture.** The bridge draws it again only when the panel changes or the zoom needs it sharper.
- **Fly in to a device to play it.** Double-click it: its panel is then live, and the mouse and the computer keyboard go to it, in its panel's own coordinates.

Using it:
- **Add a device:** drag it from the shelf onto the table.
- **Remove a device:** right-click it (its cables go with it).
- **Move a device:** drag it.
- **Cables:** drag from one jack to another to cable them. Drag a cabled jack to pull its cable out.
- **Moving around:** scroll to pan, and hold command while scrolling to zoom.
- **Playing a device:** double-click it to fly in. Escape or "Back to the table" brings you back.

The table is saved when you quit, and every minute, to `Last Table (GPUI).tabletop`. This is separate from the JUCE app's table.

## Building

You need Rust (rustup), CMake, Ninja and the Xcode Command Line Tools. Full Xcode isn't needed: GPUI's shaders compile at runtime.

The devices are submodules on their own `gpui` branches. Those branches add the key hook the table plays them through.

```sh
scripts/bootstrap.sh            # JUCE and the devices
cd gpui
cargo run --release
```

To build against sibling checkouts of the devices, set `TABLETOP_DEVICES_DIR=/path/to/folder-with-them`.

## Tests

- `TABLETOP_GPUI_SELFTEST=/tmp/report.txt` checks the first table and its shelf pictures, and that the groovebox is heard through the mixer and speakers. It pulls out a cable and plugs it back in through the table's mouse handling, flies in to the groovebox, presses a key on its live panel, and flies back.
- `TABLETOP_BENCH=/tmp/bench.txt` (with `TABLETOP_OPEN=table.tabletop`) flies in to every device and back, and reports the frames per second. It's the same measurement as the JUCE app's benchmark on its `experiment/opengl-bench` branch.

Measured on a Mac mini (1920 x 1080), on a table with all seven devices:

| | overall | worst pause |
|---|---|---|
| JUCE (CoreGraphics) | 19.0 fps | 94 ms |
| JUCE with OpenGL | 19.2 fps | 97 ms |
| GPUI | 49.5 fps | 93 ms (the zoomed-in panel's first full drawing) |

Keep the devices' files out of the way while testing: `KEYTAPE_DIR=/tmp/keytape TRACKDESK_CARD_DIR=/tmp/card`.
