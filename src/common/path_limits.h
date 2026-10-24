/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once
#include <limits.h>

// MinGW's ANSI path limit is too small for ordinary Windows paths encoded as UTF-8
#if defined(_WIN32) && defined(PATH_MAX) && PATH_MAX < 4096
#undef PATH_MAX
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define DT_MAX_FILENAME_UTF8_LEN 1024
