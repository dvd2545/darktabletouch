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
the launcher when switching interfaces: Touch's settings contain
"switch to classic", and a managed Classic session has a "touch" button.
The launcher waits for a successful process exit before honoring a new
switch request. Failed saves without a verified recovery copy keep the current
Touch session open. A crash
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

When an adjacent XMP cannot be saved, Touch verifies the catalog history and
writes a validated recovery XMP under the configuration directory's
`recovery-sidecars/<catalog hash>/image-<id>.xmp`. Atomic replacement preserves
the previous recovery file if an update fails. The catalog and recovery XMP
allow editing, export, image switching, and closing to continue even when the
source folder is read-only or offline. Missing RAW files still need to be
restored to their original location before previews or exports can render.
The persistent warning offers Retry Save and Recovery Folder. Retry clears
the warning only after the original sidecar can be written again. Recovery
copies are retained; a recovery XMP can be loaded onto the matching photo
through Classic. If catalog verification or recovery fails, the editor stays
open and reports the recovery path and storage error instead of claiming
that the latest edit is protected.

The library uses a virtualized grid and a bounded thumbnail cache. The
editor includes native scalar/enum controls, reset, undo/redo, numeric
entry, before/after, pan/zoom, and export. UI text uses Qt's `qsTr`/`tr`
markers; native strings use gettext. Fonts retain Thai shaping and fallback.

The left hamburger menu offers Library from the editor, or Editor from the
image library, collections and
filters, import sources, export, and settings. Camera ingestion remains
available in Classic. Library selection uses a floating lower-right action bar without
resizing or repositioning the grid. Export appears there after photos are
selected. All, none, invert, same film roll, untouched, and history operations
remain available in the horizontally scrollable action row.

The editor opens with a photo strip and a narrow category rail. Light, Color,
Detail, and Crop open contextual tool drawers. Tapping the canvas toggles
focus mode; double-tapping controls zoom. Undo and redo float at the lower
left beside the icon-only quick actions button and hide during crop editing.
The image reaches the top of the window;
navigation and history buttons float without filled backgrounds and use
white icons with shadows. Export lives in the left menu. One bottom chevron
toggles the photo strip; its height can still be adjusted with its handle. Swipe upward on the fitted canvas to restore the strip.
Settings control automatic hiding, with switch to Classic as the last option.
Image information appears briefly
on opening a photo and can be recalled from the active filmstrip thumbnail.

Quick actions opens above its lower-left trigger. Its selection action enables
multi-image filmstrip selection without switching the active preview. Holding
a selected thumbnail opens actions for the selected images; the menu and
destructive confirmation show the target count. All and invert operate on the
visible Touch images after native collection and rating filters.
Settings can enable persistent quick reject and quick delete buttons at the
lower right. Quick delete first chooses disk or collection removal, then
requires confirmation. Both shortcuts start hidden. Operations lasting more
than 150 ms show a centered animated loading indicator.

Settings separate interface, library, and editor options. Interface sizing
can override automatic system-density sizing from 75% to 150%; the multiplier
applies once to shared logical metrics. The override, multiplier, editor swipe
navigation, and fitted preview quality persist in the native configuration.
Horizontal canvas swipes navigate only at fit, outside crop and selection;
zoomed images keep pan gestures. Filmstrip and module-panel visibility animate
without delaying manual resize gestures. Floating editor controls use a shared
dark shadow for contrast over bright images.

Fitted preview quality offers maximum edges from 640 to 4096 pixels. After a
250 ms zoom pause, the worker renders at the displayed size and screen pixel
ratio, up to the original image dimensions. The former 4096-pixel zoom
limit no longer applies. Returning to fit requests the configured quality again. Edits keep
the current detailed extent, with temporary 640-pixel frames during adjustment.
This changes preview request sizing, not the native processing algorithms.

`UiMetrics.qml` converts reported physical screen density into a 9.5 mm
touch target, divided by the device pixel ratio. Targets are bounded to
56-80 logical pixels; missing or implausible density uses 64. Shared spacing,
fonts, surfaces, drawer width, and strip sizes derive from these tokens.
Qt applies Windows scaling to logical coordinates. Reported physical
sizes can be inaccurate, so physical touch and Windows scaling still need
hardware validation; simulated scale factors do not establish that.

Parameter names and compact touch values share a row above each slider.
Numeric fields, resets, and slider hit areas use shared parameter metrics with
a 44-pixel minimum, reducing empty padding between controls.
Long names can wrap without obscuring numeric entries. Changed scalar, enum,
and boolean values expose direct resets using their native defaults.
Each module header has its own reset beside its enable switch, including
collapsed modules. The all-modules icon opens the complete side panel
without a second popup and keeps that view while parameters change;
whole-image reset requires confirmation through the separate quick actions.
Numeric drafts preserve precision and validate before committing. Popups and
dialogs share dark surfaces with inset focus and close controls.
Exposure's additional controls use an accessible, padding-free chevron.

Small pill handles resize the filmstrip height and module-panel width.
Their slim hit areas use the shared 16-pixel resize gutter and retain a 112-pixel
long axis, with the visible pill inside the panel boundary. Thumbnails and module controls are inset
beyond the handles' hit areas so resizing does not intercept their taps.
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
operation, in addition to orientation controls. Constrain rotation fits a
centered crop inside the valid source area while preserving original format.
The bounds are stored in native rotation parameters for export, history,
and undo; repeated angle changes start from uncropped geometry.

Crop opens an uncropped preview with a movable frame, four touch handles,
thirds guides, free/preset/custom aspect ratios, and orientation swap.
Apply writes one native crop history item and one session undo action;
cancel and leaving the tool restore the original history. Automatic border
cropping proposes a centered frame at the selected aspect ratio, checking
native inverse geometry against source bounds. It does not analyze scene
composition; review the frame before applying it. Specialized guides and
complete controller crop interactions remain outstanding.

Lens correction presents a toggle using the engine's current automatic or
saved settings. Advanced lens configuration remains available in Classic.
Numeric controls retain their requested values while processing; native
acknowledgments are applied with the matching preview. D-pad left/right
navigate images in the editor, while up/down adjust the last focused
numeric parameter. Dialogs and choices retain focus navigation.

Buttons use vector icons with accessible names and hover hints where the
action is clear. Popups, choices, buttons, and module expansion animate;
crop dragging and numeric manipulation remain immediate.

The edit-history panel lists the current engine session, including disabled
steps and instance labels. Selecting a step uses native history replay and
is itself undoable. Built-in preset labels use the same localization helper
as Classic. History and quick-action panels scroll at smaller window sizes.

### Library collections and import

The collections and filters entry in the left menu opens collections,
filters, and sort priorities in one dialog. Filter rules appear above the
checkable collection list; empty default filters start collapsed. Film rolls and
folders show imported folder names, nesting beneath their nearest imported
ancestor without exposing drive or machine-directory trees. Identically named
folders remain separate collections. Folder rows expand into subfolders; parent
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

Film roll checkboxes and counts refer only to images directly in that imported
folder, even when its imported children appear beneath it. Folder checkboxes
and counts include imported descendants. Rows without children do not reserve
an empty expand/collapse button. Selection's More actions opens the same image
actions menu for all selected images, with the selected count and existing
confirmation steps for removal, deletion, and history reset.

"select" enables multi-photo selection without opening the editor. Tap photos
to toggle them, or use all, none, invert, same film roll, and untouched. Hold
a thumbnail, right-click it, or press Menu/Shift+F10 for image actions.
Shift+Space selects a range from the native anchor. Selection uses the catalog's existing selected-image table.
It is separate from the active editor image. Export still targets that active
image when using the direct export API. The export settings dialog supports
selected photos, the visible collection, or the current image.

The menu panel and Library settings offer independent rating choices for
rejected, unrated, and one-to-five-star images. These update native rating
filters without changing the base collection or other filter properties.
All ratings are visible unless a native filter excludes them; the separate
show-rejected preference has been removed. Settings retain an optional
next-image action after rejecting. Self-tests use isolated catalogs.

Dialog overlays consume outside taps until release, then dismiss without
activating controls underneath. Settings categories share equal widths.
Shared padding and explicit content insets are reduced by 15%, preserving
minimum touch targets.

Collection rows can remove an imported folder and its subfolders from the
catalog, including photos outside current filters. The collection removal
action removes photos in the current native collection. Both require a
confirmation and preserve source files and sidecars. Removal stops on the
first failure and reports partial progress.

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
adding them. Compact rows show each filename and an elided path, with the
full path available through accessibility and hover hints. Existing catalog
entries are identified and skipped; select all
new files, none, or individual files. Dropped paths use the same review step.
The source files stay in place. Import does not replace a collection or clear
filters, and reports added, existing, and failed counts. Copy-and-import,
camera ingestion, import renaming/metadata templates, collection presets,
and specialized value trees remain available through Classic.

The source chooser has checkboxes for selecting only new images, recursive
directory scanning, and ignoring non-RAW formats. RAW filtering follows the
native extension registry and includes DNG containers. Changing the visible
review candidates removes hidden entries from its selection. Already imported
images remain skipped, including when shown for reference.

### Batch export

The export dialog groups storage, name modifiers, format, and global options.
Choose one Unicode destination folder or an `exported` subfolder beside each
source. Filename patterns support `{name}`, `{sequence}` (four digits), and
`{id}`. Existing files can be skipped, given unique names, or stop the batch;
they are never overwritten. Each result is rendered to a temporary file and
renamed only after success. A failed item stops the batch and preserves
previous outputs; cancellation stops after the current image.

JPEG supports quality; PNG/TIFF support 8/16-bit output. Global controls set
maximum dimensions, upscaling, high-quality resampling, and sRGB or the image's
output profile. Native export configuration is restored after each item.
Storage plugins, metadata filters, masks-as-layers, and format-specific
advanced controls remain available in Classic.
JPEG quality has a slider and numeric entry. The basic name modifier accepts
literal text and `{name}`, `{sequence}`, and `{id}` tags. Recognized tags appear
as removable chips, with insertion buttons and an example filename. Invalid
tags or filename separators still prevent export.

Add modifier opens a searchable browser populated from Classic's full
completion catalog at configure time and current user metadata definitions at
runtime. Native `$(VARIABLE)` tags, including parameterized sequence and
category tags, appear as removable chips. Export expands them with the native
variables engine for each image. Existing `{name}`, `{sequence}`, and `{id}`
patterns remain compatible. Directory-valued modifiers are available but an
expansion containing filename separators is rejected rather than changing the
chosen destination. The monochrome Darktable vector icon is embedded in both
Windows Touch executables, with a resource icon for the Qt window.

### Coverage status

All six priority modules are **partial**, not completed ports:

| Tool | Remaining specialized work |
| --- | --- |
| Exposure | Picker, presets, and complete workflow validation |
| AgX | Range/tone/look organization, graphs, presets, and specialized interactions |
| Lens Correction | Camera/lens selection, methods, presets, and complete workflow validation |
| Denoise (profiled) | Profile selection, wavelet interactions, presets, and advanced workflows |
| Rotation | Straightening, guides, presets, and full workflow validation |
| Crop | Presets, specialized guides, and full workflow validation |

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

`Run-Touch-Build.bat` opens the existing `build/bin` application without an
installer. It honors `DT_MSYS2_ROOT` when set, otherwise discovers the UCRT64
runtime beside the compiler recorded in `build/CMakeCache.txt`, then checks
the user-local, standard MSYS2, and Codex Store app-local runtime locations.
This supports launching from Explorer outside Codex's redirected environment.
`Run-Touch-Build.bat --check-runtime` prints and validates the chosen runtime
without opening the application. Other arguments are forwarded to the launcher.

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
QT_QUICK_BACKEND=software QT_QUICK_CONTROLS_STYLE=Basic qmltestrunner \
  -platform offscreen -input src/touch/tests
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
