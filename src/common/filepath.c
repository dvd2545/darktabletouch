/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "common/filepath.h"
#include <glib/gstdio.h>

#ifdef _WIN32
#include <windows.h>

static gchar *_resolve(const gchar *path, const gboolean existing, GError **error)
{
  gunichar2 *wide = g_utf8_to_utf16(path, -1, NULL, NULL, error);
  if(!wide) return NULL;

  DWORD capacity = existing ? GetLongPathNameW((LPCWSTR)wide, NULL, 0)
                            : GetFullPathNameW((LPCWSTR)wide, 0, NULL, NULL);
  for(int attempt = 0; capacity && attempt < 4; attempt++)
  {
    wchar_t *buffer = g_new(wchar_t, capacity);
    const DWORD length = existing ? GetLongPathNameW((LPCWSTR)wide, buffer, capacity)
                                  : GetFullPathNameW((LPCWSTR)wide, capacity, buffer, NULL);
    if(length && length < capacity)
    {
      gchar *result = g_utf16_to_utf8((gunichar2 *)buffer, length, NULL, NULL, error);
      g_free(buffer);
      g_free(wide);
      return result;
    }
    g_free(buffer);
    if(!length) break;
    capacity = length + 1;
  }

  const DWORD code = GetLastError();
  g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
              "could not resolve path '%s' (Windows error %lu)", path, (unsigned long)code);
  g_free(wide);
  return NULL;
}
#endif

static gboolean _validate(const gchar *path, GError **error)
{
  if(!path || !*path || !g_utf8_validate(path, -1, NULL))
  {
    g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_INVAL,
                        "path must be a nonempty UTF-8 string");
    return FALSE;
  }
  return TRUE;
}

gchar *dt_path_absolute_utf8(const gchar *path, GError **error)
{
  if(!_validate(path, error)) return NULL;
#ifdef _WIN32
  return _resolve(path, FALSE, error);
#else
  return g_canonicalize_filename(path, NULL);
#endif
}

gchar *dt_path_existing_utf8(const gchar *path, GError **error)
{
  if(!_validate(path, error)) return NULL;
#ifdef _WIN32
  return _resolve(path, TRUE, error);
#else
  if(!g_file_test(path, G_FILE_TEST_EXISTS))
  {
    g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_NOENT, "path does not exist: '%s'", path);
    return NULL;
  }
  return g_canonicalize_filename(path, NULL);
#endif
}
