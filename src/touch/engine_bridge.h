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
typedef struct dt_touch_catalog_edit_t dt_touch_catalog_edit_t;

/* all text crossing this boundary is UTF-8; returned text and pixels use dt_touch_free */
int dt_touch_initialize(int argc, char **argv);
const char *dt_touch_version(void);
void dt_touch_shutdown(void);
void dt_touch_free(void *data);
char *dt_touch_images(char **error);
char *dt_touch_library(void);
char *dt_touch_collection_values(int property);
int dt_touch_image_action(const char *action, int image_id, char **error);
void dt_touch_settings_set(int show_rejected, int advance_after_reject);
char *dt_touch_image_path(int image_id);
char *dt_touch_folder_images(const char *folder);
int dt_touch_export_image(int image_id, const char *path, const char *format, int width, int height, int quality, int bits, int upscale, int high_quality, int srgb, char **error);
int dt_touch_image_exists(int image_id);
int dt_touch_collection_set(const char *json, char **error);
int dt_touch_selection(const char *action, int image_id, char **error);
int dt_touch_import_candidate(const char *path);
int dt_touch_import_is_raw(const char *path);
void dt_touch_quick_actions_set(int reject, int delete_button);
char *dt_touch_import(const char *path, char **error);
char *dt_touch_modules(void);
char *dt_touch_session_json(char **error);
int dt_touch_save_session(const char *next_ui, int image_id, int editor,
                           double zoom, double center_x, double center_y, char **error);
/* thumbnail pixels are RGBA8 sRGB; preview pixels are native BGRX8 sRGB on Windows */
int dt_touch_thumbnail(int image_id, uint8_t **pixels, int *width, int *height, char **error);
dt_touch_editor_t *dt_touch_editor_new(int image_id, char **error);
void dt_touch_editor_free(dt_touch_editor_t *editor);
char *dt_touch_editor_information(dt_touch_editor_t *editor, char **error);
char *dt_touch_editor_module(dt_touch_editor_t *editor, const char *operation, char **error);
char *dt_touch_history_clipboard(int image_id, char **error);
dt_touch_catalog_edit_t *dt_touch_history_apply(const char *request_json, char **error);
/* 0: unchanged; 1: restored and saved; -1: restored, with a sidecar error */
int dt_touch_catalog_restore(dt_touch_catalog_edit_t *edit, int redo, char **error);
void dt_touch_catalog_free(dt_touch_catalog_edit_t *edit);
int dt_touch_editor_reload(dt_touch_editor_t *editor, char **error);
char *dt_touch_editor_history(dt_touch_editor_t *editor);
int dt_touch_editor_history_select(dt_touch_editor_t *editor, int end, char **error);
int dt_touch_editor_set(dt_touch_editor_t *editor, const char *operation,
                        const char *field, const char *value_json, char **error);
int dt_touch_editor_enable(dt_touch_editor_t *editor, const char *operation,
                           int enabled, char **error);
int dt_touch_editor_reset(dt_touch_editor_t *editor, const char *operation, char **error);
int dt_touch_editor_enabled(dt_touch_editor_t *editor, const char *operation);
void dt_touch_editor_crop_original(dt_touch_editor_t *editor, int size[2]);
int dt_touch_editor_crop_bounds(dt_touch_editor_t *editor, float bounds[4], char **error);
int dt_touch_editor_crop_apply(dt_touch_editor_t *editor, const float bounds[4], int ratio_n, int ratio_d, char **error);
int dt_touch_editor_auto_crop(dt_touch_editor_t *editor, double aspect, float bounds[4], char **error);
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
