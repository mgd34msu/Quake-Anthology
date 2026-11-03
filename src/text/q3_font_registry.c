/* Port of src/text/q3-font-registry.ts. GPL-2.0-or-later. */
#include "qa/text/q3_font_registry.h"
#include "qa/network/common/js_number.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js_case.h"

#define RECORD QA_Q3_FONT_RECORD_BYTES

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static void pending_release(void *raw) {
  ((qa_q3_renderer_font_registry *)raw)->pending--;
}

static void require_open(const qa_q3_renderer_font_registry *registry) {
  if (registry->closed) qa_error("Renderer font registry is closed");
}

static void do_print(qa_font_print_fn print, void *print_ctx, const char *text) {
  if (print != NULL) print(print_ctx, text);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

/* String(number) for the sizes registerFont makes. */
static void format_size(double size, char *out, size_t out_size) {
  qa_net_js_number(size, out, out_size);
}

/* DataView.setInt32 value conversion (ToIntegerOrInfinity, then
 * modulo 2^32 into the signed range; non-finite becomes 0 via the
 * JS int32 conversion path used for order doubles). */
static int32_t to_int32(double value) {
  if (!isfinite(value)) return 0;
  double magnitude = fmod(fabs(trunc(value)), 4294967296.0);
  uint32_t bits = (uint32_t)magnitude;
  if (value < 0) bits = (uint32_t)(-(int64_t)bits);
  int32_t out;
  memcpy(&out, &bits, sizeof out);
  return out;
}

/* Uint8Array element store of a JS number (truncate, modulo 256). */
static uint8_t to_uint8(double value) {
  if (!isfinite(value)) return 0;
  double wrapped = fmod(trunc(value), 256.0);
  if (wrapped < 0) wrapped += 256.0;
  return (uint8_t)wrapped;
}

static void set_i32_le(uint8_t *record, size_t offset, int32_t value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  for (int i = 0; i < 4; i++) record[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}

static void set_f32_le(uint8_t *record, size_t offset, double value) {
  float narrowed = (float)value;
  uint32_t bits;
  memcpy(&bits, &narrowed, sizeof bits);
  for (int i = 0; i < 4; i++) record[offset + (size_t)i] = (uint8_t)(bits >> (8 * i));
}

static const char *freetype_kind_text(qa_freetype_init_kind kind) {
  switch (kind) {
    case QA_FREETYPE_UNAVAILABLE: return "unavailable";
    case QA_FREETYPE_FAILED: return "failed";
    case QA_FREETYPE_READY: return "ready";
  }
  return "unavailable";
}

/* qa_freetype_print_fn is (text, ctx); qa_font_print_fn is (ctx, text). */
typedef struct print_adapter {
  qa_font_print_fn print;
  void *print_ctx;
} print_adapter;

static void freetype_print(const char *text, void *ctx) {
  print_adapter *adapter = (print_adapter *)ctx;
  if (adapter->print != NULL) adapter->print(adapter->print_ctx, text);
}

/* TS FreeTypeFontLibrary.open(print?). Sets freetype state. */
static void open_freetype(qa_q3_renderer_font_registry *registry,
                          qa_font_print_fn print, void *print_ctx) {
  print_adapter adapter = {print, print_ctx};
  registry->freetype_kind = qa_freetype_font_library_open(
      &registry->freetype, print == NULL ? NULL : freetype_print, &adapter, NULL, 0);
  registry->has_freetype = true;
}

static bool push_picture(qa_q3_renderer_font_registry *registry,
                         const qa_material_picture *picture,
                         const qa_picture_asset **out) {
  qa_picture_asset *asset = (qa_picture_asset *)malloc(sizeof *asset);
  if (asset == NULL) return false;
  memset(asset, 0, sizeof *asset);
  asset->kind = QA_PICTURE_MATERIAL;
  asset->material = *picture;
  if (registry->picture_count == registry->picture_capacity) {
    size_t capacity = registry->picture_capacity == 0 ? 8 : registry->picture_capacity * 2;
    qa_picture_asset **grown =
        (qa_picture_asset **)realloc(registry->pictures, capacity * sizeof *grown);
    if (grown == NULL) {
      free(asset);
      return false;
    }
    registry->pictures = grown;
    registry->picture_capacity = capacity;
  }
  registry->pictures[registry->picture_count++] = asset;
  *out = asset;
  return true;
}

static bool generated_set(qa_q3_renderer_font_registry *registry, const char *name,
                          const uint8_t *rgba, size_t length) {
  for (size_t i = 0; i < registry->generated_count; i++) {
    qa_q3_renderer_generated_entry *entry = &registry->generated[i];
    if (strcmp(entry->name, name) != 0) continue;
    uint8_t *copy = (uint8_t *)malloc(length == 0 ? 1 : length);
    if (copy == NULL) return false;
    memcpy(copy, rgba, length);
    free(entry->rgba);
    entry->rgba = copy;
    entry->rgba_length = length;
    return true;
  }
  if (registry->generated_count == registry->generated_capacity) {
    size_t capacity =
        registry->generated_capacity == 0 ? 4 : registry->generated_capacity * 2;
    qa_q3_renderer_generated_entry *grown = (qa_q3_renderer_generated_entry *)realloc(
        registry->generated, capacity * sizeof *grown);
    if (grown == NULL) return false;
    registry->generated = grown;
    registry->generated_capacity = capacity;
  }
  char *name_copy = dup_text(name);
  uint8_t *copy = (uint8_t *)malloc(length == 0 ? 1 : length);
  if (name_copy == NULL || copy == NULL) {
    free(name_copy);
    free(copy);
    return false;
  }
  memcpy(copy, rgba, length);
  qa_q3_renderer_generated_entry *entry =
      &registry->generated[registry->generated_count++];
  entry->name = name_copy;
  entry->rgba = copy;
  entry->rgba_length = length;
  return true;
}

static bool retained_add(qa_q3_renderer_font_registry *registry,
                         qa_retained_font_file file) {
  for (size_t i = 0; i < registry->retained_count; i++)
    if (registry->retained[i].bytes.bytes == file.bytes.bytes &&
        registry->retained[i].bytes.length == file.bytes.length &&
        registry->retained[i].length == file.length)
      return true; /* TS Set identity: already retained */
  if (registry->retained_count == registry->retained_capacity) {
    size_t capacity =
        registry->retained_capacity == 0 ? 4 : registry->retained_capacity * 2;
    qa_retained_font_file *grown =
        (qa_retained_font_file *)realloc(registry->retained, capacity * sizeof *grown);
    if (grown == NULL) return false;
    registry->retained = grown;
    registry->retained_capacity = capacity;
  }
  registry->retained[registry->retained_count++] = file;
  return true;
}

static qa_q3_font_record_view destination_record(
    qa_q3_font_destination_fn destination, void *destination_ctx) {
  qa_q3_font_record_view record = destination(destination_ctx);
  if (record.length != RECORD)
    qa_range_error("fontInfo_t destination must contain exactly 20548 bytes");
  return record;
}

void qa_q3_renderer_font_registry_init(
    qa_q3_renderer_font_registry *registry, qa_font_file_reader reader,
    qa_q3_font_register_picture_fn register_picture, void *register_picture_ctx,
    qa_q3_font_sync_render_thread_fn sync_render_thread,
    void *sync_render_thread_ctx,
    const qa_q3_font_generation_services *generation) {
  memset(registry, 0, sizeof *registry);
  registry->reader = reader;
  registry->register_picture = register_picture;
  registry->register_picture_ctx = register_picture_ctx;
  registry->sync_render_thread = sync_render_thread;
  registry->sync_render_thread_ctx = sync_render_thread_ctx;
  registry->has_generation = generation != NULL;
  if (generation != NULL) registry->generation = *generation;
}

void qa_q3_renderer_font_registry_close(qa_q3_renderer_font_registry *registry) {
  if (registry->closed) return;
  registry->closed = true;
  if (registry->has_freetype && registry->freetype_kind == QA_FREETYPE_READY &&
      !qa_freetype_font_library_close(&registry->freetype))
    qa_error("FreeType cleanup failed"); /* TS AggregateError text */
  registry->has_freetype = false;
  for (size_t i = 0; i < registry->font_count; i++) {
    free(registry->fonts[i]->record);
    free(registry->fonts[i]);
    registry->fonts[i] = NULL;
  }
  registry->font_count = 0;
  for (size_t i = 0; i < registry->picture_count; i++) free(registry->pictures[i]);
  registry->picture_count = 0;
  for (size_t i = 0; i < registry->generated_count; i++) {
    free(registry->generated[i].name);
    free(registry->generated[i].rgba);
  }
  registry->generated_count = 0;
  for (size_t i = 0; i < registry->retained_count; i++)
    registry->reader.free_file(registry->reader.ctx, registry->retained[i]);
  registry->retained_count = 0;
}

void qa_q3_renderer_font_registry_destroy(qa_q3_renderer_font_registry *registry) {
  qa_q3_renderer_font_registry_close(registry);
  free(registry->pictures);
  free(registry->generated);
  free(registry->retained);
  memset(registry, 0, sizeof *registry);
}

/* qa_save_value_set/push take ownership on success only. */
static bool save_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (qa_save_value_set(object, key, child)) return true;
  qa_save_value_free(child);
  return false;
}

static bool save_push(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (qa_save_value_push(array, child)) return true;
  qa_save_value_free(child);
  return false;
}

qa_save_value *qa_q3_renderer_font_registry_capture_checkpoint(
    qa_q3_renderer_font_registry *registry) {
  require_open(registry);
  if (registry->pending != 0)
    qa_error("Cannot checkpoint pending font registration");
  qa_save_value *root = qa_save_value_object();
  if (root == NULL) return NULL;
  qa_save_value *kind = registry->has_freetype
                            ? qa_save_value_string(
                                  freetype_kind_text(registry->freetype_kind))
                            : qa_save_value_null();
  if (!save_set(root, "freeType", kind)) goto oom;
  qa_save_value *generated = qa_save_value_array();
  if (generated == NULL) goto oom;
  if (!save_set(root, "generated", generated)) goto oom;
  for (size_t i = 0; i < registry->generated_count; i++) {
    const qa_q3_renderer_generated_entry *entry = &registry->generated[i];
    qa_save_value *row = qa_save_value_object();
    if (row == NULL || !save_set(row, "name", qa_save_value_string(entry->name)) ||
        !save_set(row, "rgba",
                  qa_save_value_bytes(entry->rgba, entry->rgba_length))) {
      qa_save_value_free(row);
      goto oom;
    }
    if (!save_push(generated, row)) goto oom; /* save_push freed row */
  }
  qa_save_value *fonts = qa_save_value_array();
  if (fonts == NULL) goto oom;
  if (!save_set(root, "fonts", fonts)) goto oom;
  for (size_t i = 0; i < registry->font_count; i++) {
    const qa_q3_renderer_font_entry *entry = registry->fonts[i];
    qa_save_value *row = qa_save_value_object();
    qa_save_value *pictures = qa_save_value_array();
    if (row == NULL || pictures == NULL ||
        !save_set(row, "record", qa_save_value_bytes(entry->record, RECORD))) {
      qa_save_value_free(row);
      qa_save_value_free(pictures);
      goto oom;
    }
    bool ok = true;
    for (int glyph = 0; glyph < QA_Q3_FONT_GLYPHS && ok; glyph++) {
      const qa_picture_asset *picture = entry->font.glyphs[glyph].picture;
      const char *name = picture == NULL ? NULL
                         : picture->kind == QA_PICTURE_MATERIAL
                             ? picture->material.name
                             : picture->image.name;
      ok = save_push(pictures, name == NULL ? qa_save_value_null()
                                            : qa_save_value_string(name));
    }
    if (!ok || !save_set(row, "pictures", pictures)) {
      if (!ok) qa_save_value_free(pictures); /* save_set freed it when ok */
      qa_save_value_free(row);
      goto oom;
    }
    if (!save_push(fonts, row)) goto oom; /* save_push freed row */
  }
  return root;
oom:
  qa_save_value_free(root);
  return NULL;
}

/* Temporary name -> picture map for one restore (TS local Map).
 * Names borrow the checkpoint tree, which outlives the call. */
typedef struct restore_picture_map {
  const char **names;
  const qa_picture_asset **pictures;
  size_t count;
  size_t capacity;
} restore_picture_map;

static const qa_picture_asset *restore_map_get(const restore_picture_map *map,
                                               const char *name) {
  for (size_t i = 0; i < map->count; i++)
    if (strcmp(map->names[i], name) == 0) return map->pictures[i];
  return NULL;
}

static bool restore_map_set(restore_picture_map *map, const char *name,
                            const qa_picture_asset *picture) {
  if (map->count == map->capacity) {
    size_t capacity = map->capacity == 0 ? 4 : map->capacity * 2;
    const char **names = (const char **)realloc(map->names, capacity * sizeof *names);
    if (names == NULL) return false;
    map->names = names;
    const qa_picture_asset **pictures =
        (const qa_picture_asset **)realloc(map->pictures, capacity * sizeof *pictures);
    if (pictures == NULL) return false;
    map->pictures = pictures;
    map->capacity = capacity;
  }
  map->names[map->count] = name;
  map->pictures[map->count] = picture;
  map->count++;
  return true;
}

/* TS `finally { this.pending--; }` plus the locals a raise abandons. */
typedef struct restore_locals {
  qa_q3_renderer_font_registry *registry;
  restore_picture_map *generated;
  qa_save_reader *reader;
} restore_locals;

static void restore_locals_release(void *raw) {
  restore_locals *locals = (restore_locals *)raw;
  free(locals->generated->names);
  free(locals->generated->pictures);
  memset(locals->generated, 0, sizeof *locals->generated);
  locals->registry->pending--;
  qa_save_reader_destroy(locals->reader);
}

bool qa_q3_renderer_font_registry_restore_checkpoint(
    qa_q3_renderer_font_registry *registry, const qa_save_value *value) {
  require_open(registry);
  if (registry->pending != 0 || registry->font_count != 0 || registry->has_freetype)
    qa_error("Font restore requires an empty owner");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "client-fonts")) return false;
  bool has_kind = false;
  qa_freetype_init_kind wanted = QA_FREETYPE_UNAVAILABLE;
  qa_save_reader field;
  if (!qa_save_reader_field(&reader, "freeType", &field)) {
    qa_save_reader_destroy(&reader);
    return false;
  }
  if (!qa_save_reader_is_null(&field)) {
    const char *const choices[] = {"unavailable", "failed", "ready"};
    const char *text = qa_save_reader_choice_string(&field, choices, 3);
    has_kind = true;
    wanted = strcmp(text, "unavailable") == 0
                 ? QA_FREETYPE_UNAVAILABLE
                 : strcmp(text, "failed") == 0 ? QA_FREETYPE_FAILED
                                               : QA_FREETYPE_READY;
  }
  qa_save_reader_destroy(&field);
  registry->pending++;
  bool ok = true;
  restore_picture_map generated = {NULL, NULL, 0, 0};
  restore_locals locals = {registry, &generated, &reader};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, restore_locals_release, &locals);
  if (has_kind) {
    open_freetype(registry, NULL, NULL);
    if (registry->freetype_kind != wanted)
      qa_save_reader_fail(&reader, "font generator availability changed");
  }
  if (ok) {
    qa_save_reader list;
    if (!qa_save_reader_field(&reader, "generated", &list)) {
      ok = false;
    } else {
      size_t count = qa_save_reader_list_count(&list);
      for (size_t i = 0; i < count && ok; i++) {
        qa_save_reader item, name_reader, rgba_reader;
        if (!qa_save_reader_item(&list, i, &item)) {
          ok = false;
          break;
        }
        if (!qa_save_reader_field(&item, "name", &name_reader)) {
          qa_save_reader_destroy(&item);
          ok = false;
          break;
        }
        const char *name = qa_save_reader_string(&name_reader);
        qa_save_reader_destroy(&name_reader);
        if (!qa_save_reader_field(&item, "rgba", &rgba_reader)) {
          qa_save_reader_destroy(&item);
          ok = false;
          break;
        }
        size_t length = 0;
        const uint8_t *rgba = qa_save_reader_bytes(&rgba_reader, &length);
        qa_save_reader_destroy(&rgba_reader);
        qa_save_reader_destroy(&item);
        if (!registry->has_generation || length != 256u * 256u * 4u ||
            restore_map_get(&generated, name) != NULL)
          qa_save_reader_fail(&reader, "invalid generated font atlas");
        if (!registry->has_generation)
          qa_error("Generated font restore requires its atlas owner");
        qa_material_picture picture;
        qa_byte_view view = {rgba, length};
        if (!registry->generation.register_image(registry->generation.ctx, name,
                                                 view, &picture)) {
          ok = false;
          break;
        }
        require_open(registry);
        const qa_picture_asset *asset = NULL;
        if (!push_picture(registry, &picture, &asset) ||
            !restore_map_set(&generated, name, asset) ||
            !generated_set(registry, name, rgba, length))
          ok = false;
      }
      qa_save_reader_destroy(&list);
    }
  }
  if (ok) {
    qa_save_reader list;
    if (!qa_save_reader_field(&reader, "fonts", &list)) {
      ok = false;
    } else {
      size_t count = qa_save_reader_list_count(&list);
      if (count > 6) qa_save_reader_fail(&reader, "too many cached fonts");
      for (size_t row = 0; row < count && ok; row++) {
        qa_save_reader item, record_reader, pictures_reader;
        if (!qa_save_reader_item(&list, row, &item)) {
          ok = false;
          break;
        }
        if (!qa_save_reader_field(&item, "record", &record_reader)) {
          qa_save_reader_destroy(&item);
          ok = false;
          break;
        }
        size_t record_length = 0;
        const uint8_t *record_bytes =
            qa_save_reader_bytes(&record_reader, &record_length);
        qa_save_reader_destroy(&record_reader);
        if (!qa_save_reader_field(&item, "pictures", &pictures_reader)) {
          qa_save_reader_destroy(&item);
          ok = false;
          break;
        }
        size_t picture_count = qa_save_reader_list_count(&pictures_reader);
        if (record_length != RECORD || picture_count != QA_Q3_FONT_GLYPHS)
          qa_save_reader_fail(&reader, "invalid font record");
        qa_q3_renderer_font_entry *entry =
            (qa_q3_renderer_font_entry *)calloc(1, sizeof *entry);
        if (entry != NULL) entry->record = (uint8_t *)malloc(RECORD);
        qa_q3_font_data data;
        if (entry == NULL || entry->record == NULL) {
          ok = false;
        } else {
          memcpy(entry->record, record_bytes, RECORD);
          if (!qa_q3_read_font_data(entry->record, RECORD, NULL, &data)) ok = false;
        }
        if (ok) {
          memcpy(entry->font.name, data.name, sizeof entry->font.name);
          entry->font.glyph_scale = data.glyph_scale;
          entry->font.glyph_count = QA_Q3_FONT_GLYPHS;
          for (int index = 0; index < QA_Q3_FONT_GLYPHS && ok; index++) {
            qa_save_reader name_reader;
            if (!qa_save_reader_item(&pictures_reader, (size_t)index, &name_reader)) {
              ok = false;
              break;
            }
            const qa_picture_asset *asset = NULL;
            if (!qa_save_reader_is_null(&name_reader)) {
              const char *name = qa_save_reader_string(&name_reader);
              asset = restore_map_get(&generated, name);
              if (asset == NULL) {
                qa_material_picture picture;
                if (!registry->register_picture(registry->register_picture_ctx, name,
                                                &picture) ||
                    !push_picture(registry, &picture, &asset))
                  ok = false;
              }
            }
            qa_save_reader_destroy(&name_reader);
            if (!ok) break;
            require_open(registry);
            if (asset != NULL)
              set_i32_le(entry->record, (size_t)index * 80 + 44,
                         to_int32(asset->material.order));
            entry->font.glyphs[index].metrics = data.glyphs[index];
            entry->font.glyphs[index].picture = asset;
          }
        }
        qa_save_reader_destroy(&pictures_reader);
        qa_save_reader_destroy(&item);
        if (!ok) {
          if (entry != NULL) {
            free(entry->record);
            free(entry);
          }
          break;
        }
        registry->fonts[registry->font_count++] = entry;
      }
      qa_save_reader_destroy(&list);
    }
  }
  qa_cleanup_pop(&cleanup);
  restore_locals_release(&locals);
  return ok;
}

/* TS left.toLowerCase() === right.toLowerCase(). */
static bool ascii_ieq(const char *left, const char *right) { return qa_js_equals_ignore_case(left, right); }

static void write_name(uint8_t *bytes, size_t offset, size_t length,
                       const char *name) {
  size_t count = strlen(name);
  if (count > length - 1) count = length - 1;
  memset(bytes + offset, 0, length);
  memcpy(bytes + offset, name, count);
}

/* DataView.setInt32 of an int64 TS number: wrap modulo 2^32. */
static int32_t i64_to_i32(int64_t value) {
  uint32_t bits = (uint32_t)value;
  int32_t out;
  memcpy(&out, &bits, sizeof out);
  return out;
}

static void write_glyph(uint8_t *record, int32_t code,
                        const qa_font_glyph_bitmap *glyph, int64_t x, int64_t y) {
  size_t offset = (size_t)code * 80;
  memset(record + offset, 0, 80);
  if (glyph == NULL) return;
  const int64_t ints[] = {glyph->height, glyph->top,    glyph->bottom,
                          glyph->pitch,  glyph->x_skip, glyph->pitch,
                          glyph->height};
  for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++)
    set_i32_le(record, offset + i * 4, i64_to_i32(ints[i]));
  const double floats[] = {(double)x / 256.0, (double)y / 256.0,
                           (double)(x + glyph->pitch) / 256.0,
                           (double)(y + glyph->height) / 256.0};
  for (size_t i = 0; i < sizeof floats / sizeof floats[0]; i++)
    set_f32_le(record, offset + 28 + i * 4, floats[i]);
}

static const qa_q3_registered_font *push_font(qa_q3_renderer_font_registry *registry,
                                              const uint8_t *record,
                                              const qa_q3_font_data *data,
                                              const qa_picture_asset *const *pictures,
                                              const char *missing_message) {
  qa_q3_renderer_font_entry *entry =
      (qa_q3_renderer_font_entry *)calloc(1, sizeof *entry);
  if (entry == NULL) return NULL;
  entry->record = (uint8_t *)malloc(RECORD);
  if (entry->record == NULL) {
    free(entry);
    return NULL;
  }
  memcpy(entry->record, record, RECORD);
  memcpy(entry->font.name, data->name, sizeof entry->font.name);
  entry->font.glyph_scale = data->glyph_scale;
  entry->font.glyph_count = QA_Q3_FONT_GLYPHS;
  for (int index = 0; index < QA_Q3_FONT_GLYPHS; index++) {
    /* TS pictures arrays always have 256 slots here; the message
     * names the guard the TS keeps for shorter arrays. */
    if (pictures == NULL && missing_message != NULL) qa_range_error("%s", missing_message);
    entry->font.glyphs[index].metrics = data->glyphs[index];
    entry->font.glyphs[index].picture = pictures == NULL ? NULL : pictures[index];
  }
  registry->fonts[registry->font_count++] = entry;
  return &entry->font;
}

typedef struct generate_locals {
  qa_q3_renderer_font_registry *registry;
  qa_freetype_face *face;
  qa_retained_font_file file;
  uint8_t *record;
  bool record_owned;
  uint8_t *atlas;
  uint8_t *rgba;
} generate_locals;

static void generate_locals_release(void *raw) {
  generate_locals *locals = (generate_locals *)raw;
  qa_q3_renderer_font_registry *registry = locals->registry;
  if (!registry->closed) qa_freetype_release_face(&registry->freetype, locals->face);
  registry->reader.free_file(registry->reader.ctx, locals->file);
  if (locals->record_owned) free(locals->record);
  free(locals->atlas);
  free(locals->rgba);
}

static const qa_q3_registered_font *generate_font(
    qa_q3_renderer_font_registry *registry, const char *path, double size,
    qa_font_print_fn print, void *print_ctx,
    qa_q3_font_destination_fn destination, void *destination_ctx) {
  if (!registry->has_generation) {
    do_print(print, print_ctx, "RE_RegisterFont: FreeType code not available\n");
    return NULL;
  }
  if (!registry->has_freetype) open_freetype(registry, print, print_ctx);
  if (registry->freetype_kind != QA_FREETYPE_READY) {
    do_print(print, print_ctx,
             registry->freetype_kind == QA_FREETYPE_UNAVAILABLE
                 ? "RE_RegisterFont: FreeType code not available\n"
                 : "RE_RegisterFont: FreeType not initialized.\n");
    return NULL;
  }
  qa_freetype_font_library *freetype = &registry->freetype;
  print_adapter adapter = {print, print_ctx};
  qa_freetype_print_fn ft_print = print == NULL ? NULL : freetype_print;
  qa_retained_font_file file;
  bool has_file =
      registry->reader.read_file_retained(registry->reader.ctx,
                                          path == NULL ? "" : path, &file);
  if (registry->closed && has_file) registry->reader.free_file(registry->reader.ctx, file);
  require_open(registry);
  if (!has_file || file.length <= 0) {
    if (has_file) registry->reader.free_file(registry->reader.ctx, file);
    do_print(print, print_ctx, "RE_RegisterFont: Unable to read font file\n");
    return NULL;
  }
  int64_t face_size =
      size >= 9223372036854775807.0 ? INT64_MAX : (int64_t)size;
  qa_freetype_face *face = qa_freetype_create_face(
      freetype, file.bytes.bytes, file.bytes.length, face_size, ft_print,
      &adapter);
  if (face == NULL) {
    registry->reader.free_file(registry->reader.ctx, file);
    return NULL;
  }
  const qa_q3_registered_font *result = NULL;
  /* TS try { ... } finally { releaseFace; freeFile }: the node runs the
   * finally when a raise leaves this frame. */
  generate_locals locals = {registry, face, file, NULL, false, NULL, NULL};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, generate_locals_release, &locals);
  if (destination != NULL) {
    locals.record = destination_record(destination, destination_ctx).bytes;
  } else {
    locals.record = (uint8_t *)calloc(RECORD, 1);
    locals.record_owned = true;
  }
  locals.atlas = (uint8_t *)calloc(1024u * 1024u, 1);
  locals.rgba = (uint8_t *)malloc(256u * 256u * 4u);
  uint8_t *record = locals.record;
  uint8_t *atlas = locals.atlas;
  uint8_t *rgba = locals.rgba;
  if (record == NULL || atlas == NULL || rgba == NULL) goto finally;
  {
    const qa_picture_asset *pictures[QA_Q3_FONT_GLYPHS] = {NULL};
    int64_t max_height = 0;
    for (int32_t code = 0; code < 255; code++) {
      qa_font_glyph_bitmap glyph;
      if (qa_freetype_render_glyph(freetype, face, code, ft_print, &adapter,
                                   &glyph)) {
        if (glyph.height > max_height) max_height = glyph.height;
        qa_font_glyph_bitmap_dispose(&glyph);
      }
    }
    int64_t x = 0, y = 0;
    int32_t last_start = 0, image_number = 0;
    bool oom = false;
    for (int32_t code = 0; code <= 255 && !oom; code++) {
      qa_font_glyph_bitmap glyph;
      bool has_glyph = qa_freetype_render_glyph(freetype, face, code, ft_print,
                                                &adapter, &glyph);
      bool overflow = false;
      if (has_glyph) {
        if (glyph.height > max_height) max_height = glyph.height;
        if (x + glyph.pitch + 1 >= 255) {
          if (y + max_height + 1 >= 255)
            overflow = true;
          else {
            x = 0;
            y += max_height + 1;
          }
        } else if (y + max_height + 1 >= 255) {
          overflow = true;
        }
        if (!overflow) {
          for (int64_t row = 0; row < glyph.height; row++) {
            int64_t start = row * glyph.pitch;
            int64_t target = (y + row) * 256 + x;
            int64_t count = glyph.pitch;
            if (start < 0 || target < 0 || target + count > 1024 * 1024)
              qa_range_error("Font atlas pixel exceeds allocation");
            if (start < (int64_t)glyph.pixel_length) {
              if (start + count > (int64_t)glyph.pixel_length)
                count = (int64_t)glyph.pixel_length - start;
              if (count > 0)
                memcpy(atlas + target, glyph.pixels + start, (size_t)count);
            }
          }
        }
      }
      if (overflow || code == 255) {
        int32_t maximum = 0;
        for (size_t i = 0; i < 256u * 256u; i++)
          if (atlas[i] > maximum) maximum = atlas[i];
        float scale = maximum > 0 ? (float)(255.0 / maximum) : 0.0f;
        for (size_t pixel = 0; pixel < 256u * 256u; pixel++) {
          memset(rgba + pixel * 4, 255, 3);
          rgba[pixel * 4 + 3] = to_uint8((float)((double)atlas[pixel] * scale));
        }
        char size_text[32], name[128];
        format_size(size, size_text, sizeof size_text);
        snprintf(name, sizeof name, "fonts/fontImage_%d_%s.tga",
                 (int)image_number++, size_text);
        if (registry->generation.save_font_data(registry->generation.ctx)) {
          uint8_t *tga = (uint8_t *)calloc(256u * 256u * 4u + 18u, 1);
          if (tga == NULL) {
            if (has_glyph) qa_font_glyph_bitmap_dispose(&glyph);
            oom = true;
            break;
          }
          tga[2] = 2;
          tga[13] = 1;
          tga[15] = 1;
          tga[16] = 32;
          tga[17] = 0x28;
          memcpy(tga + 18, rgba, 256u * 256u * 4u);
          qa_byte_view view = {tga, 256u * 256u * 4u + 18u};
          registry->generation.write_file(registry->generation.ctx, name, view);
          free(tga);
        }
        require_open(registry);
        qa_material_picture picture;
        qa_byte_view view = {rgba, 256u * 256u * 4u};
        const qa_picture_asset *asset = NULL;
        if (!registry->generation.register_image(registry->generation.ctx, name, view,
                                                 &picture) ||
            !push_picture(registry, &picture, &asset) ||
            !generated_set(registry, name, rgba, 256u * 256u * 4u)) {
          if (has_glyph) qa_font_glyph_bitmap_dispose(&glyph);
          oom = true;
          break;
        }
        require_open(registry);
        for (int32_t index = last_start; index < code; index++) {
          set_i32_le(record, (size_t)index * 80 + 44, to_int32(picture.order));
          write_name(record, (size_t)index * 80 + 48, 32, name);
          pictures[index] = asset;
        }
        last_start = code;
        memset(atlas, 0, 1024u * 1024u);
        x = 0;
        y = 0;
      } else {
        write_glyph(record, code, has_glyph ? &glyph : NULL, x, y);
        if (has_glyph) x += glyph.pitch + 1;
      }
      if (has_glyph) qa_font_glyph_bitmap_dispose(&glyph);
    }
    if (oom) goto finally;
    set_f32_le(record, 20480, (float)(48.0 / (float)size));
    {
      char size_text[32], name[128];
      format_size(size, size_text, sizeof size_text);
      snprintf(name, sizeof name, "fonts/fontImage_%s.dat", size_text);
      write_name(record, 20484, 64, name);
    }
    qa_q3_font_data data;
    if (!qa_q3_read_font_data(record, RECORD, NULL, &data)) goto finally;
    result = push_font(registry, record, &data, pictures,
                       "Missing generated font glyph slot");
    if (result == NULL) goto finally;
    if (registry->generation.save_font_data(registry->generation.ctx)) {
      char size_text[32], name[128];
      format_size(size, size_text, sizeof size_text);
      snprintf(name, sizeof name, "fonts/fontImage_%s.dat", size_text);
      qa_byte_view view = {record, RECORD};
      registry->generation.write_file(registry->generation.ctx, name, view);
    }
    require_open(registry);
  }
finally:
  qa_cleanup_pop(&cleanup);
  generate_locals_release(&locals);
  return result;
}

static const qa_q3_registered_font *load_font(
    qa_q3_renderer_font_registry *registry, const char *name, const char *path,
    double size, qa_font_print_fn print, void *print_ctx,
    qa_q3_font_destination_fn destination, void *destination_ctx) {
  require_open(registry);
  if (registry->sync_render_thread != NULL)
    registry->sync_render_thread(registry->sync_render_thread_ctx);
  if (registry->font_count >= 6) {
    do_print(print, print_ctx,
             "RE_RegisterFont: Too many fonts registered already.\n");
    return NULL;
  }
  for (size_t i = 0; i < registry->font_count; i++) {
    qa_q3_renderer_font_entry *cached = registry->fonts[i];
    if (!ascii_ieq(cached->font.name, name)) continue;
    if (destination != NULL) {
      qa_q3_font_record_view view = destination_record(destination, destination_ctx);
      memcpy(view.bytes, cached->record, RECORD);
    }
    return &cached->font;
  }
  qa_retained_font_file file;
  bool has_file = false;
  if (registry->reader.read_file_length(registry->reader.ctx, name) == RECORD)
    has_file = registry->reader.read_file_retained(registry->reader.ctx, name, &file);
  if (registry->closed && has_file) registry->reader.free_file(registry->reader.ctx, file);
  require_open(registry);
  if (!has_file || file.length != RECORD) {
    if (has_file) registry->reader.free_file(registry->reader.ctx, file);
    return generate_font(registry, path, size, print, print_ctx, destination,
                         destination_ctx);
  }
  /* Q3 retains DAT allocations through registration; release with
   * this renderer (TS retained set). On OOM the file stays with its
   * reader and registration fails as TS null. */
  if (!retained_add(registry, file)) {
    registry->reader.free_file(registry->reader.ctx, file);
    return NULL;
  }
  uint8_t *record = NULL;
  bool record_owned = false;
  if (destination != NULL) {
    record = destination_record(destination, destination_ctx).bytes;
  } else {
    record = (uint8_t *)malloc(RECORD);
    record_owned = true;
    if (record == NULL) return NULL;
  }
  size_t copy = file.bytes.length < RECORD ? file.bytes.length : RECORD;
  memcpy(record, file.bytes.bytes, copy);
  if (copy < RECORD) memset(record + copy, 0, RECORD - copy);
  memset(record + 20484, 0, RECORD - 20484);
  for (size_t index = 0; name[index] != '\0'; index++) {
    /* Typed-array stores past the end are silently dropped in JS. */
    if (20484 + index < RECORD) record[20484 + index] = (uint8_t)name[index];
  }
  const qa_picture_asset *pictures[QA_Q3_FONT_GLYPHS] = {NULL};
  const qa_q3_registered_font *result = NULL;
  bool oom = false;
  for (int index = 0; index < 255 && !oom; index++) {
    size_t offset = (size_t)index * 80 + 48;
    size_t end = offset;
    while (end < RECORD && record[end] != 0) end++;
    if (end >= RECORD)
      qa_range_error("Font shader name has no terminator inside fontInfo_t");
    char *shader_name = (char *)malloc(end - offset + 1);
    if (shader_name == NULL) {
      oom = true;
      break;
    }
    memcpy(shader_name, record + offset, end - offset);
    shader_name[end - offset] = '\0';
    qa_material_picture picture;
    bool registered = registry->register_picture(registry->register_picture_ctx,
                                                  shader_name, &picture);
    free(shader_name);
    if (!registered) {
      oom = true;
      break;
    }
    require_open(registry);
    if (!push_picture(registry, &picture, &pictures[index])) {
      oom = true;
      break;
    }
    set_i32_le(record, (size_t)index * 80 + 44, to_int32(picture.order));
  }
  if (!oom) {
    qa_q3_font_data data;
    if (qa_q3_read_font_data(record, RECORD, name, &data))
      result = push_font(registry, record, &data, pictures,
                         "Missing registered font glyph");
  }
  if (record_owned) free(record);
  return result;
}

const qa_q3_registered_font *qa_q3_renderer_font_registry_register_font(
    qa_q3_renderer_font_registry *registry, const char *path, double point_size,
    qa_font_print_fn print, void *print_ctx,
    qa_q3_font_destination_fn destination, void *destination_ctx) {
  if (!isfinite(point_size)) qa_range_error("Invalid font point size");
  double integer_size = trunc(point_size);
  double size = integer_size <= 0 ? 12 : integer_size;
  char size_text[32], name[128];
  format_size(size, size_text, sizeof size_text);
  snprintf(name, sizeof name, "fonts/fontImage_%s.dat", size_text);
  registry->pending++;
  /* TS decrements pending when the registration settles, rejected too. */
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, pending_release, registry);
  const qa_q3_registered_font *result =
      load_font(registry, name, path, size, print, print_ctx, destination,
                destination_ctx);
  qa_cleanup_pop(&cleanup);
  registry->pending--;
  return result;
}
