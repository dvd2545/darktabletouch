#pragma once

#include <glib.h>
#include <stdio.h>

// use the Unicode CRT entry point so paths never pass through the system code page
// including this file will add a wrapper that acts together with linker switch -municode

int main(int argc, char *argv[]);

int wmain(int argc, wchar_t *argv[])
{
  char **owned = g_new0(char *, argc + 1);
  for(int i = 0; i < argc; i++)
  {
    GError *error = NULL;
    owned[i] = g_utf16_to_utf8((const gunichar2 *)argv[i], -1, NULL, NULL, &error);
    if(!owned[i])
    {
      fprintf(stderr, "invalid Unicode command-line argument: %s\n", error->message);
      g_clear_error(&error);
      g_strfreev(owned);
      return 1;
    }
  }
  // dt_init compacts argv in place, so retain a separate ownership list
  char **_argv = g_new0(char *, argc + 1);
  for(int i = 0; i < argc; i++) _argv[i] = owned[i];
  int res = main(argc, _argv);
  g_free(_argv);
  g_strfreev(owned);
  return res;
}
// clang-format off
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.py
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
// clang-format on

