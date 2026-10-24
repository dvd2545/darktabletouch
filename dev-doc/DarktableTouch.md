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

## Verification and publication

Build each logical change before committing, then push the verified commit
to `master` without force-pushing. Record incomplete coverage honestly.
Measure UI interaction, RAW processing, and export separately. Ally frame
rate and memory targets require measurements on the target hardware.
