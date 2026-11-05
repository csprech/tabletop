# Tabletop with a GPUI front end (experimental)

This branch (`gpui`) draws the table with [GPUI](https://gpui.rs). `main` keeps the JUCE app as it was, with its own saved table.

How it's put together:
- **The studio is unchanged.** The session, its audio graph, every device and the devices' panels sit behind a C interface (`bridge/TableBridge.h`).
- **GPUI draws the table itself.** That includes the camera (panning and zooming are GPU transforms, not redraws), the wood, each device's jack strip and shadow, the cables (stroked curves with their plugs), the shelf and the top bar.
- **Each device's panel is a picture.** The bridge draws it again only when the panel changes or the zoom needs it sharper.
- **Fly in to a device to play it.** Double-click it: its panel is then live, and the mouse and the computer keyboard go to it, in its panel's own coordinates.
- **Each panel has a stand-in window.** The bridge gives every panel a JUCE peer that never draws, and keeps the played one where the panel is on screen. JUCE then delivers the mouse the normal way, as if the panel were in a window of its own, with hover and double-clicks. That includes fields whose text hands clicks on to them, such as the drum machine's screen menus. Menus and pop-ups open over the panel, at its size.
- **Pictures GPUI no longer shows are let go of.** GPUI keeps every picture it's given in its GPU atlas until it's told to drop it, so each replaced panel picture is dropped two frames later.

Using it:
- **Add a device:** drag it from the shelf onto the table.
- **Remove a device:** right-click it (its cables go with it).
- **Move a device:** drag it.
- **Cables:** drag from one jack to another to cable them. Drag a cabled jack to pull its cable out.
- **Moving around:** scroll to pan, and hold command while scrolling to zoom.
- **Playing a device:** double-click it to fly in. Escape or "Back to the table" brings you back.
- **Tooltips:** rest the mouse on a jack, a device, a shelf card, a top-bar button or a part of the panel you're playing. Panel parts show their own tooltip, their name or caption, or a slider's or menu's value.
- **While you drag a cable,** the jacks it could go into light up green.

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

- `TABLETOP_GPUI_SELFTEST=/tmp/report.txt` checks the first table and its shelf pictures, and that the groovebox is heard through the mixer and speakers. It pulls out a cable and plugs it back in through the table's mouse handling, flies in to the groovebox, presses a key on its live panel, and flies back. It then:
  - flies in to the drum machine and opens a screen field's menu, checking that the menu is over the panel;
  - reads the floppy drive's tooltip;
  - checks that redrawn pictures are dropped;
  - lights the jacks for a pulled cable and reads a jack's tooltip.
- `TABLETOP_PROBE=/tmp/probe.txt` clicks and drags every part of every device's panel, and lists the parts that change nothing: no state, no pixels, no menu or window. `TABLETOP_PROBE_ONLY=padlab` limits it to one kind of device. It works every control, so it records, saves and loads too. Run it with the devices' folders set aside (below), and expect a few parts that do nothing by design, such as captions and the groovebox's synth knobs on a drum part.
- `TABLETOP_BENCH=/tmp/bench.txt` (with `TABLETOP_OPEN=table.tabletop`) flies in to every device and back, and reports the frames per second. It's the same measurement as the JUCE app's benchmark on its `experiment/opengl-bench` branch.

Measured on a Mac mini (1920 x 1080), on a table with all seven devices:

| | overall | worst pause |
|---|---|---|
| JUCE (CoreGraphics) | 19.0 fps | 94 ms |
| JUCE with OpenGL | 19.2 fps | 97 ms |
| GPUI | 49.5 fps | 93 ms (the zoomed-in panel's first full drawing) |

Keep the devices' files out of the way while testing: `KEYTAPE_DIR=/tmp/keytape TRACKDESK_CARD_DIR=/tmp/card`.
