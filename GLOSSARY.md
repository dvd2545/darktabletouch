# DarktableTouch

A fork of darktable that adds a touch-first interface for browsing, editing
and exporting raw photos, alongside the original desktop interface.

## Language

### Interfaces

**Touch**:
The fork's own touch-first interface, designed for touchscreen, pen,
keyboard and controller use. It is the interface being rewritten and
redesigned.
_Avoid_: new UI, mobile UI, frontend

**Classic**:
darktable's original desktop interface, kept as upstream ships it and
available as a fallback for tools Touch does not offer yet.
_Avoid_: old UI, GTK UI, legacy UI

**Engine**:
darktable's image-processing and catalog core that both interfaces share:
raw decoding, the processing modules, history, the library and export.
_Avoid_: backend, core, darktable lib

**Engine bridge**:
The narrow boundary through which Touch asks the Engine for images,
edits and exports; Touch reaches the Engine only through it.
_Avoid_: API, FFI layer, adapter

### Viewing

**Library**:
The grid of photos in the current collection, where photos are browsed,
rated and rejected.
_Avoid_: gallery, lighttable, browser

**Viewer**:
The full-screen view of one photo from the Library, moved through by
swiping.
_Avoid_: preview mode, loupe

**Embedded preview**:
The JPEG the camera stored inside the raw file. It shows none of the
edits made in darktable.
_Avoid_: camera JPEG, thumbnail

**Render**:
A picture of a photo produced by the Engine with all its edits applied.
_Avoid_: export, processed image

**Progressive display**:
Showing the fastest picture available for a photo first, then replacing
it with its Render; an interim picture is marked as such.
_Avoid_: lazy loading, placeholder
