/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/engine_bridge.h"
#include "common/darktable.h"
#include "common/history.h"
#include "common/history_snapshot.h"
#include "common/iop_order.h"
#include "common/undo.h"
#include "common/filepath.h"
#include "common/image.h"
#include "common/mipmap_cache.h"
#include "common/touch_session.h"
#include "common/collection.h"
#include "common/selection.h"
#include "common/film.h"
#include "common/overlay.h"
#include "common/dtdata.h"
#include "common/exif.h"
#include "common/variables.h"
#include "common/metadata.h"
#include "modifier_catalog.h"
#include <glib/gstdio.h>
#include <errno.h>
#include "control/conf.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/masks.h"
#include "develop/pixelpipe_hb.h"
#include "imageio/imageio_module.h"
#include "imageio/imageio_common.h"
#include "mcp/dt_bridge.h"
#include <json-glib/json-glib.h>
#include <math.h>

struct dt_touch_editor_t
{
  dt_develop_t dev;
  dt_dev_pixelpipe_t pipe;
  dt_mipmap_buffer_t input;
  gboolean pipe_ready;
  gboolean input_ready;
  guint64 generation;
  char *synced_token;
  char *saved_token;
  gboolean speculative;
  gboolean rendered;
  dt_iop_module_t *sync_top;
};

struct dt_touch_render_t
{
  GMutex mutex;
  dt_dev_pixelpipe_t *pipe;
  gboolean canceled;
};

static char *_history_token(dt_touch_editor_t *source);

dt_touch_render_t *dt_touch_render_new(void)
{
  dt_touch_render_t *render = g_new0(dt_touch_render_t, 1);
  g_mutex_init(&render->mutex);
  return render;
}

void dt_touch_render_free(dt_touch_render_t *render)
{
  g_mutex_clear(&render->mutex);
  g_free(render);
}

void dt_touch_render_cancel(dt_touch_render_t *render)
{
  g_mutex_lock(&render->mutex);
  render->canceled = TRUE;
  // the stop request is also honored before process() marks the pipe as running
  if(render->pipe) dt_atomic_set_int(&render->pipe->shutdown, DT_DEV_PIXELPIPE_STOP_NODES);
  g_mutex_unlock(&render->mutex);
}

void dt_touch_render_reset(dt_touch_render_t *render)
{
  g_mutex_lock(&render->mutex);
  g_assert(render->pipe == NULL);
  render->canceled = FALSE;
  g_mutex_unlock(&render->mutex);
}

uint64_t dt_touch_memory_available(void)
{
  uint64_t available;
#ifdef _WIN32
  MEMORYSTATUSEX memory = { .dwLength = sizeof(MEMORYSTATUSEX) };
  if(!GlobalMemoryStatusEx(&memory)) return 0;
  available = MIN(memory.ullAvailPhys, memory.ullAvailPageFile);
#else
  // the resource budget already reserves unified GPU memory
  available = dt_get_available_mem();
#endif
  const char *cap = g_getenv("DARKTABLETOUCH_TEST_AVAILABLE_MEMORY_MIB");
  if(cap)
  {
    char *end = NULL;
    const uint64_t mib = g_ascii_strtoull(cap, &end, 10);
    if(end != cap && !*end && mib < 1048576) available = MIN(available, mib * DT_MEGA);
  }
  return available;
}

int dt_touch_image_prefetch(int image_id, int request)
{
  dt_mipmap_buffer_t input = { 0 };
  dt_mipmap_cache_get(&input, image_id, DT_MIPMAP_FULL, DT_MIPMAP_TESTLOCK, 'r');
  const gboolean ready = input.buf != NULL;
  dt_mipmap_cache_release(&input);
  if(!ready && request) dt_mipmap_cache_get(&input, image_id, DT_MIPMAP_F, DT_MIPMAP_PREFETCH, 'r');
  return ready;
}

void dt_touch_image_invalidate(int image_id)
{
  dt_mipmap_cache_remove_at_size(image_id, DT_MIPMAP_F);
  dt_mipmap_cache_remove_at_size(image_id, DT_MIPMAP_FULL);
}

char *dt_touch_image_stamp(int image_id)
{
  if(!dt_image_exists(image_id)) return NULL;
  char path[PATH_MAX] = { 0 };
  dt_image_full_path(image_id, path, sizeof(path), NULL);
  GStatBuf source;
  if(g_stat(path, &source)) return NULL;
  dt_history_hash_values_t hash = { 0 };
  dt_history_hash_read(image_id, &hash);
  GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
  if(hash.current) g_checksum_update(checksum, hash.current, hash.current_len);
  g_checksum_update(checksum, (const guchar *)&source.st_size, sizeof(source.st_size));
  g_checksum_update(checksum, (const guchar *)&source.st_mtime, sizeof(source.st_mtime));
  char *xmp = g_strconcat(path, ".xmp", NULL);
  GStatBuf sidecar;
  if(!g_stat(xmp, &sidecar))
  {
    g_checksum_update(checksum, (const guchar *)&sidecar.st_size, sizeof(sidecar.st_size));
    g_checksum_update(checksum, (const guchar *)&sidecar.st_mtime, sizeof(sidecar.st_mtime));
  }
  char *stamp = g_strdup(g_checksum_get_string(checksum));
  g_free(xmp);
  g_checksum_free(checksum);
  dt_history_hash_free(&hash);
  return stamp;
}

uint64_t dt_touch_editor_cache_bytes(dt_touch_editor_t *editor)
{
  if(!editor || !editor->pipe_ready) return 0;
  return editor->pipe.cache.allmem + editor->pipe.bcache_size
         + editor->pipe.mask_cache_size + editor->pipe.scharr.size;
}

uint64_t dt_touch_editor_cache_hits(dt_touch_editor_t *editor)
{
  return editor && editor->pipe_ready ? editor->pipe.cache.hits : 0;
}

void dt_touch_editor_trim(dt_touch_editor_t *editor, uint64_t limit)
{
  if(!editor || !editor->pipe_ready) return;
  dt_dev_pixelpipe_t *pipe = &editor->pipe;
  dt_dev_pixelpipe_cache_t *cache = &pipe->cache;
  while(dt_touch_editor_cache_bytes(editor) > limit)
  {
    int oldest = -1;
    for(int k = DT_PIPECACHE_MIN; k < cache->entries; k++)
      if(cache->data[k] && (oldest < 0 || cache->used[k] > cache->used[oldest])) oldest = k;
    if(oldest < 0) break;
    void *data = cache->data[oldest];
    if(data == pipe->backbuf) pipe->backbuf = NULL;
    dt_dev_pixelpipe_clear_cacheline(pipe, data, "touch memory budget");
  }
}

void dt_touch_editor_park(dt_touch_editor_t *editor)
{
  if(!editor) return;
  if(editor->pipe_ready) dt_dev_pixelpipe_cleanup(&editor->pipe);
  editor->pipe_ready = FALSE;
  editor->speculative = FALSE;
  editor->sync_top = NULL;
  g_clear_pointer(&editor->synced_token, g_free);
}

static guint64 _editor_generation = 0;
static GHashTable *_recovery_sidecars = NULL;

struct dt_touch_state_t
{
  dt_imgid_t image;
  GList *history;
  int history_end;
  int snapshot;
  guint64 generation;
};

static int _error(char **error, const char *message)
{
  if(error) *error = g_strdup(_(message));
  return FALSE;
}

int dt_touch_initialize(int argc, char **argv)
{
  return dt_init(argc, argv, FALSE, TRUE, NULL) == 0;
}

const char *dt_touch_version(void)
{
  return darktable_package_version;
}

void dt_touch_shutdown(void)
{
  dt_cleanup();
  g_clear_pointer(&_recovery_sidecars, g_hash_table_unref);
}

void dt_touch_free(void *data)
{
  g_free(data);
}

char *dt_touch_session_json(char **error)
{
  return dt_touch_session_restore_collection(error);
}

int dt_touch_save_session(const char *next_ui, int image_id, int editor,
                           double zoom, double center_x, double center_y, char **error)
{
  return dt_touch_session_save(next_ui, image_id, editor ? "editor" : "library",
                                zoom, center_x, center_y, error);
}

char *dt_touch_images(char **error)
{
  char *all = dt_bridge_list_images_json(0, NULL, -1, NULL, FALSE, 0, error);
  if(!all) return NULL;
  JsonParser *parser = json_parser_new();
  json_parser_load_from_data(parser, all, -1, NULL);
  g_free(all);
  GHashTable *index = g_hash_table_new(g_direct_hash, g_direct_equal);
  JsonArray *images = json_node_get_array(json_parser_get_root(parser));
  for(guint i = 0; i < json_array_get_length(images); i++)
  {
    JsonNode *node = json_array_get_element(images, i);
    const int id = json_object_get_int_member(json_node_get_object(node), "imgid");
    g_hash_table_insert(index, GINT_TO_POINTER(id), node);
  }
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_UNDEF, NULL);
  GList *collection = dt_collection_get_all(darktable.collection, -1);
  JsonArray *ordered = json_array_new();
  for(GList *item = collection; item; item = item->next)
  {
    JsonNode *node = g_hash_table_lookup(index, item->data);
    if(node) json_array_add_element(ordered, json_node_copy(node));
  }
  JsonNode *root = json_node_new(JSON_NODE_ARRAY);
  json_node_take_array(root, ordered);
  char *result = json_to_string(root, FALSE);
  json_node_free(root);
  g_list_free(collection);
  g_hash_table_unref(index);
  g_object_unref(parser);
  return result;
}

char *dt_touch_modules(void)
{
  return dt_bridge_list_modules_json();
}

static JsonArray *_library_rules(const char *prefix, const gboolean sorting)
{
  JsonArray *rules = json_array_new();
  char key[160];
  g_snprintf(key, sizeof(key), "%s/num_%s", prefix, sorting ? "sort" : "rules");
  const int count = CLAMP(dt_conf_get_int(key), 0, sorting ? DT_COLLECTION_SORT_LAST : DT_COLLECTION_MAX_RULES);
  const char *members[] = { "item", "mode", "off", "top", "string", NULL };
  for(int i = 0; i < count; i++)
  {
    JsonObject *rule = json_object_new();
    if(sorting)
    {
      g_snprintf(key, sizeof(key), "%s/sort%d", prefix, i);
      json_object_set_int_member(rule, "item", dt_conf_get_int(key));
      g_snprintf(key, sizeof(key), "%s/sortorder%d", prefix, i);
      json_object_set_int_member(rule, "order", dt_conf_get_int(key));
    }
    else for(int k = 0; members[k]; k++)
    {
      g_snprintf(key, sizeof(key), "%s/%s%d", prefix, members[k], i);
      if(!strcmp(members[k], "string"))
        json_object_set_string_member(rule, members[k], dt_conf_get_string_const(key));
      else json_object_set_int_member(rule, members[k], dt_conf_get_int(key));
    }
    json_array_add_object_element(rules, rule);
  }
  return rules;
}

char *dt_touch_library(void)
{
  JsonObject *library = json_object_new();
  json_object_set_array_member(library, "collection", _library_rules("plugins/lighttable/collect", FALSE));
  json_object_set_array_member(library, "filtering", _library_rules("plugins/lighttable/filtering", FALSE));
  json_object_set_int_member(library, "rating_property", DT_COLLECTION_PROP_RATING);
  json_object_set_int_member(library, "rating_range_property", DT_COLLECTION_PROP_RATING_RANGE);
  json_object_set_array_member(library, "sort", _library_rules("plugins/lighttable/filtering", TRUE));
  JsonArray *properties = json_array_new();
  for(int i = 0; i < DT_COLLECTION_PROP_LAST; i++)
  {
    const char *name = dt_collection_name_untranslated(i);
    if(!name) continue;
    JsonObject *property = json_object_new();
    json_object_set_int_member(property, "item", i);
    json_object_set_string_member(property, "label", _(name));
    JsonArray *values = json_array_new();
    if(i == DT_COLLECTION_PROP_COLORLABEL)
    {
      const char *colors[] = { N_("red"), N_("yellow"), N_("green"), N_("blue"), N_("purple"), NULL };
      for(int k = 0; colors[k]; k++) json_array_add_string_element(values, _(colors[k]));
    }
    else if(i == DT_COLLECTION_PROP_HISTORY)
    {
      json_array_add_string_element(values, _("basic"));
      json_array_add_string_element(values, _("auto applied"));
      json_array_add_string_element(values, _("altered"));
    }
    else if(i == DT_COLLECTION_PROP_RATING || i == DT_COLLECTION_PROP_RATING_RANGE)
    {
      const char *ratings[] = { "=0", ">=1", ">=2", ">=3", ">=4", "=5", "-1", NULL };
      for(int k = 0; ratings[k]; k++) json_array_add_string_element(values, ratings[k]);
    }
    json_object_set_array_member(property, "values", values);
    json_array_add_object_element(properties, property);
  }
  dt_pthread_mutex_lock(&darktable.metadata_threadsafe);
  for(GList *item = dt_metadata_get_list(); item; item = item->next)
  {
    dt_metadata_t *metadata = item->data;
    if(metadata->internal || !metadata->visible) continue;
    JsonObject *property = json_object_new();
    json_object_set_int_member(property, "item", DT_COLLECTION_PROP_METADATA_OFFSET + metadata->key);
    json_object_set_string_member(property, "label", metadata->name);
    json_array_add_object_element(properties, property);
  }
  dt_pthread_mutex_unlock(&darktable.metadata_threadsafe);
  json_object_set_array_member(library, "properties", properties);
  JsonParser *parser = json_parser_new();
  char *folders = dt_bridge_list_film_rolls_json();
  if(json_parser_load_from_data(parser, folders, -1, NULL))
    json_object_set_member(library, "folders", json_node_copy(json_parser_get_root(parser)));
  g_free(folders);
  g_object_unref(parser);
  JsonArray *selected = json_array_new();
  GList *ids = dt_selection_get_list(darktable.selection, FALSE, FALSE);
  for(GList *item = ids; item; item = item->next)
    json_array_add_int_element(selected, GPOINTER_TO_INT(item->data));
  g_list_free(ids);
  json_object_set_array_member(library, "selected", selected);
  JsonObject *settings = json_object_new();
  json_object_set_boolean_member(settings, "advance_after_reject", dt_conf_get_bool("touch/advance_after_reject"));
  json_object_set_boolean_member(settings, "quick_reject", dt_conf_get_bool("touch/quick_reject"));
  json_object_set_boolean_member(settings, "quick_delete", dt_conf_get_bool("touch/quick_delete"));
  json_object_set_object_member(library, "touch_settings", settings);
  JsonObject *options = json_object_new();
  json_object_set_boolean_member(options, "scale_override", dt_conf_get_bool("touch/scale_override"));
  json_object_set_int_member(options, "scale", dt_conf_get_int("touch/ui_scale"));
  json_object_set_boolean_member(options, "swipe_images", dt_conf_get_bool("touch/swipe_images"));
  json_object_set_int_member(options, "preview_quality", dt_conf_get_int("touch/preview_quality"));
  json_object_set_object_member(library, "ui_options", options);
  JsonArray *modifiers = json_array_new();
  for(int i = 0; _touch_modifier_specs[i].varname; i++)
  {
    JsonObject *entry = json_object_new();
    json_object_set_string_member(entry, "tag", _touch_modifier_specs[i].varname);
    json_object_set_string_member(entry, "description", _(_touch_modifier_specs[i].description));
    json_array_add_object_element(modifiers, entry);
  }
  dt_pthread_mutex_lock(&darktable.metadata_threadsafe);
  for(GList *item = dt_metadata_get_list(); item; item = item->next)
  {
    const dt_metadata_t *metadata = item->data;
    if(metadata->internal) continue;
    JsonObject *entry = json_object_new();
    json_object_set_string_member(entry, "tag", metadata->tagname);
    json_object_set_string_member(entry, "description", _("from metadata"));
    json_array_add_object_element(modifiers, entry);
  }
  dt_pthread_mutex_unlock(&darktable.metadata_threadsafe);
  json_object_set_array_member(library, "name_modifiers", modifiers);
  JsonNode *root = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(root, library);
  char *result = json_to_string(root, FALSE);
  json_node_free(root);
  return result;
}

char *dt_touch_collection_values(int property)
{
  GList *values = dt_collection_get_property_values(property);
  JsonArray *array = json_array_new();
  for(GList *item = values; item; item = item->next)
  {
    dt_collection_value_t *value = item->data;
    JsonObject *entry = json_object_new();
    json_object_set_string_member(entry, "value", value->value);
    json_object_set_int_member(entry, "count", value->count);
    json_array_add_object_element(array, entry);
  }
  g_list_free_full(values, dt_collection_value_free);
  JsonNode *root = json_node_new(JSON_NODE_ARRAY);
  json_node_take_array(root, array);
  char *result = json_to_string(root, FALSE);
  json_node_free(root);
  return result;
}

static gboolean _trash_file(const char *path, char **error)
{
  if(!g_file_test(path, G_FILE_TEST_EXISTS)) return TRUE;
  GFile *file = g_file_new_for_path(path);
  GError *failure = NULL;
  const gboolean success = g_file_trash(file, NULL, &failure);
  g_object_unref(file);
  if(!success)
  {
    if(error) *error = g_strdup(failure->message);
    g_error_free(failure);
  }
  return success;
}

int dt_touch_image_exists(int image_id)
{
  return dt_image_exists(image_id);
}

int dt_touch_image_action(const char *action, int image_id, char **error)
{
  if(!dt_image_exists(image_id)) return _error(error, N_("image does not exist in this catalog"));
  if(!strcmp(action, "reject") || !strcmp(action, "unrate"))
  {
    JsonArray *ids = json_array_new();
    json_array_add_int_element(ids, image_id);
    const int success = dt_bridge_set_rating(ids, 0, !strcmp(action, "reject"), error);
    json_array_unref(ids);
    return success;
  }
  if(strcmp(action, "remove") && strcmp(action, "trash")) return _error(error, N_("unknown image action"));
  GList *overlays = dt_overlay_get_used_in_imgs(image_id, TRUE);
  const gboolean used = overlays != NULL;
  g_list_free(overlays);
  if(used) return _error(error, N_("this image is used as an overlay and cannot be removed"));
  if(!dt_image_safe_remove(image_id) || dt_image_local_copy_reset(image_id))
    return _error(error, N_("the original image must be accessible before removing its local copy"));
  char path[PATH_MAX] = { 0 }, versioned[PATH_MAX] = { 0 }, data[PATH_MAX] = { 0 };
  gboolean cached = FALSE;
  dt_image_full_path(image_id, path, sizeof(path), &cached);
  if(!*path) return _error(error, N_("could not resolve the original image path"));
  g_strlcpy(versioned, path, sizeof(versioned));
  dt_image_path_append_version(image_id, versioned, sizeof(versioned));
  dt_dtdata_path_for_image(versioned, data, sizeof(data));
  char *xmp = g_strconcat(versioned, ".xmp", NULL);
  const gboolean trash = !strcmp(action, "trash");
  if(trash && dt_image_count_same_file(image_id) == 1)
  {
    if(cached || !g_file_test(path, G_FILE_TEST_IS_REGULAR))
    { g_free(xmp); return _error(error, N_("the original image is missing or is not a regular file")); }
    if(!_trash_file(path, error)) { g_free(xmp); return FALSE; }
  }
  dt_image_remove(image_id);
  if(dt_image_exists(image_id)) { g_free(xmp); return _error(error, N_("could not remove the image from the catalog")); }
  gboolean success = TRUE;
  if(trash)
  {
    success = _trash_file(xmp, error);
    if(success) success = _trash_file(data, error);
  }
  g_free(xmp);
  dt_film_remove_empty();
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_RELOAD, DT_COLLECTION_PROP_UNDEF, NULL);
  return success;
}

int dt_touch_collection_set(const char *json, char **error)
{
  JsonParser *parser = json_parser_new();
  if(!json_parser_load_from_data(parser, json, -1, NULL)
     || !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser)))
  { g_object_unref(parser); return _error(error, N_("invalid collection settings")); }
  JsonObject *settings = json_node_get_object(json_parser_get_root(parser));
  const char *groups[] = { "collection", "filtering", "sort" };
  // validate the entire request before changing Classic's shared preferences
  for(int group = 0; group < 3; group++)
  {
    JsonNode *node = json_object_get_member(settings, groups[group]);
    if(!node || !JSON_NODE_HOLDS_ARRAY(node)) goto invalid;
    JsonArray *rules = json_node_get_array(node);
    const guint count = json_array_get_length(rules);
    if(count > (group == 2 ? DT_COLLECTION_SORT_LAST : DT_COLLECTION_MAX_RULES)
       || (group == 0 && count == 0)) goto invalid;
    for(guint i = 0; i < count; i++)
    {
      JsonNode *entry = json_array_get_element(rules, i);
      if(!JSON_NODE_HOLDS_OBJECT(entry)) goto invalid;
      JsonObject *rule = json_node_get_object(entry);
      node = json_object_get_member(rule, "item");
      if(!node || json_node_get_value_type(node) != G_TYPE_INT64) goto invalid;
      const int property = json_node_get_int(node);
      if(group == 2)
      {
        node = json_object_get_member(rule, "order");
        if(property < 0 || property >= DT_COLLECTION_SORT_LAST || !node
           || json_node_get_value_type(node) != G_TYPE_INT64
           || json_node_get_int(node) < 0 || json_node_get_int(node) > 1) goto invalid;
      }
      else
      {
        if(!dt_collection_name_untranslated(property)
           && !(property >= DT_COLLECTION_PROP_METADATA_OFFSET
                && dt_metadata_get_metadata_by_keyid(property - DT_COLLECTION_PROP_METADATA_OFFSET))) goto invalid;
        node = json_object_get_member(rule, "string");
        if(!node || json_node_get_value_type(node) != G_TYPE_STRING
           || !g_utf8_validate(json_node_get_string(node), -1, NULL)) goto invalid;
        const char *numbers[] = { "mode", "off", "top" };
        for(int k = 0; k < 3; k++)
        {
          node = json_object_get_member(rule, numbers[k]);
          if(!node || json_node_get_value_type(node) != G_TYPE_INT64
             || json_node_get_int(node) < 0 || json_node_get_int(node) > (k == 0 ? 2 : 1)) goto invalid;
        }
      }
    }
  }
  for(int group = 0; group < 3; group++)
  {
    const char *prefix = group == 0 ? "plugins/lighttable/collect" : "plugins/lighttable/filtering";
    JsonArray *rules = json_object_get_array_member(settings, groups[group]);
    // native filtering preferences require at least one rule, including when cleared
    if(group == 1 && json_array_get_length(rules) == 0)
    {
      JsonObject *empty = json_object_new();
      json_object_set_int_member(empty, "item", DT_COLLECTION_PROP_RATING);
      json_object_set_int_member(empty, "mode", 0);
      json_object_set_int_member(empty, "off", 1);
      json_object_set_int_member(empty, "top", 0);
      json_object_set_string_member(empty, "string", "");
      json_array_add_object_element(rules, empty);
    }
    char key[160];
    g_snprintf(key, sizeof(key), "%s/num_%s", prefix, group == 2 ? "sort" : "rules");
    dt_conf_set_int(key, json_array_get_length(rules));
    for(guint i = 0; i < json_array_get_length(rules); i++)
    {
      JsonObject *rule = json_array_get_object_element(rules, i);
      const char *members[] = { "item", "mode", "off", "top", "string", NULL };
      if(group == 2)
      {
        g_snprintf(key, sizeof(key), "%s/sort%d", prefix, i);
        dt_conf_set_int(key, json_object_get_int_member(rule, "item"));
        g_snprintf(key, sizeof(key), "%s/sortorder%d", prefix, i);
        dt_conf_set_int(key, json_object_get_int_member(rule, "order"));
      }
      else for(int k = 0; members[k]; k++)
      {
        g_snprintf(key, sizeof(key), "%s/%s%d", prefix, members[k], i);
        if(k == 4) dt_conf_set_string(key, json_object_get_string_member(rule, members[k]));
        else dt_conf_set_int(key, json_object_get_int_member(rule, members[k]));
      }
    }
  }
  g_object_unref(parser);
  dt_collection_update_query(darktable.collection, DT_COLLECTION_CHANGE_NEW_QUERY, DT_COLLECTION_PROP_UNDEF, NULL);
  return TRUE;
invalid:
  g_object_unref(parser);
  return _error(error, N_("invalid collection rule or sort order"));
}

int dt_touch_selection(const char *action, int image_id, char **error)
{
  if(!strcmp(action, "all")) dt_selection_select_all(darktable.selection);
  else if(!strcmp(action, "none")) dt_selection_clear(darktable.selection);
  else if(!strcmp(action, "invert")) dt_selection_invert(darktable.selection);
  else if(!strcmp(action, "film")) dt_selection_select_filmroll(darktable.selection);
  else if(!strcmp(action, "untouched")) dt_selection_select_unaltered(darktable.selection);
  else if(image_id > 0 && dt_image_exists(image_id))
  {
    if(!strcmp(action, "toggle")) dt_selection_toggle(darktable.selection, image_id);
    else if(!strcmp(action, "range")) dt_selection_select_range(darktable.selection, image_id);
    else if(!strcmp(action, "single")) dt_selection_select_single(darktable.selection, image_id);
    else return _error(error, N_("unknown selection action"));
  }
  else return _error(error, N_("unknown selection action or image"));
  return TRUE;
}

int dt_touch_import_candidate(const char *path)
{
  if(!dt_supported_image(path)) return -1;
  GFile *file = g_file_new_for_path(path);
  char *canonical = g_file_get_path(file);
  g_object_unref(file);
#ifdef _WIN32
  g_strdelimit(canonical, "/", G_DIR_SEPARATOR);
#endif
  const int existing = MAX(0, dt_image_get_id_full_path(canonical));
  g_free(canonical);
  return existing;
}

int dt_touch_import_is_raw(const char *path)
{
  const char *extension = strrchr(path, '.');
  return extension && (dt_imageio_is_raw_by_extension(extension) || !g_ascii_strcasecmp(extension, ".dng"));
}

void dt_touch_quick_actions_set(int reject, int delete_button)
{
  dt_conf_set_bool("touch/quick_reject", reject);
  dt_conf_set_bool("touch/quick_delete", delete_button);
}

char *dt_touch_editor_information(dt_touch_editor_t *editor, char **error)
{
  if(!editor) return NULL;
  char *text = dt_bridge_get_metadata_json(editor->dev.image_storage.id, error);
  if(!text) return NULL;
  JsonParser *parser = json_parser_new();
  json_parser_load_from_data(parser, text, -1, NULL);
  const char *recovery = _recovery_sidecars ? g_hash_table_lookup(_recovery_sidecars, GINT_TO_POINTER(editor->dev.image_storage.id)) : NULL;
  json_object_set_string_member(json_node_get_object(json_parser_get_root(parser)), "recovery_sidecar", recovery ? recovery : "");
  json_object_set_int_member(json_node_get_object(json_parser_get_root(parser)), "preview_max_extent",
                             MAX(editor->dev.image_storage.width, editor->dev.image_storage.height));
  g_free(text);
  text = json_to_string(json_parser_get_root(parser), FALSE);
  g_object_unref(parser);
  return text;
}

static int _thumbnail(int image_id, uint8_t **pixels, int *width, int *height,
                       dt_mipmap_get_flags_t flags, char **error)
{
  dt_mipmap_buffer_t buffer = { 0 };
  dt_mipmap_cache_get(&buffer, image_id, DT_MIPMAP_1, flags, 'r');
  if(!buffer.buf)
  {
    dt_mipmap_cache_release(&buffer);
    return _error(error, N_("could not load the image thumbnail"));
  }
  *width = buffer.width;
  *height = buffer.height;
  const size_t bytes = (size_t)*width * *height * 4;
  *pixels = g_malloc(bytes);
  memcpy(*pixels, buffer.buf, bytes);
  if(buffer.color_space != DT_COLORSPACE_SRGB)
  {
    pthread_rwlock_rdlock(&darktable.color_profiles->xprofile_lock);
    const dt_colorspaces_color_profile_t *source = dt_colorspaces_get_profile(buffer.color_space, NULL, DT_PROFILE_DIRECTION_ANY);
    const dt_colorspaces_color_profile_t *target = dt_colorspaces_get_profile(DT_COLORSPACE_SRGB, NULL, DT_PROFILE_DIRECTION_OUT);
    cmsHTRANSFORM transform = source && target
      ? cmsCreateTransform(source->profile, TYPE_RGBA_8, target->profile, TYPE_RGBA_8,
                            INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA) : NULL;
    if(transform)
    {
      cmsDoTransform(transform, buffer.buf, *pixels, (cmsUInt32Number)*width * *height);
      cmsDeleteTransform(transform);
    }
    pthread_rwlock_unlock(&darktable.color_profiles->xprofile_lock);
    if(!transform)
    {
      g_free(*pixels); *pixels = NULL;
      dt_mipmap_cache_release(&buffer);
      return _error(error, N_("could not color manage the image thumbnail"));
    }
  }
  // mipmap alpha is unused; Qt's scene graph needs opaque RGBA thumbnails
  for(size_t i = 3; i < bytes; i += 4) (*pixels)[i] = 255;
  dt_mipmap_cache_release(&buffer);
  return TRUE;
}

int dt_touch_thumbnail(int image_id, uint8_t **pixels, int *width, int *height, char **error)
{
  return _thumbnail(image_id, pixels, width, height, DT_MIPMAP_BLOCKING, error);
}

char *dt_touch_import(const char *path, char **error)
{
  GError *failure = NULL;
  if(!dt_path_check_engine_utf8(path, &failure))
  {
    _error(error, failure->message);
    g_error_free(failure);
    return NULL;
  }
  if(g_file_test(path, G_FILE_TEST_IS_DIR))
    return dt_bridge_import_images_json(NULL, path, TRUE, error);
  JsonArray *paths = json_array_new();
  json_array_add_string_element(paths, path);
  char *result = dt_bridge_import_images_json(paths, NULL, FALSE, error);
  json_array_unref(paths);
  return result;
}

dt_touch_editor_t *dt_touch_editor_new(int image_id, char **error)
{
  if(!dt_is_valid_imgid(image_id) || !dt_image_exists(image_id))
  {
    _error(error, N_("image does not exist in this catalog"));
    return NULL;
  }
  char *path = dt_touch_image_path(image_id);
  const gboolean available = path && g_file_test(path, G_FILE_TEST_IS_REGULAR);
  g_free(path);
  if(!available)
  {
    _error(error, N_("original image is unavailable"));
    return NULL;
  }
  dt_touch_editor_t *editor = g_new0(dt_touch_editor_t, 1);
  editor->generation = ++_editor_generation;
  dt_dev_init(&editor->dev, FALSE);
  dt_dev_load_image(&editor->dev, image_id);
  if(editor->dev.image_storage.load_status != DT_IMAGEIO_OK)
  {
    dt_touch_editor_free(editor);
    _error(error, N_("could not decode the source image"));
    return NULL;
  }
  dt_dev_pop_history_items_ext(&editor->dev, editor->dev.history_end);
  return editor;
}

void dt_touch_editor_free(dt_touch_editor_t *editor)
{
  if(!editor) return;
  if(editor->pipe_ready) dt_dev_pixelpipe_cleanup(&editor->pipe);
  if(editor->input_ready) dt_mipmap_cache_release(&editor->input);
  dt_dev_cleanup(&editor->dev);
  g_free(editor->synced_token);
  g_free(editor->saved_token);
  g_free(editor);
}

dt_touch_editor_t *dt_touch_editor_preload(int image_id)
{
  dt_touch_editor_t *editor = g_new0(dt_touch_editor_t, 1);
  if(!dt_touch_session_preload(&editor->dev, image_id))
  {
    g_free(editor);
    return NULL;
  }
  editor->generation = ++_editor_generation;
  editor->speculative = TRUE;
  return editor;
}

static dt_iop_module_t *_module(dt_touch_editor_t *editor, const char *operation, char **error)
{
  dt_iop_module_t *module = editor && operation
    ? dt_iop_get_module_by_op_priority(editor->dev.iop, operation, 0) : NULL;
  if(!module) _error(error, N_("module is unavailable for this editing session"));
  return module;
}

int dt_touch_editor_enabled(dt_touch_editor_t *editor, const char *operation)
{
  dt_iop_module_t *module = _module(editor, operation, NULL);
  return module && module->enabled;
}

void dt_touch_editor_crop_original(dt_touch_editor_t *editor, int size[2])
{
  const int width = MAX(1, editor->dev.image_storage.p_width), height = MAX(1, editor->dev.image_storage.p_height);
  const gboolean landscape = editor->pipe.processed_width >= editor->pipe.processed_height;
  size[0] = landscape ? MAX(width, height) : MIN(width, height);
  size[1] = landscape ? MIN(width, height) : MAX(width, height);
}

int dt_touch_editor_crop_bounds(dt_touch_editor_t *editor, float bounds[4], char **error)
{
  dt_iop_module_t *module = _module(editor, "crop", error);
  if(!module || !editor->pipe_ready) return FALSE;
  const dt_dev_pixelpipe_iop_t *piece = dt_dev_distort_get_iop_pipe(&editor->dev, &editor->pipe, module);
  if(!piece || piece->buf_out.width < 1 || piece->buf_out.height < 1)
    return _error(error, N_("crop geometry is not ready"));
  const char *names[] = { "cx", "cy", "cw", "ch" };
  for(int i = 0; i < 4; i++)
  {
    const float *value = module->get_p(module->params, names[i]);
    if(!value) return _error(error, N_("crop parameters are unavailable"));
    bounds[i] = *value * (i % 2 ? piece->buf_out.height : piece->buf_out.width);
  }
  if(!dt_dev_distort_transform_plus(&editor->dev, &editor->pipe, module->iop_order,
                                    DT_DEV_TRANSFORM_DIR_FORW_EXCL, bounds, 2))
    return _error(error, N_("could not map the crop frame"));
  for(int i = 0; i < 4; i++) bounds[i] /= i % 2 ? editor->pipe.processed_height : editor->pipe.processed_width;
  return TRUE;
}

int dt_touch_editor_crop_apply(dt_touch_editor_t *editor, const float bounds[4],
                               int ratio_n, int ratio_d, char **error)
{
  if(editor) editor->sync_top = NULL;
  dt_iop_module_t *module = _module(editor, "crop", error);
  if(!module || !editor->pipe_ready) return FALSE;
  for(int i = 0; i < 4; i++)
    if(!isfinite(bounds[i]) || bounds[i] < 0 || bounds[i] > 1)
      return _error(error, N_("crop frame is outside the image"));
  if(bounds[2] - bounds[0] < 0.01f || bounds[3] - bounds[1] < 0.01f || ratio_n < 0 || ratio_d < 0 || ((ratio_n == 0) != (ratio_d == 0)))
    return _error(error, N_("crop frame is too small or has an invalid aspect ratio"));
  float points[4];
  for(int i = 0; i < 4; i++) points[i] = bounds[i] * (i % 2 ? editor->pipe.processed_height : editor->pipe.processed_width);
  if(!dt_dev_distort_backtransform_plus(&editor->dev, &editor->pipe, module->iop_order,
                                       DT_DEV_TRANSFORM_DIR_FORW_EXCL, points, 2))
    return _error(error, N_("could not map the crop frame"));
  const dt_dev_pixelpipe_iop_t *piece = dt_dev_distort_get_iop_pipe(&editor->dev, &editor->pipe, module);
  if(!piece || piece->buf_out.width < 1 || piece->buf_out.height < 1)
    return _error(error, N_("crop geometry is not ready"));
  const char *names[] = { "cx", "cy", "cw", "ch" };
  for(int i = 0; i < 4; i++)
  {
    points[i] /= i % 2 ? piece->buf_out.height : piece->buf_out.width;
    if(!isfinite(points[i]) || points[i] < -0.001f || points[i] > 1.001f)
      return _error(error, N_("this crop cannot be represented in the native crop operation"));
  }
  for(int i = 0; i < 4; i++) *(float *)module->get_p(module->params, names[i]) = CLAMP(points[i], 0, 1);
  const gboolean landscape = piece->buf_out.width >= piece->buf_out.height;
  const gboolean flipped = ratio_n && (landscape != (ratio_n >= ratio_d));
  // classic stores the smaller component as n and signals orientation with d
  *(int *)module->get_p(module->params, "ratio_n") = MIN(ratio_n, ratio_d);
  *(int *)module->get_p(module->params, "ratio_d") = (flipped ? -1 : 1) * MAX(ratio_n, ratio_d);
  module->enabled = TRUE;
  dt_dev_add_history_item_ext(&editor->dev, module, TRUE, TRUE);
  return TRUE;
}

static gboolean _crop_inside_source(dt_touch_editor_t *editor, double width, double height,
                                    dt_iop_module_t *space)
{
  const dt_dev_pixelpipe_iop_t *piece = space
    ? dt_dev_distort_get_iop_pipe(&editor->dev, &editor->pipe, space) : NULL;
  const int target_width = piece ? piece->buf_out.width : editor->pipe.processed_width;
  const int target_height = piece ? piece->buf_out.height : editor->pipe.processed_height;
  float points[2 * 81];
  for(int y = 0; y <= 8; y++) for(int x = 0; x <= 8; x++)
  {
    const int i = 2 * (y * 9 + x);
    points[i] = (0.5 - width / 2 + width * x / 8) * target_width;
    points[i + 1] = (0.5 - height / 2 + height * y / 8) * target_height;
  }
  if(space && !dt_dev_distort_transform_plus(&editor->dev, &editor->pipe, space->iop_order,
                                            DT_DEV_TRANSFORM_DIR_FORW_EXCL, points, 81)) return FALSE;
  const dt_iop_module_t *rawprepare = _module(editor, "rawprepare", NULL);
  const dt_dev_pixelpipe_iop_t *raw = rawprepare
    ? dt_dev_distort_get_iop_pipe(&editor->dev, &editor->pipe, (dt_iop_module_t *)rawprepare) : NULL;
  const gboolean active_raw = raw && raw->enabled && raw->buf_out.width > 0 && raw->buf_out.height > 0;
  const float source_width = active_raw ? raw->buf_out.width : editor->pipe.iwidth;
  const float source_height = active_raw ? raw->buf_out.height : editor->pipe.iheight;
  // masked sensor borders are not valid image area after raw preparation
  if(!dt_dev_distort_backtransform_plus(&editor->dev, &editor->pipe, active_raw ? rawprepare->iop_order : 0,
                                       active_raw ? DT_DEV_TRANSFORM_DIR_FORW_EXCL : DT_DEV_TRANSFORM_DIR_ALL,
                                       points, 81)) return FALSE;
  for(int i = 0; i < 81; i++)
    if(!isfinite(points[2*i]) || !isfinite(points[2*i+1])
       || points[2*i] < 2 || points[2*i] > source_width - 2
       || points[2*i+1] < 2 || points[2*i+1] > source_height - 2) return FALSE;
  return TRUE;
}

static int _auto_crop(dt_touch_editor_t *editor, double aspect, dt_iop_module_t *space,
                      float bounds[4], char **error)
{
  if(!editor || !editor->pipe_ready || !isfinite(aspect) || aspect <= 0)
    return _error(error, N_("automatic cropping needs a rendered image and a valid aspect ratio"));
  const dt_dev_pixelpipe_iop_t *piece = space
    ? dt_dev_distort_get_iop_pipe(&editor->dev, &editor->pipe, space) : NULL;
  const int width_px = piece ? piece->buf_out.width : editor->pipe.processed_width;
  const int height_px = piece ? piece->buf_out.height : editor->pipe.processed_height;
  if(width_px < 1 || height_px < 1) return _error(error, N_("crop geometry is not ready"));
  const double relative = aspect * height_px / width_px;
  const double width = fmin(1.0, relative), height = fmin(1.0, 1.0 / relative);
  double lo = 0, hi = 1;
  for(int iteration = 0; iteration < 24; iteration++)
  {
    const double scale = (lo + hi) / 2;
    if(_crop_inside_source(editor, width * scale, height * scale, space)) lo = scale; else hi = scale;
  }
  if(lo < 0.02) return _error(error, N_("automatic cropping could not find a valid centered frame"));
  bounds[0] = (1 - width * lo) / 2; bounds[1] = (1 - height * lo) / 2;
  bounds[2] = 1 - bounds[0]; bounds[3] = 1 - bounds[1];
  return TRUE;
}

int dt_touch_editor_auto_crop(dt_touch_editor_t *editor, double aspect, float bounds[4], char **error)
{
  return _auto_crop(editor, aspect, NULL, bounds, error);
}

static int _constrain_rotation(dt_touch_editor_t *editor, dt_iop_module_t *module, char **error)
{
  const int *mode = module->get_p(module->params, "cropmode");
  if(!mode || (*mode != 0 && *mode != 2)) return TRUE;
  const char *names[] = { "cl", "ct", "cr", "cb" };
  for(int i = 0; i < 4; i++) *(float *)module->get_p(module->params, names[i]) = i < 2 ? 0.0f : 1.0f;
  dt_dev_add_history_item_ext(&editor->dev, module, TRUE, TRUE);
  if(*mode == 0) return TRUE;
  // fit in the uncropped rotation output so repeated angle edits do not compound the crop
  uint8_t *pixels = NULL;
  int width = 0, height = 0;
  if(!dt_touch_editor_preview(editor, 160, 160, &pixels, &width, &height, error)) return FALSE;
  dt_touch_free(pixels);
  const dt_dev_pixelpipe_iop_t *piece = dt_dev_distort_get_iop_pipe(&editor->dev, &editor->pipe, module);
  if(!piece || piece->buf_in.height < 1) return _error(error, N_("crop geometry is not ready"));
  // original format uses the module input, as in src/iop/ashift.c:2725
  const double aspect = (double)piece->buf_in.width / piece->buf_in.height;
  float bounds[4];
  if(!_auto_crop(editor, aspect, module, bounds, error)) return FALSE;
  for(int i = 0; i < 4; i++) *(float *)module->get_p(module->params, names[i]) = bounds[i];
  dt_dev_add_history_item_ext(&editor->dev, module, TRUE, TRUE);
  return TRUE;
}

static char *_hex(const void *params, const size_t size)
{
  const guint8 *data = params;
  char *out = g_malloc(size * 2 + 1);
  static const char digits[] = "0123456789abcdef";
  for(size_t i = 0; i < size; i++)
  {
    out[i * 2] = digits[data[i] >> 4];
    out[i * 2 + 1] = digits[data[i] & 15];
  }
  out[size * 2] = 0;
  return out;
}

static JsonParser *_decode(dt_iop_module_t *module, const void *params, char **error)
{
  char *hex = _hex(params, module->params_size);
  char *text = dt_bridge_decode_params_json(module->op, hex, error);
  g_free(hex);
  if(!text) return NULL;
  JsonParser *parser = json_parser_new();
  if(!json_parser_load_from_data(parser, text, -1, NULL))
  {
    g_object_unref(parser);
    parser = NULL;
    _error(error, N_("could not decode module state"));
  }
  g_free(text);
  return parser;
}

static gboolean _exposure_automatic(dt_iop_module_t *module, const int mode)
{
  dt_introspection_field_t *field = module->get_f("mode");
  for(dt_introspection_type_enum_tuple_t *choice = field ? field->Enum.values : NULL;
      choice && choice->name; choice++)
    if(choice->value == mode) return !strcmp(choice->name, "EXPOSURE_MODE_DEFLICKER");
  return FALSE;
}

static gboolean _exposure_automatic_supported(dt_iop_module_t *module)
{
  const dt_image_t *image = &module->dev->image_storage;
  return dt_image_is_raw(image) && image->buf_dsc.channels == 1
    && image->buf_dsc.datatype == TYPE_UINT16;
}

char *dt_touch_editor_module(dt_touch_editor_t *editor, const char *operation, char **error)
{
  dt_iop_module_t *module = _module(editor, operation, error);
  if(!module) return NULL;
  char *schema = dt_bridge_module_schema_json(operation, error);
  if(!schema) return NULL;
  JsonParser *parser = json_parser_new();
  json_parser_load_from_data(parser, schema, -1, NULL);
  g_free(schema);
  JsonParser *current = _decode(module, module->params, error);
  JsonParser *defaults = current ? _decode(module, module->default_params, error) : NULL;
  if(!current || !defaults)
  {
    if(current) g_object_unref(current);
    g_object_unref(parser);
    return NULL;
  }
  JsonObject *object = json_node_get_object(json_parser_get_root(parser));
  JsonObject *values = json_object_get_object_member(
    json_node_get_object(json_parser_get_root(current)), "fields");
  JsonObject *initial = json_object_get_object_member(
    json_node_get_object(json_parser_get_root(defaults)), "fields");
  json_object_set_string_member(object, "label", module->name());
  json_object_set_boolean_member(object, "enabled", module->enabled);
  const gboolean exposure = !strcmp(operation, "exposure");
  const int *mode = exposure ? module->get_p(module->params, "mode") : NULL;
  const gboolean automatic_supported = exposure && _exposure_automatic_supported(module);
  const gboolean automatic = mode && automatic_supported && _exposure_automatic(module, *mode);
  JsonArray *fields = json_object_get_array_member(object, "fields");
  for(guint i = 0; i < json_array_get_length(fields); i++)
  {
    JsonObject *field = json_array_get_object_element(fields, i);
    const char *name = json_object_get_string_member(field, "name");
    dt_introspection_field_t *description = module->get_f(name);
    const char *label = description ? description->header.description : NULL;
    json_object_set_string_member(field, "label", label && *label ? label : name);
    if(exposure)
    {
      const gboolean deflicker = g_str_has_prefix(name, "deflicker_");
      const gboolean manual = !strcmp(name, "exposure") || g_str_has_prefix(name, "compensate_");
      json_object_set_boolean_member(field, "visible", deflicker ? automatic
        : manual ? !automatic : TRUE);
      if(!strcmp(name, "mode"))
      {
        json_object_set_boolean_member(field, "editable", automatic_supported);
        json_object_set_string_member(field, "hint", automatic_supported
          ? (automatic ? _("adjust target brightness and percentile; manual exposure is not used in this mode") : "")
          : _("automatic mode requires a single-channel 16-bit RAW image"));
      }
      if(!strcmp(name, "deflicker_target_level"))
      {
        json_object_set_string_member(field, "label", _("target brightness"));
        json_object_set_string_member(field, "unit", "EV");
        json_object_set_double_member(field, "soft_min", -8.0);
        json_object_set_double_member(field, "soft_max", 0.0);
      }
      if(!strcmp(name, "deflicker_percentile")) json_object_set_string_member(field, "unit", "%");
      if(!strcmp(name, "exposure")) json_object_set_string_member(field, "unit", "EV");
    }
    if(!strcmp(operation, "exposure") && !strcmp(name, "exposure"))
    {
      json_object_set_double_member(field, "soft_min", -3.0);
      json_object_set_double_member(field, "soft_max", 4.0);
    }
    if(!strcmp(operation, "exposure") && !strcmp(name, "black"))
    {
      json_object_set_double_member(field, "soft_min", -0.1);
      json_object_set_double_member(field, "soft_max", 0.1);
    }
    if(description && description->header.type == DT_INTROSPECTION_TYPE_ENUM
       && json_object_has_member(field, "values"))
    {
      JsonArray *choices = json_object_get_array_member(field, "values");
      for(guint choice = 0; choice < json_array_get_length(choices); choice++)
      {
        JsonObject *entry = json_array_get_object_element(choices, choice);
        const int number = json_object_get_int_member(entry, "value");
        for(dt_introspection_type_enum_tuple_t *tuple = description->Enum.values;
            tuple && tuple->name; tuple++)
          if(tuple->value == number && tuple->description && *tuple->description)
            json_object_set_string_member(entry, "name", tuple->description);
      }
    }
    if(json_object_has_member(values, name))
      json_object_set_member(field, "value", json_node_copy(json_object_get_member(values, name)));
    if(json_object_has_member(initial, name))
      json_object_set_member(field, "default", json_node_copy(json_object_get_member(initial, name)));
    if(description && description->header.type == DT_INTROSPECTION_TYPE_ENUM)
    {
      const int *live = module->get_p(module->params, name);
      const int *reset = module->get_p(module->default_params, name);
      if(live) json_object_set_int_member(field, "value", *live);
      if(reset) json_object_set_int_member(field, "default", *reset);
      if(exposure && !automatic_supported && !strcmp(name, "mode"))
        for(dt_introspection_type_enum_tuple_t *choice = description->Enum.values; choice && choice->name; choice++)
          if(!strcmp(choice->name, "EXPOSURE_MODE_MANUAL")) json_object_set_int_member(field, "value", choice->value);
    }
  }
  if(!strcmp(operation, "flip"))
  {
    char *angle_schema = dt_touch_editor_module(editor, "ashift", error);
    if(!angle_schema)
    { g_object_unref(defaults); g_object_unref(current); g_object_unref(parser); return NULL; }
    JsonParser *angle = json_parser_new();
    json_parser_load_from_data(angle, angle_schema, -1, NULL);
    JsonArray *angle_fields = json_object_get_array_member(json_node_get_object(json_parser_get_root(angle)), "fields");
    for(guint i = 0; i < json_array_get_length(angle_fields); i++)
    {
      JsonObject *field = json_array_get_object_element(angle_fields, i);
      const char *name = json_object_get_string_member(field, "name");
      if(strcmp(name, "rotation") && strcmp(name, "cropmode")) continue;
      JsonNode *copy = json_node_copy(json_array_get_element(angle_fields, i));
      JsonObject *control = json_node_get_object(copy);
      json_object_set_string_member(control, "operation", "ashift");
      if(!strcmp(name, "rotation"))
      {
        json_object_set_string_member(control, "label", _("precise angle"));
        json_object_set_string_member(control, "unit", _("deg"));
        json_object_set_double_member(control, "step", 0.01);
        json_object_set_double_member(control, "soft_min", -15.0);
        json_object_set_double_member(control, "soft_max", 15.0);
      }
      else
      {
        json_object_set_string_member(control, "label", _("constrain rotation"));
        json_object_set_int_member(control, "value", json_object_get_int_member(control, "value") == 2 ? 2 : 0);
        json_object_set_int_member(control, "default", 0);
        JsonArray *choices = json_array_new();
        for(int choice = 0; choice < 2; choice++)
        {
          JsonObject *entry = json_object_new();
          json_object_set_int_member(entry, "value", choice ? 2 : 0);
          json_object_set_string_member(entry, "name", choice ? _("original format") : _("off"));
          json_array_add_object_element(choices, entry);
        }
        json_object_set_array_member(control, "values", choices);
      }
      json_array_add_element(fields, copy);
    }
    g_object_unref(angle); g_free(angle_schema);
  }
  JsonGenerator *generator = json_generator_new();
  json_generator_set_root(generator, json_parser_get_root(parser));
  char *result = json_generator_to_data(generator, NULL);
  g_object_unref(generator);
  g_object_unref(defaults);
  g_object_unref(current);
  g_object_unref(parser);
  return result;
}

char *dt_touch_editor_history(dt_touch_editor_t *editor)
{
  JsonObject *object = json_object_new();
  json_object_set_int_member(object, "end", editor ? editor->dev.history_end : 0);
  JsonArray *steps = json_array_new();
  int index = 0;
  for(GList *item = editor ? editor->dev.history : NULL; item; item = item->next)
  {
    const dt_dev_history_item_t *history = item->data;
    JsonObject *step = json_object_new();
    json_object_set_int_member(step, "index", ++index);
    json_object_set_string_member(step, "operation", history->op_name);
    const char *label = history->module ? history->module->name() : history->op_name;
    char *instance = history->multi_name_hand_edited ? g_strdup(history->multi_name)
      : dt_util_localize_segmented_name(history->multi_name, FALSE);
    char *name = *instance && strcmp(instance, "0") ? g_strdup_printf("%s - %s", label, instance) : g_strdup(label);
    json_object_set_string_member(step, "label", name);
    json_object_set_boolean_member(step, "enabled", history->enabled);
    json_object_set_int_member(step, "instance", history->multi_priority);
    json_array_add_object_element(steps, step);
    g_free(name); g_free(instance);
  }
  json_object_set_array_member(object, "steps", steps);
  JsonNode *node = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(node, object);
  char *text = json_to_string(node, FALSE);
  json_node_free(node);
  return text;
}

int dt_touch_editor_history_select(dt_touch_editor_t *editor, int end, char **error)
{
  if(editor) editor->sync_top = NULL;
  if(!editor || end < 0 || end > g_list_length(editor->dev.history))
    return _error(error, N_("history step is outside the current editing session"));
  dt_dev_pop_history_items_ext(&editor->dev, end);
  return TRUE;
}

int dt_touch_editor_set(dt_touch_editor_t *editor, const char *operation,
                        const char *name, const char *value_json, char **error)
{
  dt_iop_module_t *module = _module(editor, operation, error);
  if(!module || !module->so->have_introspection) return FALSE;
  dt_introspection_field_t *field = module->get_f(name);
  void *value = module->get_p(module->params, name);
  if(!field || !value) return _error(error, N_("unknown module parameter"));
  char *before = _history_token(editor);
  const gboolean synchronized = !g_strcmp0(before, editor->synced_token);
  g_free(before);
  editor->sync_top = NULL;
  const gboolean exposure = !strcmp(operation, "exposure");
  const int *mode = exposure ? module->get_p(module->params, "mode") : NULL;
  if(mode && _exposure_automatic_supported(module) && _exposure_automatic(module, *mode) && !strcmp(name, "exposure"))
    return _error(error, N_("adjust target brightness in automatic exposure mode"));
  JsonParser *parser = json_parser_new();
  if(!json_parser_load_from_data(parser, value_json, -1, NULL)
     || !JSON_NODE_HOLDS_VALUE(json_parser_get_root(parser)))
  {
    g_object_unref(parser);
    return _error(error, N_("parameter value must be a JSON scalar"));
  }
  JsonNode *node = json_parser_get_root(parser);
  gboolean ok = TRUE;
  if(field->header.type == DT_INTROSPECTION_TYPE_BOOL)
  {
    ok = json_node_get_value_type(node) == G_TYPE_BOOLEAN;
    if(ok) *(gboolean *)value = json_node_get_boolean(node);
  }
  else
  {
    const GType type = json_node_get_value_type(node);
    double number = (type == G_TYPE_INT64 || type == G_TYPE_DOUBLE)
      ? json_node_get_double(node) : NAN;
    ok = isfinite(number);
    if(ok)
    {
      switch(field->header.type)
      {
#define SET_NUMBER(kind, member, ctype, whole) \
        case DT_INTROSPECTION_TYPE_##kind: \
          ok = number >= field->member.Min && number <= field->member.Max \
               && (!(whole) || trunc(number) == number); \
          if(ok) *(ctype *)value = (ctype)number; \
          break
        SET_NUMBER(FLOAT, Float, float, FALSE);
        SET_NUMBER(DOUBLE, Double, double, FALSE);
        SET_NUMBER(INT, Int, int, TRUE);
        SET_NUMBER(UINT, UInt, unsigned int, TRUE);
        SET_NUMBER(INT8, Int8, int8_t, TRUE);
        SET_NUMBER(UINT8, UInt8, uint8_t, TRUE);
        SET_NUMBER(SHORT, Short, short, TRUE);
        SET_NUMBER(USHORT, UShort, unsigned short, TRUE);
#undef SET_NUMBER
        case DT_INTROSPECTION_TYPE_ENUM:
          ok = FALSE;
          for(dt_introspection_type_enum_tuple_t *choice = field->Enum.values;
              choice && choice->name; choice++)
            if(number == choice->value) { ok = TRUE; break; }
          if(ok && exposure && !strcmp(name, "mode") && _exposure_automatic(module, (int)number)
             && !_exposure_automatic_supported(module))
          {
            g_object_unref(parser);
            return _error(error, N_("automatic mode requires a single-channel 16-bit RAW image"));
          }
          if(ok) *(int *)value = (int)number;
          break;
        default: ok = FALSE; break;
      }
    }
  }
  g_object_unref(parser);
  if(!ok) return _error(error, N_("parameter value has an invalid type or is outside its limits"));
  module->enabled = TRUE;
  dt_dev_add_history_item_ext(&editor->dev, module, TRUE, TRUE);
  if(synchronized && strcmp(operation, "ashift")) editor->sync_top = module;
  if(!strcmp(operation, "ashift") && (!strcmp(name, "rotation") || !strcmp(name, "cropmode")))
    return _constrain_rotation(editor, module, error);
  return TRUE;
}

int dt_touch_editor_enable(dt_touch_editor_t *editor, const char *operation,
                           int enabled, char **error)
{
  if(editor) editor->sync_top = NULL;
  dt_iop_module_t *module = _module(editor, operation, error);
  if(!module) return FALSE;
  if(!enabled && module->hide_enable_button)
    return _error(error, N_("this module is required by the processing pipeline"));
  module->enabled = enabled != 0;
  dt_dev_add_history_item_ext(&editor->dev, module, module->enabled, TRUE);
  return TRUE;
}

int dt_touch_editor_reset(dt_touch_editor_t *editor, const char *operation, char **error)
{
  if(editor) editor->sync_top = NULL;
  dt_iop_module_t *module = _module(editor, operation, error);
  if(!module) return FALSE;
  if(!strcmp(operation, "flip"))
  {
    dt_iop_module_t *perspective = _module(editor, "ashift", error);
    if(!perspective) return FALSE;
    float *rotation = perspective->get_p(perspective->params, "rotation");
    if(rotation)
    {
      *rotation = 0.0f;
      const char *names[] = { "cl", "ct", "cr", "cb" };
      for(int i = 0; i < 4; i++)
        *(float *)perspective->get_p(perspective->params, names[i]) = i < 2 ? 0.0f : 1.0f;
      *(int *)perspective->get_p(perspective->params, "cropmode") =
        *(int *)perspective->get_p(perspective->default_params, "cropmode");
      dt_dev_add_history_item_ext(&editor->dev, perspective, perspective->enabled, TRUE);
    }
  }
  memcpy(module->params, module->default_params, module->params_size);
  dt_iop_commit_blend_params(module, module->default_blendop_params, NULL);
  module->enabled = module->default_enabled;
  dt_dev_add_history_item_ext(&editor->dev, module, module->enabled, TRUE);
  return TRUE;
}

static gboolean _saved_history_matches(dt_touch_editor_t *editor)
{
  const int image = editor->dev.image_storage.id;
  int end = -1;
  if(!dt_image_get_history_end(image, &end) || end != editor->dev.history_end) return FALSE;
  sqlite3_stmt *statement = NULL;
  if(sqlite3_prepare_v2(dt_database_get(darktable.db),
      "SELECT operation, op_params, enabled, blendop_params, multi_priority, multi_name, multi_name_hand_edited, module"
      " FROM main.history WHERE imgid = ?1 ORDER BY num", -1, &statement, NULL) != SQLITE_OK) return FALSE;
  sqlite3_bind_int(statement, 1, image);
  gboolean same = TRUE;
  // reloading can normalize auto-applied modules; compare the persisted parameter blobs directly
  for(const GList *item = editor->dev.history; same && item; item = item->next)
  {
    const dt_dev_history_item_t *history = item->data;
    same = sqlite3_step(statement) == SQLITE_ROW
      && !g_strcmp0((const char *)sqlite3_column_text(statement, 0), history->module->op)
      && sqlite3_column_bytes(statement, 1) == history->module->params_size
      && !memcmp(sqlite3_column_blob(statement, 1), history->params, history->module->params_size)
      && sqlite3_column_int(statement, 2) == history->enabled
      && sqlite3_column_bytes(statement, 3) == sizeof(dt_develop_blend_params_t)
      && !memcmp(sqlite3_column_blob(statement, 3), history->blend_params, sizeof(dt_develop_blend_params_t))
      && sqlite3_column_int(statement, 4) == history->multi_priority
      && !g_strcmp0((const char *)sqlite3_column_text(statement, 5), history->multi_name)
      && sqlite3_column_int(statement, 6) == history->multi_name_hand_edited
      && sqlite3_column_int(statement, 7) == history->module->version();
  }
  same = same && sqlite3_step(statement) == SQLITE_DONE;
  sqlite3_finalize(statement);
  return same;
}

static int _save_recovery(const int image, char **error)
{
  const char *catalog = sqlite3_db_filename(dt_database_get(darktable.db), "main");
  char *identity = g_compute_checksum_for_string(G_CHECKSUM_SHA256, catalog ? catalog : "", -1);
  // leave room for Unicode profile paths and the temporary suffix on Windows
  identity[16] = '\0';
  char *directory = g_build_filename(darktable.configdir, "recovery-sidecars", identity, NULL);
  char *name = g_strdup_printf("image-%d.xmp", image);
  char *path = g_build_filename(directory, name, NULL);
  char *temporary = g_strconcat(path, ".tmp-XXXXXX", NULL);
  GError *failure = NULL;
  char *contents = NULL;
  gsize size = 0;
  gboolean success = FALSE;
  int fd = -1;
  if(g_mkdir_with_parents(directory, 0700) == 0) fd = g_mkstemp(temporary);
  if(fd >= 0)
  {
    g_close(fd, NULL);
    char source[PATH_MAX] = { 0 };
    dt_image_full_path(image, source, sizeof(source), NULL);
    if(g_file_test(source, G_FILE_TEST_IS_REGULAR))
    {
      if(!dt_exif_xmp_write(image, temporary, TRUE))
        g_file_get_contents(temporary, &contents, &size, &failure);
    }
    else
    {
      // the native writer requires a source file; catalog XMP also protects offline edits
      contents = dt_exif_xmp_read_string(image);
      if(contents) size = strlen(contents);
    }
    if(contents && size)
    {
      const GMarkupParser xml = { 0 };
      GMarkupParseContext *context = g_markup_parse_context_new(&xml, 0, NULL, NULL);
      if(g_markup_parse_context_parse(context, contents, size, &failure)
         && g_markup_parse_context_end_parse(context, &failure))
      {
        GFile *file = g_file_new_for_path(path);
        success = g_file_replace_contents(file, contents, size, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, NULL, &failure);
        g_object_unref(file);
      }
      g_markup_parse_context_free(context);
    }
    g_unlink(temporary);
  }
  if(success)
  {
    if(!_recovery_sidecars) _recovery_sidecars = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    g_hash_table_replace(_recovery_sidecars, GINT_TO_POINTER(image), g_strdup(path));
  }
  else if(error) *error = g_strdup_printf(_("could not update the image sidecar or save recovery at %s: %s; keep this session open and restore write access or free disk space"),
                                         path, failure ? failure->message : errno ? g_strerror(errno) : _("recovery sidecar could not be serialized or written"));
  g_clear_error(&failure);
  g_free(contents); g_free(temporary); g_free(path); g_free(name); g_free(directory); g_free(identity);
  return success;
}

int dt_touch_editor_save(dt_touch_editor_t *editor, char **error)
{
  if(!editor) return TRUE;
  dt_dev_write_history_ext(&editor->dev, editor->dev.image_storage.id);
  if(!_saved_history_matches(editor)) return _error(error, N_("could not verify the saved catalog history; keep this session open"));
  if(dt_image_write_sidecar_file(editor->dev.image_storage.id))
  {
    if(!_save_recovery(editor->dev.image_storage.id, error)) return FALSE;
  }
  else if(_recovery_sidecars) g_hash_table_remove(_recovery_sidecars, GINT_TO_POINTER(editor->dev.image_storage.id));
  g_free(editor->saved_token);
  editor->saved_token = _history_token(editor);
  return TRUE;
}

int dt_touch_editor_save_changed(dt_touch_editor_t *editor, char **error)
{
  if(!editor) return TRUE;
  char *token = _history_token(editor);
  const gboolean saved = !g_strcmp0(token, editor->saved_token);
  g_free(token);
  return saved || dt_touch_editor_save(editor, error);
}

dt_touch_state_t *dt_touch_editor_capture(dt_touch_editor_t *editor)
{
  if(!editor) return NULL;
  dt_touch_state_t *state = g_new0(dt_touch_state_t, 1);
  state->image = editor->dev.image_storage.id;
  state->history = dt_history_duplicate(editor->dev.history);
  state->history_end = editor->dev.history_end;
  state->generation = editor->generation;
  dt_dev_write_history_ext(&editor->dev, state->image);
  dt_history_snapshot_undo_create(state->image, &state->snapshot, &state->history_end);
  return state;
}

int dt_touch_editor_restore(dt_touch_editor_t *editor, const dt_touch_state_t *state, char **error)
{
  if(editor) editor->sync_top = NULL;
  if(!editor || !state || state->image != editor->dev.image_storage.id)
    return _error(error, N_("history snapshot belongs to a different image"));
  if(state->generation != editor->generation)
  {
    dt_history_snapshot_restore(state->image, state->image, state->snapshot, state->history_end);
    return dt_touch_editor_reload(editor, error);
  }
  g_list_free_full(editor->dev.history, dt_dev_free_history_item);
  editor->dev.history = dt_history_duplicate(state->history);
  dt_dev_pop_history_items_ext(&editor->dev, state->history_end);
  return TRUE;
}

void dt_touch_state_free(dt_touch_state_t *state)
{
  if(!state) return;
  dt_history_snapshot_clear(state->image, state->snapshot);
  g_list_free_full(state->history, dt_dev_free_history_item);
  g_free(state);
}

static int _preview(dt_touch_editor_t *editor, int max_width, int max_height,
                      uint8_t **pixels, int *width, int *height,
                      dt_touch_render_t *render, char **error)
{
  if(!editor || max_width < 1 || max_height < 1)
    return _error(error, N_("invalid preview request"));
  if(!editor->input_ready)
  {
    dt_mipmap_cache_get(&editor->input, editor->dev.image_storage.id,
                        DT_MIPMAP_FULL, DT_MIPMAP_BLOCKING, 'r');
    editor->input_ready = editor->input.buf != NULL;
    if(!editor->input_ready)
    {
      dt_mipmap_cache_release(&editor->input);
      return _error(error, N_("could not decode the source image"));
    }
  }
  if(!editor->pipe_ready)
  {
    editor->pipe_ready = dt_dev_pixelpipe_init_export(&editor->pipe,
                                                      MIN(max_width, editor->input.width),
                                                      MIN(max_height, editor->input.height),
                                                      IMAGEIO_RGB | IMAGEIO_INT8, FALSE);
    if(!editor->pipe_ready) return _error(error, N_("could not allocate the preview pipeline"));
    if(editor->rendered && !editor->speculative && !g_getenv("DARKTABLETOUCH_DISABLE_PIPE_CACHE"))
    {
      dt_dev_pixelpipe_cache_cleanup(&editor->pipe);
      dt_dev_pixelpipe_cache_init(&editor->pipe, 12, 0, 2);
    }
    dt_dev_pixelpipe_set_input(&editor->pipe, &editor->dev, (float *)editor->input.buf,
                               editor->input.width, editor->input.height, editor->input.iscale);
    dt_dev_pixelpipe_set_icc(&editor->pipe, DT_COLORSPACE_SRGB, NULL, DT_INTENT_LAST);
    dt_dev_pixelpipe_create_nodes(&editor->pipe, &editor->dev);
    // every newly created node needs its parameters, including unchanged modules
    editor->sync_top = NULL;
  }
  else
  {
    if(editor->rendered && !editor->speculative && editor->pipe.cache.entries == DT_PIPECACHE_MIN
       && !g_getenv("DARKTABLETOUCH_DISABLE_PIPE_CACHE"))
    {
      dt_dev_pixelpipe_cache_cleanup(&editor->pipe);
      editor->pipe.backbuf = NULL;
      dt_dev_pixelpipe_cache_init(&editor->pipe, 12, 0, 2);
    }
    dt_dev_pixelpipe_set_input(&editor->pipe, &editor->dev, (float *)editor->input.buf,
                               editor->input.width, editor->input.height, editor->input.iscale);
  }
  char *token = _history_token(editor);
  if(g_strcmp0(token, editor->synced_token))
  {
    const GList *top = g_list_nth(editor->dev.history, editor->dev.history_end - 1);
    if(editor->sync_top && top && ((dt_dev_history_item_t *)top->data)->module == editor->sync_top)
      dt_dev_pixelpipe_synch_top(&editor->pipe, &editor->dev);
    else dt_dev_pixelpipe_synch_all(&editor->pipe, &editor->dev);
    editor->sync_top = NULL;
    dt_dev_pixelpipe_get_dimensions(&editor->pipe, &editor->dev,
                                    editor->input.width, editor->input.height,
                                    &editor->pipe.processed_width, &editor->pipe.processed_height);
    g_free(editor->synced_token);
    editor->synced_token = token;
  }
  else g_free(token);
  if(editor->pipe.processed_width < 1 || editor->pipe.processed_height < 1)
    return _error(error, N_("the processing pipeline produced invalid image dimensions"));
  const float scale = fminf(1.0f, fminf((float)max_width / editor->pipe.processed_width,
                                       (float)max_height / editor->pipe.processed_height));
  *width = MAX(1, (int)floorf(editor->pipe.processed_width * scale));
  *height = MAX(1, (int)floorf(editor->pipe.processed_height * scale));
  if(render)
  {
    g_mutex_lock(&render->mutex);
    if(render->canceled)
    {
      g_mutex_unlock(&render->mutex);
      return FALSE;
    }
    dt_atomic_set_int(&editor->pipe.shutdown, DT_DEV_PIXELPIPE_STOP_NO);
    render->pipe = &editor->pipe;
    g_mutex_unlock(&render->mutex);
  }
  else dt_atomic_set_int(&editor->pipe.shutdown, DT_DEV_PIXELPIPE_STOP_NO);
  const gboolean failure = dt_dev_pixelpipe_process(&editor->pipe, &editor->dev, 0, 0, *width, *height, scale, DT_DEVICE_NONE);
  gboolean canceled = FALSE;
  if(render)
  {
    g_mutex_lock(&render->mutex);
    render->pipe = NULL;
    canceled = render->canceled;
    g_mutex_unlock(&render->mutex);
  }
  if(canceled) return FALSE;
  if(failure || !editor->pipe.backbuf)
    return _error(error, N_("could not render the preview"));
  editor->rendered = TRUE;
  const size_t size = (size_t)*width * *height * 4;
  *pixels = g_malloc(size);
  memcpy(*pixels, editor->pipe.backbuf, size);
  // gamma leaves the unused BGRX byte undefined; frames must compare deterministically
  for(size_t i = 3; i < size; i += 4) (*pixels)[i] = 255;
  return TRUE;
}

int dt_touch_editor_preview(dt_touch_editor_t *editor, int max_width, int max_height,
                            uint8_t **pixels, int *width, int *height, char **error)
{
  return dt_touch_editor_render(editor, max_width, max_height, pixels, width, height, NULL, error);
}

int dt_touch_editor_draft(dt_touch_editor_t *editor, uint8_t **pixels, int *width, int *height,
                          dt_touch_render_t *render)
{
  if(!editor || !dt_history_hash_is_mipmap_synced(editor->dev.image_storage.id)) return FALSE;
  g_mutex_lock(&render->mutex);
  const gboolean canceled = render->canceled;
  g_mutex_unlock(&render->mutex);
  return !canceled && _thumbnail(editor->dev.image_storage.id, pixels, width, height, DT_MIPMAP_TESTLOCK, NULL);
}

int dt_touch_editor_render(dt_touch_editor_t *editor, int max_width, int max_height,
                           uint8_t **pixels, int *width, int *height,
                           dt_touch_render_t *render, char **error)
{
  const int result = _preview(editor, max_width, max_height, pixels, width, height, render, error);
  if(editor && editor->input_ready)
  {
    dt_mipmap_cache_release(&editor->input);
    editor->input_ready = FALSE;
  }
  return result;
}

int dt_touch_editor_export(dt_touch_editor_t *editor, const char *path,
                           const char *format, char **error)
{
  if(!dt_touch_editor_save(editor, error)) return FALSE;
  if(!editor) return _error(error, N_("no image is open for export"));
  GError *failure = NULL;
  if(!dt_path_check_engine_utf8(path, &failure))
  {
    _error(error, failure->message);
    g_error_free(failure);
    return FALSE;
  }
  return dt_bridge_export_images(NULL, editor->dev.image_storage.id, 0, 0, -1,
                                  path, NULL, NULL, format, 95, FALSE, TRUE, NULL, NULL, error);
}

char *dt_touch_folder_images(const char *folder)
{
  char *key = g_strdup(folder); g_strdelimit(key, "\\", '/');
  JsonArray *array = json_array_new();
  GList *films = dt_film_get_by_access();
  for(GList *film = films; film; film = film->next)
  {
    char *path = dt_film_get_folder(GPOINTER_TO_INT(film->data));
    if(!path) continue;
    g_strdelimit(path, "\\", '/');
    if(!g_strcmp0(path, key) || (g_str_has_prefix(path, key) && path[strlen(key)] == '/'))
    {
      GList *ids = dt_film_get_image_ids(GPOINTER_TO_INT(film->data));
      for(GList *id = ids; id; id = id->next) json_array_add_int_element(array, GPOINTER_TO_INT(id->data));
      g_list_free(ids);
    }
    g_free(path);
  }
  g_list_free(films); g_free(key);
  JsonNode *node = json_node_new(JSON_NODE_ARRAY); json_node_take_array(node, array);
  char *result = json_to_string(node, FALSE); json_node_free(node); return result;
}

int dt_touch_export_image(int image_id, const char *path, const char *format, int width, int height,
                           int quality, int bits, int upscale, int high_quality, int srgb, char **error)
{
  GError *failure = NULL;
  if(!dt_path_check_engine_utf8(path, &failure))
  { _error(error, failure->message); g_error_free(failure); return FALSE; }
  if(width < 0 || height < 0 || width > 100000 || height > 100000 || quality < 1 || quality > 100
     || (bits != 8 && bits != 16)) return _error(error, N_("invalid export dimensions, quality, or bit depth"));
  const char *keys[] = { "plugins/lighttable/export/icctype", "plugins/imageio/format/png/bpp", "plugins/imageio/format/tiff/bpp" };
  int saved[3];
  for(int i = 0; i < 3; i++) saved[i] = dt_conf_get_int(keys[i]);
  dt_conf_set_int(keys[0], srgb ? DT_COLORSPACE_SRGB : -1);
  dt_conf_set_int(keys[1], bits); dt_conf_set_int(keys[2], bits);
  const int result = dt_bridge_export_images(NULL, image_id, width, height, -1, path, NULL, NULL,
                                              format, quality, upscale, high_quality, NULL, NULL, error);
  for(int i = 0; i < 3; i++) dt_conf_set_int(keys[i], saved[i]);
  return result;
}

char *dt_touch_image_path(int image_id)
{
  if(!dt_image_exists(image_id)) return NULL;
  char path[PATH_MAX] = {0}; gboolean cached = FALSE;
  dt_image_full_path(image_id, path, sizeof(path), &cached);
  return *path ? g_strdup(path) : NULL;
}

void dt_touch_settings_set(int show_rejected, int advance_after_reject)
{
  (void)show_rejected;
  dt_conf_set_bool("touch/advance_after_reject", advance_after_reject);
}

void dt_touch_ui_options_set(int override, int scale, int swipe, int quality)
{
  dt_conf_set_bool("touch/scale_override", override);
  dt_conf_set_int("touch/ui_scale", CLAMP(scale, 75, 150));
  dt_conf_set_bool("touch/swipe_images", swipe);
  dt_conf_set_int("touch/preview_quality", CLAMP(quality, 640, 4096));
}

char *dt_touch_export_name(int image, const char *pattern, int sequence, int width, int height, int upscale)
{
  char *path = dt_touch_image_path(image);
  if(!path) return NULL;
  dt_variables_params_t *params;
  dt_variables_params_init(&params);
  params->filename = path;
  params->imgid = image;
  params->jobcode = "export";
  params->sequence = sequence;
  dt_variables_set_max_width_height(params, width, height);
  dt_variables_set_upscale(params, upscale);
  char *template = g_strdup(pattern);
  char *name = dt_variables_expand(params, template, FALSE);
  g_free(template);
  dt_variables_params_destroy(params);
  g_free(path);
  return name;
}

int dt_touch_editor_reload(dt_touch_editor_t *editor, char **error)
{
  if(!editor) return TRUE;
  const int id = editor->dev.image_storage.id;
  if(!dt_image_exists(id)) return _error(error, N_("the editing image no longer exists"));
  if(editor->pipe_ready) dt_dev_pixelpipe_cleanup(&editor->pipe);
  if(editor->input_ready) dt_mipmap_cache_release(&editor->input);
  dt_dev_cleanup(&editor->dev);
  g_free(editor->synced_token);
  g_free(editor->saved_token);
  memset(editor, 0, sizeof(*editor)); editor->generation = ++_editor_generation;
  dt_dev_init(&editor->dev, FALSE); dt_dev_load_image(&editor->dev, id);
  dt_dev_pop_history_items_ext(&editor->dev, editor->dev.history_end);
  return TRUE;
}

static char *_history_token(dt_touch_editor_t *source)
{
  // the render hash omits disabled states and older rows that determine indices
  GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
  g_checksum_update(checksum, (const guchar *)&source->dev.history_end, sizeof(source->dev.history_end));
  for(const GList *item = source->dev.history; item; item = item->next) {
    const dt_dev_history_item_t *history = item->data;
    g_checksum_update(checksum, (const guchar *)history->op_name, strlen(history->op_name) + 1);
    g_checksum_update(checksum, (const guchar *)&history->num, sizeof(history->num));
    g_checksum_update(checksum, (const guchar *)&history->enabled, sizeof(history->enabled));
    g_checksum_update(checksum, (const guchar *)&history->iop_order, sizeof(history->iop_order));
    g_checksum_update(checksum, (const guchar *)&history->multi_priority, sizeof(history->multi_priority));
    g_checksum_update(checksum, (const guchar *)history->multi_name, strlen(history->multi_name) + 1);
    g_checksum_update(checksum, (const guchar *)&history->multi_name_hand_edited, sizeof(history->multi_name_hand_edited));
    const int size = history->module && history->params ? history->module->params_size : 0;
    g_checksum_update(checksum, (const guchar *)&size, sizeof(size));
    if(size) g_checksum_update(checksum, (const guchar *)history->params, size);
    if(history->blend_params)
      g_checksum_update(checksum, (const guchar *)history->blend_params, sizeof(*history->blend_params));
    const int forms = g_list_length(history->forms);
    g_checksum_update(checksum, (const guchar *)&forms, sizeof(forms));
    for(const GList *f = history->forms; f; f = f->next) {
      const dt_masks_form_t *form = f->data;
      g_checksum_update(checksum, (const guchar *)&form->type, sizeof(form->type));
      g_checksum_update(checksum, (const guchar *)&form->version, sizeof(form->version));
      g_checksum_update(checksum, (const guchar *)&form->formid, sizeof(form->formid));
      g_checksum_update(checksum, (const guchar *)form->name, strlen(form->name) + 1);
      g_checksum_update(checksum, (const guchar *)form->source, sizeof(form->source));
      const int points = g_list_length(form->points);
      g_checksum_update(checksum, (const guchar *)&points, sizeof(points));
      for(const GList *point = form->points; point; point = point->next)
        g_checksum_update(checksum, point->data, form->functions->point_struct_size);
    }
  }
  char *order = dt_ioppr_serialize_text_iop_order_list(source->dev.iop_order_list);
  if(order) g_checksum_update(checksum, (const guchar *)order, strlen(order) + 1);
  char *token = g_strdup(g_checksum_get_string(checksum));
  g_free(order); g_checksum_free(checksum);
  return token;
}

char *dt_touch_history_clipboard(int image_id, char **error)
{
  dt_touch_editor_t *source = dt_touch_editor_new(image_id, error);
  if(!source) return NULL;
  dt_dev_write_history_ext(&source->dev, image_id);
  char *text = dt_touch_editor_history(source);
  JsonParser *parser = json_parser_new(); json_parser_load_from_data(parser, text, -1, NULL); g_free(text);
  JsonObject *object = json_node_get_object(json_parser_get_root(parser));
  JsonArray *steps = json_object_get_array_member(object, "steps");
  const int end = source->dev.history_end;
  for(guint i = 0; i < json_array_get_length(steps); i++)
  {
    JsonObject *step = json_array_get_object_element(steps, i);
    const dt_dev_history_item_t *history = g_list_nth_data(source->dev.history, i);
    gboolean latest = TRUE;
    int later_index = i + 1;
    for(GList *later = g_list_nth(source->dev.history, i + 1); later && later_index < end; later = later->next, later_index++) {
      const dt_dev_history_item_t *h = later->data;
      if(!strcmp(h->op_name, history->op_name) && h->multi_priority == history->multi_priority) { latest = FALSE; break; }
    }
    // native module copy uses the final enabled/name state, not an older row's
    const gboolean selectable = latest && (int)i < end && history->module && !dt_iop_is_hidden(history->module);
    json_object_set_boolean_member(step, "selectable", selectable);
    json_object_set_boolean_member(step, "safe", selectable && !dt_history_module_skip_copy(history->module->flags()));
    json_object_set_boolean_member(step, "latest", latest);
  }
  char *token = _history_token(source);
  json_object_set_string_member(object, "token", token); g_free(token);
  json_object_set_int_member(object, "source", image_id);
  json_object_set_string_member(object, "filename", source->dev.image_storage.filename);
  text = json_to_string(json_parser_get_root(parser), FALSE);
  g_object_unref(parser); dt_touch_editor_free(source); return text;
}

struct dt_touch_catalog_edit_t { GList *records; };

static void _clear_native_before(gpointer user, dt_undo_type_t type, dt_undo_data_t data)
{
  if(data && type == DT_UNDO_LT_HISTORY) {
    dt_undo_lt_history_t *record = data;
    dt_history_snapshot_clear(record->imgid, record->before);
  }
}

dt_touch_catalog_edit_t *dt_touch_history_apply(const char *request_json, char **error)
{
  JsonParser *parser = json_parser_new();
  if(!json_parser_load_from_data(parser, request_json, -1, NULL) || !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser)))
  { g_object_unref(parser); _error(error, N_("invalid history request")); return NULL; }
  JsonObject *request = json_node_get_object(json_parser_get_root(parser));
  const gboolean reset = json_object_get_boolean_member_with_default(request, "reset", FALSE);
  const int source = json_object_get_int_member_with_default(request, "source", 0);
  GList *ops = NULL;
  if(!reset)
  {
    char *descriptor = dt_touch_history_clipboard(source, error);
    if(!descriptor) { g_object_unref(parser); return NULL; }
    JsonParser *check = json_parser_new(); json_parser_load_from_data(check, descriptor, -1, NULL); g_free(descriptor);
    JsonObject *current = json_node_get_object(json_parser_get_root(check));
    if(g_strcmp0(json_object_get_string_member_with_default(request, "token", ""), json_object_get_string_member(current, "token")))
    { g_object_unref(check); g_object_unref(parser); _error(error, N_("the source history changed; copy it again before pasting")); return NULL; }
    JsonArray *selected = json_object_get_array_member(request, "steps");
    JsonArray *available = json_object_get_array_member(current, "steps");
    if(selected) for(guint i = 0; i < json_array_get_length(selected); i++) {
      JsonObject *choice = json_array_get_object_element(selected, i);
      const int index = json_object_get_int_member_with_default(choice, "index", 0) - 1;
      if(index < 0 || index >= (int)json_array_get_length(available)
         || !json_object_get_boolean_member(json_array_get_object_element(available, index), "selectable"))
      { g_list_free(ops); g_object_unref(check); g_object_unref(parser); _error(error, N_("a selected history step is unavailable")); return NULL; }
      const gboolean automatic = index > 0 && json_object_get_boolean_member_with_default(choice, "auto_init", FALSE);
      ops = g_list_append(ops, GINT_TO_POINTER(automatic ? -index : index));
    }
    g_object_unref(check);
    if(!ops) { g_object_unref(parser); _error(error, N_("select at least one history step")); return NULL; }
  }
  JsonArray *targets = json_object_get_array_member(request, "targets");
  if(!targets || !json_array_get_length(targets)) { g_list_free(ops); g_object_unref(parser); _error(error, N_("select a destination image")); return NULL; }
  dt_touch_catalog_edit_t *edit = g_new0(dt_touch_catalog_edit_t, 1);
  dt_undo_t *saved_undo = darktable.undo; darktable.undo = dt_undo_init();
  gboolean success = TRUE;
  for(guint i = 0; i < json_array_get_length(targets); i++)
  {
    const int id = json_array_get_int_element(targets, i);
    if(!reset && id == source) continue;
    if(!dt_image_exists(id)) { success = _error(error, N_("a destination image no longer exists")); break; }
    dt_undo_lt_history_t *record = g_new0(dt_undo_lt_history_t, 1); record->imgid = id; record->after = -1;
    dt_history_snapshot_undo_create(id, &record->before, &record->before_history_end);
    edit->records = g_list_append(edit->records, record);
    if(reset) dt_history_delete_on_image_ext(id, FALSE, TRUE);
    else if(!dt_dtdata_merge(source, id) || !dt_history_copy_and_paste_on_image(source, id,
      json_object_get_boolean_member_with_default(request, "merge", TRUE), ops,
      json_object_get_boolean_member_with_default(request, "copy_order", FALSE), TRUE, FALSE))
    { success = _error(error, N_("could not paste the selected history and masks")); break; }
    dt_history_snapshot_undo_create(id, &record->after, &record->after_history_end);
    if(dt_image_write_sidecar_file(id) && !_save_recovery(id, error)) { success = FALSE; break; }
  }
  dt_undo_iterate(darktable.undo, DT_UNDO_LT_HISTORY, NULL, _clear_native_before);
  dt_undo_cleanup(darktable.undo); darktable.undo = saved_undo;
  g_list_free(ops); g_object_unref(parser);
  if(!success || !edit->records) {
    dt_touch_catalog_restore(edit, FALSE, NULL); dt_touch_catalog_free(edit);
    if(success) _error(error, N_("choose a destination different from the copied image"));
    return NULL;
  }
  return edit;
}

int dt_touch_catalog_restore(dt_touch_catalog_edit_t *edit, int redo, char **error)
{
  for(GList *item = edit ? edit->records : NULL; item; item = item->next) {
    dt_undo_lt_history_t *record = item->data;
    if(!dt_image_exists(record->imgid)) return _error(error, N_("an image in this history action was removed"));
  }
  for(GList *item = edit ? edit->records : NULL; item; item = item->next) {
    dt_undo_lt_history_t *record = item->data;
    dt_history_snapshot_restore(record->imgid, record->imgid, redo ? record->after : record->before,
                                redo ? record->after_history_end : record->before_history_end);
    dt_mipmap_cache_remove(record->imgid);
  }
  gboolean saved = TRUE;
  for(GList *item = edit ? edit->records : NULL; item; item = item->next) {
    const dt_undo_lt_history_t *record = item->data;
    if(dt_image_write_sidecar_file(record->imgid) && !_save_recovery(record->imgid, NULL)) saved = FALSE;
  }
  if(!saved) {
    _error(error, N_("history restored in the catalog, but some sidecars could not be saved"));
    return -1;
  }
  return 1;
}

void dt_touch_catalog_free(dt_touch_catalog_edit_t *edit)
{
  if(!edit) return;
  for(GList *item = edit->records; item; item = item->next) {
    dt_undo_lt_history_t *record = item->data;
    dt_history_snapshot_clear(record->imgid, record->before);
    if(record->after >= 0) dt_history_snapshot_clear(record->imgid, record->after);
    g_free(record);
  }
  g_list_free(edit->records); g_free(edit);
}
