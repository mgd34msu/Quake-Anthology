/* Port of src/persistence/saved-game.ts. GPL-2.0-or-later. */
#include "qa/persistence/saved_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/persistence/save_policy.h"
#include "qa/persistence/value.h"

/* Shared-branch prologue of TS decodeSaveImage (save-image.ts),
 * reproduced so malformed shared saves fail here exactly as they
 * fail there; parseSaveImage itself is not yet ported. */
static noreturn void decode_shared_image(const uint8_t *bytes, size_t length) {
  static const char magic[] = "QTSAVE3\n";
  static const char legacy_magic[] = "QTSAVE2\n";
  double version = 0;
  if (length >= sizeof magic - 1 && memcmp(bytes, magic, sizeof magic - 1) == 0)
    version = 3;
  else if (length >= sizeof legacy_magic - 1 &&
           memcmp(bytes, legacy_magic, sizeof legacy_magic - 1) == 0)
    version = 2;
  else
    qa_save_format_error("save", "unsupported unified save signature/version");
  qa_save_value *value =
      qa_decode_checkpoint_value(bytes + (sizeof magic - 1),
                                 length - (sizeof magic - 1));
  if (value == NULL)
    qa_save_format_error("save", "unified save image codec is not yet ported");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) {
    qa_save_value_free(value);
    qa_save_format_error("save", "unified save image codec is not yet ported");
  }
  qa_save_reader schema;
  if (!qa_save_reader_field(&reader, "schemaVersion", &schema)) {
    qa_save_reader_destroy(&reader);
    qa_save_value_free(value);
    qa_save_format_error("save", "unified save image codec is not yet ported");
  }
  (void)qa_save_reader_literal_number(&schema, version);
  qa_save_reader_destroy(&schema);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
  qa_save_format_error("save", "unified save image codec is not yet ported");
}

/* TS /^[A-Za-z0-9_/-]+$/ plus the split("/") part check. The Q1 map
 * is a byte string in both implementations, so UTF-16 vs byte
 * lengths cannot differ on this ASCII-only alphabet. */
static bool valid_source_map(const char *map) {
  if (map == NULL || map[0] == '\0') return false;
  for (const char *cursor = map; *cursor != '\0'; cursor++) {
    char ch = *cursor;
    bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' || ch == '-';
    if (!ok) return false;
  }
  const char *part = map;
  for (;;) {
    const char *slash = strchr(part, '/');
    size_t part_length = slash != NULL ? (size_t)(slash - part) : strlen(part);
    if (part_length == 0) return false;
    if (part_length == 1 && part[0] == '.') return false;
    if (part_length == 2 && part[0] == '.' && part[1] == '.') return false;
    if (slash == NULL) break;
    part = slash + 1;
  }
  return true;
}

bool qa_decode_saved_game(const uint8_t *bytes, size_t length, qa_saved_game *out) {
  memset(out, 0, sizeof *out);
  /* TS indexes bytes[0]/[1]; a short buffer reads as undefined there
   * and misses the shared signature, as the length guard does here. */
  if (length >= 2 && bytes[0] == 81 && bytes[1] == 84)
    decode_shared_image(bytes, length);
  if (!qa_decode_q1_save(bytes, length, &out->data)) return false;
  if (!valid_source_map(out->data.map))
    qa_save_format_error("q1.map", "invalid source map name");
  if (out->data.time < 0 || out->data.skill < 0 || out->data.skill > 3 ||
      out->data.entity_count < 2 || out->data.entities[0].pair_count == 0)
    qa_save_format_error("q1", "save has no valid singleplayer world");
  out->kind = QA_SAVED_GAME_Q1_SOURCE;
  return true;
}

bool qa_read_saved_game(const char *path, qa_saved_game *out) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return false;
  size_t capacity = 65536;
  size_t length = 0;
  uint8_t *bytes = (uint8_t *)malloc(capacity);
  if (bytes == NULL) {
    fclose(file);
    return false;
  }
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = (uint8_t *)realloc(bytes, capacity);
      if (grown == NULL) {
        free(bytes);
        fclose(file);
        return false;
      }
      bytes = grown;
    }
    size_t count = fread(bytes + length, 1, capacity - length, file);
    length += count;
    if (count == 0) break;
  }
  bool failed = ferror(file) != 0;
  fclose(file);
  if (failed) {
    free(bytes);
    return false;
  }
  bool ok = qa_decode_saved_game(bytes, length, out);
  free(bytes);
  return ok;
}

bool qa_write_saved_game(const char *directory, const char *path,
                         const qa_saved_game *save) {
  if (save->kind == QA_SAVED_GAME_SHARED)
    qa_save_format_error("save", "unified save image codec is not yet ported");
  size_t length = 0;
  uint8_t *bytes = qa_encode_q1_save(&save->data, &length);
  if (bytes == NULL) return false;
  bool ok = qa_write_contained_save(directory, path, bytes, length);
  free(bytes);
  return ok;
}

void qa_saved_game_free(qa_saved_game *save) {
  if (save == NULL) return;
  qa_q1_save_data_free(&save->data);
  memset(save, 0, sizeof *save);
}
