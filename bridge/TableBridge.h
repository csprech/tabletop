/* Tabletop for the GPUI front end: the studio (its audio graph, the devices and their panels) behind a C interface.
   The front end draws the table itself (the camera, the wood, the jack strips, the cables, the shelf) and asks
   here for each device's panel as a picture, for the session (devices, cables) and to change it. The device you've
   flown in to gets the mouse and the keyboard through here, in its panel's own coordinates.

   Everything runs on the main thread, which is JUCE's message thread. Pixels are straight-alpha BGRA. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TtTable TtTable;

TtTable* tt_create (void);
void tt_destroy (TtTable*);

/* The shelf: every device type, with its size, kind and jacks (JSON). */
const char* tt_types_json (TtTable*);
/* The session: {"revision","devices":[{"uid","type","x","y","panelW","panelH","rear"}],"cables":[{"from","fromJack","to","toJack","colour","midi"}],"camera"} */
const char* tt_session_json (TtTable*);
/* Bumped by every change to devices, positions or cables (from here or from the session itself). */
uint64_t tt_revision (TtTable*);

int tt_shelf_picture_size (TtTable*, const char* type, int* w, int* h);
int tt_shelf_picture (TtTable*, const char* type, uint8_t* bgra, int w, int h);
int tt_wood_tile (TtTable*, int sizePx, float sizeMm, uint8_t* bgra);

/* A device's panel drawn into exactly w x h pixels (its panel size times scale). */
int tt_render_device (TtTable*, const char* uid, float scale, uint8_t* bgra, int w, int h);
/* Changes when the panel would look different (redraw it then): its widgets' state, input sent to it, and a
   steady tick while `live` (the device you're playing). */
uint64_t tt_device_signature (TtTable*, const char* uid, int live);

/* Changing the table. tt_add_device returns the new uid ("" if it can't: an unknown type, a second interface). */
const char* tt_add_device (TtTable*, const char* type, float x, float y);
void tt_remove_device (TtTable*, const char* uid);
void tt_move_device (TtTable*, const char* uid, float x, float y);
void tt_bring_to_front (TtTable*, const char* uid);
int tt_can_connect (TtTable*, const char* a, int aJack, const char* b, int bJack);
int tt_connect (TtTable*, const char* a, int aJack, const char* b, int bJack);
int tt_find_cable (TtTable*, const char* uid, int jack);
void tt_disconnect (TtTable*, int index);

/* Playing a device: the mouse (kind 0 down, 1 drag, 2 up; mods 1 shift, 2 command, 4 option, 8 control) and the
   computer keyboard, in its panel's coordinates. */
void tt_device_mouse (TtTable*, const char* uid, int kind, float x, float y, int mods, int clicks);
void tt_device_wheel (TtTable*, const char* uid, float x, float y, float dx, float dy, int mods);
int tt_device_key (TtTable*, const char* uid, const char* key, const char* chars, int down, int mods);
void tt_device_modifiers (TtTable*, const char* uid, int mods);

/* Tables: the first table, saving and opening (.tabletop, with the front end's camera), the autosave. */
void tt_new_table (TtTable*);
int tt_save (TtTable*, const char* path, const char* cameraJson);
int tt_load (TtTable*, const char* path);
void tt_autosave (TtTable*, const char* cameraJson);
void tt_show_audio_settings (TtTable*);
void tt_show_help (TtTable*);

/* For the self-test: the speakers' and a mixer channel's level, and the groovebox playing (1) or stopped (0). */
float tt_speaker_level (TtTable*);
float tt_mixer_channel_level (TtTable*, int channel);
void tt_test_sound (TtTable*, int start);
int tt_device_count (TtTable*);

#ifdef __cplusplus
}
#endif
