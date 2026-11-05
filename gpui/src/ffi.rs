//! The C bridge (bridge/TableBridge.h), wrapped. Everything runs on the main thread.

use std::ffi::{CStr, CString, c_char, c_float, c_int};

#[repr(C)]
pub struct TtTable {
    _private: [u8; 0],
}

unsafe extern "C" {
    fn tt_create() -> *mut TtTable;
    fn tt_destroy(t: *mut TtTable);
    fn tt_types_json(t: *mut TtTable) -> *const c_char;
    fn tt_session_json(t: *mut TtTable) -> *const c_char;
    fn tt_revision(t: *mut TtTable) -> u64;
    fn tt_shelf_picture_size(t: *mut TtTable, ty: *const c_char, w: *mut c_int, h: *mut c_int) -> c_int;
    fn tt_shelf_picture(t: *mut TtTable, ty: *const c_char, bgra: *mut u8, w: c_int, h: c_int) -> c_int;
    fn tt_wood_tile(t: *mut TtTable, size: c_int, mm: c_float, bgra: *mut u8) -> c_int;
    fn tt_render_device(t: *mut TtTable, uid: *const c_char, scale: c_float, bgra: *mut u8, w: c_int, h: c_int) -> c_int;
    fn tt_device_signature(t: *mut TtTable, uid: *const c_char, live: c_int) -> u64;
    fn tt_add_device(t: *mut TtTable, ty: *const c_char, x: c_float, y: c_float) -> *const c_char;
    fn tt_remove_device(t: *mut TtTable, uid: *const c_char);
    fn tt_move_device(t: *mut TtTable, uid: *const c_char, x: c_float, y: c_float);
    fn tt_bring_to_front(t: *mut TtTable, uid: *const c_char);
    fn tt_can_connect(t: *mut TtTable, a: *const c_char, aj: c_int, b: *const c_char, bj: c_int) -> c_int;
    fn tt_connect(t: *mut TtTable, a: *const c_char, aj: c_int, b: *const c_char, bj: c_int) -> c_int;
    fn tt_find_cable(t: *mut TtTable, uid: *const c_char, jack: c_int) -> c_int;
    fn tt_disconnect(t: *mut TtTable, index: c_int);
    fn tt_device_mouse(t: *mut TtTable, uid: *const c_char, kind: c_int, x: c_float, y: c_float, mods: c_int, clicks: c_int);
    fn tt_device_wheel(t: *mut TtTable, uid: *const c_char, x: c_float, y: c_float, dx: c_float, dy: c_float, mods: c_int);
    fn tt_device_key(t: *mut TtTable, uid: *const c_char, key: *const c_char, chars: *const c_char, down: c_int, mods: c_int) -> c_int;
    fn tt_device_modifiers(t: *mut TtTable, uid: *const c_char, mods: c_int);
    fn tt_window_origin(t: *mut TtTable, x: c_float, y: c_float);
    fn tt_window_origin_from_pointer(t: *mut TtTable, x: c_float, y: c_float);
    fn tt_place_device(t: *mut TtTable, uid: *const c_char, x: c_float, y: c_float, scale: c_float);
    fn tt_set_focused(t: *mut TtTable, uid: *const c_char);
    fn tt_device_tooltip(t: *mut TtTable, uid: *const c_char, x: c_float, y: c_float) -> *const c_char;
    fn tt_new_table(t: *mut TtTable);
    fn tt_autosave(t: *mut TtTable, camera: *const c_char);
    fn tt_show_audio_settings(t: *mut TtTable);
    fn tt_show_help(t: *mut TtTable);
    fn tt_speaker_level(t: *mut TtTable) -> c_float;
    fn tt_mixer_channel_level(t: *mut TtTable, ch: c_int) -> c_float;
    fn tt_test_sound(t: *mut TtTable, start: c_int);
    fn tt_device_count(t: *mut TtTable) -> c_int;
    fn tt_menu_check(t: *mut TtTable, uid: *const c_char) -> c_int;
    fn tt_close_menus(t: *mut TtTable);
    fn tt_probe(t: *mut TtTable, path: *const c_char);
    fn tt_probe_done(t: *mut TtTable) -> c_int;
}

fn cs(s: &str) -> CString {
    CString::new(s).unwrap_or_default()
}

pub struct Table(*mut TtTable);

impl Table {
    pub fn new() -> Self {
        Self(unsafe { tt_create() })
    }
    fn string(p: *const c_char) -> String {
        unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
    }
    pub fn types_json(&self) -> String {
        Self::string(unsafe { tt_types_json(self.0) })
    }
    pub fn session_json(&self) -> String {
        Self::string(unsafe { tt_session_json(self.0) })
    }
    pub fn revision(&self) -> u64 {
        unsafe { tt_revision(self.0) }
    }
    pub fn shelf_picture(&self, ty: &str) -> Option<(usize, usize, Vec<u8>)> {
        let t = cs(ty);
        let (mut w, mut h) = (0, 0);
        if unsafe { tt_shelf_picture_size(self.0, t.as_ptr(), &mut w, &mut h) } != 0 || w <= 0 || h <= 0 {
            return None;
        }
        let mut px = vec![0u8; (w * h * 4) as usize];
        unsafe { tt_shelf_picture(self.0, t.as_ptr(), px.as_mut_ptr(), w, h) };
        Some((w as usize, h as usize, px))
    }
    pub fn wood_tile(&self, size: usize, mm: f32) -> Vec<u8> {
        let mut px = vec![0u8; size * size * 4];
        unsafe { tt_wood_tile(self.0, size as c_int, mm, px.as_mut_ptr()) };
        px
    }
    pub fn render_device(&self, uid: &str, scale: f32, w: usize, h: usize) -> Vec<u8> {
        let u = cs(uid);
        let mut px = vec![0u8; w * h * 4];
        unsafe { tt_render_device(self.0, u.as_ptr(), scale, px.as_mut_ptr(), w as c_int, h as c_int) };
        px
    }
    pub fn device_signature(&self, uid: &str, live: bool) -> u64 {
        let u = cs(uid);
        unsafe { tt_device_signature(self.0, u.as_ptr(), live as c_int) }
    }
    pub fn add_device(&self, ty: &str, x: f32, y: f32) -> String {
        let t = cs(ty);
        Self::string(unsafe { tt_add_device(self.0, t.as_ptr(), x, y) })
    }
    pub fn remove_device(&self, uid: &str) {
        let u = cs(uid);
        unsafe { tt_remove_device(self.0, u.as_ptr()) }
    }
    pub fn move_device(&self, uid: &str, x: f32, y: f32) {
        let u = cs(uid);
        unsafe { tt_move_device(self.0, u.as_ptr(), x, y) }
    }
    pub fn bring_to_front(&self, uid: &str) {
        let u = cs(uid);
        unsafe { tt_bring_to_front(self.0, u.as_ptr()) }
    }
    pub fn can_connect(&self, a: &str, aj: usize, b: &str, bj: usize) -> bool {
        let (a, b) = (cs(a), cs(b));
        unsafe { tt_can_connect(self.0, a.as_ptr(), aj as c_int, b.as_ptr(), bj as c_int) != 0 }
    }
    pub fn connect(&self, a: &str, aj: usize, b: &str, bj: usize) -> bool {
        let (a, b) = (cs(a), cs(b));
        unsafe { tt_connect(self.0, a.as_ptr(), aj as c_int, b.as_ptr(), bj as c_int) != 0 }
    }
    pub fn find_cable(&self, uid: &str, jack: usize) -> Option<usize> {
        let u = cs(uid);
        let i = unsafe { tt_find_cable(self.0, u.as_ptr(), jack as c_int) };
        if i >= 0 { Some(i as usize) } else { None }
    }
    pub fn disconnect(&self, index: usize) {
        unsafe { tt_disconnect(self.0, index as c_int) }
    }
    pub fn device_mouse(&self, uid: &str, kind: i32, x: f32, y: f32, mods: i32, clicks: i32) {
        let u = cs(uid);
        unsafe { tt_device_mouse(self.0, u.as_ptr(), kind, x, y, mods, clicks) }
    }
    pub fn device_wheel(&self, uid: &str, x: f32, y: f32, dx: f32, dy: f32, mods: i32) {
        let u = cs(uid);
        unsafe { tt_device_wheel(self.0, u.as_ptr(), x, y, dx, dy, mods) }
    }
    pub fn device_key(&self, uid: &str, key: &str, chars: &str, down: bool, mods: i32) -> bool {
        let (u, k, c) = (cs(uid), cs(key), cs(chars));
        unsafe { tt_device_key(self.0, u.as_ptr(), k.as_ptr(), c.as_ptr(), down as c_int, mods) != 0 }
    }
    pub fn device_modifiers(&self, uid: &str, mods: i32) {
        let u = cs(uid);
        unsafe { tt_device_modifiers(self.0, u.as_ptr(), mods) }
    }
    pub fn window_origin(&self, x: f32, y: f32) {
        unsafe { tt_window_origin(self.0, x, y) }
    }
    pub fn window_origin_from_pointer(&self, x: f32, y: f32) {
        unsafe { tt_window_origin_from_pointer(self.0, x, y) }
    }
    pub fn place_device(&self, uid: &str, x: f32, y: f32, scale: f32) {
        let u = cs(uid);
        unsafe { tt_place_device(self.0, u.as_ptr(), x, y, scale) }
    }
    pub fn set_focused(&self, uid: &str) {
        let u = cs(uid);
        unsafe { tt_set_focused(self.0, u.as_ptr()) }
    }
    pub fn device_tooltip(&self, uid: &str, x: f32, y: f32) -> String {
        let u = cs(uid);
        Self::string(unsafe { tt_device_tooltip(self.0, u.as_ptr(), x, y) })
    }
    pub fn new_table(&self) {
        unsafe { tt_new_table(self.0) }
    }
    pub fn autosave(&self, camera: &str) {
        let c = cs(camera);
        unsafe { tt_autosave(self.0, c.as_ptr()) }
    }
    pub fn show_audio_settings(&self) {
        unsafe { tt_show_audio_settings(self.0) }
    }
    pub fn show_help(&self) {
        unsafe { tt_show_help(self.0) }
    }
    pub fn speaker_level(&self) -> f32 {
        unsafe { tt_speaker_level(self.0) }
    }
    pub fn mixer_channel_level(&self, ch: i32) -> f32 {
        unsafe { tt_mixer_channel_level(self.0, ch) }
    }
    pub fn test_sound(&self, start: bool) {
        unsafe { tt_test_sound(self.0, start as c_int) }
    }
    pub fn device_count(&self) -> usize {
        unsafe { tt_device_count(self.0) as usize }
    }
    pub fn menu_check(&self, uid: &str) -> i32 {
        let u = cs(uid);
        unsafe { tt_menu_check(self.0, u.as_ptr()) }
    }
    pub fn close_menus(&self) {
        unsafe { tt_close_menus(self.0) }
    }
    pub fn probe(&self, path: &str) {
        let p = cs(path);
        unsafe { tt_probe(self.0, p.as_ptr()) }
    }
    pub fn probe_done(&self) -> bool {
        unsafe { tt_probe_done(self.0) != 0 }
    }
}

impl Drop for Table {
    fn drop(&mut self) {
        unsafe { tt_destroy(self.0) }
    }
}
