/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "common/filepath.h"
#include "common/locale.h"
#include <glib/gstdio.h>
#include <locale.h>
#include <string.h>
#include <time.h>

static void _engine_limits(void)
{
  GError *error = NULL;
  GString *filename = g_string_new("รูปที่ ๑");
  for(int i = 0; i < 18; i++) g_string_append(filename, "กำลัง");
  g_string_append(filename, ".CR3");
  g_assert_cmpuint(filename->len, >, 256);
  g_assert_true(dt_path_check_engine_utf8(filename->str, &error));
  g_assert_no_error(error);
  gchar *too_long = g_strnfill(PATH_MAX, 'a');
  g_assert_false(dt_path_check_engine_utf8(too_long, &error));
  g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_NAMETOOLONG);
  g_clear_error(&error);
  g_free(too_long);
  g_string_free(filename, TRUE);
}

#ifdef _WIN32
#include "win/main_wrapper.h"
#include <windows.h>
#endif

static void _round_trip(void)
{
  GError *error = NULL;
  gchar *root = g_dir_make_tmp("darktabletouch-path-XXXXXX", &error);
  g_assert_no_error(error);
  gchar *directory = g_build_filename(root, "ผู้ใช้", "ทดสอบ ภาพถ่าย #100%", NULL);
  g_assert_cmpint(g_mkdir_with_parents(directory, 0700), ==, 0);
  const gchar *names[] = { "photo.png", "รูปที่ ๑.png", "กำลัง_กําลัง_🌅.png", NULL };
  gchar *old_cwd = g_get_current_dir();
  g_assert_cmpint(g_chdir(root), ==, 0);
  for(int i = 0; names[i]; i++)
  {
    gchar *path = g_build_filename(directory, names[i], NULL);
    g_assert_true(g_file_set_contents(path, "image bytes", -1, &error));
    g_assert_no_error(error);
    gchar *relative = g_build_filename(".", "ผู้ใช้", "ทดสอบ ภาพถ่าย #100%", names[i], NULL);
    gchar *absolute = dt_path_absolute_utf8(relative, &error);
    g_assert_no_error(error);
    g_assert_nonnull(absolute);
    g_assert_true(g_str_has_suffix(absolute, names[i]));
    gchar *existing = dt_path_existing_utf8(absolute, &error);
    g_assert_no_error(error);
    gchar *contents = NULL;
    g_assert_true(g_file_get_contents(existing, &contents, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(contents, ==, "image bytes");
    gchar *sidecar = g_strconcat(path, ".xmp", NULL);
    g_assert_true(g_file_set_contents(sidecar, "<xmp>คำอธิบาย</xmp>", -1, &error));
    g_assert_no_error(error);
    gchar *renamed = g_strconcat(path, ".สำเนา", NULL);
    g_assert_cmpint(g_rename(path, renamed), ==, 0);
    g_assert_true(g_file_test(renamed, G_FILE_TEST_EXISTS));
    g_assert_cmpint(g_unlink(renamed), ==, 0);
    g_assert_cmpint(g_unlink(sidecar), ==, 0);
    g_free(renamed);
    g_free(sidecar);
    g_free(contents);
    g_free(existing);
    g_free(absolute);
    g_free(relative);
    g_free(path);
  }
  g_assert_cmpint(g_chdir(old_cwd), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
  gchar *parent = g_path_get_dirname(directory);
  g_assert_cmpint(g_rmdir(parent), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
  g_free(parent);
  g_free(old_cwd);
  g_free(directory);
  g_free(root);
}

static void _invalid(void)
{
  const gchar *paths[] = { "", "\xff", NULL };
  for(int i = 0; i < 3; i++)
  {
    GError *error = NULL;
    g_assert_null(dt_path_absolute_utf8(paths[i], &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
    g_clear_error(&error);
  }
}

static void _unicode_cwd(void)
{
  GError *error = NULL;
  gchar *root = g_dir_make_tmp("darktabletouch-cwd-XXXXXX", &error);
  g_assert_no_error(error);
  gchar *directory = g_build_filename(root, "โฟลเดอร์ทำงาน", NULL);
  g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
  gchar *old_cwd = g_get_current_dir();
  g_assert_cmpint(g_chdir(directory), ==, 0);
  gchar *expected = g_build_filename(directory, "photo.png", NULL);
  gchar *absolute = dt_path_absolute_utf8("photo.png", &error);
  g_assert_no_error(error);
  g_assert_cmpstr(absolute, ==, expected);
  g_assert_true(g_utf8_validate(absolute, -1, NULL));
  g_assert_cmpint(g_chdir(old_cwd), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
  g_assert_cmpint(g_rmdir(root), ==, 0);
  g_free(expected);
  g_free(absolute);
  g_free(old_cwd);
  g_free(directory);
  g_free(root);
}

static void _missing(void)
{
  GError *error = NULL;
  gchar *path = g_build_filename(g_get_tmp_dir(), "ไม่มีไฟล์นี้-darktabletouch-missing", NULL);
  gchar *existing = dt_path_existing_utf8(path, &error);
  g_assert_null(existing);
  g_assert_nonnull(error);
  g_clear_error(&error);
  g_free(path);
}

static void _english_locale(void)
{
  g_setenv("LANGUAGE", "th_TH", TRUE);
  dt_l10n_use_en_us();
  g_assert_cmpstr(g_getenv("LANGUAGE"), ==, "C");
  g_assert_cmpstr(localeconv()->decimal_point, ==, ".");
  struct tm date = { 0 };
  date.tm_year = 126;
  date.tm_mday = 1;
  char month[64];
  g_assert_cmpuint(strftime(month, sizeof(month), "%B", &date), >, 0);
  g_assert_cmpstr(month, ==, "January");
  _round_trip();
}

#ifdef _WIN32
static void _unc(void)
{
  GError *error = NULL;
  gchar *path = dt_path_absolute_utf8("\\\\server\\แชร์\\รูปที่ ๑.png", &error);
  g_assert_no_error(error);
  g_assert_cmpstr(path, ==, "\\\\server\\แชร์\\รูปที่ ๑.png");
  g_free(path);
}

static void _long_path(void)
{
  GString *path = g_string_new("C:\\");
  for(int i = 0; i < 100; i++) g_string_append(path, "ภาพ\\");
  g_string_append(path, "รูป.png");
  GError *error = NULL;
  gchar *absolute = dt_path_absolute_utf8(path->str, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(absolute, ==, path->str);
  g_free(absolute);
  g_string_free(path, TRUE);
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
  G_STATIC_ASSERT(PATH_MAX >= 4096);
#endif
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/paths/engine-utf8-buffer-limits", _engine_limits);
  g_test_add_func("/paths/unicode-round-trip", _round_trip);
  g_test_add_func("/paths/reject-invalid-utf8", _invalid);
  g_test_add_func("/paths/unicode-working-directory", _unicode_cwd);
  g_test_add_func("/paths/missing", _missing);
  g_test_add_func("/locale/en-US-with-unicode-paths", _english_locale);
#ifdef _WIN32
  g_test_add_func("/paths/unc", _unc);
  g_test_add_func("/paths/no-truncation", _long_path);
#endif
  return g_test_run();
}
