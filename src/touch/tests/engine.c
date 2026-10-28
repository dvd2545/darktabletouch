/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/engine_bridge.h"
#include "common/touch_session.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/mipmap_cache.h"
#include "common/exif.h"
#include "common/collection.h"
#include "common/database.h"
#include "control/conf.h"
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <string.h>
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
  if(_count("SELECT COUNT(*) FROM memory.collected_images") != count)
    g_printerr("collection mismatch: %s\nquery: %s\n", text, dt_collection_get_query(darktable.collection));
  g_assert_cmpint(_count("SELECT COUNT(*) FROM memory.collected_images"), ==, count);
  g_free(text);
  json_node_free(root);
}

static void _test_library(const char *filename, const char *folder, int first)
{
  char *other_folder = g_build_filename(folder, "second folder", NULL);
  g_mkdir_with_parents(other_folder, 0700);
  char *basename = g_path_get_basename(filename);
  const char *extension = strrchr(basename, '.');
  char *other_name = g_strconcat("second #%", extension ? extension : "", NULL);
  char *other = g_build_filename(other_folder, other_name, NULL);
  g_free(other_name);
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
  // rating choices exclude individual buckets while retaining the base collection
  g_assert_true(dt_touch_image_action("reject", first, &error));
  json_array_add_object_element(filters, _rule(DT_COLLECTION_PROP_RATING, "=-1", 2, 0));
  _apply_collection(settings, 1);
  g_assert_cmpint(_count("SELECT imgid FROM memory.collected_images"), ==, second);
  json_array_remove_element(filters, 0);
  json_array_add_object_element(filters, _rule(DT_COLLECTION_PROP_RATING, ">=0", 2, 0));
  _apply_collection(settings, 1);
  g_assert_cmpint(_count("SELECT imgid FROM memory.collected_images"), ==, first);
  json_array_remove_element(filters, 0);
  g_assert_true(dt_touch_image_action("unrate", first, &error));
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
  json_array_add_object_element(rules, _rule(DT_COLLECTION_PROP_FILENAME, basename, 0, 0));
  g_free(basename);
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

static double _exposure(dt_touch_editor_t *editor)
{
  char *error = NULL, *text = dt_touch_editor_module(editor, "exposure", &error);
  g_assert_null(error); g_assert_nonnull(text);
  JsonParser *parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(parser, text, -1, NULL));
  JsonArray *fields = json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)), "fields");
  double value = NAN;
  for(guint i = 0; i < json_array_get_length(fields); i++) {
    JsonObject *field = json_array_get_object_element(fields, i);
    if(!strcmp(json_object_get_string_member(field, "name"), "exposure"))
      value = json_object_get_double_member(field, "value");
  }
  g_object_unref(parser); g_free(text);
  return value;
}

static void _test_save_recovery(dt_touch_editor_t *editor, int image, const char *filename)
{
  char *error = NULL;
  dt_touch_state_t *before = dt_touch_editor_capture(editor);
  g_assert_true(dt_touch_editor_save(editor, &error));
  char *sidecar = g_strconcat(filename, ".xmp", NULL);
  char *backup = g_strconcat(sidecar, ".backup", NULL);
  g_assert_cmpint(g_rename(sidecar, backup), ==, 0);
  g_assert_cmpint(g_mkdir(sidecar, 0700), ==, 0);
  g_assert_true(dt_touch_editor_set(editor, "exposure", "exposure", "0.85", &error));
  const int protected = dt_touch_editor_save(editor, &error);
  g_assert_cmpstr(error, ==, NULL);
  g_assert_true(protected);
  char *information = dt_touch_editor_information(editor, &error);
  JsonParser *parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(parser, information, -1, NULL));
  char *recovery = g_strdup(json_object_get_string_member(json_node_get_object(json_parser_get_root(parser)), "recovery_sidecar"));
  g_free(information);
  g_assert_true(g_file_test(recovery, G_FILE_TEST_IS_REGULAR));
  const int duplicate = dt_image_duplicate(image);
  dt_image_t *img = dt_image_cache_get(duplicate, 'w');
  g_assert_false(dt_exif_xmp_read(img, recovery, TRUE));
  dt_image_cache_write_release(img, DT_IMAGE_CACHE_RELAXED);
  dt_touch_editor_t *restored = dt_touch_editor_new(duplicate, &error);
  g_assert_cmpfloat(_exposure(restored), ==, (double)(float)0.85);
  dt_touch_editor_free(restored);
  g_assert_true(dt_touch_image_action("remove", duplicate, &error));
  char *last_good = g_strconcat(recovery, ".backup", NULL);
  g_assert_cmpint(g_rename(recovery, last_good), ==, 0);
  g_assert_cmpint(g_mkdir(recovery, 0700), ==, 0);
  g_assert_true(dt_touch_editor_set(editor, "exposure", "exposure", "0.95", &error));
  g_assert_false(dt_touch_editor_save(editor, &error));
  g_assert_nonnull(error); g_clear_pointer(&error, g_free);
  g_assert_cmpfloat(_exposure(editor), ==, (double)(float)0.95);
  g_assert_true(g_file_test(last_good, G_FILE_TEST_IS_REGULAR));
  g_assert_cmpint(g_rmdir(recovery), ==, 0);
  g_assert_cmpint(g_rename(last_good, recovery), ==, 0);
  g_assert_true(dt_touch_editor_set(editor, "exposure", "exposure", "0.96", &error));
  g_assert_cmpint(sqlite3_exec(dt_database_get(darktable.db), "PRAGMA query_only=ON", NULL, NULL, NULL), ==, SQLITE_OK);
  g_assert_false(dt_touch_editor_save(editor, &error));
  g_assert_nonnull(error); g_clear_pointer(&error, g_free);
  g_assert_cmpint(sqlite3_exec(dt_database_get(darktable.db), "PRAGMA query_only=OFF", NULL, NULL, NULL), ==, SQLITE_OK);
  g_assert_cmpfloat(_exposure(editor), ==, (double)(float)0.96);
  g_assert_cmpint(g_rmdir(sidecar), ==, 0);
  g_assert_cmpint(g_rename(backup, sidecar), ==, 0);
  char *offline = g_strconcat(filename, ".offline", NULL);
  g_assert_cmpint(g_rename(filename, offline), ==, 0);
  g_assert_true(dt_touch_editor_save(editor, &error));
  g_assert_null(error);
  g_assert_cmpint(g_rename(offline, filename), ==, 0);
  g_assert_true(dt_touch_editor_save(editor, &error));
  information = dt_touch_editor_information(editor, &error);
  g_assert_true(json_parser_load_from_data(parser, information, -1, NULL));
  g_assert_cmpstr(json_object_get_string_member(json_node_get_object(json_parser_get_root(parser)), "recovery_sidecar"), ==, "");
  g_free(information); g_object_unref(parser);
  g_assert_true(dt_touch_editor_restore(editor, before, &error));
  g_assert_true(dt_touch_editor_save(editor, &error));
  dt_touch_state_free(before);
  g_free(offline); g_free(last_good); g_free(recovery); g_free(backup); g_free(sidecar);
}

static void _test_history(dt_touch_editor_t *source, int source_id)
{
  char *error = NULL;
  dt_touch_state_t *original = dt_touch_editor_capture(source);
  g_assert_true(dt_touch_editor_set(source, "exposure", "exposure", "0.5", &error));
  g_assert_true(dt_touch_editor_enable(source, "exposure", FALSE, &error));
  g_assert_true(dt_touch_editor_set(source, "ashift", "rotation", "1.0", &error));
  g_assert_true(dt_touch_editor_set(source, "exposure", "exposure", "1.25", &error));
  g_assert_true(dt_touch_editor_save(source, &error));
  dt_touch_state_t *modified = dt_touch_editor_capture(source);
  const int target_id = dt_image_duplicate(source_id);
  dt_touch_editor_t *target = dt_touch_editor_new(target_id, &error);
  g_assert_nonnull(target); g_assert_null(error);
  g_assert_true(dt_touch_editor_set(target, "exposure", "exposure", "0.25", &error));
  g_assert_true(dt_touch_editor_save(target, &error));
  char *agx = dt_touch_editor_module(target, "agx", &error);
  char *clipboard = dt_touch_history_clipboard(source_id, &error);
  JsonParser *parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(parser, clipboard, -1, NULL));
  JsonObject *descriptor = json_node_get_object(json_parser_get_root(parser));
  char *token = g_strdup(json_object_get_string_member(descriptor, "token"));
  int exposure_index = 0;
  JsonArray *steps = json_object_get_array_member(descriptor, "steps");
  for(guint i = 0; i < json_array_get_length(steps); i++) {
    JsonObject *step = json_array_get_object_element(steps, i);
    if(!json_object_get_boolean_member(step, "latest"))
      g_assert_false(json_object_get_boolean_member(step, "selectable"));
    if(!strcmp(json_object_get_string_member(step, "operation"), "exposure")
       && json_object_get_boolean_member(step, "latest")) exposure_index = json_object_get_int_member(step, "index");
  }
  g_assert_cmpint(exposure_index, >, 0);
  char *request = g_strdup_printf("{\"source\":%d,\"token\":\"%s\",\"steps\":[{\"index\":%d}],\"merge\":true,\"targets\":[%d]}",
                                  source_id, token, exposure_index, target_id);
  dt_touch_catalog_edit_t *paste = dt_touch_history_apply(request, &error);
  g_assert_nonnull(paste); g_assert_null(error);
  g_assert_true(dt_touch_editor_reload(target, &error));
  g_assert_cmpfloat(_exposure(target), ==, 1.25);
  char *agx_after = dt_touch_editor_module(target, "agx", &error);
  g_assert_cmpstr(agx, ==, agx_after); g_free(agx_after); g_free(agx);
  g_assert_true(dt_touch_catalog_restore(paste, FALSE, &error));
  g_assert_true(dt_touch_editor_reload(target, &error));
  g_assert_cmpfloat(_exposure(target), ==, 0.25);
  g_assert_true(dt_touch_catalog_restore(paste, TRUE, &error));
  g_assert_true(dt_touch_editor_reload(target, &error));
  g_assert_cmpfloat(_exposure(target), ==, 1.25);
  dt_touch_catalog_free(paste);
  g_assert_true(dt_touch_editor_set(target, "ashift", "rotation", "2.0", &error));
  g_assert_true(dt_touch_editor_save(target, &error));
  char *replace = g_strdup_printf("{\"source\":%d,\"token\":\"%s\",\"steps\":[{\"index\":%d}],\"merge\":false,\"copy_order\":true,\"targets\":[%d]}",
                                  source_id, token, exposure_index, target_id);
  paste = dt_touch_history_apply(replace, &error);
  g_assert_nonnull(paste); g_assert_null(error);
  g_assert_true(dt_touch_editor_reload(target, &error));
  g_assert_cmpfloat(_exposure(target), ==, 1.25);
  char *query = g_strdup_printf("SELECT COUNT(*) FROM main.history WHERE imgid=%d AND operation='ashift'", target_id);
  g_assert_cmpint(_count(query), ==, 0);
  g_assert_true(dt_touch_catalog_restore(paste, FALSE, &error));
  g_assert_cmpint(_count(query), >, 0);
  g_assert_true(dt_touch_editor_reload(target, &error));
  dt_touch_catalog_free(paste); g_free(replace); g_free(query);
  g_assert_true(dt_touch_editor_set(source, "exposure", "exposure", "1.5", &error));
  g_assert_true(dt_touch_editor_save(source, &error));
  g_assert_null(dt_touch_history_apply(request, &error));
  g_assert_nonnull(error); g_clear_pointer(&error, g_free);
  g_assert_true(dt_touch_editor_restore(source, modified, &error));
  g_assert_true(dt_touch_editor_save(source, &error));
  g_assert_true(dt_touch_editor_enable(source, "exposure", FALSE, &error));
  g_assert_true(dt_touch_editor_save(source, &error));
  char *disabled_clipboard = dt_touch_history_clipboard(source_id, &error);
  JsonParser *disabled_parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(disabled_parser, disabled_clipboard, -1, NULL));
  const char *disabled_token = json_object_get_string_member(json_node_get_object(json_parser_get_root(disabled_parser)), "token");
  char *disabled_request = g_strdup_printf("{\"source\":%d,\"token\":\"%s\",\"steps\":[{\"index\":%d}],\"merge\":true,\"targets\":[%d]}",
                                           source_id, disabled_token, exposure_index, target_id);
  g_assert_true(dt_touch_editor_set(source, "exposure", "exposure", "2.0", &error));
  g_assert_true(dt_touch_editor_enable(source, "exposure", FALSE, &error));
  g_assert_true(dt_touch_editor_save(source, &error));
  g_assert_null(dt_touch_history_apply(disabled_request, &error));
  g_assert_nonnull(error); g_clear_pointer(&error, g_free);
  g_free(disabled_clipboard); g_free(disabled_request); g_object_unref(disabled_parser);
  g_assert_true(dt_touch_editor_restore(source, modified, &error));
  g_assert_true(dt_touch_editor_save(source, &error));
  g_free(request);
  request = g_strdup_printf("{\"reset\":true,\"targets\":[%d]}", source_id);
  dt_touch_catalog_edit_t *reset = dt_touch_history_apply(request, &error);
  g_assert_nonnull(reset); g_assert_null(error);
  g_assert_true(dt_touch_editor_reload(source, &error));
  g_assert_cmpfloat(_exposure(source), !=, 1.25);
  g_assert_true(dt_touch_catalog_restore(reset, FALSE, &error));
  g_assert_true(dt_touch_editor_reload(source, &error));
  g_assert_cmpfloat(_exposure(source), ==, 1.25);
  g_assert_true(dt_touch_editor_restore(source, original, &error));
  g_assert_true(dt_touch_editor_restore(source, modified, &error));
  g_assert_cmpfloat(_exposure(source), ==, 1.25);
  dt_touch_catalog_free(reset); g_free(request);
  request = g_strdup_printf("{\"reset\":true,\"targets\":[%d,%d]}", source_id, target_id);
  reset = dt_touch_history_apply(request, &error);
  g_assert_nonnull(reset); g_assert_null(error);
  char source_path[PATH_MAX] = {0};
  dt_image_full_path(source_id, source_path, sizeof(source_path), NULL);
  dt_image_path_append_version(source_id, source_path, sizeof(source_path));
  char *sidecar = g_strconcat(source_path, ".xmp", NULL);
  char *backup = g_strconcat(sidecar, ".backup", NULL);
  g_assert_cmpint(g_rename(sidecar, backup), ==, 0);
  g_assert_cmpint(g_mkdir(sidecar, 0700), ==, 0);
  g_assert_cmpint(dt_touch_catalog_restore(reset, FALSE, &error), ==, 1);
  g_assert_null(error);
  g_assert_true(dt_touch_editor_reload(source, &error));
  g_assert_true(dt_touch_editor_reload(target, &error));
  g_assert_cmpfloat(_exposure(source), ==, 1.25);
  g_assert_cmpfloat(_exposure(target), ==, 1.25);
  g_assert_cmpint(g_rmdir(sidecar), ==, 0);
  g_assert_cmpint(g_rename(backup, sidecar), ==, 0);
  g_free(sidecar); g_free(backup); dt_touch_catalog_free(reset);
  reset = dt_touch_history_apply(request, &error);
  g_assert_nonnull(reset); g_assert_null(error);
  g_assert_true(dt_touch_editor_reload(source, &error));
  const double reset_exposure = _exposure(source);
  dt_touch_editor_free(target);
  g_assert_true(dt_touch_image_action("remove", target_id, &error));
  g_assert_false(dt_touch_catalog_restore(reset, FALSE, &error));
  g_assert_nonnull(error); g_clear_pointer(&error, g_free);
  g_assert_true(dt_touch_editor_reload(source, &error));
  g_assert_cmpfloat(_exposure(source), ==, reset_exposure);
  dt_touch_catalog_free(reset);
  g_assert_true(dt_touch_editor_restore(source, original, &error));
  g_assert_true(dt_touch_editor_save(source, &error));
  dt_touch_state_free(original); dt_touch_state_free(modified);
  g_assert_cmpint(_count("SELECT COUNT(*) FROM memory.snapshot_history"), ==, 0);
  g_assert_cmpint(_count("SELECT COUNT(*) FROM memory.snapshot_masks_history"), ==, 0);
  g_assert_cmpint(_count("SELECT COUNT(*) FROM memory.snapshot_module_order"), ==, 0);
  g_object_unref(parser); g_free(clipboard); g_free(request); g_free(token);
}

static void _test_preview_cache(int image)
{
  char *error = NULL;
  dt_touch_editor_t *reference = dt_touch_editor_new(image, &error);
  dt_touch_editor_t *cached = dt_touch_editor_new(image, &error);
  g_assert_null(error);
  g_assert_nonnull(reference);
  g_assert_nonnull(cached);
  dt_touch_render_t *interactive = dt_touch_render_new();
  for(int step = 0; step < 4; step++)
  {
    const char *value = step % 2 ? "0.5" : "-0.5";
    g_assert_true(dt_touch_editor_set(reference, "exposure", "exposure", value, &error));
    g_assert_true(dt_touch_editor_set(cached, "exposure", "exposure", value, &error));
    uint8_t *expected = NULL, *actual = NULL;
    int ew, eh, aw, ah;
    g_assert_true(dt_touch_editor_interactive(cached, 640, &actual, &aw, &ah, interactive, &error));
    g_assert_cmpint(MAX(aw, ah), <=, 640);
    g_assert_cmpint(MIN(aw, ah), >, 0);
    dt_touch_free(actual); actual = NULL;
    g_setenv("DARKTABLETOUCH_DISABLE_PIPE_CACHE", "1", TRUE);
    g_assert_true(dt_touch_editor_preview(reference, 1280, 1280, &expected, &ew, &eh, &error));
    g_unsetenv("DARKTABLETOUCH_DISABLE_PIPE_CACHE");
    g_assert_true(dt_touch_editor_preview(cached, 1280, 1280, &actual, &aw, &ah, &error));
    g_assert_null(error);
    g_assert_cmpint(ew, ==, aw);
    g_assert_cmpint(eh, ==, ah);
    if(memcmp(expected, actual, (size_t)ew * eh * 4))
    {
      int changed = 0, maximum = 0;
      for(size_t i = 0; i < (size_t)ew * eh * 4; i++)
      {
        const int delta = abs((int)expected[i] - actual[i]);
        changed += delta != 0;
        maximum = MAX(maximum, delta);
      }
      g_printerr("cache parity step %d: %d changed bytes, maximum difference %d\n", step, changed, maximum);
    }
    g_assert_cmpmem(expected, (size_t)ew * eh * 4, actual, (size_t)aw * ah * 4);
    dt_touch_free(expected);
    dt_touch_free(actual);
  }
  dt_touch_render_free(interactive);
  g_assert_true(dt_touch_editor_set(reference, "exposure", "exposure", "0.75", &error));
  g_assert_true(dt_touch_editor_set(cached, "exposure", "exposure", "0.75", &error));
  dt_touch_editor_park(cached);
  uint8_t *parked_expected = NULL, *parked_actual = NULL;
  int pw, ph, qw, qh;
  g_assert_true(dt_touch_editor_preview(reference, 1280, 1280, &parked_expected, &pw, &ph, &error));
  g_assert_true(dt_touch_editor_preview(cached, 1280, 1280, &parked_actual, &qw, &qh, &error));
  g_assert_cmpint(pw, ==, qw); g_assert_cmpint(ph, ==, qh);
  g_assert_cmpmem(parked_expected, (size_t)pw * ph * 4, parked_actual, (size_t)qw * qh * 4);
  dt_touch_free(parked_expected); dt_touch_free(parked_actual);
  g_assert_true(dt_touch_editor_set(reference, "exposure", "exposure", "0.5", &error));
  g_assert_true(dt_touch_editor_set(cached, "exposure", "exposure", "0.5", &error));
  g_assert_true(dt_touch_editor_enable(reference, "agx", FALSE, &error));
  g_assert_true(dt_touch_editor_enable(cached, "agx", FALSE, &error));
  g_assert_true(dt_touch_editor_enable(reference, "exposure", TRUE, &error));
  g_assert_true(dt_touch_editor_enable(cached, "exposure", TRUE, &error));
  g_assert_true(dt_touch_editor_preview(reference, 1280, 1280, &parked_expected, &pw, &ph, &error));
  g_assert_true(dt_touch_editor_preview(cached, 1280, 1280, &parked_actual, &qw, &qh, &error));
  g_assert_cmpmem(parked_expected, (size_t)pw * ph * 4, parked_actual, (size_t)qw * qh * 4);
  dt_touch_free(parked_expected); dt_touch_free(parked_actual);
  dt_touch_render_t *render = dt_touch_render_new();
  dt_touch_render_cancel(render);
  uint8_t *pixels = NULL;
  int width, height;
  g_assert_false(dt_touch_editor_render(cached, 640, 640, &pixels, &width, &height, render, &error));
  g_assert_null(error);
  g_assert_null(pixels);
  dt_touch_render_reset(render);
  g_assert_true(dt_touch_editor_render(cached, 640, 640, &pixels, &width, &height, render, &error));
  g_assert_null(error);
  dt_touch_free(pixels);
  dt_touch_render_free(render);
  dt_touch_editor_trim(cached, 0);
  pixels = NULL;
  g_assert_true(dt_touch_editor_preview(cached, 1280, 1280, &pixels, &width, &height, &error));
  g_assert_null(error);
  dt_touch_free(pixels);
  dt_touch_editor_free(cached);
  dt_touch_editor_free(reference);
  g_print("passed: cached preview parity, cancellation, resize, and cache eviction\n");
}

static void _persistent_write(void *data, int operation, const char *database,
                               const char *table, sqlite3_int64 row)
{
  (void)row;
  if(!strcmp(database, "main")
     && (!strcmp(table, "history") || !strcmp(table, "masks_history")
         || !strcmp(table, "module_order") || !strcmp(table, "history_hash")))
  {
    g_printerr("unexpected preload write: %d %s.%s\n", operation, database, table);
    g_atomic_int_inc((gint *)data);
  }
}

static void _test_input_prefetch(int image)
{
  if(dt_touch_memory_available() < (UINT64_C(2) << 30))
  {
    g_print("skipped: RAW prefetch while available memory is below 2 GiB\n");
    return;
  }
  dt_mipmap_buffer_t input = { 0 };
  dt_mipmap_cache_get(&input, image, DT_MIPMAP_F, DT_MIPMAP_BLOCKING, 'r');
  g_assert_nonnull(input.buf);
  dt_mipmap_cache_release(&input);
  dt_mipmap_cache_evict_at_size(image, DT_MIPMAP_FULL);
  dt_touch_render_t *render = dt_touch_render_new();
  // input loading needs this metadata lock, so an inline decode cannot return
  dt_image_t *metadata = dt_image_cache_get(image, 'w');
  g_assert_false(dt_touch_image_prefetch(image, TRUE, render));
  dt_image_cache_write_release(metadata, DT_IMAGE_CACHE_RELAXED);
  const gint64 deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
  while(!dt_touch_image_prefetch(image, FALSE, render) && g_get_monotonic_time() < deadline)
    g_usleep(10000);
  g_assert_true(dt_touch_image_prefetch(image, FALSE, render));
  dt_touch_render_stop_inputs(render);
  dt_touch_render_free(render);
  g_print("passed: asynchronous RAW prefetch with an existing float mipmap\n");
}

static void _test_preload(int image)
{
  char *error = NULL;
  dt_touch_editor_t *reference = dt_touch_editor_new(image, &error);
  g_assert_nonnull(reference);
  g_assert_true(dt_touch_editor_save(reference, &error));
  dt_touch_state_t *held = dt_touch_editor_capture(reference);
  const int duplicate = dt_image_duplicate(image);
  g_assert_cmpint(duplicate, >, 0);
  dt_touch_editor_t *initialized = dt_touch_editor_new(duplicate, &error);
  g_assert_nonnull(initialized);
  g_assert_true(dt_touch_editor_save(initialized, &error));
  char *before = dt_touch_image_stamp(image);
  char *target_before = dt_touch_image_stamp(duplicate);
  const int snapshots = _count("SELECT COUNT(*) FROM memory.snapshot_history");
  gint writes = 0;
  sqlite3_update_hook(dt_database_get(darktable.db), _persistent_write, &writes);
  g_assert_true(dt_touch_editor_save_changed(initialized, &error));
  dt_touch_editor_t *preloaded = dt_touch_editor_preload(duplicate);
  sqlite3_update_hook(dt_database_get(darktable.db), NULL, NULL);
  g_assert_cmpint(writes, ==, 0);
  g_assert_nonnull(preloaded);
  char *after = dt_touch_image_stamp(image);
  g_assert_cmpstr(before, ==, after);
  char *target_after = dt_touch_image_stamp(duplicate);
  g_assert_cmpstr(target_before, ==, target_after);
  g_assert_cmpint(snapshots, ==, _count("SELECT COUNT(*) FROM memory.snapshot_history"));
  uint8_t *expected = NULL, *actual = NULL;
  int ew, eh, aw, ah;
  g_assert_true(dt_touch_editor_preview(initialized, 1280, 1280, &expected, &ew, &eh, &error));
  g_assert_true(dt_touch_editor_preview(preloaded, 1280, 1280, &actual, &aw, &ah, &error));
  g_assert_null(error);
  g_assert_cmpint(ew, ==, aw);
  g_assert_cmpint(eh, ==, ah);
  g_assert_cmpmem(expected, (size_t)ew * eh * 4, actual, (size_t)aw * ah * 4);
  dt_touch_free(expected); dt_touch_free(actual);
  dt_touch_editor_park(preloaded);
  g_assert_cmpuint(dt_touch_editor_cache_bytes(preloaded), ==, 0);
  dt_touch_editor_free(reference); dt_touch_editor_free(preloaded);
  dt_touch_editor_free(initialized);
  dt_touch_state_free(held);
  dt_image_remove(duplicate);
  g_free(before); g_free(after);
  g_free(target_before); g_free(target_after);
  g_print("passed: read-only preload parity and snapshot cleanup\n");
}

int main(int argc, char **argv)
{
  if(argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--save-recovery-only")))
  { fprintf(stderr, "usage: test-touch-engine RAW_FILE [--save-recovery-only]\n"); return 2; }
  GError *error = NULL;
  char *root = g_dir_make_tmp("darktabletouch-engine-XXXXXX", &error);
  g_assert_no_error(error);
  char *folder = g_build_filename(root, "ผู้ใช้", NULL);
  char *cache = g_build_filename(folder, "แคช", NULL);
  char *config = g_build_filename(folder, "ตั้งค่า", NULL);
  g_mkdir_with_parents(folder, 0700);
  char *basename = g_path_get_basename(argv[1]);
  char *test_name = g_strconcat("รูป-", basename, NULL);
  char *filename = g_build_filename(folder, test_name, NULL);
  g_free(basename);
  GFile *source = g_file_new_for_path(argv[1]);
  GFile *target = g_file_new_for_path(filename);
  g_assert_true(g_file_copy(source, target, G_FILE_COPY_NONE, NULL, NULL, NULL, &error));
  g_assert_no_error(error);
  g_object_unref(source); g_object_unref(target);
  GPtrArray *args = g_ptr_array_new();
  const char *test_cache = g_getenv("DARKTABLETOUCH_TEST_OPENCL_CACHE");
  const char *base_args[] = { argv[0], "--configdir", config, "--cachedir", test_cache ? test_cache : cache,
    "--conf", "host_memory_limit=2048", "--conf", "worker_threads=2", "-t", "2" };
  for(size_t i = 0; i < G_N_ELEMENTS(base_args); i++) g_ptr_array_add(args, (gpointer)base_args[i]);
  if(test_cache)
  {
    g_ptr_array_add(args, "--conf"); g_ptr_array_add(args, "clplatform_amdacceleratedparallelprocessing=true");
    g_ptr_array_add(args, "--conf"); g_ptr_array_add(args, "opencl=true");
    g_ptr_array_add(args, "-d"); g_ptr_array_add(args, "opencl");
  }
  else g_ptr_array_add(args, "--disable-opencl");
  g_ptr_array_add(args, NULL);
  g_assert_true(dt_touch_initialize(args->len - 1, (char **)args->pdata));
  g_ptr_array_free(args, TRUE);
  g_assert_true(dt_touch_import_is_raw("folder/IMAGE.CR3"));
  g_assert_true(dt_touch_import_is_raw("folder/image.dng"));
  g_assert_false(dt_touch_import_is_raw("folder/image.jpg"));
  g_assert_false(dt_touch_import_is_raw("folder/image.xmp"));
  dt_touch_quick_actions_set(TRUE, TRUE);
  char *settings_json = dt_touch_library();
  g_assert_nonnull(strstr(settings_json, "\"quick_reject\":true"));
  g_assert_nonnull(strstr(settings_json, "\"quick_delete\":true"));
  g_free(settings_json);
  dt_touch_quick_actions_set(FALSE, FALSE);
  dt_touch_ui_options_set(TRUE, 125, FALSE, 1920);
  settings_json = dt_touch_library();
  g_assert_nonnull(strstr(settings_json, "\"scale_override\":true"));
  g_assert_nonnull(strstr(settings_json, "\"preview_quality\":1920"));
  g_assert_nonnull(strstr(settings_json, "EXIF.ISO"));
  g_assert_nonnull(strstr(settings_json, "CATEGORY_EACH[,]"));
  g_free(settings_json);
  dt_touch_ui_options_set(FALSE, 100, TRUE, 1280);
  char *message = NULL;
  char *imported = dt_touch_import(filename, &message);
  g_assert_null(message); g_assert_nonnull(imported);
  JsonParser *parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(parser, imported, -1, &error));
  g_assert_no_error(error);
  JsonArray *images = json_object_get_array_member(json_node_get_object(json_parser_get_root(parser)), "images");
  const int image_id = json_object_get_int_member(json_array_get_object_element(images, 0), "imgid");
  _test_preview_cache(image_id);
  _test_input_prefetch(image_id);
  _test_preload(image_id);
  char *export_name = dt_touch_export_name(image_id, "$(FILE.NAME)_$(IMAGE.ID)_$(SEQUENCE[4,1])", 7, 1920, 1080, FALSE);
  char *expected_suffix = g_strdup_printf("_%d_0007", image_id);
  g_assert_nonnull(export_name);
  g_assert_true(g_str_has_suffix(export_name, expected_suffix));
  g_free(export_name); g_free(expected_suffix);
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
  const int source_extent = json_object_get_int_member(metadata, "preview_max_extent");
  g_assert_cmpstr(json_object_get_string_member(metadata, "filename"), ==, test_name);
  g_free(test_name);
  g_assert_nonnull(json_object_get_object_member(metadata, "camera"));
  g_assert_cmpfloat(json_object_get_double_member(json_object_get_object_member(metadata, "exif"), "iso"), >, 0);
  dt_touch_free(information);
  if(argc > 2 && !strcmp(argv[2], "--save-recovery-only"))
  {
    _test_save_recovery(editor, image_id, filename);
    dt_touch_editor_free(editor);
    dt_touch_shutdown();
    printf("passed: sidecar recovery, XMP restore, failed recovery preservation, offline save, and retry\n");
    return 0;
  }
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
  if(source_extent > 4096)
  {
    uint8_t *detailed = NULL;
    int detailed_width, detailed_height;
    g_assert_true(dt_touch_editor_preview(editor, source_extent, source_extent,
                                          &detailed, &detailed_width, &detailed_height, &message));
    g_assert_null(message);
    g_assert_cmpint(MAX(detailed_width, detailed_height), >, MAX(width, height));
    g_assert_cmpint(MAX(detailed_width, detailed_height), <=, source_extent);
    g_print("source-resolution RAW preview after crop: %d x %d\n", detailed_width, detailed_height);
    dt_touch_free(detailed);
  }
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
  g_assert_nonnull(strstr(rotation_schema, "constrain rotation"));
  g_free(rotation_schema);
  g_assert_true(dt_touch_editor_set(editor, "ashift", "rotation", "1.25", &message));
  uint8_t *rotated = NULL;
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &rotated, &w2, &h2, &message));
  g_assert_true(w2 != width || h2 != height || memcmp(edited, rotated, (size_t)w2 * h2 * 4) != 0);
  const int rotated_width = w2, rotated_height = h2;
  g_assert_true(dt_touch_editor_set(editor, "ashift", "cropmode", "2", &message));
  uint8_t *constrained = NULL;
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &constrained, &w2, &h2, &message));
  g_assert_null(message);
  g_assert_cmpfloat(fabs((double)w2 / h2 - (double)width / height), <, 0.01);
  const int constrained_width = w2, constrained_height = h2;
  g_assert_true(dt_touch_editor_set(editor, "ashift", "rotation", "7.5", &message));
  uint8_t *changed_rotation = NULL;
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &changed_rotation, &w2, &h2, &message));
  g_assert_cmpfloat(fabs((double)w2 / h2 - (double)width / height), <, 0.01);
  g_free(changed_rotation);
  char *rotation_export = g_build_filename(folder, "constrained rotation.png", NULL);
  g_assert_true(dt_touch_editor_save(editor, &message));
  g_assert_true(dt_touch_export_image(image_id, rotation_export, "png", 512, 512, 95, 8, FALSE, TRUE, TRUE, &message));
  int export_width = 0, export_height = 0;
  g_assert_nonnull(gdk_pixbuf_get_file_info(rotation_export, &export_width, &export_height));
  g_assert_cmpfloat(fabs((double)export_width / export_height - (double)width / height), <, 0.01);
  g_assert_cmpint(g_unlink(rotation_export), ==, 0);
  g_free(rotation_export);
  g_assert_true(dt_touch_editor_set(editor, "ashift", "rotation", "1.25", &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &changed_rotation, &w2, &h2, &message));
  g_assert_cmpint(w2, ==, constrained_width); g_assert_cmpint(h2, ==, constrained_height);
  g_assert_cmpmem(constrained, (size_t)w2 * h2 * 4, changed_rotation, (size_t)w2 * h2 * 4);
  g_free(changed_rotation); g_free(constrained);
  g_assert_true(dt_touch_editor_set(editor, "ashift", "cropmode", "0", &message));
  g_assert_true(dt_touch_editor_preview(editor, 512, 512, &changed_rotation, &w2, &h2, &message));
  g_assert_cmpint(w2, ==, rotated_width); g_assert_cmpint(h2, ==, rotated_height);
  g_assert_cmpmem(rotated, (size_t)w2 * h2 * 4, changed_rotation, (size_t)w2 * h2 * 4);
  g_free(changed_rotation); g_free(rotated);
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
  _test_save_recovery(editor, image_id, filename);
  _test_history(editor, image_id);
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
  char *large_path = g_build_filename(folder, "large preview.png", NULL);
  GdkPixbuf *large = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 5008, 128);
  gdk_pixbuf_fill(large, 0x8090a0ff);
  g_assert_true(gdk_pixbuf_save(large, large_path, "png", &error, NULL));
  g_object_unref(large);
  char *large_import = dt_touch_import(large_path, &message);
  g_assert_nonnull(large_import); g_assert_null(message); g_free(large_import);
  dt_touch_editor_t *large_editor = dt_touch_editor_new(dt_touch_import_candidate(large_path), &message);
  g_assert_nonnull(large_editor); g_assert_null(message);
  uint8_t *large_pixels = NULL;
  int large_width, large_height;
  g_assert_true(dt_touch_editor_preview(large_editor, 5008, 5008, &large_pixels, &large_width, &large_height, &message));
  g_assert_cmpint(large_width, ==, 5008); g_assert_cmpint(large_height, ==, 128);
  g_print("uncapped preview: %d x %d\n", large_width, large_height);
  dt_touch_free(large_pixels); dt_touch_editor_free(large_editor); g_free(large_path);
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
  printf("passed: native collections, filters, selection, sorting, preview, selective history merge/replace, reset, reload-safe undo/redo, snapshot cleanup, and Unicode session handoff\n");
  g_free(filename); g_free(cache); g_free(config); g_free(folder); g_free(root);
  return 0;
}
