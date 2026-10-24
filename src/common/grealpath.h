/*
 This code is taken from http://git.gnome.org/browse/gobject-introspection/tree/giscanner/grealpath.h .
 According to http://git.gnome.org/browse/gobject-introspection/tree/COPYING it's licensed under the LGPLv2+.
*/

#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib.h>

#include "common/darktable.h" // for dt_print
#include "common/filepath.h"

#ifdef _WIN32
#include <fileapi.h>
#endif

/**
 * g_realpath:
 *
 * this should be a) filled in for win32 and b) put in glib...
 */

static inline gchar *g_realpath(const char *path)
{
#ifndef _WIN32
  // If 2nd parameter is specified as NULL, then realpath() uses malloc to
  // allocate a buffer of up to PATH_MAX bytes to hold the resolved pathname,
  // and returns a pointer to this buffer.
  // It is explicitly standardized in POSIX.1-2008.
  char* resolvedpath = realpath(path, NULL);

  if(resolvedpath)
  {
    gchar *result = g_strdup(resolvedpath);
    free(resolvedpath);
    return result;
  }
  else
  {
    dt_print(DT_DEBUG_ALWAYS,
             "[g_realpath] path lookup '%s' fails with: '%s'",
             path,
             strerror(errno));
    return g_strdup(path);
  }
#else
  GError *error = NULL;
  gchar *buffer = dt_path_absolute_utf8(path, &error);
  if(!buffer)
  {
    dt_print(DT_DEBUG_ALWAYS, "[g_realpath] %s", error->message);
    g_clear_error(&error);
  }
  return buffer;
#endif
}

// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on
