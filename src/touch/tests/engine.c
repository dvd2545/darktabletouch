/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/engine_bridge.h"
#include "common/touch_session.h"
#include "common/image.h"
#include "common/collection.h"
#include "common/database.h"
#include "control/conf.h"
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <stdio.h>
#ifdef _WIN32
#include "win/main_wrapper.h"
#endif

static JsonObject *_rule(int property, const char *value, int mode, int off)
{
  JsonObject *rule = json_object_new();
  json_object_set_int_member(rule, "item", property);
  json_object_set_int_member(rule, "mode", mode);
  json_object_set_int_member(rule, "off", off);
  json_object_set_int_member(rule, "top", 0);
  json_object_set_string_member(rule, "string", value);
  return rule;
}

static int _count(const char *query)
{
  sqlite3_stmt *statement = NULL;
  g_assert_cmpint(sqlite3_prepare_v2(dt_database_get(darktable.db), query, -1, &statement, NULL), ==, SQLITE_OK);
  g_assert_cmpint(sqlite3_step(statement), ==, SQLITE_ROW);
  const int value = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return value;
}

static void _apply_collection(JsonObject *settings, int count)
{
  JsonNode *root = json_node_new(JSON_NODE_OBJECT);
  json_node_set_object(root, settings);
  char *text = json_to_string(root, FALSE), *error = NULL;
  g_assert_true(dt_touch_collection_set(text, &error));
  g_assert_null(error);
  g_assert_cmpint(_count("SELECT COUNT(*) FROM memory.collected_images"), ==, count);
  g_free(text);
  json_node_free(root);
}

static void _test_library(const char *filename, const char *folder, int first)
{
  char *other_folder = g_build_filename(folder, "second folder", NULL);
  g_mkdir_with_parents(other_folder, 0700);
  char *other = g_build_filename(other_folder, "second #%.CR2", NULL);
  GFile *source = g_file_new_for_path(filename), *target = g_file_new_for_path(other);
  g_assert_true(g_file_copy(source, target, G_FILE_COPY_NONE, NULL, NULL, NULL, NULL));
  g_object_unref(source); g_object_unref(target);
  g_assert_cmpint(dt_touch_import_candidate(other), ==, 0);
  char *error = NULL, *result = dt_touch_import(other, &error);
  g_assert_nonnull(result); g_assert_null(error); g_free(result);
  const int second = dt_touch_import_candidate(other);
  g_assert_cmpint(second, >, first);
  g_assert_cmpint(dt_touch_import_candidate(filename), ==, first);
#ifdef _WIN32
  char *qt_path = g_strdup(filename);
  g_strdelimit(qt_path, "\\", '/');
  g_assert_cmpint(dt_touch_import_candidate(qt_path), ==, first);
  g_free(qt_path);
#endif
  g_assert_cmpint(dt_touch_import_candidate("not-a-photo.xmp"), ==, -1);

  JsonObject *settings = json_object_new();
  JsonArray *rules = json_array_new(), *filters = json_array_new(), *sort = json_array_new();
  json_object_set_array_member(settings, "collection", rules);
  json_object_set_array_member(settings, "filtering", filters);
  json_object_set_array_member(settings, "sort", sort);
  json_array_add_object_element(rules, _rule(DT_COLLECTION_PROP_FILMROLL, "%", 0, 0));
  JsonObject *order = json_object_new();
  json_object_set_int_member(order, "item", DT_COLLECTION_SORT_ID);
  json_object_set_int_member(order, "order", 1);
  json_array_add_object_element(sort, order);
  _apply_collection(settings, 2);
  const int properties[] = { DT_COLLECTION_PROP_CAMERA, DT_COLLECTION_PROP_LENS, DT_COLLECTION_PROP_ISO,
    DT_COLLECTION_PROP_APERTURE, DT_COLLECTION_PROP_EXPOSURE, DT_COLLECTION_PROP_DAY, DT_COLLECTION_PROP_HISTORY,
    DT_COLLECTION_PROP_MONTH, DT_COLLECTION_PROP_FOCAL_LENGTH, DT_COLLECTION_PROP_ASPECT_RATIO };
  for(guint i = 0; i < G_N_ELEMENTS(properties); i++)
  {
    GList *values = dt_collection_get_property_values(properties[i]);
    g_assert_nonnull(values);
    uint32_t total = 0;
    for(GList *item = values; item; item = item->next)
    {
      const dt_collection_value_t *value = item->data;
      total += value->count;
      json_array_remove_element(rules, 0);
      json_array_add_object_element(rules, _rule(properties[i], value->value, 0, 0));
      _apply_collection(settings, value->count);
    }
    g_assert_cmpuint(total, ==, _count("SELECT COUNT(*) FROM main.images"));
    g_list_free_full(values, dt_collection_value_free);
  }
  json_array_remove_element(rules, 0);
  json_array_add_object_element(rules, _rule(DT_COLLECTION_PROP_FILMROLL, "%", 0, 0));
  _apply_collection(settings, 2);
  g_assert_cmpint(_count("SELECT imgid FROM memory.collected_images ORDER BY rowid LIMIT 1"), ==, second);
  g_assert_true(dt_touch_selection("all", 0, &error));
  g_assert_cmpint(_count("SELECT COUNT(*) FROM main.selected_images"), ==, 2);
  g_assert_true(dt_touch_selection("invert", 0, &error));
  g_assert_cmpint(_count("SELECT COUNT(*) FROM main.selected_images"), ==, 0);
  g_assert_true(dt_touch_selection("toggle", first, &error));
  g_assert_true(dt_touch_selection("film", 0, &error));
  g_assert_cmpint(_count("SELECT COUNT(*) FROM main.selected_images"), ==, 1);
  g_assert_true(dt_touch_selection("single", first, &error));
  g_assert_true(dt_touch_selection("range", second, &error));
  g_assert_cmpint(_count("SELECT COUNT(*) FROM main.selected_images"), ==, 2);
  g_assert_true(dt_touch_selection("none", 0, &error));
  g_assert_true(dt_touch_selection("untouched", 0, &error));
  g_assert_cmpint(_count("SELECT COUNT(*) FROM main.selected_images"), ==,
    _count("SELECT COUNT(*) FROM main.images i WHERE EXISTS (SELECT 1 FROM main.history_hash h"
           " WHERE h.imgid = i.id AND (h.current_hash = h.auto_hash OR h.current_hash IS NULL))"));
  json_array_remove_element(rules, 0);
  json_array_add_object_element(rules, _rule(DT_COLLECTION_PROP_FILENAME, "รูป.CR2", 0, 0));
  _apply_collection(settings, 1);
  g_assert_cmpint(_count("SELECT imgid FROM memory.collected_images"), ==, first);
  json_array_add_object_element(rules, _rule(DT_COLLECTION_PROP_FILENAME, "second%", 1, 0));
  _apply_collection(settings, 2);
  json_array_add_object_element(filters, _rule(DT_COLLECTION_PROP_FILENAME, "second%", 2, 0));
  _apply_collection(settings, 1);
  json_object_set_int_member(json_array_get_object_element(filters, 0), "off", 1);
  _apply_collection(settings, 2);
  json_object_set_int_member(json_array_get_object_element(rules, 1), "mode", 0);
  _apply_collection(settings, 0);
  char *state = dt_touch_library();
  JsonParser *parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(parser, state, -1, NULL));
  JsonObject *library = json_node_get_object(json_parser_get_root(parser));
  g_assert_cmpuint(json_array_get_length(json_object_get_array_member(library, "folders")), ==, 2);
  g_assert_cmpuint(json_array_get_length(json_object_get_array_member(library, "properties")), >, 30);
  g_free(state); g_object_unref(parser);
  g_assert_false(dt_touch_collection_set("{\"collection\":[]}", &error));
  g_assert_nonnull(error); g_free(error); error = NULL;
  g_assert_cmpint(dt_conf_get_int("plugins/lighttable/collect/num_rules"), ==, 2);
  json_array_remove_element(rules, 1);
  json_array_remove_element(rules, 0);
  json_array_remove_element(filters, 0);
  GString *long_value = g_string_new("ผู้ใช้/$#%");
  for(int i = 0; i < 200; i++) g_string_append(long_value, "ทดสอบ ภาพถ่าย/");
  json_array_add_object_element(rules, _rule(DT_COLLECTION_PROP_FILENAME, long_value->str, 0, 0));
  _apply_collection(settings, 0);
  g_assert_cmpstr(dt_conf_get_string_const("plugins/lighttable/collect/string0"), ==, long_value->str);
  g_string_free(long_value, TRUE);
  json_array_remove_element(rules, 0);
  json_array_add_object_element(rules, _rule(DT_COLLECTION_PROP_FILMROLL, "%", 0, 0));
  _apply_collection(settings, 2);
  json_object_unref(settings);
  g_free(other_folder); g_free(other);
}

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
  _test_library(filename, folder, image_id);
  char truncated[4] = "bad";
  gboolean cached = FALSE;
  dt_image_full_path(image_id, truncated, sizeof(truncated), &cached);
  g_assert_cmpstr(truncated, ==, "");
  dt_touch_editor_t *editor = dt_touch_editor_new(image_id, &message);
  g_assert_null(message); g_assert_nonnull(editor);
  char *information = dt_touch_editor_information(editor, &message);
  g_assert_null(message); g_assert_nonnull(information);
  g_assert_true(json_parser_load_from_data(parser, information, -1, &error));
  g_assert_no_error(error);
  JsonObject *metadata = json_node_get_object(json_parser_get_root(parser));
  g_assert_cmpint(json_object_get_int_member(metadata, "imgid"), ==, image_id);
  g_assert_cmpstr(json_object_get_string_member(metadata, "filename"), ==, "รูป.CR2");
  g_assert_nonnull(json_object_get_object_member(metadata, "camera"));
  g_assert_cmpfloat(json_object_get_double_member(json_object_get_object_member(metadata, "exif"), "iso"), >, 0);
  dt_touch_free(information);
  char *schema = dt_touch_editor_module(editor, "exposure", &message);
  g_assert_null(message); g_assert_nonnull(schema);
  JsonParser *schema_parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(schema_parser, schema, -1, &error));
  g_assert_no_error(error);
  JsonArray *fields = json_object_get_array_member(json_node_get_object(json_parser_get_root(schema_parser)), "fields");
  gboolean found_mode = FALSE;
  int automatic_mode = -1;
  for(guint i = 0; i < json_array_get_length(fields); i++)
  {
    JsonObject *field = json_array_get_object_element(fields, i);
    if(!g_strcmp0(json_object_get_string_member(field, "name"), "mode"))
    {
      found_mode = TRUE;
      g_assert_cmpint(json_node_get_value_type(json_object_get_member(field, "value")), ==, G_TYPE_INT64);
      g_assert_cmpuint(json_array_get_length(json_object_get_array_member(field, "values")), ==, 2);
      JsonArray *choices = json_object_get_array_member(field, "values");
      for(guint k = 0; k < json_array_get_length(choices); k++)
      {
        JsonObject *choice = json_array_get_object_element(choices, k);
        if(!strcmp(json_object_get_string_member(choice, "name"), "automatic"))
          automatic_mode = json_object_get_int_member(choice, "value");
      }
    }
  }
  g_assert_true(found_mode);
  g_assert_cmpint(automatic_mode, >=, 0);
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
  for(size_t i = 3; i < (size_t)tw * th * 4; i += 4) g_assert_cmpuint(thumbnail[i], ==, 255);
  dt_touch_free(thumbnail);
  g_assert_true(dt_touch_editor_set(editor, "exposure", "exposure", "0.5", &message));
  g_assert_null(message);
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &edited, &w2, &h2, &message));
  g_assert_cmpint(width, ==, w2); g_assert_cmpint(height, ==, h2);
  g_assert_true(memcmp(original, edited, (size_t)width * height * 4) != 0);
  dt_touch_state_t *edited_state = dt_touch_editor_capture(editor);
  char *rotation_schema = dt_touch_editor_module(editor, "flip", &message);
  g_assert_nonnull(rotation_schema); g_assert_null(message);
  g_assert_nonnull(strstr(rotation_schema, "precise angle"));
  g_free(rotation_schema);
  g_assert_true(dt_touch_editor_set(editor, "ashift", "rotation", "1.25", &message));
  uint8_t *rotated = NULL;
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &rotated, &w2, &h2, &message));
  g_assert_true(w2 != width || h2 != height || memcmp(edited, rotated, (size_t)w2 * h2 * 4) != 0);
  g_free(rotated);
  dt_touch_state_t *crop_before = dt_touch_editor_capture(editor);
  g_assert_true(dt_touch_editor_enable(editor, "crop", FALSE, &message));
  uint8_t *crop_preview = NULL;
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &crop_preview, &w2, &h2, &message)); g_free(crop_preview);
  float crop_bounds[4];
  g_assert_true(dt_touch_editor_auto_crop(editor, 1.5, crop_bounds, &message));
  g_assert_cmpfloat(crop_bounds[0], >, 0); g_assert_cmpfloat(crop_bounds[2], <, 1);
  const float frame[4] = {0.1f,0.1f,0.9f,0.9f};
  g_assert_true(dt_touch_editor_crop_apply(editor, frame, 0, 0, &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &crop_preview, &w2, &h2, &message)); g_free(crop_preview);
  g_assert_true(dt_touch_editor_enable(editor, "crop", FALSE, &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &crop_preview, &w2, &h2, &message)); g_free(crop_preview);
  g_assert_true(dt_touch_editor_crop_bounds(editor, crop_bounds, &message));
  for(int i = 0; i < 4; i++) g_assert_cmpfloat(fabsf(crop_bounds[i] - frame[i]), <, 0.002f);
  g_assert_true(dt_touch_editor_restore(editor, crop_before, &message));
  g_assert_true(dt_touch_editor_enable(editor, "crop", FALSE, &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &crop_preview, &w2, &h2, &message)); g_free(crop_preview);
  const float portrait_height = 0.4f * w2 / h2 * 1.5f;
  const float portrait[4] = {0.3f, (1-portrait_height)/2, 0.7f, (1+portrait_height)/2};
  g_assert_true(dt_touch_editor_crop_apply(editor, portrait, 2, 3, &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &crop_preview, &w2, &h2, &message)); g_free(crop_preview);
  g_assert_cmpfloat(fabsf((float)w2/h2 - 2.0f/3.0f), <, 0.02f);
  char *portrait_schema = dt_touch_editor_module(editor, "crop", &message);
  g_assert_true(json_parser_load_from_data(parser, portrait_schema, -1, &error));
  JsonArray *portrait_fields = json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)), "fields");
  for(guint i = 0; i < json_array_get_length(portrait_fields); ++i) {
    JsonObject *field = json_array_get_object_element(portrait_fields, i);
    if(!strcmp(json_object_get_string_member(field, "name"), "ratio_d")) g_assert_cmpint(json_object_get_int_member(field, "value"), ==, -3);
  }
  g_free(portrait_schema);
  g_assert_true(dt_touch_editor_restore(editor, crop_before, &message)); dt_touch_state_free(crop_before);
  g_assert_true(dt_touch_editor_reset(editor, "flip", &message));
  g_assert_true(dt_touch_editor_restore(editor, edited_state, &message));
  char *automatic_json = g_strdup_printf("%d", automatic_mode);
  g_assert_true(dt_touch_editor_set(editor, "exposure", "mode", automatic_json, &message));
  uint8_t *automatic = NULL, *brighter = NULL, *restored = NULL;
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &automatic, &w2, &h2, &message));
  dt_touch_state_t *automatic_state = dt_touch_editor_capture(editor);
  g_assert_true(dt_touch_editor_set(editor, "exposure", "deflicker_target_level", "-2.0", &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &brighter, &w2, &h2, &message));
  g_assert_cmpint(memcmp(automatic, brighter, (size_t)w2 * h2 * 4), !=, 0);
  g_assert_true(dt_touch_editor_restore(editor, automatic_state, &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &restored, &w2, &h2, &message));
  g_assert_cmpmem(automatic, (size_t)w2 * h2 * 4, restored, (size_t)w2 * h2 * 4);
  dt_touch_free(restored); restored = NULL;
  g_assert_true(dt_touch_editor_set(editor, "exposure", "deflicker_percentile", "90.0", &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &restored, &w2, &h2, &message));
  g_assert_cmpint(memcmp(automatic, restored, (size_t)w2 * h2 * 4), !=, 0);
  g_assert_false(dt_touch_editor_set(editor, "exposure", "exposure", "1.0", &message));
  g_assert_nonnull(message); g_free(message); message = NULL;
  dt_touch_free(automatic); dt_touch_free(brighter); dt_touch_free(restored);
  dt_touch_state_free(automatic_state);
  g_assert_true(dt_touch_editor_restore(editor, edited_state, &message));
  g_free(automatic_json);
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
  char *png = g_build_filename(folder, "automatic unavailable.png", NULL);
  g_assert_true(dt_touch_editor_export(editor, png, "png", &message));
  g_assert_null(message);
  dt_touch_editor_free(editor);
  char *png_import = dt_touch_import(png, &message);
  g_assert_nonnull(png_import); g_assert_null(message); g_free(png_import);
  dt_touch_editor_t *raster = dt_touch_editor_new(dt_touch_import_candidate(png), &message);
  g_assert_nonnull(raster); g_assert_null(message);
  char *raster_schema = dt_touch_editor_module(raster, "exposure", &message);
  g_assert_true(json_parser_load_from_data(parser, raster_schema, -1, &error));
  JsonArray *raster_fields = json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)), "fields");
  for(guint i = 0; i < json_array_get_length(raster_fields); i++)
  {
    JsonObject *field = json_array_get_object_element(raster_fields, i);
    if(!strcmp(json_object_get_string_member(field, "name"), "mode"))
      g_assert_false(json_object_get_boolean_member(field, "editable"));
  }
  char *mode_json = g_strdup_printf("%d", automatic_mode);
  g_assert_false(dt_touch_editor_set(raster, "exposure", "mode", mode_json, &message));
  g_assert_nonnull(message); g_free(message); message = NULL;
  g_free(mode_json); g_free(raster_schema);
  dt_touch_editor_free(raster);
  const int png_id = dt_touch_import_candidate(png);
  g_assert_true(dt_touch_image_action("reject", png_id, &message));
  g_assert_cmpint(_count("SELECT COUNT(*) FROM main.images WHERE flags & 8"), ==, 1);
  g_assert_true(dt_touch_image_action("unrate", png_id, &message));
  g_assert_cmpint(_count("SELECT COUNT(*) FROM main.images WHERE flags & 8"), ==, 0);
  g_assert_true(dt_touch_image_action("remove", png_id, &message));
  g_assert_false(dt_touch_image_exists(png_id));
  g_assert_true(g_file_test(png, G_FILE_TEST_IS_REGULAR));
  char *trash_path = g_build_filename(folder, "ลบ #%.png", NULL);
  GFile *trash_source = g_file_new_for_path(png), *trash_target = g_file_new_for_path(trash_path);
  g_assert_true(g_file_copy(trash_source, trash_target, G_FILE_COPY_NONE, NULL, NULL, NULL, &error));
  g_object_unref(trash_source); g_object_unref(trash_target);
  char *trash_import = dt_touch_import(trash_path, &message);
  g_assert_nonnull(trash_import); g_assert_null(message); g_free(trash_import);
  const int trash_id = dt_touch_import_candidate(trash_path);
  const int duplicate = dt_image_duplicate(trash_id);
  g_assert_cmpint(duplicate, >, 0);
  g_assert_true(dt_touch_image_action("trash", duplicate, &message));
  g_assert_null(message); g_assert_true(g_file_test(trash_path, G_FILE_TEST_IS_REGULAR));
  g_assert_true(dt_touch_image_action("trash", trash_id, &message));
  g_assert_null(message); g_assert_false(g_file_test(trash_path, G_FILE_TEST_EXISTS));
  g_assert_false(dt_touch_image_exists(trash_id));
  g_free(trash_path); g_free(png);
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
  printf("passed: native collections, filters, selection, sorting, persistent Touch preview, and Unicode session handoff\n");
  g_free(filename); g_free(cache); g_free(config); g_free(folder); g_free(root);
  return 0;
}
