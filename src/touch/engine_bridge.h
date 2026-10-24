/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dt_touch_editor_t dt_touch_editor_t;
typedef struct dt_touch_state_t dt_touch_state_t;

/* all text crossing this boundary is UTF-8; returned text and pixels use dt_touch_free */
int dt_touch_initialize(int argc, char **argv);
const char *dt_touch_version(void);
void dt_touch_shutdown(void);
void dt_touch_free(void *data);
char *dt_touch_images(char **error);
char *dt_touch_import(const char *path, char **error);
char *dt_touch_modules(void);
char *dt_touch_session_json(char **error);
int dt_touch_save_session(const char *next_ui, int image_id, int editor,
                           double zoom, double center_x, double center_y, char **error);
/* thumbnail pixels are RGBA8 sRGB; preview pixels are native BGRX8 sRGB on Windows */
int dt_touch_thumbnail(int image_id, uint8_t **pixels, int *width, int *height, char **error);
dt_touch_editor_t *dt_touch_editor_new(int image_id, char **error);
void dt_touch_editor_free(dt_touch_editor_t *editor);
char *dt_touch_editor_module(dt_touch_editor_t *editor, const char *operation, char **error);
int dt_touch_editor_set(dt_touch_editor_t *editor, const char *operation,
                        const char *field, const char *value_json, char **error);
int dt_touch_editor_enable(dt_touch_editor_t *editor, const char *operation,
                           int enabled, char **error);
int dt_touch_editor_reset(dt_touch_editor_t *editor, const char *operation, char **error);
int dt_touch_editor_save(dt_touch_editor_t *editor, char **error);
dt_touch_state_t *dt_touch_editor_capture(dt_touch_editor_t *editor);
int dt_touch_editor_restore(dt_touch_editor_t *editor, const dt_touch_state_t *state, char **error);
void dt_touch_state_free(dt_touch_state_t *state);
int dt_touch_editor_preview(dt_touch_editor_t *editor, int max_width, int max_height,
                            uint8_t **pixels, int *width, int *height, char **error);
int dt_touch_editor_export(dt_touch_editor_t *editor, const char *path,
                           const char *format, char **error);

#ifdef __cplusplus
}
#endif
