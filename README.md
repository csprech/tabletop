# Tabletop

A studio on a table. The research instruments sit on an endless wooden table at life size, and you cable them together the way you would in a real studio:
- [Fourpole](https://github.com/csprech/fourpole), a three-oscillator monosynth;
- [Valvebox](https://github.com/csprech/valvebox), a groovebox;
- [Padlab](https://github.com/csprech/padlab), a sampling drum machine;
- an audio interface and a mixer.

Zoom out to see the whole setup. Double-click a device and the view flies in, so you can play its real panel.

C++20 / JUCE 8, macOS (Apple Silicon tested). Each device lives in its own repository and is built into Tabletop from there. More devices will follow the same pattern.

![The table: Valvebox, Padlab and Fourpole cabled into the mixer and the audio interface](docs/tabletop.jpg)

![Zoomed in on the Valvebox to play it, its jacks along the back](docs/tabletop-playing.jpg)

## Build & run

### Requirements (macOS)

- **Xcode Command Line Tools**: `xcode-select --install`
- **CMake 3.22+ and Ninja**, e.g. `brew install cmake ninja`
- Access to the device repositories on GitHub (they're git submodules).

### Build

```sh
bash scripts/bootstrap.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
open build/Tabletop_artefacts/Release/Tabletop.app
```

- `bootstrap.sh` downloads JUCE 8.0.15 into `external/` and checks out the devices into `devices/`.
- To build against checkouts of the device repositories you're working on, add `-DTABLETOP_DEVICES_DIR=/path/to/folder-with-them`.

## Using it

### The table

- **Move around:** drag the wood. Two-finger scroll pans too.
- **Zoom:** pinch, or Cmd + scroll.
- **Fit** (or Cmd + 0): shows everything.

### Devices

- **Add one:** drag it from the shelf on the left onto the table, or right-click the wood and choose "Put on the table". There's one audio interface; you can add as many of everything else as you like.
- **Move one:** drag it.
- **Remove one:** right-click it, then "Take off the table". Its cables come with it.
- **Play one:** double-click it and the view flies in to its real, live panel. Everything on it works as it does in the device's own app, including its computer-keyboard keys.
- **Step back:** "Back to the table", Cmd + Up, or double-click the wood.

### Cables

- **Connect:** every device has a strip of jacks along its back. Drag from a jack to a jack on another device.
- **Rules:** outputs go to inputs, and MIDI goes to MIDI. While you drag, the jacks the cable can go into light up.
- **Repatch or unplug:** drag a plug out of its jack and drop it on another jack, or on the table to unplug it.
- **Colours:** audio cables come in several colours; MIDI cables are black.

### The built-in gear

- **Audio interface:** your computer, on the table.
  - Cable to its **SPEAKERS** jacks to hear something. With only L cabled, both speakers play it.
  - **IN 1** and **IN 2** are your computer's audio inputs. macOS asks for permission the first time you cable them.
  - Its **MIDI** jack carries every MIDI controller you plug into the computer, now or later.
  - **Audio/MIDI** picks the computer's audio device.
- **Mixer MX-6:** six stereo channels, each with a fader, PAN and MUTE, plus the MAIN fader and meters. A channel with only its L jack cabled is mono.

### Your tables

- **Saved automatically:** the table is saved when you quit (and every minute), and opens again next time. That includes every device's state: Fourpole's patch, Valvebox's whole memory, Padlab's project with its samples, and the mixer's settings.
- **Copies:** **File > Save As** keeps a copy as a `.tabletop` file; **File > Open** brings one back.
- **New Table** sets up the first table again: everything cabled into the mixer and on to the speakers, with MIDI from your controllers going to Fourpole.

## How it works

- **The audio graph:** each device on the table is a node in one JUCE `AudioProcessorGraph`, and each cable is a connection in it, audio or MIDI. The audio interface's jacks are joined to the graph's links to the computer's audio device and MIDI inputs.
- **The devices:** each repository publishes a `<device>-module.cmake` listing its sources, and a processor wrapper in `src/plugin/`. The wrapper uses the same engine and panel as the device's own app, in a hosted mode that opens no audio device and takes its audio and MIDI from the graph. Tabletop compiles those sources into its own target.
- **Real size:** the table works in millimetres. Each device's panel is scaled to the device's real width, so they sit at their true sizes relative to each other.
- **Panel pictures:** zoomed out, the devices show pictures of their panels, refreshed a few times a second at a resolution that suits the zoom. That keeps panning and zooming smooth. The device you zoom in on gets its live panel.

| Folder | What's in it |
|---|---|
| `src/studio` | The device registry (sizes, jacks, how to make each), the session (graph, cables, saving), the audio interface and the mixer |
| `src/ui` | The table (camera, devices, jacks, cables, flying in), the wood, the window (shelf, menus, files) |
| `devices/` | The devices' repositories (git submodules) |

## Tests

The self-test runs the real app:

```sh
TABLETOP_SELFTEST=/tmp/report.txt ./build/Tabletop_artefacts/Release/Tabletop.app/Contents/MacOS/Tabletop
```

It checks:
- the first table's devices and cables;
- the cable rules (outputs to inputs, MIDI to MIDI, one cable per jack);
- mono detection on the mixer;
- each device's state round trip (Fourpole's patch, Valvebox's memory, Padlab's project and samples);
- saving and opening a table;
- sound from the Valvebox through the mixer to the speakers;
- flying in to a live panel and back;
- removing a device along with its cables.

Other testing aids:
- `TABLETOP_NO_AUTOSAVE=1` leaves your saved table alone.
- `TABLETOP_OPEN=/path.tabletop` opens a table at launch.
- `TABLETOP_WINDOW_SIZE=WxH` forces a window size.

Each device's own tests live in its repository.
