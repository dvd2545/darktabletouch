---
status: accepted
---

# Touch is rewritten in Rust with Slint; the Engine stays C

Touch is rewritten in Rust with the Slint toolkit, replacing the Qt/QML and
C++ interface outright rather than running beside it. Classic is left as
upstream ships it, and the Engine stays darktable's C code, reached only
through the Engine bridge. Only the interface moves: rewriting Classic would
mean replacing about 137k lines of GTK code plus the GUI half of 88 of the
97 processing modules, and would make every upstream merge conflict. A
rewritten Engine would also have to reproduce every module's output on the
CPU and OpenCL paths.

## Considered Options

- **Slint** was chosen over iced, egui, Tauri and cxx-qt. Its `.slint` files
  compile to native code with no JavaScript engine, its runtime is small, it
  handles touch and IME input, and its declarative style is close to the
  QML it replaces. egui redraws the whole frame on every change, Tauri adds
  a WebView and copies every image into it, and cxx-qt keeps QML, so the
  interface would not be Rust.
- **Removing Qt at once** was chosen over keeping the Qt Touch until the Rust
  one matches it. Until the Rust Touch reaches its first milestone (Library,
  Viewer, rating and reject, switching to Classic), users have Classic only.
- **CMake drives cargo** through a hand-written custom command, using the
  MSYS2 UCRT64 Rust toolchain. The Engine is built with UCRT64 GCC, and an
  MSVC-target Rust cannot link it cleanly. Corrosion was rejected to avoid a
  dependency while Windows is the only platform.

## Consequences

- The Engine bridge stays C. Calls that run often (the collection's image
  list, thumbnails, Renders) move from JSON to typed structs fetched in
  pages: `dt_touch_images()` serializes and reparses the whole library on
  every call. Calls that run rarely keep JSON.
- The interface is English only and has no translation catalog, but Thai
  paths, Thai display (shaping, eliding) and Thai input must work. It uses
  the system fonts (Segoe UI, Leelawadee UI) rather than bundled ones.
- Touch, keyboard and gamepad (`gilrs`) work from the first milestone.
- Performance targets, measured against the Qt Touch before it is removed:
  Library visible under 1.5 s after launch, thumbnail scrolling at 60 fps,
  first picture in the Viewer under 100 ms, full-screen Render of an unedited
  photo under 1 s, and a prefetched swipe under 50 ms. Any target the Qt
  Touch already beats becomes "no slower than before".
- The photo is the hero of every screen; adjustments and the filmstrip are
  secondary and stay out of its way.
