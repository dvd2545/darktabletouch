/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "common/touch_session.h"
#include "common/collection.h"
#include "common/darktable.h"
#include "common/database.h"
#include "common/debug.h"
#include "common/image.h"
#include "common/image_cache.h"
#include "common/iop_order.h"
#include "common/history_snapshot.h"
#include "common/selection.h"
#include "control/conf.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/masks.h"
#include "develop/pixelpipe_hb.h"
#include "views/view.h"
#include "whereami.h"
#include <json-glib/json-glib.h>
#include <math.h>

static gboolean _switch_pending = FALSE;

gboolean dt_touch_session_preload(dt_develop_t *dev, int image_id)
{
  if(!dt_image_exists(image_id)) return FALSE;
  dt_lock_image(image_id);
  const dt_image_t *image = dt_image_cache_get(image_id, 'r');
  dt_image_t metadata = *image;
  dt_image_cache_read_release(image);
  if(!(metadata.flags & DT_IMAGE_AUTO_PRESETS_APPLIED))
  {
    dt_unlock_image(image_id);
    return FALSE;
  }
  dt_dev_init(dev, FALSE);
  dev->image_storage = metadata;
  dev->requested_id = image_id;
  dev->first_load = TRUE;
  dt_pthread_mutex_lock(&darktable.dev_threadsafe);
  dev->iop = dt_iop_load_modules(dev);
  dt_ioppr_set_default_iop_order(dev, image_id);
  dt_dev_reset_chroma(dev);
  for(GList *item = g_list_last(dev->iop); item; item = g_list_previous(item))
    dt_iop_reload_defaults(item->data);

  // migrations can write mask history even when reading a snapshot
  sqlite3_stmt *statement = NULL;
  // clang-format off
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
      "SELECT operation, module, length(op_params), blendop_version, blendop_params IS NULL"
      " FROM main.history WHERE imgid=?1", -1, &statement, NULL);
  // clang-format on
  DT_DEBUG_SQLITE3_BIND_INT(statement, 1, image_id);
  gboolean supported = TRUE;
  int rows = 0;
  int status;
  while((status = sqlite3_step(statement)) == SQLITE_ROW)
  {
    rows++;
    const char *operation = (const char *)sqlite3_column_text(statement, 0);
    dt_iop_module_t *module = NULL;
    for(GList *item = dev->iop; item; item = g_list_next(item))
      if(!g_strcmp0(((dt_iop_module_t *)item->data)->op, operation)) { module = item->data; break; }
    const int size = sqlite3_column_int(statement, 2);
    if(!module || abs(sqlite3_column_int(statement, 1)) != module->version()
       || (size && size != module->params_size)
       || (!sqlite3_column_int(statement, 4) && sqlite3_column_int(statement, 3) != dt_develop_blend_version()))
      supported = FALSE;
  }
  supported = supported && status == SQLITE_DONE;
  sqlite3_finalize(statement);
  // clang-format off
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
      "SELECT version FROM main.masks_history WHERE imgid=?1", -1, &statement, NULL);
  // clang-format on
  DT_DEBUG_SQLITE3_BIND_INT(statement, 1, image_id);
  while((status = sqlite3_step(statement)) == SQLITE_ROW)
    if(sqlite3_column_int(statement, 0) != dt_masks_version()) supported = FALSE;
  supported = supported && status == SQLITE_DONE;
  sqlite3_finalize(statement);

  if(supported && rows)
  {
    int end = 0;
    dt_image_get_history_end(image_id, &end);
    // snapshot reads select by id alone, while undo ids are normally per image
    DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db),
        "SELECT COALESCE(MAX(id), 0) + 1 FROM memory.snapshot_history", -1, &statement, NULL);
    int snapshot = 0;
    if(sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int64(statement, 0) < INT_MAX)
      snapshot = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    // keep inactive future rows too: opening a preloaded editor must not truncate history
    if(snapshot > 0)
    {
      dt_history_snapshot_create(image_id, snapshot, INT_MAX);
      dev->snapshot_id = snapshot;
      dt_dev_read_history_ext(dev, image_id, TRUE);
      dev->snapshot_id = -1;
      dt_history_snapshot_clear(image_id, snapshot);
      supported = g_list_length(dev->history) == rows;
      if(supported) dt_dev_pop_history_items_ext(dev, MIN(end, rows));
    }
    else supported = FALSE;
  }
  dt_pthread_mutex_unlock(&darktable.dev_threadsafe);
  dev->first_load = FALSE;
  dt_unlock_image(image_id);
  if(!supported || !rows) dt_dev_cleanup(dev);
  return supported && rows;
}

static char *_path(void)
{
  return g_build_filename(darktable.configdir, "touch-session.json", NULL);
}

static gboolean _error(char **error, const char *message)
{
  if(error) *error = g_strdup(_(message));
  return FALSE;
}

static const char *_rule_keys[] = { "mode", "item", "off", "top", "string", NULL };

static JsonArray *_rules(const gboolean filtering)
{
  const char *prefix = filtering ? "plugins/lighttable/filtering" : "plugins/lighttable/collect";
  char *key = g_strdup_printf("%s/num_rules", prefix);
  const int count = dt_conf_get_int(key);
  g_free(key);
  if(count < 0 || count > DT_COLLECTION_MAX_RULES) return NULL;
  JsonArray *rules = json_array_new();
  for(int i = 0; i < count; i++)
  {
    JsonObject *rule = json_object_new();
    for(int k = 0; _rule_keys[k]; k++)
    {
      key = g_strdup_printf("%s/%s%d", prefix, _rule_keys[k], i);
      if(!strcmp(_rule_keys[k], "string"))
        json_object_set_string_member(rule, _rule_keys[k], dt_conf_get_string_const(key));
      else
        json_object_set_int_member(rule, _rule_keys[k], dt_conf_get_int(key));
      g_free(key);
    }
    json_array_add_object_element(rules, rule);
  }
  return rules;
}

static void _restore_rules(JsonArray *rules, const gboolean filtering)
{
  const char *prefix = filtering ? "plugins/lighttable/filtering" : "plugins/lighttable/collect";
  char *key = g_strdup_printf("%s/num_rules", prefix);
  dt_conf_set_int(key, json_array_get_length(rules));
  g_free(key);
  for(guint i = 0; i < json_array_get_length(rules); i++)
  {
    JsonObject *rule = json_array_get_object_element(rules, i);
    for(int k = 0; _rule_keys[k]; k++)
    {
      key = g_strdup_printf("%s/%s%d", prefix, _rule_keys[k], i);
      if(!strcmp(_rule_keys[k], "string"))
        dt_conf_set_string(key, json_object_get_string_member(rule, _rule_keys[k]));
      else
        dt_conf_set_int(key, json_object_get_int_member(rule, _rule_keys[k]));
      g_free(key);
    }
  }
}

static void _restore_collection(JsonObject *state)
{
  _restore_rules(json_object_get_array_member(state, "collection"), FALSE);
  _restore_rules(json_object_get_array_member(state, "filtering"), TRUE);
  JsonArray *sort = json_object_get_array_member(state, "sort");
  dt_conf_set_int("plugins/lighttable/filtering/num_sort", json_array_get_length(sort));
  for(guint i = 0; i < json_array_get_length(sort); i++)
  {
    JsonObject *entry = json_array_get_object_element(sort, i);
    char *key = g_strdup_printf("plugins/lighttable/filtering/sort%d", i);
    dt_conf_set_int(key, json_object_get_int_member(entry, "item"));
    g_free(key);
    key = g_strdup_printf("plugins/lighttable/filtering/sortorder%d", i);
    dt_conf_set_int(key, json_object_get_int_member(entry, "order"));
    g_free(key);
  }
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_UNDEF, NULL);
}

static gboolean _valid_member(JsonObject *object, const char *key, const GType type)
{
  JsonNode *node = json_object_get_member(object, key);
  return node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == type;
}

static gboolean _valid_rules(JsonObject *state, const char *key, const gboolean sort)
{
  JsonNode *node = json_object_get_member(state, key);
  if(!node || !JSON_NODE_HOLDS_ARRAY(node)) return FALSE;
  JsonArray *rules = json_node_get_array(node);
  if(json_array_get_length(rules) > (sort ? DT_COLLECTION_SORT_LAST : DT_COLLECTION_MAX_RULES)) return FALSE;
  for(guint i = 0; i < json_array_get_length(rules); i++)
  {
    JsonNode *entry = json_array_get_element(rules, i);
    if(!JSON_NODE_HOLDS_OBJECT(entry)) return FALSE;
    JsonObject *object = json_node_get_object(entry);
    if(sort)
    {
      if(!_valid_member(object, "item", G_TYPE_INT64) || !_valid_member(object, "order", G_TYPE_INT64)) return FALSE;
      if(json_object_get_int_member(object, "item") < 0
         || json_object_get_int_member(object, "item") >= DT_COLLECTION_SORT_LAST) return FALSE;
    }
    else for(int k = 0; _rule_keys[k]; k++)
      if(!_valid_member(object, _rule_keys[k], !strcmp(_rule_keys[k], "string") ? G_TYPE_STRING : G_TYPE_INT64))
        return FALSE;
  }
  return TRUE;
}

gboolean dt_touch_session_available(void)
{
  int directory = 0;
  const int length = wai_getExecutablePath(NULL, 0, &directory);
  if(length < 1) return FALSE;
  char *path = g_malloc(length + 1);
  if(wai_getExecutablePath(path, length, &directory) != length)
  { g_free(path); return FALSE; }
  path[directory] = 0;
#ifdef _WIN32
  char *launcher = g_build_filename(path, "darktabletouch.exe", NULL);
#else
  char *launcher = g_build_filename(path, "darktabletouch", NULL);
#endif
  const gboolean available = g_file_test(launcher, G_FILE_TEST_IS_EXECUTABLE);
  g_free(launcher);
  g_free(path);
  return available;
}

char *dt_touch_session_read(char **error)
{
  char *path = _path();
  char *text = NULL;
  gsize length = 0;
  GError *failure = NULL;
  if(!g_file_get_contents(path, &text, &length, &failure))
  {
    if(!g_error_matches(failure, G_FILE_ERROR, G_FILE_ERROR_NOENT))
      _error(error, N_("could not read the saved interface session"));
    g_clear_error(&failure);
    g_free(path);
    return NULL;
  }
  g_free(path);
  JsonParser *parser = json_parser_new();
  gboolean valid = length <= 1024 * 1024 && json_parser_load_from_data(parser, text, length, NULL)
    && JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser));
  if(valid)
  {
    JsonObject *object = json_node_get_object(json_parser_get_root(parser));
    JsonNode *version = json_object_get_member(object, "version");
    JsonNode *catalog = json_object_get_member(object, "catalog");
    valid = version && JSON_NODE_HOLDS_VALUE(version) && json_node_get_value_type(version) == G_TYPE_INT64
      && json_node_get_int(version) == 1 && catalog && JSON_NODE_HOLDS_VALUE(catalog)
      && json_node_get_value_type(catalog) == G_TYPE_STRING;
    if(valid && g_strcmp0(json_node_get_string(catalog), dt_database_get_path(darktable.db)))
    {
      g_free(text);
      text = NULL;
    }
    const char *strings[] = { "request_id", "next_ui", "view", NULL };
    for(int i = 0; valid && strings[i]; i++)
    {
      JsonNode *node = json_object_get_member(object, strings[i]);
      valid = node && JSON_NODE_HOLDS_VALUE(node) && json_node_get_value_type(node) == G_TYPE_STRING;
    }
    const char *numbers[] = { "image_id", "zoom_factor", "center_x", "center_y", NULL };
    for(int i = 0; valid && numbers[i]; i++)
    {
      JsonNode *node = json_object_get_member(object, numbers[i]);
      valid = node && JSON_NODE_HOLDS_VALUE(node)
        && (json_node_get_value_type(node) == G_TYPE_INT64 || json_node_get_value_type(node) == G_TYPE_DOUBLE)
        && isfinite(json_node_get_double(node));
    }
    valid = valid && _valid_rules(object, "collection", FALSE) && _valid_rules(object, "filtering", FALSE)
      && _valid_rules(object, "sort", TRUE);
    if(valid)
    {
      const char *view = json_object_get_string_member(object, "view");
      const char *next = json_object_get_string_member(object, "next_ui");
      valid = (!strcmp(view, "editor") || !strcmp(view, "library"))
        && (!*next || !strcmp(next, "touch") || !strcmp(next, "classic"))
        && _valid_member(object, "image_id", G_TYPE_INT64)
        && json_object_get_int_member(object, "image_id") >= -1
        && json_object_get_int_member(object, "image_id") <= G_MAXINT32
        && json_object_get_double_member(object, "zoom_factor") >= 1
        && json_object_get_double_member(object, "zoom_factor") <= 128
        && json_object_get_double_member(object, "center_x") >= 0
        && json_object_get_double_member(object, "center_x") <= 1
        && json_object_get_double_member(object, "center_y") >= 0
        && json_object_get_double_member(object, "center_y") <= 1;
    }
  }
  if(!valid)
  {
    _error(error, N_("the saved interface session is invalid"));
    g_free(text);
    text = NULL;
  }
  g_object_unref(parser);
  return text;
}

char *dt_touch_session_restore_collection(char **error)
{
  char *text = dt_touch_session_read(error);
  if(!text) return NULL;
  JsonParser *parser = json_parser_new();
  json_parser_load_from_data(parser, text, -1, NULL);
  JsonObject *state = json_node_get_object(json_parser_get_root(parser));
  _restore_collection(state);
  g_object_unref(parser);
  return text;
}

gboolean dt_touch_session_save(const char *next_ui, int image_id, const char *view,
                                double zoom, double center_x, double center_y, char **error)
{
  if(!darktable.db || !darktable.configdir || !isfinite(zoom) || !isfinite(center_x) || !isfinite(center_y)
     || (g_strcmp0(view, "editor") && g_strcmp0(view, "library"))
     || (g_strcmp0(next_ui, "") && g_strcmp0(next_ui, "touch") && g_strcmp0(next_ui, "classic")))
    return _error(error, N_("could not save an invalid interface session"));
  JsonArray *collection = _rules(FALSE);
  JsonArray *filtering = _rules(TRUE);
  if(!collection || !filtering)
  {
    if(collection) json_array_unref(collection);
    if(filtering) json_array_unref(filtering);
    return _error(error, N_("could not save the current collection"));
  }
  const int sorting = dt_conf_get_int("plugins/lighttable/filtering/num_sort");
  if(sorting < 0 || sorting > DT_COLLECTION_SORT_LAST)
  { json_array_unref(collection); json_array_unref(filtering); return _error(error, N_("could not save the current sort order")); }
  JsonArray *sort = json_array_new();
  for(int i = 0; i < sorting; i++)
  {
    JsonObject *entry = json_object_new();
    char *key = g_strdup_printf("plugins/lighttable/filtering/sort%d", i);
    json_object_set_int_member(entry, "item", dt_conf_get_int(key));
    g_free(key);
    key = g_strdup_printf("plugins/lighttable/filtering/sortorder%d", i);
    json_object_set_int_member(entry, "order", dt_conf_get_int(key));
    g_free(key);
    json_array_add_object_element(sort, entry);
  }
  JsonObject *object = json_object_new();
  char *request = g_uuid_string_random();
  json_object_set_int_member(object, "version", 1);
  json_object_set_string_member(object, "catalog", dt_database_get_path(darktable.db));
  json_object_set_string_member(object, "request_id", request);
  json_object_set_string_member(object, "next_ui", next_ui);
  json_object_set_string_member(object, "view", view);
  json_object_set_int_member(object, "image_id", image_id);
  json_object_set_double_member(object, "zoom_factor", CLAMP(zoom, 1.0, 128.0));
  json_object_set_double_member(object, "center_x", CLAMP(center_x, 0.0, 1.0));
  json_object_set_double_member(object, "center_y", CLAMP(center_y, 0.0, 1.0));
  json_object_set_array_member(object, "collection", collection);
  json_object_set_array_member(object, "filtering", filtering);
  json_object_set_array_member(object, "sort", sort);
  JsonNode *node = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(node, object);
  JsonGenerator *generator = json_generator_new();
  json_generator_set_root(generator, node);
  gsize length = 0;
  char *text = json_generator_to_data(generator, &length);
  char *path = _path();
  GFile *file = g_file_new_for_path(path);
  GError *failure = NULL;
  const gboolean saved = length <= 1024 * 1024 && g_file_replace_contents(file, text, length, NULL, FALSE,
                                                  G_FILE_CREATE_PRIVATE, NULL, NULL, &failure);
  if(!saved) _error(error, N_("could not write the interface session; the current interface remains open"));
  g_clear_error(&failure);
  g_object_unref(file); g_free(path); g_free(text); g_object_unref(generator); json_node_free(node);
  g_free(request);
  return saved;
}

gboolean dt_touch_session_save_classic(const char *next_ui, char **error)
{
  const gboolean editor = dt_view_get_current() == DT_VIEW_DARKROOM;
  const int image = editor ? darktable.develop->image_storage.id
    : dt_selection_get_last_single_id(darktable.selection);
  double zoom = 1.0, x = 0.5, y = 0.5;
  if(editor && dt_is_valid_imgid(image))
  {
    dt_dev_write_history_ext(darktable.develop, image);
    if(dt_image_write_sidecar_file(image)) return _error(error, N_("could not save the image sidecar"));
    dt_dev_viewport_t *port = &darktable.develop->full;
    float cx, cy, scale;
    dt_dev_get_pointer_zoom_pos(port, port->border_size + port->width * 0.5f,
                                 port->border_size + port->height * 0.5f, &cx, &cy, &scale);
    const float fit = dt_dev_get_zoom_scale(port, DT_ZOOM_FIT, 1, TRUE);
    zoom = fit > 0 ? scale / fit : 1.0;
    x = cx; y = cy;
  }
  const gboolean saved = dt_touch_session_save(next_ui, image, editor ? "editor" : "library", zoom, x, y, error);
  if(saved && next_ui && *next_ui) _switch_pending = TRUE;
  return saved;
}

void dt_touch_session_finish_classic(void)
{
  if(_switch_pending) return;
  char *error = NULL;
  if(!dt_touch_session_save_classic("", &error))
  { dt_print(DT_DEBUG_ALWAYS, "[touch session] %s", error); g_free(error); }
}

typedef struct _restore_t { JsonObject *state; int attempts; } _restore_t;

static gboolean _restore_zoom(gpointer data)
{
  _restore_t *restore = data;
  dt_dev_viewport_t *port = &darktable.develop->full;
  int width = 0, height = 0;
  dt_dev_get_processed_size(port, &width, &height);
  if(darktable.develop->image_storage.id != json_object_get_int_member(restore->state, "image_id"))
  {
    json_object_unref(restore->state);
    g_free(restore);
    return G_SOURCE_REMOVE;
  }
  if(port->pipe && width > 0 && height > 0)
  {
    const float fit = dt_dev_get_zoom_scale(port, DT_ZOOM_FIT, 1, FALSE);
    dt_dev_zoom_move(port, DT_ZOOM_FREE, fit * json_object_get_double_member(restore->state, "zoom_factor"), 0, 0, 0, FALSE);
    dt_dev_zoom_move(port, DT_ZOOM_POSITION, 0, 0,
                      json_object_get_double_member(restore->state, "center_x") - 0.5,
                      json_object_get_double_member(restore->state, "center_y") - 0.5, TRUE);
  }
  else if(++restore->attempts < 100) return G_SOURCE_CONTINUE;
  json_object_unref(restore->state);
  g_free(restore);
  return G_SOURCE_REMOVE;
}

gboolean dt_touch_session_restore_classic(gpointer data)
{
  char *error = NULL;
  char *text = dt_touch_session_read(&error);
  if(error) { dt_print(DT_DEBUG_ALWAYS, "[touch session] %s", error); g_free(error); }
  if(!text) return G_SOURCE_REMOVE;
  JsonParser *parser = json_parser_new();
  json_parser_load_from_data(parser, text, -1, NULL);
  JsonObject *state = json_node_get_object(json_parser_get_root(parser));
  const int image = json_object_get_int_member(state, "image_id");
  if(!data)
  {
    _restore_collection(state);
    if(dt_is_valid_imgid(image) && dt_image_exists(image))
    {
      dt_selection_select_single(darktable.selection, image);
      if(!g_strcmp0(json_object_get_string_member(state, "view"), "editor"))
      {
        char filename[PATH_MAX] = { 0 };
        gboolean cached = FALSE;
        dt_image_full_path(image, filename, sizeof(filename), &cached);
        if(dt_is_valid_imgid(dt_load_from_string(filename, TRUE, NULL)))
        {
          _restore_t *restore = g_new0(_restore_t, 1);
          restore->state = json_object_ref(state);
          g_timeout_add(100, _restore_zoom, restore);
        }
      }
    }
  }
  g_object_unref(parser); g_free(text);
  return G_SOURCE_REMOVE;
}
