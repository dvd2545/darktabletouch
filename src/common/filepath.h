/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <glib.h>

/** absolute UTF-8 path without changing filename spelling; free with g_free */
gchar *dt_path_absolute_utf8(const gchar *path, GError **error);

/** expand an existing path using Unicode filesystem APIs; free with g_free */
gchar *dt_path_existing_utf8(const gchar *path, GError **error);
