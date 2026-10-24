/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/engine_bridge.h"
#include "common/darktable.h"
#include "common/history.h"
#include "common/filepath.h"
#include "common/image.h"
#include "common/mipmap_cache.h"
#include "common/touch_session.h"
#include "common/collection.h"
#include "common/selection.h"
#include "common/film.h"
#include "common/overlay.h"
#include "common/dtdata.h"
#include "control/conf.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/pixelpipe_hb.h"
#include "imageio/imageio_module.h"
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
};

struct dt_touch_state_t
{
  dt_imgid_t image;
  GList *history;
  int history_end;
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

char *dt_touch_editor_information(dt_touch_editor_t *editor, char **error)
{
  return editor ? dt_bridge_get_metadata_json(editor->dev.image_storage.id, error) : NULL;
}

int dt_touch_thumbnail(int image_id, uint8_t **pixels, int *width, int *height, char **error)
{
  dt_mipmap_buffer_t buffer = { 0 };
  dt_mipmap_cache_get(&buffer, image_id, DT_MIPMAP_1, DT_MIPMAP_BLOCKING, 'r');
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
  dt_touch_editor_t *editor = g_new0(dt_touch_editor_t, 1);
  dt_dev_init(&editor->dev, FALSE);
  dt_dev_load_image(&editor->dev, image_id);
  dt_dev_pop_history_items_ext(&editor->dev, editor->dev.history_end);
  return editor;
}

void dt_touch_editor_free(dt_touch_editor_t *editor)
{
  if(!editor) return;
  if(editor->pipe_ready) dt_dev_pixelpipe_cleanup(&editor->pipe);
  if(editor->input_ready) dt_mipmap_cache_release(&editor->input);
  dt_dev_cleanup(&editor->dev);
  g_free(editor);
}

static dt_iop_module_t *_module(dt_touch_editor_t *editor, const char *operation, char **error)
{
  dt_iop_module_t *module = editor && operation
    ? dt_iop_get_module_by_op_priority(editor->dev.iop, operation, 0) : NULL;
  if(!module) _error(error, N_("module is unavailable for this editing session"));
  return module;
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
      if(strcmp(json_object_get_string_member(field, "name"), "rotation")) continue;
      JsonNode *copy = json_node_copy(json_array_get_element(angle_fields, i));
      JsonObject *control = json_node_get_object(copy);
      json_object_set_string_member(control, "operation", "ashift");
      json_object_set_string_member(control, "label", _("precise angle"));
      json_object_set_string_member(control, "unit", _("deg"));
      json_object_set_double_member(control, "step", 0.01);
      json_object_set_double_member(control, "soft_min", -15.0);
      json_object_set_double_member(control, "soft_max", 15.0);
      json_array_add_element(fields, copy);
      break;
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
  return TRUE;
}

int dt_touch_editor_enable(dt_touch_editor_t *editor, const char *operation,
                           int enabled, char **error)
{
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
  dt_iop_module_t *module = _module(editor, operation, error);
  if(!module) return FALSE;
  if(!strcmp(operation, "flip"))
  {
    dt_iop_module_t *perspective = _module(editor, "ashift", error);
    if(!perspective) return FALSE;
    float *rotation = perspective->get_p(perspective->params, "rotation");
    if(rotation && *rotation != 0.0f)
    {
      *rotation = 0.0f;
      dt_dev_add_history_item_ext(&editor->dev, perspective, perspective->enabled, TRUE);
    }
  }
  memcpy(module->params, module->default_params, module->params_size);
  dt_iop_commit_blend_params(module, module->default_blendop_params, NULL);
  module->enabled = module->default_enabled;
  dt_dev_add_history_item_ext(&editor->dev, module, module->enabled, TRUE);
  return TRUE;
}

int dt_touch_editor_save(dt_touch_editor_t *editor, char **error)
{
  if(!editor) return TRUE;
  dt_dev_write_history_ext(&editor->dev, editor->dev.image_storage.id);
  if(dt_image_write_sidecar_file(editor->dev.image_storage.id))
    return _error(error, N_("could not save the image sidecar; edits remain open"));
  return TRUE;
}

dt_touch_state_t *dt_touch_editor_capture(dt_touch_editor_t *editor)
{
  if(!editor) return NULL;
  dt_touch_state_t *state = g_new0(dt_touch_state_t, 1);
  state->image = editor->dev.image_storage.id;
  state->history = dt_history_duplicate(editor->dev.history);
  state->history_end = editor->dev.history_end;
  return state;
}

int dt_touch_editor_restore(dt_touch_editor_t *editor, const dt_touch_state_t *state, char **error)
{
  if(!editor || !state || state->image != editor->dev.image_storage.id)
    return _error(error, N_("history snapshot belongs to a different image"));
  g_list_free_full(editor->dev.history, dt_dev_free_history_item);
  editor->dev.history = dt_history_duplicate(state->history);
  dt_dev_pop_history_items_ext(&editor->dev, state->history_end);
  return TRUE;
}

void dt_touch_state_free(dt_touch_state_t *state)
{
  if(!state) return;
  g_list_free_full(state->history, dt_dev_free_history_item);
  g_free(state);
}

static int _preview(dt_touch_editor_t *editor, int max_width, int max_height,
                      uint8_t **pixels, int *width, int *height, char **error)
{
  if(!editor || max_width < 1 || max_height < 1 || max_width > 4096 || max_height > 4096)
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
    editor->pipe_ready = dt_dev_pixelpipe_init_export(&editor->pipe, max_width, max_height,
                                                      IMAGEIO_RGB | IMAGEIO_INT8, FALSE);
    if(!editor->pipe_ready) return _error(error, N_("could not allocate the preview pipeline"));
    dt_dev_pixelpipe_set_input(&editor->pipe, &editor->dev, (float *)editor->input.buf,
                               editor->input.width, editor->input.height, editor->input.iscale);
    dt_dev_pixelpipe_set_icc(&editor->pipe, DT_COLORSPACE_SRGB, NULL, DT_INTENT_LAST);
    dt_dev_pixelpipe_create_nodes(&editor->pipe, &editor->dev);
  }
  else dt_dev_pixelpipe_set_input(&editor->pipe, &editor->dev, (float *)editor->input.buf,
                             editor->input.width, editor->input.height, editor->input.iscale);
  dt_dev_pixelpipe_synch_all(&editor->pipe, &editor->dev);
  dt_dev_pixelpipe_get_dimensions(&editor->pipe, &editor->dev,
                                  editor->input.width, editor->input.height,
                                  &editor->pipe.processed_width, &editor->pipe.processed_height);
  if(editor->pipe.processed_width < 1 || editor->pipe.processed_height < 1)
    return _error(error, N_("the processing pipeline produced invalid image dimensions"));
  const float scale = fminf(1.0f, fminf((float)max_width / editor->pipe.processed_width,
                                       (float)max_height / editor->pipe.processed_height));
  *width = MAX(1, (int)floorf(editor->pipe.processed_width * scale));
  *height = MAX(1, (int)floorf(editor->pipe.processed_height * scale));
  if(dt_dev_pixelpipe_process(&editor->pipe, &editor->dev, 0, 0, *width, *height, scale, DT_DEVICE_NONE)
     || !editor->pipe.backbuf)
    return _error(error, N_("could not render the preview"));
  const size_t size = (size_t)*width * *height * 4;
  *pixels = g_malloc(size);
  memcpy(*pixels, editor->pipe.backbuf, size);
  return TRUE;
}

int dt_touch_editor_preview(dt_touch_editor_t *editor, int max_width, int max_height,
                            uint8_t **pixels, int *width, int *height, char **error)
{
  const int result = _preview(editor, max_width, max_height, pixels, width, height, error);
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
