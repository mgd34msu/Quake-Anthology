/* Port of src/text/mounted.ts. GPL-2.0-or-later. */
#include "qa/text/mounted.h"

#include <stdlib.h>
#include <string.h>

static uint8_t *mounted_read(void *ctx, const char *path, size_t *out_length) {
  qa_mounted_text_fonts *mounted = (qa_mounted_text_fonts *)ctx;
  qa_opened_resource opened;
  if (!mounted->content.open(mounted->content.ctx, path, &opened)) return NULL;
  uint8_t *copy = (uint8_t *)malloc(opened.bytes.length != 0
                                        ? opened.bytes.length
                                        : 1);
  if (copy == NULL) return NULL;
  if (opened.bytes.length != 0) {
    memcpy(copy, opened.bytes.bytes, opened.bytes.length);
  }
  *out_length = opened.bytes.length;
  return copy;
}

static qa_renderer_image mounted_register(void *ctx, const char *name,
                                          const qa_render_image *content) {
  qa_mounted_text_fonts *mounted = (qa_mounted_text_fonts *)ctx;
  qa_texture_sampling sampling = {"clamp", "linear"};
  return mounted->images.register_image(mounted->images.ctx, name, content,
                                        sampling);
}

static void mounted_release(void *ctx, qa_renderer_image image) {
  qa_mounted_text_fonts *mounted = (qa_mounted_text_fonts *)ctx;
  mounted->images.release(mounted->images.ctx, image);
}

void qa_create_mounted_text_fonts(qa_mounted_text_fonts *mounted,
                                  qa_mounted_content content,
                                  qa_mounted_scene_image_registry images,
                                  qa_text_font_registry *out) {
  mounted->content = content;
  mounted->images = images;
  qa_font_image_services services;
  services.ctx = mounted;
  services.read = mounted_read;
  services.register_image = mounted_register;
  services.release_image = mounted_release;
  qa_text_font_registry_init(out, services);
}

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

void qa_mounted_font_reader_init(qa_mounted_font_reader *reader,
                                 qa_mounted_content content) {
  memset(reader, 0, sizeof *reader);
  reader->content = content;
}

void qa_mounted_font_reader_close(qa_mounted_font_reader *reader) {
  for (size_t i = 0; i < reader->retained_count; i++) {
    free((void *)reader->retained[i]);
  }
  free(reader->retained);
  for (size_t i = 0; i < reader->file_count; i++) {
    free(reader->files[i].path);
    free(reader->files[i].bytes);
  }
  free(reader->files);
  memset(reader, 0, sizeof *reader);
}

/* TS private open(): one cached result per path, null included.
 * Returns the cache entry, or NULL only on OOM. */
static qa_mounted_file_entry *reader_open(qa_mounted_font_reader *reader,
                                          const char *path) {
  for (size_t i = 0; i < reader->file_count; i++) {
    if (strcmp(reader->files[i].path, path) == 0) return &reader->files[i];
  }
  if (reader->file_count == reader->file_capacity) {
    size_t grown =
        reader->file_capacity == 0 ? 8 : reader->file_capacity * 2;
    qa_mounted_file_entry *files =
        (qa_mounted_file_entry *)realloc(reader->files, grown * sizeof *files);
    if (files == NULL) return NULL;
    reader->files = files;
    reader->file_capacity = grown;
  }
  qa_mounted_file_entry *entry = &reader->files[reader->file_count];
  memset(entry, 0, sizeof *entry);
  entry->path = dup_text(path);
  if (entry->path == NULL) return NULL;
  qa_opened_resource opened;
  if (reader->content.open(reader->content.ctx, path, &opened)) {
    entry->bytes = (uint8_t *)malloc(opened.bytes.length != 0
                                          ? opened.bytes.length
                                          : 1);
    if (entry->bytes == NULL) {
      free(entry->path);
      entry->path = NULL;
      return NULL;
    }
    if (opened.bytes.length != 0) {
      memcpy(entry->bytes, opened.bytes.bytes, opened.bytes.length);
    }
    entry->length = opened.bytes.length;
    entry->reference = opened.reference;
    entry->found = true;
  }
  reader->file_count++;
  return entry;
}

double qa_mounted_font_reader_read_file_length(qa_mounted_font_reader *reader,
                                               const char *path) {
  qa_mounted_file_entry *entry = reader_open(reader, path);
  if (entry == NULL || !entry->found) return -1;
  return (double)entry->reference.byte_length;
}

bool qa_mounted_font_reader_read_file_retained(qa_mounted_font_reader *reader,
                                               const char *path,
                                               qa_retained_font_file *out) {
  qa_mounted_file_entry *entry = reader_open(reader, path);
  if (entry == NULL || !entry->found) return false;
  uint8_t *slice = (uint8_t *)malloc(entry->length != 0 ? entry->length : 1);
  if (slice == NULL) return false;
  if (entry->length != 0) memcpy(slice, entry->bytes, entry->length);
  if (reader->retained_count == reader->retained_capacity) {
    size_t grown =
        reader->retained_capacity == 0 ? 8 : reader->retained_capacity * 2;
    const uint8_t **retained =
        (const uint8_t **)realloc(reader->retained, grown * sizeof *retained);
    if (retained == NULL) {
      free(slice);
      return false;
    }
    reader->retained = retained;
    reader->retained_capacity = grown;
  }
  reader->retained[reader->retained_count++] = slice;
  out->bytes.bytes = slice;
  out->bytes.length = entry->length;
  out->length = (double)entry->length;
  return true;
}

void qa_mounted_font_reader_free_file(qa_mounted_font_reader *reader,
                                      qa_retained_font_file file) {
  for (size_t i = 0; i < reader->retained_count; i++) {
    if (reader->retained[i] != file.bytes.bytes) continue;
    free((void *)reader->retained[i]);
    memmove(&reader->retained[i], &reader->retained[i + 1],
            (reader->retained_count - i - 1) * sizeof *reader->retained);
    reader->retained_count--;
    return;
  }
}

static double reader_length(void *ctx, const char *path) {
  return qa_mounted_font_reader_read_file_length(
      (qa_mounted_font_reader *)ctx, path);
}
static bool reader_retained(void *ctx, const char *path,
                            qa_retained_font_file *out) {
  return qa_mounted_font_reader_read_file_retained(
      (qa_mounted_font_reader *)ctx, path, out);
}
static void reader_free(void *ctx, qa_retained_font_file file) {
  qa_mounted_font_reader_free_file((qa_mounted_font_reader *)ctx, file);
}

qa_font_file_reader qa_mounted_font_reader_as_reader(
    qa_mounted_font_reader *reader) {
  qa_font_file_reader services;
  services.ctx = reader;
  services.read_file_length = reader_length;
  services.read_file_retained = reader_retained;
  services.free_file = reader_free;
  return services;
}
