//! Tabletop with a GPUI front end.
//!
//! The studio (its audio graph, the devices and their panels) sits behind the C bridge in `bridge/TableBridge.h`.
//! This window draws the table itself on the GPU: the camera (pan and zoom are transforms, not redraws), the wood,
//! each device's jack strip and its panel (a picture from the bridge, drawn again only when it changes or the zoom
//! needs it sharper), the cables, and the shelf. Double-click a device to fly in: its panel is then live, and the
//! mouse and keyboard go to it.

mod ffi;

use gpui::{
    App, Application, Background, Bounds, BoxShadow, Context, FocusHandle, Hsla, KeyDownEvent, KeyUpEvent, Modifiers,
    ModifiersChangedEvent, MouseButton, MouseDownEvent, MouseMoveEvent, MouseUpEvent, ObjectFit, PathBuilder, Pixels, Point,
    RenderImage, Rgba, ScrollWheelEvent, Timer, Window, WindowBounds, WindowOptions, canvas, div, fill, hsla, img, point, prelude::*,
    px, rgb, rgba, size,
};
use serde::Deserialize;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};

const STRIP_MM: f32 = 26.0; // the jack strip behind every device
const JACK_Y_MM: f32 = 15.5; // jack centres, from the back edge
const WOOD_MM: f32 = 1000.0;
const SHELF_W: f32 = 250.0;
const BAR_H: f32 = 44.0;
const CARD_H: f32 = 96.0;

#[derive(Deserialize, Clone)]
struct Jack {
    label: String,
    midi: bool,
}

#[derive(Deserialize, Clone)]
struct DeviceType {
    id: String,
    name: String,
    kind: String,
    #[serde(rename = "widthMm")]
    width_mm: f32,
    #[serde(rename = "depthMm")]
    depth_mm: f32,
    #[serde(rename = "panelW")]
    panel_w: f32,
    #[serde(rename = "panelH")]
    panel_h: f32,
    rear: String,
    jacks: Vec<Jack>,
}

#[derive(Deserialize, Clone)]
struct Device {
    uid: String,
    #[serde(rename = "type")]
    type_id: String,
    x: f32,
    y: f32,
}

#[derive(Deserialize, Clone)]
struct Cable {
    from: String,
    #[serde(rename = "fromJack")]
    from_jack: usize,
    to: String,
    #[serde(rename = "toJack")]
    to_jack: usize,
    colour: String,
    midi: bool,
}

#[derive(Deserialize, Default)]
struct Session {
    devices: Vec<Device>,
    cables: Vec<Cable>,
    #[serde(default)]
    camera: serde_json::Value,
}

struct Sprite {
    signature: u64,
    scale: f32,
    image: Arc<RenderImage>,
}

#[derive(Clone, Copy)]
struct Camera {
    zoom: f32,
    off: (f32, f32), // screen = mm * zoom + off
}

struct Flight {
    start: Instant,
    from: Camera,
    to: Camera,
    then_focus: Option<String>,
}

enum Drag {
    Pan { start: (f32, f32), off: (f32, f32) },
    Move { uid: String, grab: (f32, f32) },
    Cable { uid: String, jack: usize, colour: Hsla, midi: bool, pos: (f32, f32) },
    Shelf { type_id: String, pos: (f32, f32) },
    Play { uid: String },
}

/// "#AARRGGBB" (JUCE's colour strings) to a GPUI colour.
fn colour(s: &str) -> Hsla {
    let hex = u32::from_str_radix(s.trim_start_matches('#'), 16).unwrap_or(0xff808080);
    let (a, rest) = (hex >> 24, hex & 0xffffff);
    Hsla::from(rgba((rest << 8) | a))
}

fn image(w: usize, h: usize, bgra: Vec<u8>) -> Arc<RenderImage> {
    let buffer = image::RgbaImage::from_raw(w as u32, h as u32, bgra).expect("a picture's pixels");
    Arc::new(RenderImage::new(smallvec::smallvec![image::Frame::new(buffer)]))
}

fn mods_of(m: &Modifiers) -> i32 {
    (m.shift as i32) | ((m.platform as i32) << 1) | ((m.alt as i32) << 2) | ((m.control as i32) << 3)
}

fn f(p: Pixels) -> f32 {
    f32::from(p)
}

struct TableView {
    table: ffi::Table,
    types: Vec<DeviceType>,
    session: Session,
    revision: u64,
    camera: Camera,
    flight: Option<Flight>,
    focused: Option<String>,
    wood: Arc<RenderImage>,
    pictures: HashMap<String, Arc<RenderImage>>,
    sprites: HashMap<String, Sprite>,
    refresh_cursor: usize,
    drag: Option<Drag>,
    focus: FocusHandle,
    view: (f32, f32),
    dpr: f32,
    fitted: bool,
    last_save: Instant,
    frames: Option<Vec<Instant>>, // (the benchmark: when each frame was drawn)
}

impl TableView {
    fn new(cx: &mut Context<Self>) -> Self {
        let table = ffi::Table::new();
        let types: Vec<DeviceType> = serde_json::from_str(&table.types_json()).expect("the device types");
        let mut pictures = HashMap::new();
        for t in &types {
            if let Some((w, h, px)) = table.shelf_picture(&t.id) {
                pictures.insert(t.id.clone(), image(w, h, px));
            }
        }
        let wood = image(1024, 1024, table.wood_tile(1024, WOOD_MM));
        let mut this = Self {
            table,
            types,
            session: Session::default(),
            revision: 0,
            camera: Camera { zoom: 0.9, off: (600.0, 400.0) },
            flight: None,
            focused: None,
            wood,
            pictures,
            sprites: HashMap::new(),
            refresh_cursor: 0,
            drag: None,
            focus: cx.focus_handle(),
            view: (1200.0, 800.0),
            dpr: 2.0,
            fitted: false,
            last_save: Instant::now(),
            frames: None,
        };
        this.reload();
        if let Some(c) = this.session.camera.as_object() {
            // where the table was looked at last time (its centre, so another window size shows the same place)
            if let (Some(z), Some(x), Some(y)) = (c.get("zoom").and_then(|v| v.as_f64()), c.get("x").and_then(|v| v.as_f64()), c.get("y").and_then(|v| v.as_f64())) {
                this.camera.zoom = z as f32;
                this.camera.off = (x as f32, y as f32); // (made into an offset once the window size is known)
                this.fitted = true;
                this.session.camera = serde_json::json!({ "pending": true, "zoom": z, "x": x, "y": y });
            }
        }
        // 60 times a second: the flight, the live panel, autosave
        cx.spawn(async move |this, cx| {
            loop {
                Timer::after(Duration::from_millis(16)).await;
                if this.update(cx, |this, cx| this.tick(cx)).is_err() {
                    break;
                }
            }
        })
        .detach();
        cx.on_app_quit(|this, _| {
            this.table.autosave(&this.camera_json());
            async {}
        })
        .detach();
        if let Ok(report) = std::env::var("TABLETOP_GPUI_SELFTEST") {
            Self::self_test(report, cx);
        }
        if let Ok(report) = std::env::var("TABLETOP_BENCH") {
            Self::benchmark(report, cx);
        }
        this
    }

    fn reload(&mut self) {
        if let Ok(s) = serde_json::from_str::<Session>(&self.table.session_json()) {
            let camera = std::mem::take(&mut self.session.camera);
            self.session = s;
            if camera.get("pending").is_some() {
                self.session.camera = camera;
            }
        }
        self.revision = self.table.revision();
        let alive: Vec<String> = self.session.devices.iter().map(|d| d.uid.clone()).collect();
        self.sprites.retain(|uid, _| alive.contains(uid));
    }

    fn type_of(&self, id: &str) -> Option<&DeviceType> {
        self.types.iter().find(|t| t.id == id)
    }

    fn device(&self, uid: &str) -> Option<&Device> {
        self.session.devices.iter().find(|d| d.uid == uid)
    }

    fn camera_json(&self) -> String {
        let (cx, cy) = self.to_table((self.view.0 * 0.5, self.view.1 * 0.5));
        serde_json::json!({ "zoom": self.camera.zoom, "x": cx, "y": cy }).to_string()
    }

    // ---------------------------------------------------------------- the camera

    fn to_screen(&self, mm: (f32, f32)) -> (f32, f32) {
        (mm.0 * self.camera.zoom + self.camera.off.0, mm.1 * self.camera.zoom + self.camera.off.1)
    }

    fn to_table(&self, px: (f32, f32)) -> (f32, f32) {
        ((px.0 - self.camera.off.0) / self.camera.zoom, (px.1 - self.camera.off.1) / self.camera.zoom)
    }

    /// A device's whole footprint (strip and body), in mm.
    fn footprint(&self, d: &Device) -> Option<(f32, f32, f32, f32)> {
        let t = self.type_of(&d.type_id)?;
        Some((d.x, d.y - STRIP_MM, t.width_mm, t.depth_mm + STRIP_MM))
    }

    fn jack_centre(t: &DeviceType, d: &Device, j: usize) -> (f32, f32) {
        let n = t.jacks.len().max(1) as f32;
        let spacing = 30.0_f32.min((t.width_mm - 30.0) / n);
        let x0 = (t.width_mm - spacing * (t.jacks.len() as f32 - 1.0)) * 0.5;
        (d.x + x0 + spacing * j as f32, d.y - STRIP_MM + JACK_Y_MM)
    }

    fn jack_radius(j: &Jack) -> f32 {
        if j.midi { 6.4 } else { 4.8 }
    }

    /// The camera that shows `r` (mm) in the free part of the window.
    fn camera_for(&self, r: (f32, f32, f32, f32), focused: bool) -> Camera {
        let left = if focused { 24.0 } else { SHELF_W + 32.0 };
        let (vx, vy, vw, vh) = (left, BAR_H + 12.0, self.view.0 - left - 24.0, self.view.1 - BAR_H - 24.0);
        let zoom = (vw / r.2).min(vh / r.3).clamp(0.08, 8.0);
        let off = (vx + vw * 0.5 - (r.0 + r.2 * 0.5) * zoom, vy + vh * 0.5 - (r.1 + r.3 * 0.5) * zoom);
        Camera { zoom, off }
    }

    fn fit_all(&mut self, animate: bool) {
        let mut all: Option<(f32, f32, f32, f32)> = None;
        for d in &self.session.devices {
            if let Some(r) = self.footprint(d) {
                all = Some(match all {
                    None => r,
                    Some(a) => {
                        let (x0, y0) = (a.0.min(r.0), a.1.min(r.1));
                        let (x1, y1) = ((a.0 + a.2).max(r.0 + r.2), (a.1 + a.3).max(r.1 + r.3));
                        (x0, y0, x1 - x0, y1 - y0)
                    }
                });
            }
        }
        let Some(a) = all else { return };
        let to = self.camera_for((a.0 - 60.0, a.1 - 60.0, a.2 + 120.0, a.3 + 120.0), false);
        self.fly(to, None, animate);
    }

    fn fly(&mut self, to: Camera, then_focus: Option<String>, animate: bool) {
        if animate {
            self.flight = Some(Flight { start: Instant::now(), from: self.camera, to, then_focus });
        } else {
            self.camera = to;
            self.focused = then_focus.or(self.focused.take());
        }
    }

    fn focus_on(&mut self, uid: &str) {
        let Some(d) = self.device(uid).cloned() else { return };
        let Some(r) = self.footprint(&d) else { return };
        self.table.bring_to_front(uid);
        self.reload();
        let to = self.camera_for(r, true);
        self.focused = None;
        self.fly(to, Some(uid.to_string()), true);
    }

    fn back_to_table(&mut self) {
        self.focused = None;
        self.fit_all(true);
    }

    fn zoom_around(&mut self, p: (f32, f32), factor: f32) {
        let mm = self.to_table(p);
        let zoom = (self.camera.zoom * factor).clamp(0.08, 8.0);
        self.camera = Camera { zoom, off: (p.0 - mm.0 * zoom, p.1 - mm.1 * zoom) };
    }

    fn tick(&mut self, cx: &mut Context<Self>) {
        if let Some(fl) = &self.flight {
            // zoom geometrically, and keep the point flown to on a straight path (as the JUCE table does)
            let t = (fl.start.elapsed().as_secs_f32() / 0.42).min(1.0);
            let e = 1.0 - (1.0 - t).powi(3);
            let (cx0, cy0) = ((self.view.0 * 0.5 - fl.from.off.0) / fl.from.zoom, (self.view.1 * 0.5 - fl.from.off.1) / fl.from.zoom);
            let (cx1, cy1) = ((self.view.0 * 0.5 - fl.to.off.0) / fl.to.zoom, (self.view.1 * 0.5 - fl.to.off.1) / fl.to.zoom);
            let zoom = fl.from.zoom * (fl.to.zoom / fl.from.zoom).powf(e);
            let (mx, my) = (cx0 + (cx1 - cx0) * e, cy0 + (cy1 - cy0) * e);
            self.camera = Camera { zoom, off: (self.view.0 * 0.5 - mx * zoom, self.view.1 * 0.5 - my * zoom) };
            if t >= 1.0 {
                let fl = self.flight.take().unwrap();
                self.camera = fl.to;
                self.focused = fl.then_focus;
            }
        }
        if self.table.revision() != self.revision {
            self.reload();
        }
        if self.last_save.elapsed() > Duration::from_secs(60) {
            self.last_save = Instant::now();
            self.table.autosave(&self.camera_json());
        }
        cx.notify();
    }

    // ---------------------------------------------------------------- the devices' pictures

    /// The scale to draw a device's panel at: sharp enough for the zoom, in steps (exact for the one being played).
    fn wanted_scale(&self, t: &DeviceType, live: bool) -> f32 {
        let wanted = self.camera.zoom * t.width_mm / t.panel_w * self.dpr;
        if live {
            return ((wanted * 8.0).ceil() / 8.0).clamp(0.125, 2.0);
        }
        [0.25_f32, 0.5, 0.75, 1.0, 1.5].into_iter().find(|s| *s >= wanted).unwrap_or(1.5)
    }

    fn sprite(&mut self, d: &Device, budget: &mut usize) -> Option<Arc<RenderImage>> {
        let t = self.type_of(&d.type_id)?.clone();
        let live = self.focused.as_deref() == Some(d.uid.as_str());
        let scale = self.wanted_scale(&t, live);
        let existing = self.sprites.get(&d.uid);
        // (while flying, keep what's there: redrawing a panel mid-flight would stutter)
        if let Some(s) = existing {
            if self.flight.is_some() {
                return Some(s.image.clone());
            }
        }
        let signature = self.table.device_signature(&d.uid, live);
        if let Some(s) = existing {
            let sharp = (s.scale - scale).abs() < 0.01;
            if sharp && (s.signature == signature || (!live && *budget == 0)) {
                return Some(s.image.clone());
            }
            if !live && *budget == 0 {
                return Some(s.image.clone());
            }
        }
        if !live {
            *budget = budget.saturating_sub(1);
        }
        let (w, h) = ((t.panel_w * scale).ceil() as usize, (t.panel_h * scale).ceil() as usize);
        let img = image(w.max(1), h.max(1), self.table.render_device(&d.uid, scale, w.max(1), h.max(1)));
        self.sprites.insert(d.uid.clone(), Sprite { signature, scale, image: img.clone() });
        Some(img)
    }

    // ---------------------------------------------------------------- hit-testing

    /// The jack under a point (mm): the device on top first.
    fn jack_at(&self, mm: (f32, f32)) -> Option<(String, usize)> {
        for d in self.session.devices.iter().rev() {
            let Some(t) = self.type_of(&d.type_id) else { continue };
            for (j, jack) in t.jacks.iter().enumerate() {
                let c = Self::jack_centre(t, d, j);
                let r = Self::jack_radius(jack) + 3.0;
                if (mm.0 - c.0).powi(2) + (mm.1 - c.1).powi(2) <= r * r {
                    return Some((d.uid.clone(), j));
                }
            }
        }
        None
    }

    fn device_at(&self, mm: (f32, f32)) -> Option<String> {
        self.session
            .devices
            .iter()
            .rev()
            .find(|d| self.footprint(d).is_some_and(|r| mm.0 >= r.0 && mm.0 < r.0 + r.2 && mm.1 >= r.1 && mm.1 < r.1 + r.3))
            .map(|d| d.uid.clone())
    }

    /// A point on the focused device's panel, in its panel's own pixels (None if it's off the panel).
    fn panel_point(&self, uid: &str, px: (f32, f32)) -> Option<(f32, f32)> {
        let d = self.device(uid)?;
        let t = self.type_of(&d.type_id)?;
        let mm = self.to_table(px);
        let (u, v) = ((mm.0 - d.x) / t.width_mm, (mm.1 - d.y) / t.depth_mm);
        if (0.0..1.0).contains(&u) && (0.0..1.0).contains(&v) { Some((u * t.panel_w, v * t.panel_h)) } else { None }
    }

    fn shelf_card_at(&self, px: (f32, f32)) -> Option<String> {
        if self.focused.is_some() || px.0 < 12.0 || px.0 > 12.0 + SHELF_W {
            return None;
        }
        let i = ((px.1 - (BAR_H + 8.0 + 30.0)) / (CARD_H + 8.0)).floor();
        if i < 0.0 {
            return None;
        }
        self.types.get(i as usize).map(|t| t.id.clone())
    }

    fn in_shelf(&self, px: (f32, f32)) -> bool {
        self.focused.is_none() && px.0 >= 12.0 && px.0 <= 12.0 + SHELF_W && px.1 >= BAR_H + 8.0 && px.1 <= BAR_H + 8.0 + 30.0 + self.types.len() as f32 * (CARD_H + 8.0)
    }

    // ---------------------------------------------------------------- the mouse and keys

    fn mouse_down(&mut self, ev: &MouseDownEvent, cx: &mut Context<Self>) {
        let p = (f(ev.position.x), f(ev.position.y));
        if p.1 < BAR_H {
            return;
        }
        if let Some(id) = self.shelf_card_at(p) {
            self.drag = Some(Drag::Shelf { type_id: id, pos: p });
            return;
        }
        if self.in_shelf(p) {
            return;
        }
        if let Some(uid) = self.focused.clone() {
            if let Some(pp) = self.panel_point(&uid, p) {
                self.table.device_mouse(&uid, 0, pp.0, pp.1, mods_of(&ev.modifiers), ev.click_count as i32);
                self.drag = Some(Drag::Play { uid });
                cx.notify();
                return;
            }
        }
        let mm = self.to_table(p);
        if let Some((uid, jack)) = self.jack_at(mm) {
            // a cabled jack: its cable comes out, held by the other end; a free one: a new cable from it
            if let Some(i) = self.table.find_cable(&uid, jack) {
                let c = self.session.cables[i].clone();
                let (other, oj) = if c.from == uid && c.from_jack == jack { (c.to.clone(), c.to_jack) } else { (c.from.clone(), c.from_jack) };
                self.table.disconnect(i);
                self.reload();
                self.drag = Some(Drag::Cable { uid: other, jack: oj, colour: colour(&c.colour), midi: c.midi, pos: p });
            } else {
                let midi = self.device(&uid).and_then(|d| self.type_of(&d.type_id)).is_some_and(|t| t.jacks[jack].midi);
                let palette = [0xd8322cff_u32, 0xf2b31eff, 0x2f7de1ff, 0x3fa34dff, 0xe0e0dcff, 0xe56a1fff, 0x8e44c9ff, 0x26272bff];
                let colour_now = Hsla::from(rgba(palette[self.session.cables.len() % palette.len()]));
                self.drag = Some(Drag::Cable { uid, jack, colour: colour_now, midi, pos: p });
            }
            return;
        }
        if let Some(uid) = self.device_at(mm) {
            if ev.click_count >= 2 {
                self.focus_on(&uid);
                return;
            }
            let d = self.device(&uid).unwrap().clone();
            self.table.bring_to_front(&uid);
            self.reload();
            self.drag = Some(Drag::Move { uid, grab: (mm.0 - d.x, mm.1 - d.y) });
            return;
        }
        if ev.click_count >= 2 {
            if self.focused.is_some() {
                self.back_to_table();
            } else {
                let to_zoom = (self.camera.zoom * 1.6).clamp(0.08, 8.0);
                let mm2 = self.to_table(p);
                let to = Camera { zoom: to_zoom, off: (p.0 - mm2.0 * to_zoom, p.1 - mm2.1 * to_zoom) };
                self.fly(to, None, true);
            }
            return;
        }
        self.drag = Some(Drag::Pan { start: p, off: self.camera.off });
    }

    fn mouse_move(&mut self, ev: &MouseMoveEvent, cx: &mut Context<Self>) {
        let p = (f(ev.position.x), f(ev.position.y));
        let mm = self.to_table(p);
        match &mut self.drag {
            Some(Drag::Pan { start, off }) => {
                self.camera.off = (off.0 + p.0 - start.0, off.1 + p.1 - start.1);
                self.flight = None;
            }
            Some(Drag::Move { uid, grab }) => {
                let (uid, grab) = (uid.clone(), *grab);
                self.table.move_device(&uid, mm.0 - grab.0, mm.1 - grab.1);
                self.reload();
            }
            Some(Drag::Cable { pos, .. }) | Some(Drag::Shelf { pos, .. }) => *pos = p,
            Some(Drag::Play { uid }) => {
                let uid = uid.clone();
                // (outside the panel too: a knob turned past its edge keeps turning)
                let d = self.device(&uid).cloned();
                if let Some(d) = d {
                    if let Some(t) = self.type_of(&d.type_id) {
                        let (u, v) = ((mm.0 - d.x) / t.width_mm, (mm.1 - d.y) / t.depth_mm);
                        self.table.device_mouse(&uid, 1, u * t.panel_w, v * t.panel_h, mods_of(&ev.modifiers), 1);
                    }
                }
            }
            None => return,
        }
        cx.notify();
    }

    fn mouse_up(&mut self, ev: &MouseUpEvent, cx: &mut Context<Self>) {
        let p = (f(ev.position.x), f(ev.position.y));
        let mm = self.to_table(p);
        match self.drag.take() {
            Some(Drag::Cable { uid, jack, .. }) => {
                if let Some((u2, j2)) = self.jack_at(mm) {
                    if self.table.can_connect(&uid, jack, &u2, j2) {
                        self.table.connect(&uid, jack, &u2, j2);
                    }
                }
                self.reload();
            }
            Some(Drag::Shelf { type_id, .. }) => {
                if !self.in_shelf(p) && p.1 > BAR_H {
                    if let Some(t) = self.type_of(&type_id).cloned() {
                        let uid = self.table.add_device(&t.id, mm.0 - t.width_mm * 0.5, mm.1 - t.depth_mm * 0.5);
                        if uid.is_empty() {
                            // (a second audio interface: there's only one computer)
                        }
                        self.reload();
                    }
                }
            }
            Some(Drag::Play { uid }) => {
                if let Some(d) = self.device(&uid).cloned() {
                    if let Some(t) = self.type_of(&d.type_id) {
                        let (u, v) = ((mm.0 - d.x) / t.width_mm, (mm.1 - d.y) / t.depth_mm);
                        self.table.device_mouse(&uid, 2, u * t.panel_w, v * t.panel_h, mods_of(&ev.modifiers), ev.click_count as i32);
                    }
                }
            }
            _ => {}
        }
        cx.notify();
    }

    fn scroll(&mut self, ev: &ScrollWheelEvent, cx: &mut Context<Self>) {
        let p = (f(ev.position.x), f(ev.position.y));
        let d = ev.delta.pixel_delta(px(16.0));
        let (dx, dy) = (f(d.x), f(d.y));
        if let Some(uid) = self.focused.clone() {
            if let Some(pp) = self.panel_point(&uid, p) {
                self.table.device_wheel(&uid, pp.0, pp.1, dx / 160.0, dy / 160.0, mods_of(&ev.modifiers));
                cx.notify();
                return;
            }
        }
        if ev.modifiers.platform || ev.modifiers.control {
            self.zoom_around(p, 1.0025_f32.powf(dy * 1.5));
        } else {
            self.camera.off = (self.camera.off.0 + dx, self.camera.off.1 + dy);
        }
        self.flight = None;
        cx.notify();
    }

    fn key(&mut self, key: &str, chars: &str, down: bool, m: &Modifiers, cx: &mut Context<Self>) -> bool {
        if key == "escape" && down && self.focused.is_some() && !m.platform {
            self.back_to_table();
            cx.notify();
            return true;
        }
        if let Some(uid) = self.focused.clone() {
            let used = self.table.device_key(&uid, key, chars, down, mods_of(m));
            cx.notify();
            return used;
        }
        false
    }

    // ---------------------------------------------------------------- the self-test

    fn self_test(report: String, cx: &mut Context<Self>) {
        cx.spawn(async move |this, cx| {
            Timer::after(Duration::from_millis(1500)).await;
            let mut lines: Vec<String> = Vec::new();
            let check = |ok: bool, what: String, lines: &mut Vec<String>| lines.push(format!("{}  {what}", if ok { "ok  " } else { "FAIL" }));
            let _ = this.update(cx, |this, _| {
                let n = this.table.device_count();
                check(n == 5, format!("the first table: {n} devices"), &mut lines);
                check(this.session.cables.len() == 8, format!("{} cables", this.session.cables.len()), &mut lines);
                check(this.pictures.len() == this.types.len(), format!("{} shelf pictures for {} devices", this.pictures.len(), this.types.len()), &mut lines);
                this.table.test_sound(true);
            });
            Timer::after(Duration::from_millis(1500)).await;
            let _ = this.update(cx, |this, _| {
                let (ch1, spk) = (this.table.mixer_channel_level(0), this.table.speaker_level());
                check(ch1 > 0.01, format!("mixer channel 1 hears the groovebox ({ch1:.3})"), &mut lines);
                check(spk > 0.01, format!("the speakers hear the mix ({spk:.3})"), &mut lines);
                this.table.test_sound(false);
                // a cable pulled out and plugged back in, through the table's own mouse handling
                let before = this.session.cables.len();
                let c = this.session.cables[0].clone();
                let d = this.device(&c.to).unwrap().clone();
                let t = this.type_of(&d.type_id).unwrap().clone();
                let jack_mm = Self::jack_centre(&t, &d, c.to_jack);
                let at = this.to_screen(jack_mm);
                this.drag = None;
                let fake_down = MouseDownEvent { button: MouseButton::Left, position: point(px(at.0), px(at.1)), modifiers: Modifiers::default(), click_count: 1, first_mouse: false };
                this.mouse_down_test(&fake_down);
                check(this.session.cables.len() == before - 1, "a cabled jack: its cable comes out".into(), &mut lines);
                this.drag_end_test(at);
                check(this.session.cables.len() == before, "and plugs back in where it's let go".into(), &mut lines);
            });
            // flying in to the groovebox, playing a key on its panel, and back
            let vb = this.update(cx, |this, _| this.session.devices.iter().find(|d| d.type_id == "valvebox").map(|d| d.uid.clone())).ok().flatten();
            if let Some(uid) = vb {
                let _ = this.update(cx, |this, _| this.focus_on(&uid));
                Timer::after(Duration::from_millis(900)).await;
                let _ = this.update(cx, |this, _| {
                    check(this.focused.as_deref() == Some(uid.as_str()), "double-click: the view flies in to the groovebox".into(), &mut lines);
                    let before = this.table.device_signature(&uid, false);
                    // its PLAY key, in its panel's own pixels
                    this.table.device_mouse(&uid, 0, 640.0, 604.0, 0, 1);
                    let during = this.table.device_signature(&uid, false);
                    this.table.device_mouse(&uid, 2, 640.0, 604.0, 0, 1);
                    check(before != during, "a key on its live panel presses".into(), &mut lines);
                    this.back_to_table();
                });
                Timer::after(Duration::from_millis(900)).await;
                let _ = this.update(cx, |this, _| check(this.focused.is_none(), "back to the table".into(), &mut lines));
            }
            let _ = this.update(cx, |_, cx| {
                let fails = lines.iter().filter(|l| l.starts_with("FAIL")).count();
                lines.push(String::new());
                lines.push(if fails == 0 { "ALL PASSED".into() } else { format!("{fails} FAILED") });
                let _ = std::fs::write(&report, lines.join("\n") + "\n");
                cx.quit();
            });
        })
        .detach();
    }

    /// The benchmark (as the JUCE app's TABLETOP_BENCH): flying in to every device and back, timing the frames drawn.
    fn benchmark(report: String, cx: &mut Context<Self>) {
        cx.spawn(async move |this, cx| {
            Timer::after(Duration::from_millis(2500)).await;
            let mut lines = vec![String::from("renderer: GPUI (Metal)")];
            let uids: Vec<(String, String)> = this
                .update(cx, |this, _| {
                    this.session.devices.iter().map(|d| (d.uid.clone(), this.type_of(&d.type_id).map(|t| t.name.clone()).unwrap_or_default())).collect()
                })
                .unwrap_or_default();
            let (mut frames_total, mut secs_total, mut worst_total) = (0usize, 0.0f64, 0.0f64);
            for (uid, name) in uids {
                for leg in 0..2 {
                    let _ = this.update(cx, |this, _| {
                        this.frames = Some(Vec::new());
                        if leg == 0 { this.focus_on(&uid) } else { this.back_to_table() }
                    });
                    Timer::after(Duration::from_millis(1300)).await;
                    let ft = this.update(cx, |this, _| this.frames.take().unwrap_or_default()).unwrap_or_default();
                    // (the moving part: until the frames stop coming for 100 ms)
                    let mut last = ft.len().saturating_sub(1);
                    for k in 1..ft.len() {
                        if (ft[k] - ft[k - 1]).as_secs_f64() > 0.1 {
                            last = k - 1;
                            break;
                        }
                    }
                    let mut worst = 0.0f64;
                    for k in 1..=last.min(ft.len().saturating_sub(1)) {
                        worst = worst.max((ft[k] - ft[k - 1]).as_secs_f64());
                    }
                    let secs = if last > 0 { (ft[last] - ft[0]).as_secs_f64() } else { 0.0 };
                    frames_total += last;
                    secs_total += secs;
                    worst_total = worst_total.max(worst);
                    let what = if leg == 0 { format!("fly in to {name}") } else { "back to the table".to_string() };
                    lines.push(format!("{what:<34}{:.1} fps, longest gap {:.0} ms", if secs > 0.0 { last as f64 / secs } else { 0.0 }, worst * 1000.0));
                }
            }
            lines.push(String::new());
            lines.push(format!("overall: {:.1} fps, longest gap {:.0} ms", frames_total as f64 / secs_total.max(0.001), worst_total * 1000.0));
            let _ = std::fs::write(&report, lines.join("\n") + "\n");
            let _ = this.update(cx, |_, cx| cx.quit());
        })
        .detach();
    }

    fn mouse_down_test(&mut self, ev: &MouseDownEvent) {
        let p = (f(ev.position.x), f(ev.position.y));
        let mm = self.to_table(p);
        if let Some((uid, jack)) = self.jack_at(mm) {
            if let Some(i) = self.table.find_cable(&uid, jack) {
                let c = self.session.cables[i].clone();
                let (other, oj) = if c.from == uid && c.from_jack == jack { (c.to.clone(), c.to_jack) } else { (c.from.clone(), c.from_jack) };
                self.table.disconnect(i);
                self.reload();
                self.drag = Some(Drag::Cable { uid: other, jack: oj, colour: colour(&c.colour), midi: c.midi, pos: p });
            }
        }
    }

    fn drag_end_test(&mut self, at: (f32, f32)) {
        let mm = self.to_table(at);
        if let Some(Drag::Cable { uid, jack, .. }) = self.drag.take() {
            if let Some((u2, j2)) = self.jack_at(mm) {
                if self.table.can_connect(&uid, jack, &u2, j2) {
                    self.table.connect(&uid, jack, &u2, j2);
                }
            }
            self.reload();
        }
    }
}

fn button(label: &str) -> gpui::Div {
    div()
        .px(px(14.0))
        .h(px(30.0))
        .flex()
        .items_center()
        .justify_center()
        .bg(rgb(0x26272b))
        .hover(|s| s.bg(rgb(0x34363b)))
        .border_1()
        .border_color(rgba(0xffffff2e))
        .rounded(px(5.0))
        .text_size(px(12.5))
        .text_color(rgb(0xe6e2d6))
        .child(label.to_string())
}

impl Render for TableView {
    fn render(&mut self, window: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        if let Some(frames) = &mut self.frames {
            frames.push(Instant::now());
        }
        let vs = window.viewport_size();
        self.view = (f(vs.width), f(vs.height));
        self.dpr = window.scale_factor();
        if let Some(c) = self.session.camera.get("pending") {
            if c.as_bool() == Some(true) {
                // the saved camera: its centre, now that the window's size is known
                let (cx0, cy0) = self.camera.off;
                self.camera.off = (self.view.0 * 0.5 - cx0 * self.camera.zoom, self.view.1 * 0.5 - cy0 * self.camera.zoom);
                self.session.camera = serde_json::Value::Null;
            }
        }
        if !self.fitted {
            self.fitted = true;
            self.fit_all(false);
        }
        let z = self.camera.zoom;

        let mut root = div()
            .size_full()
            .relative()
            .overflow_hidden()
            .bg(rgb(0x101113))
            .track_focus(&self.focus)
            .on_mouse_down(MouseButton::Left, cx.listener(|this, ev: &MouseDownEvent, window, cx| {
                this.focus.focus(window);
                this.mouse_down(ev, cx);
            }))
            .on_mouse_down(MouseButton::Right, cx.listener(|this, ev: &MouseDownEvent, _, cx| {
                // right-click a device: off the table (with its cables)
                let p = (f(ev.position.x), f(ev.position.y));
                if this.focused.is_none() && p.1 > BAR_H && !this.in_shelf(p) {
                    if let Some(uid) = this.device_at(this.to_table(p)) {
                        this.table.remove_device(&uid);
                        this.reload();
                        cx.notify();
                    }
                }
            }))
            .on_mouse_move(cx.listener(|this, ev: &MouseMoveEvent, _, cx| this.mouse_move(ev, cx)))
            .on_mouse_up(MouseButton::Left, cx.listener(|this, ev: &MouseUpEvent, _, cx| this.mouse_up(ev, cx)))
            .on_scroll_wheel(cx.listener(|this, ev: &ScrollWheelEvent, _, cx| this.scroll(ev, cx)))
            .on_key_down(cx.listener(|this, ev: &KeyDownEvent, _, cx| {
                let k = &ev.keystroke;
                if this.key(&k.key, k.key_char.as_deref().unwrap_or(""), true, &k.modifiers, cx) {
                    cx.stop_propagation();
                }
            }))
            .on_key_up(cx.listener(|this, ev: &KeyUpEvent, _, cx| {
                let k = &ev.keystroke;
                this.key(&k.key, k.key_char.as_deref().unwrap_or(""), false, &k.modifiers, cx);
            }))
            .on_modifiers_changed(cx.listener(|this, ev: &ModifiersChangedEvent, _, _| {
                if let Some(uid) = this.focused.clone() {
                    this.table.device_modifiers(&uid, mods_of(&ev.modifiers));
                }
            }));

        // ---- the wood, tile by tile across what's visible
        let tile = WOOD_MM * z;
        let (mm0x, mm0y) = self.to_table((0.0, 0.0));
        let (mm1x, mm1y) = self.to_table(self.view);
        let (tx0, ty0) = ((mm0x / WOOD_MM).floor() as i32, (mm0y / WOOD_MM).floor() as i32);
        let (tx1, ty1) = ((mm1x / WOOD_MM).ceil() as i32, (mm1y / WOOD_MM).ceil() as i32);
        for ty in ty0..ty1 {
            for tx in tx0..tx1 {
                let (sx, sy) = self.to_screen((tx as f32 * WOOD_MM, ty as f32 * WOOD_MM));
                root = root.child(
                    div().absolute().left(px(sx)).top(px(sy)).w(px(tile + 1.0)).h(px(tile + 1.0)).child(img(self.wood.clone()).size_full().object_fit(ObjectFit::Fill)),
                );
            }
        }

        // ---- the devices: shadow, jack strip, panel
        let devices = self.session.devices.clone();
        let mut budget = 1usize; // (pictures freshened per frame, besides the one being played)
        let n = devices.len().max(1);
        self.refresh_cursor = (self.refresh_cursor + 1) % n;
        let order: Vec<usize> = (0..devices.len()).map(|i| (i + self.refresh_cursor) % n).collect();
        let mut pictures: HashMap<String, Option<Arc<RenderImage>>> = HashMap::new();
        for &i in &order {
            let d = &devices[i];
            let p = self.sprite(d, &mut budget);
            pictures.insert(d.uid.clone(), p);
        }
        for d in &devices {
            let Some(t) = self.type_of(&d.type_id).cloned() else { continue };
            let (sx, sy) = self.to_screen((d.x, d.y - STRIP_MM));
            let (w, h) = (t.width_mm * z, (t.depth_mm + STRIP_MM) * z);
            let rear = colour(&t.rear);
            let mut node = div()
                .absolute()
                .left(px(sx))
                .top(px(sy))
                .w(px(w))
                .h(px(h))
                .shadow(vec![
                    BoxShadow { color: hsla(0.0, 0.0, 0.0, 0.45), offset: point(px(5.0 * z), px(10.0 * z)), blur_radius: px(18.0 * z), spread_radius: px(0.0) },
                    BoxShadow { color: hsla(0.0, 0.0, 0.0, 0.35), offset: point(px(1.0 * z), px(2.0 * z)), blur_radius: px(3.0 * z), spread_radius: px(0.0) },
                ])
                .child(
                    // the back strip: folded metal in the device's colour
                    div().absolute().left(px(0.0)).top(px(0.0)).w(px(w)).h(px((STRIP_MM + 3.0) * z)).rounded_t(px(3.0 * z)).bg(rear),
                );
            for (j, jack) in t.jacks.iter().enumerate() {
                let c = Self::jack_centre(&t, d, j);
                let r = Self::jack_radius(jack) * z;
                let (cxp, cyp) = ((c.0 - d.x) * z, (c.1 - (d.y - STRIP_MM)) * z);
                node = node.child(
                    div().absolute().left(px(cxp - r)).top(px(cyp - r)).w(px(r * 2.0)).h(px(r * 2.0)).rounded_full().bg(rgb(0xc4c8ce)).child(
                        div().absolute().left(px(r * 0.3)).top(px(r * 0.3)).w(px(r * 1.4)).h(px(r * 1.4)).rounded_full().bg(rgb(0x0d0e10)),
                    ),
                );
                if z > 0.55 {
                    node = node.child(
                        div()
                            .absolute()
                            .left(px(cxp - 15.0 * z))
                            .top(px(1.0 * z))
                            .w(px(30.0 * z))
                            .flex()
                            .justify_center()
                            .text_size(px(3.2 * z))
                            .text_color(hsla(0.0, 0.0, 1.0, 0.88))
                            .child(jack.label.clone()),
                    );
                }
            }
            if let Some(Some(pic)) = pictures.get(&d.uid) {
                node = node.child(div().absolute().left(px(0.0)).top(px(STRIP_MM * z)).w(px(w)).h(px(t.depth_mm * z)).child(img(pic.clone()).size_full().object_fit(ObjectFit::Fill)));
            }
            root = root.child(node);
        }

        // ---- the cables (and one being dragged), drawn as stroked curves
        let mut wires: Vec<((f32, f32), (f32, f32), Hsla, bool, bool)> = Vec::new();
        for c in &self.session.cables {
            let (Some(a), Some(b)) = (self.device(&c.from), self.device(&c.to)) else { continue };
            let (Some(ta), Some(tb)) = (self.type_of(&a.type_id), self.type_of(&b.type_id)) else { continue };
            let (pa, pb) = (self.to_screen(Self::jack_centre(ta, a, c.from_jack)), self.to_screen(Self::jack_centre(tb, b, c.to_jack)));
            wires.push((pa, pb, if c.midi { Hsla::from(rgb(0x1e1f22)) } else { colour(&c.colour) }, c.midi, false));
        }
        if let Some(Drag::Cable { uid, jack, colour: col, midi, pos }) = &self.drag {
            if let Some(d) = self.device(uid) {
                if let Some(t) = self.type_of(&d.type_id) {
                    let pa = self.to_screen(Self::jack_centre(t, d, *jack));
                    wires.push((pa, *pos, if *midi { Hsla::from(rgb(0x1e1f22)) } else { *col }, *midi, true));
                }
            }
        }
        root = root.child(
            canvas(
                move |_, _, _| wires,
                move |_, wires, window, _| {
                    for (a, b, col, midi, dragging) in wires {
                        let width = (if midi { 6.4 } else { 5.2 } * z).max(2.0);
                        let lift = (45.0 * z).max(((a.0 - b.0).powi(2) + (a.1 - b.1).powi(2)).sqrt() * 0.32);
                        let stroke = |w: f32, dx: f32, dy: f32, c: Hsla, window: &mut Window| {
                            let mut pb = PathBuilder::stroke(px(w));
                            pb.move_to(point(px(a.0 + dx), px(a.1 + dy)));
                            pb.cubic_bezier_to(point(px(b.0 + dx), px(b.1 + dy)), point(px(a.0 + dx), px(a.1 - lift + dy)), point(px(b.0 + dx), px(b.1 - lift + dy)));
                            if let Ok(path) = pb.build() {
                                window.paint_path(path, c);
                            }
                        };
                        stroke(width * 1.1, width * 0.5, width * 0.9, hsla(0.0, 0.0, 0.0, 0.32), window);
                        let mut dark = col;
                        dark.l *= 0.55;
                        stroke(width, 0.0, 0.0, dark, window);
                        stroke(width * 0.74, 0.0, 0.0, col, window);
                        stroke(width * 0.2, -width * 0.16, -width * 0.18, hsla(0.0, 0.0, 1.0, if col.l > 0.5 { 0.35 } else { 0.22 }), window);
                        // the plugs at each end
                        let r = if midi { 6.4 } else { 4.8 } * z;
                        for (i, p) in [a, b].into_iter().enumerate() {
                            if i == 1 && !dragging {
                                // (both ends are plugged in: the second plug too)
                            }
                            let boot = Bounds { origin: point(px(p.0 - r * 1.1), px(p.1 - r * 1.1)), size: size(px(r * 2.2), px(r * 2.2)) };
                            window.paint_quad(fill(boot, if midi { Hsla::from(rgb(0x1e1f22)) } else { col }).corner_radii(px(r * 1.1)));
                            let collar = Bounds { origin: point(px(p.0 - r * 0.65), px(p.1 - r * 0.65)), size: size(px(r * 1.3), px(r * 1.3)) };
                            window.paint_quad(fill(collar, Hsla::from(rgb(0xc4c8ce))).corner_radii(px(r * 0.65)));
                        }
                    }
                },
            )
            .absolute()
            .left(px(0.0))
            .top(px(0.0))
            .size_full(),
        );

        // ---- the shelf
        if self.focused.is_none() {
            let mut shelf = div()
                .absolute()
                .left(px(12.0))
                .top(px(BAR_H + 8.0))
                .w(px(SHELF_W))
                .bg(Background::from(Hsla::from(rgba(0x1a1b1ef0))))
                .rounded(px(8.0))
                .border_1()
                .border_color(rgba(0xffffff14))
                .p(px(8.0))
                .flex()
                .flex_col()
                .gap(px(8.0))
                .child(div().h(px(14.0)).text_size(px(11.0)).text_color(rgb(0xbdb9ae)).child("DRAG ONTO THE TABLE"));
            for t in &self.types {
                let mut card = div().h(px(CARD_H)).flex().items_center().gap(px(10.0)).bg(rgb(0x232428)).rounded(px(6.0)).px(px(8.0));
                if let Some(pic) = self.pictures.get(&t.id) {
                    card = card.child(div().w(px(100.0)).h(px(76.0)).child(img(pic.clone()).size_full().object_fit(ObjectFit::Contain)));
                }
                card = card.child(
                    div()
                        .flex()
                        .flex_col()
                        .gap(px(4.0))
                        .child(div().text_size(px(13.0)).text_color(rgb(0xf0ede4)).child(t.name.clone()))
                        .child(div().text_size(px(10.0)).text_color(rgb(0xf2b31e)).child(t.kind.to_uppercase()))
                        .child(div().text_size(px(10.0)).text_color(rgb(0x8d8a82)).child(format!("{:.0} x {:.0} mm", t.width_mm, t.depth_mm + STRIP_MM))),
                );
                shelf = shelf.child(card);
            }
            root = root.child(shelf);
        }
        if let Some(Drag::Shelf { type_id, pos }) = &self.drag {
            if let (Some(pic), Some(t)) = (self.pictures.get(type_id), self.type_of(type_id)) {
                let (w, h) = (t.width_mm * z, (t.depth_mm + STRIP_MM) * z);
                root = root.child(div().absolute().left(px(pos.0 - w * 0.5)).top(px(pos.1 - h * 0.5)).w(px(w)).h(px(h)).opacity(0.8).child(img(pic.clone()).size_full().object_fit(ObjectFit::Contain)));
            }
        }

        // ---- the top bar
        let mut bar = div().absolute().left(px(0.0)).top(px(0.0)).w(px(self.view.0)).h(px(BAR_H)).px(px(12.0)).flex().items_center().gap(px(8.0));
        if self.focused.is_some() {
            bar = bar.child(button("<  Back to the table").on_mouse_down(MouseButton::Left, cx.listener(|this, _, _, cx| {
                this.back_to_table();
                cx.stop_propagation();
                cx.notify();
            })));
        } else {
            bar = bar.child(button("New Table").on_mouse_down(MouseButton::Left, cx.listener(|this, _, _, cx| {
                this.table.new_table();
                this.reload();
                this.fit_all(true);
                cx.stop_propagation();
                cx.notify();
            })));
        }
        bar = bar
            .child(div().flex_1())
            .child(button("Fit").on_mouse_down(MouseButton::Left, cx.listener(|this, _, _, cx| {
                if this.focused.is_some() {
                    this.back_to_table();
                } else {
                    this.fit_all(true);
                }
                cx.stop_propagation();
                cx.notify();
            })))
            .child(button("Audio/MIDI").on_mouse_down(MouseButton::Left, cx.listener(|this, _, _, cx| {
                this.table.show_audio_settings();
                cx.stop_propagation();
            })))
            .child(button("Help").on_mouse_down(MouseButton::Left, cx.listener(|this, _, _, cx| {
                this.table.show_help();
                cx.stop_propagation();
            })));
        root.child(bar)
    }
}

fn main() {
    Application::new().run(|cx: &mut App| {
        let display = cx.primary_display();
        let (w, h) = display.map(|d| (f(d.bounds().size.width) * 0.92, f(d.bounds().size.height) * 0.86)).unwrap_or((1400.0, 860.0));
        let bounds = Bounds::centered(None, size(px(w), px(h)), cx);
        cx.open_window(
            WindowOptions { window_bounds: Some(WindowBounds::Windowed(bounds)), ..Default::default() },
            |window, cx| {
                let view = cx.new(TableView::new);
                view.read(cx).focus.focus(window);
                view
            },
        )
        .unwrap();
        cx.activate(true);
    });
}

#[allow(dead_code)]
fn unused(_: Rgba, _: Point<Pixels>) {}
