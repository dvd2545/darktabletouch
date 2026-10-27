/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once
#include <glib.h>

gboolean dt_touch_session_available(void);
char *dt_touch_session_read(char **error);
char *dt_touch_session_restore_collection(char **error);
gboolean dt_touch_session_save(const char *next_ui, int image_id, const char *view,
                                double zoom, double center_x, double center_y, char **error);
gboolean dt_touch_session_save_classic(const char *next_ui, char **error);
void dt_touch_session_finish_classic(void);
gboolean dt_touch_session_restore_classic(gpointer data);

struct dt_develop_t;
/* load current-version history without saving history or sidecars
   on success the caller owns dev; on failure no develop resources remain */
gboolean dt_touch_session_preload(struct dt_develop_t *dev, int image_id);
