/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/engine_bridge.h"
#include "common/touch_session.h"
#include "common/image.h"
#include "control/conf.h"
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

int main(int argc, char **argv)
{
  if(argc != 2) { fprintf(stderr, "usage: test-touch-engine RAW_FILE\n"); return 2; }
  GError *error = NULL;
  char *root = g_dir_make_tmp("darktabletouch-engine-XXXXXX", &error);
  g_assert_no_error(error);
  char *folder = g_build_filename(root, "ผู้ใช้", NULL);
  char *cache = g_build_filename(folder, "แคช", NULL);
  char *config = g_build_filename(folder, "ตั้งค่า", NULL);
  g_mkdir_with_parents(folder, 0700);
  char *filename = g_build_filename(folder, "รูป.CR2", NULL);
  GFile *source = g_file_new_for_path(argv[1]);
  GFile *target = g_file_new_for_path(filename);
  g_assert_true(g_file_copy(source, target, G_FILE_COPY_NONE, NULL, NULL, NULL, &error));
  g_assert_no_error(error);
  g_object_unref(source); g_object_unref(target);
  char *args[] = { argv[0], "--configdir", config, "--cachedir", cache,
    "--disable-opencl", "--conf", "host_memory_limit=2048", "--conf", "worker_threads=2", "-t", "2", NULL };
  g_assert_true(dt_touch_initialize(G_N_ELEMENTS(args) - 1, args));
  char *message = NULL;
  char *imported = dt_touch_import(filename, &message);
  g_assert_null(message); g_assert_nonnull(imported);
  JsonParser *parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(parser, imported, -1, &error));
  g_assert_no_error(error);
  JsonArray *images = json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)), "images");
  const int image_id = json_object_get_int_member(json_array_get_object_element(images, 0), "imgid");
  char truncated[4] = "bad";
  gboolean cached = FALSE;
  dt_image_full_path(image_id, truncated, sizeof(truncated), &cached);
  g_assert_cmpstr(truncated, ==, "");
  dt_touch_editor_t *editor = dt_touch_editor_new(image_id, &message);
  g_assert_null(message); g_assert_nonnull(editor);
  char *schema = dt_touch_editor_module(editor, "exposure", &message);
  g_assert_null(message); g_assert_nonnull(schema);
  JsonParser *schema_parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(schema_parser, schema, -1, &error));
  g_assert_no_error(error);
  JsonArray *fields = json_object_get_array_member(json_node_get_object(json_parser_get_root(schema_parser)), "fields");
  gboolean found_mode = FALSE;
  for(guint i = 0; i < json_array_get_length(fields); i++)
  {
    JsonObject *field = json_array_get_object_element(fields, i);
    if(!g_strcmp0(json_object_get_string_member(field, "name"), "mode"))
    {
      found_mode = TRUE;
      g_assert_cmpint(json_node_get_value_type(json_object_get_member(field, "value")), ==, G_TYPE_INT64);
      g_assert_cmpuint(json_array_get_length(json_object_get_array_member(field, "values")), ==, 2);
    }
  }
  g_assert_true(found_mode);
  dt_touch_free(schema);
  g_object_unref(schema_parser);
  dt_touch_state_t *before = dt_touch_editor_capture(editor);
  uint8_t *original = NULL, *edited = NULL, *undone = NULL;
  int width, height, w2, h2;
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &original, &width, &height, &message));
  g_assert_null(message);
  uint8_t *thumbnail = NULL;
  int tw, th;
  g_assert_true(dt_touch_thumbnail(image_id, &thumbnail, &tw, &th, &message));
  g_assert_null(message);
  g_assert_cmpint(tw, >, 0); g_assert_cmpint(th, >, 0);
  dt_touch_free(thumbnail);
  g_assert_true(dt_touch_editor_set(editor, "exposure", "exposure", "0.5", &message));
  g_assert_null(message);
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &edited, &w2, &h2, &message));
  g_assert_cmpint(width, ==, w2); g_assert_cmpint(height, ==, h2);
  g_assert_true(memcmp(original, edited, (size_t)width * height * 4) != 0);
  dt_touch_state_t *edited_state = dt_touch_editor_capture(editor);
  char *history = dt_touch_editor_history(editor);
  g_assert_true(json_parser_load_from_data(parser, history, -1, &error));
  JsonObject *steps = json_node_get_object(json_parser_get_root(parser));
  const int count = json_array_get_length(json_object_get_array_member(steps, "steps"));
  g_assert_cmpint(count, >, 0);
  g_assert_true(dt_touch_editor_history_select(editor, 0, &message));
  dt_touch_free(history);
  history = dt_touch_editor_history(editor);
  g_assert_true(json_parser_load_from_data(parser, history, -1, &error));
  g_assert_cmpint(json_object_get_int_member(json_node_get_object(json_parser_get_root(parser)), "end"), ==, 0);
  dt_touch_free(history);
  g_assert_false(dt_touch_editor_history_select(editor, count + 1, &message));
  g_assert_nonnull(message); dt_touch_free(message); message = NULL;
  g_assert_true(dt_touch_editor_restore(editor, edited_state, &message));
  dt_touch_state_free(edited_state);
  g_assert_true(dt_touch_editor_restore(editor, before, &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &undone, &w2, &h2, &message));
  g_assert_cmpmem(original, (size_t)width * height * 4, undone, (size_t)w2 * h2 * 4);
  dt_touch_free(original); dt_touch_free(edited); dt_touch_free(undone);
  dt_touch_state_free(before);
  dt_touch_editor_free(editor);
  GString *long_path = g_string_new("C:/ผู้ใช้/");
  for(int i = 0; i < 100; i++) g_string_append(long_path, "ทดสอบ ภาพถ่าย/$#%");
  dt_conf_set_int("plugins/lighttable/collect/num_rules", 1);
  dt_conf_set_string("plugins/lighttable/collect/string0", long_path->str);
  g_assert_true(dt_touch_save_session("classic", image_id, TRUE, 2.5, 0.3, 0.7, &message));
  g_assert_null(message);
  dt_conf_set_string("plugins/lighttable/collect/string0", "changed");
  char *session = dt_touch_session_json(&message);
  g_assert_nonnull(session); g_assert_null(message);
  g_assert_cmpstr(dt_conf_get_string_const("plugins/lighttable/collect/string0"), ==, long_path->str);
  g_assert_true(json_parser_load_from_data(parser, session, -1, &error));
  JsonObject *view = json_node_get_object(json_parser_get_root(parser));
  g_assert_cmpstr(json_object_get_string_member(view, "next_ui"), ==, "classic");
  g_assert_cmpint(json_object_get_int_member(view, "image_id"), ==, image_id);
  g_assert_cmpfloat(json_object_get_double_member(view, "zoom_factor"), ==, 2.5);
  g_assert_cmpfloat(json_object_get_double_member(view, "center_x"), ==, 0.3);
  g_assert_cmpfloat(json_object_get_double_member(view, "center_y"), ==, 0.7);
  dt_touch_free(session);
  g_string_free(long_path, TRUE);
  dt_touch_free(imported);
  g_object_unref(parser);
  dt_touch_shutdown();
  char *library = g_build_filename(config, "library.db", NULL);
  char *data = g_build_filename(config, "data.db", NULL);
  g_assert_cmpint(g_unlink(library), ==, 0);
  g_assert_cmpint(g_unlink(data), ==, 0);
  g_free(library); g_free(data);
  printf("passed: persistent Touch preview, history restore, and Unicode session handoff\n");
  g_free(filename); g_free(cache); g_free(config); g_free(folder); g_free(root);
  return 0;
}
