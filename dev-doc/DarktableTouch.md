# DarktableTouch

DarktableTouch targets Windows on the 7-inch ROG Ally Z1 Extreme with 16 GB
of shared memory. Project communication, application text, documentation,
diagnostics, and new comments use U.S. English. Filenames and user content
retain their original Unicode text.

## Platform contract

- Engine and bridge paths are UTF-8. Qt boundaries use explicit UTF-8
  conversions. Native Windows operations use UTF-16 APIs.
- Do not convert paths through the ANSI code page, transliterate them, or
  normalize Unicode filename spelling. Invalid encoding is an error.
- Do not truncate paths. Resolve Windows path buffers using the size
  reported by the native API, including room for the terminator.
- Legacy engine path buffers have at least 4096 UTF-8 bytes on Windows;
  MinGW's 260-byte ANSI limit cannot represent many ordinary Thai paths.
  In-memory image filenames allow 1024 UTF-8 bytes. Persisted module
  parameter buffers remain unchanged. The fork has a distinct module ABI
  so older upstream DLLs cannot load against these internal layouts.
- Reject paths beyond bounded engine capacity explicitly, reserving space
  for sidecars and version suffixes. Dynamic path-resolution helpers can
  resolve longer names; that alone does not establish engine support.
- Default configuration and cache directories use `darktabletouch` instead
  of `darktable`. Explicit `--configdir` and `--cachedir` still take priority.
- Application presentation uses en-US. Numeric serialization remains in
  the C locale; existing EXIF, XMP, and database formats stay unchanged.
- Classic and Touch share the DarktableTouch catalog. They must not open it
  concurrently. A switch saves edits and releases the lock before relaunch.

## Delivery order

1. Unicode paths, en-US startup, isolated configuration, and regression tests.
2. Qt Quick launcher, engine bridge, library/editor/export workflow, Exposure.
3. AgX, Lens Correction, Denoise (profiled), Rotation, then Crop.
4. Remaining editing modules, blending, masks, curves, and specialized tools.
5. Advanced library workflows, Ally profiling, and Windows packaging.

Tool priority does not change pixelpipe ordering. Rotation combines the
existing orientation and rotate-and-perspective operations. Crop uses the
separate crop operation. Old histories retain their deprecated operations.

Print, Slideshow, and Tethering remain available in Classic only. A generated
parameter panel is not complete coverage of a module's specialized tools.

## Touch foundation

`BUILD_TOUCH_UI=ON` builds `darktable-touch` and the `darktabletouch` launcher
with Qt 6.8 or newer. Windows builds enable it by default. Set
`BUILD_TOUCH_UI=OFF` for a Classic-only build without Qt.

The launcher accepts `--ui=touch|classic`, defaults to Touch, and stores the
selection in `launcher.ini` inside the engine configuration directory. Use
the launcher when switching interfaces: Touch's quick actions contain
"switch to classic", and a managed Classic session has a "touch" button.
The launcher waits for a successful process exit before honoring a new
switch request. Failed saves keep the current Touch session open. A crash
or an old request does not trigger a relaunch.

`touch-session.json` binds the selected image, collection, filters, sort,
relative-to-fit zoom, and image center to the current catalog. Collection
rules use structured UTF-8 JSON rather than the legacy preset serializer,
which has a fixed text buffer. Explicit command-line photos take priority
on the initial launch; subsequent handoffs restore the current session.

The bridge keeps the existing native parameter blobs, histories, and
module version conversions. A worker thread owns the engine and processes
commands serially. Preview revisions prevent old frames from replacing
new edits. Slider changes are coalesced and one completed gesture records
one undo entry. Preview processing releases the full-resolution input lock
before thumbnail generation or export. The preview pipeline remains cached.

The library uses a virtualized grid and a bounded thumbnail cache. The
editor includes native scalar/enum controls, reset, undo/redo, numeric
entry, before/after, pan/zoom, and export. UI text uses Qt's `qsTr`/`tr`
markers; native strings use gettext. Fonts retain Thai shaping and fallback.

The editor places a virtualized photo filmstrip below the canvas and the
six priority tools in a vertical panel on the right. One tool expands at a
time; its menu contains reset and additional controls. The lower-left
hamburger button sits beside the thumbnails inside the filmstrip, outside
the image viewport, and opens library, export,
history, undo/redo, comparison, import, and interface-switching actions.
Image information appears in the upper-left corner when a photo opens,
then fades after four seconds. Tap the canvas or choose "image information"
to show it again. Filenames remain literal Unicode text; camera and
exposure information come from the engine's metadata, with en-US numbers.
Busy operations and errors remain visible even after transient status
messages fade. Focus navigation scrolls tool controls and quick actions
into view on smaller screens.

Small pill handles resize the filmstrip height and module-panel width.
Their short axis is 24 logical pixels, with the visible pill centered on
the panel boundary. The filmstrip leaves 14 pixels above its thumbnails,
and the module panel uses an 18-pixel left gutter. Primary buttons retain
their 64-pixel touch targets.
Drag the filmstrip handle up or down, or the module handle left or right.
The handles have larger touch areas, support arrow-key adjustment and
accessibility actions, and reset to their automatic sizes on a double-tap
or the controller's confirm button. Sizes are bounded to retain room for
the image and controls and remain adjustable during the current session.

Exposure displays the controls used by its current mode. Manual mode uses
the exposure adjustment; automatic mode uses target brightness and RAW
histogram percentile. Automatic mode is available for single-channel
16-bit RAW images, matching the native operation's eligibility checks.
Numeric fields commit only after text is edited, so moving focus does not
add a history item or round an unchanged parameter.

Mouse-wheel zoom anchors the image under the pointer; pinch zoom tracks
the midpoint between fingers, including midpoint movement. Rotation offers
a numeric precise-angle field backed by the native rotate-and-perspective
operation, in addition to orientation controls.

The edit-history panel lists the current engine session, including disabled
steps and instance labels. Selecting a step uses native history replay and
is itself undoable. Built-in preset labels use the same localization helper
as Classic. History and quick-action panels scroll at smaller window sizes.

### Library collections and import

The funnel button at the upper-right of the library opens collections,
filters, and sort priorities in one dialog. Filter rules appear above the
checkable collection list; empty default filters start collapsed. Folder rows expand into subfolders; parent
checkboxes select descendants and show partial selections. Other supported
properties list catalog values and counts. Checking values replaces the
base collection rules and keeps filters separate; "reset all" clears both.
The engine supports up to ten collection rules; selecting a parent folder
can combine subfolder selections. Counts refer to the entire catalog. Rules use Classic's
properties and matching syntax, including AND, OR, AND NOT, `%` wildcards,
numeric comparisons, and metadata fields. Rating, color-label, and history
rules offer common values. Filters can be disabled without removing them.
Changes are applied together or discarded with cancel. The same preferences
and native collection query drive the filmstrip and Classic handoff.

"select" enables multi-photo selection without opening the editor. Tap photos
to toggle them, or use all, none, invert, same film roll, and untouched. Hold
a thumbnail, right-click it, or press Menu/Shift+F10 for image actions.
Shift+Space selects a range from the native anchor. Selection uses the catalog's existing selected-image table.
It is separate from the active editor image. Export still targets that active
image; selecting several images does not imply batch export.

Image actions include reject, unreject/unrate, remove from the catalog, and
delete from disk. Unreject/unrate clears both rejection and stars. Removal
keeps original files and sidecars. Disk deletion uses the Recycle Bin,
including version sidecars, with no permanent-delete fallback. A shared
original stays on disk while another catalog version refers to it. Both
removal actions require confirmation. Removing the active image saves and
closes its editor before changing the catalog.

Windows photo, folder, and export browsing uses native file dialogs.
Import supports adding photos at their existing locations. Choose files or
a folder, optionally include subfolders, and review supported files before
adding them. Existing catalog entries are identified and skipped; select all
new files, none, or individual files. Dropped paths use the same review step.
The source files stay in place. Import does not replace a collection or clear
filters, and reports added, existing, and failed counts. Copy-and-import,
camera ingestion, import renaming/metadata templates, collection presets,
and specialized value trees remain available through Classic.

### Coverage status

All six priority modules are **partial**, not completed ports:

| Tool | Remaining specialized work |
| --- | --- |
| Exposure | Picker, presets, and complete workflow validation |
| AgX | Range/tone/look organization, graphs, presets, and specialized interactions |
| Lens Correction | Camera/lens selection, methods, presets, and complete workflow validation |
| Denoise (profiled) | Profile selection, wavelet interactions, presets, and advanced workflows |
| Rotation | Straightening, guides, presets, and full workflow validation |
| Crop | Frame manipulation, aspect ratios, orientation swap, and apply/cancel |

Other modules remain unported. `--coverage-report FILE` writes a registry
from the modules actually loaded by that engine build, with priority and
explicit partial/not-started status. Unsupported field types point users
to Classic instead of displaying invalid numeric controls.

Controller polling supports XInput reconnection, focus navigation,
confirmation/back, image navigation, compare, tool selection, pan, and zoom.
Remapping, configurable dead zones, tool cursor interactions, focused
parameter triggers, and a controller keyboard remain outstanding. Monitor
ICC integration, fit/100% behavior, and broader UI sizing also need work.

## Building and packaging on Windows

Use MSYS2 UCRT64 and the dependencies listed in
`.github/workflows/touch-platform.yml`. The Qt additions are `qt6-base` and
`qt6-declarative`; NSIS builds the installer. For example:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TOUCH_UI=ON -DBUILD_TESTING=OFF -DBINARY_PACKAGE_BUILD=ON \
  -DDONT_USE_INTERNAL_LIBRAW=ON -DUSE_GMIC=OFF \
  -DUSE_GRAPHICSMAGICK=OFF -DUSE_OPENCV=OFF -DUSE_AI=OFF
cmake --build build -j 3
cmake --install build --prefix "$PWD/build-stage" --component DTApplication
python tools/test_windows_package.py --prefix build-stage --raw /path/to/sample.CR2
cpack --config build/CPackConfig.cmake -G NSIS -B build-installer
```

The staged application includes Qt runtime DLLs, QML imports, platform
plugins, Qt license notices, the processing engine, and Classic. The NSIS
package uses the DarktableTouch installation directory and registry keys
so it can coexist with upstream darktable. Its dialogs use English and
its filesystem handling is Unicode. The upstream Inno Setup template is
not the DarktableTouch packaging entry point.

The upstream `BUILD_TESTING=ON` configuration currently references missing
unit-test directories. The standalone platform suite and the Touch tests
below avoid that unrelated configuration failure.

## Platform regression tests

The platform suite can be built independently of the engine and its optional
dependencies, including when the Touch UI is disabled:

```sh
cmake -S src/tests/platform -B build-platform -G Ninja
cmake --build build-platform
ctest --test-dir build-platform --output-on-failure
```

On Windows, use the MSYS2 UCRT64 shell. The suite requires the UCRT64 GCC,
CMake, Ninja, pkgconf, and GLib packages. Tests cover relative Thai paths,
combining characters, supplementary characters, spaces, punctuation,
sidecars, file operations, UNC resolution, invalid encoding, and path-buffer
sizing. UNC resolution tests do not require an accessible network share.

The full application additionally needs actual import, RAW decode,
metadata/sidecar, preview, export, and interface-switch tests with Thai paths.
Passing the platform suite alone does not establish that every image codec
or editor workflow supports Unicode.

With a built engine and a real RAW fixture, run:

```sh
python tools/test_unicode_workflow.py --raw /path/to/sample.CR2
```

This imports English and Thai copies of the same RAW, checks repeat-import
identity, applies Exposure, compares preview pixels, writes an XMP and PNG,
restarts the engine, and checks persistence and the original file's hash.
The test uses isolated temporary catalogs. `--keep build/unicode-workflow`
retains the output and engine log for inspection.

The Windows CI also exercises a real Canon EOS M50 C-RAW (`.CR3`) sample
from [raw.pixls.us](https://raw.pixls.us/), repository item 2663, released
under CC0. Its pinned SHA-256 is
`15384b775867ec4c42b11882837f1e368cedc0561832ffab271221e6bb80be4c`.
The RAW files are downloaded as test data rather than committed to this
repository. Do not rename another RAW format to `.CR3` to simulate support.

Additional Touch checks:

```sh
python tools/test_touch_launcher.py
build/bin/test-touch-engine.exe /path/to/sample.CR2
QT_QUICK_BACKEND=software build/bin/darktable-touch.exe -platform offscreen \
  --self-test /path/to/sample.CR2 --screenshot "$PWD/build/touch-editor.png" \
  --coverage-report "$PWD/build/module-coverage.json"
```

The native bridge test checks preview changes, exact history restoration,
thumbnail generation while an editor exists, session round-trips, and
release of database handles. It checks history selection and out-of-range
step rejection. Touch self-tests select original history and verify that
Undo restores the edited preview exactly. Screenshots include the editor,
history panel, and quick actions at 1280x720 and 960x540. Launcher probes
check Unicode executable and
argument paths, selection persistence, handoffs, failed children, and old
requests. Package tests remove the developer toolchain from child-process
PATH and exercise both UI startup and regular/command-line RAW workflows.

The local validation host identifies as an ROG Ally Z1 Extreme. Software
rendering tests and a visible Touch editor session have run on that host.
The host uses Windows ANSI code page 1252 and the en-US Windows user locale;
the Unicode workflows do not rely on the system UTF-8 compatibility option.
Touch-to-Classic process handoff has run, but visual confirmation of the
Classic restored view was blocked by a computer-use window-binding error.
Physical touch/controller workflows, GPU frame pacing, both Windows
regional-setting configurations, and complete installer installation/
uninstallation still require validation. No delivery phase is marked
complete by these foundation checks.

## Verification and publication

Build each logical change before committing, then push the verified commit
to `master` without force-pushing. Record incomplete coverage honestly.
Measure UI interaction, RAW processing, and export separately. Ally frame
rate and memory targets require measurements on the target hardware.
