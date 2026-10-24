/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "common/locale.h"
#include <glib.h>
#include <locale.h>

void dt_l10n_use_en_us(void)
{
  g_setenv("LANGUAGE", "C", TRUE);
  if(!setlocale(LC_ALL, "en_US.UTF-8"))
  {
#ifdef _WIN32
    if(!setlocale(LC_ALL, "English_United States.utf8"))
#endif
      setlocale(LC_ALL, "C");
  }
  // parameter and configuration serialization must remain locale independent
  setlocale(LC_NUMERIC, "C");
}
