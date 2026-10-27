/* Port of src/persistence/q2-classic.ts. GPL-2.0-or-later. */
#include "qa/persistence/q2_classic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/persistence/value.h"

static size_t record_size(const qa_q2_classic_record *record) {
  size_t total = record->byte_length;
  for (size_t i = 0; i < record->string_count; i++) total += record->strings[i].length;
  return total;
}
static int32_t field_i32(const uint8_t *bytes, size_t length,
                         const qa_q2_classic_save_field *field) {
  if (field->offset < 0 || field->offset > 9007199254740991LL ||
      (size_t)field->offset + 4 > length)
    qa_save_format_error(field->name, "save field is outside its native struct");
  int32_t value;
  memcpy(&value, bytes + (size_t)field->offset, 4);
  /* Native little-endian source layout; read as little-endian. */
  uint32_t raw = (uint32_t)bytes[(size_t)field->offset] |
      ((uint32_t)bytes[(size_t)field->offset + 1] << 8) |
      ((uint32_t)bytes[(size_t)field->offset + 2] << 16) |
      ((uint32_t)bytes[(size_t)field->offset + 3] << 24);
  memcpy(&value, &raw, 4);
  return value;
}
static void field_set_i32(uint8_t *bytes, size_t length,
                          const qa_q2_classic_save_field *field, int32_t value) {
  if (field->offset < 0 || (size_t)field->offset + 4 > length)
    qa_save_format_error(field->name, "save field is outside its native struct");
  uint32_t raw;
  memcpy(&raw, &value, 4);
  bytes[(size_t)field->offset] = (uint8_t)raw;
  bytes[(size_t)field->offset + 1] = (uint8_t)(raw >> 8);
  bytes[(size_t)field->offset + 2] = (uint8_t)(raw >> 16);
  bytes[(size_t)field->offset + 3] = (uint8_t)(raw >> 24);
}

void qa_q2_classic_record_free(qa_q2_classic_record *record) {
  free(record->bytes);
  for (size_t i = 0; i < record->string_count; i++) free((void *)record->strings[i].bytes);
  free(record->strings);
  free(record->references);
  memset(record, 0, sizeof *record);
}
void qa_q2_classic_game_save_free(qa_q2_classic_game_save *save) {
  free(save->build_date);
  free(save->game);
  for (size_t i = 0; i < save->client_count; i++) qa_q2_classic_record_free(&save->clients[i]);
  free(save->clients);
  memset(save, 0, sizeof *save);
}
void qa_q2_classic_level_save_free(qa_q2_classic_level_save *save) {
  qa_q2_classic_record_free(&save->level);
  for (size_t i = 0; i < save->entity_count; i++)
    qa_q2_classic_record_free(&save->entities[i].record);
  free(save->entities);
  memset(save, 0, sizeof *save);
}

static bool read_record(qa_binary_reader *reader,
                        const qa_q2_classic_record_layout *layout,
                        qa_q2_classic_record *out) {
  memset(out, 0, sizeof *out);
  out->bytes = qa_binary_reader_bytes(reader, layout->byte_length);
  out->byte_length = layout->byte_length > 0 ? (size_t)layout->byte_length : 0;
  if (out->byte_length > 0 && out->bytes == NULL) return false;
  for (size_t i = 0; i < layout->field_count; i++) {
    const qa_q2_classic_save_field *field = &layout->fields[i];
    int32_t value = field_i32(out->bytes, out->byte_length, field);
    if (field->kind == QA_Q2_CLASSIC_FIELD_STRING) {
      qa_q2_classic_string *strings = (qa_q2_classic_string *)realloc(
          out->strings, (out->string_count + 1) * sizeof *strings);
      if (strings == NULL) { qa_q2_classic_record_free(out); return false; }
      out->strings = strings;
      qa_q2_classic_string *entry = &out->strings[out->string_count++];
      entry->field = field->name;
      entry->bytes = NULL;
      entry->length = 0;
      if (value != 0) {
        uint8_t *data = qa_binary_reader_bytes(reader, value);
        if (value > 0 && data == NULL) { qa_q2_classic_record_free(out); return false; }
        entry->bytes = data;
        entry->length = value > 0 ? (size_t)value : 0;
        if (entry->length == 0 || data[entry->length - 1] != 0)
          qa_save_format_error(field->name, "source string lacks a NUL terminator");
      }
    } else {
      qa_q2_classic_reference *references = (qa_q2_classic_reference *)realloc(
          out->references, (out->reference_count + 1) * sizeof *references);
      if (references == NULL) { qa_q2_classic_record_free(out); return false; }
      out->references = references;
      out->references[out->reference_count].field = field->name;
      out->references[out->reference_count].index = value;
      out->reference_count++;
    }
  }
  return true;
}
static void write_record(qa_binary_writer *writer,
                         const qa_q2_classic_record *record,
                         const qa_q2_classic_record_layout *layout) {
  if ((int64_t)record->byte_length != layout->byte_length)
    qa_save_format_error("q2-record", "native struct size mismatch");
  uint8_t *bytes = record->byte_length > 0
      ? (uint8_t *)malloc(record->byte_length) : NULL;
  if (record->byte_length > 0 && bytes == NULL) return;
  if (record->byte_length > 0) memcpy(bytes, record->bytes, record->byte_length);
  for (size_t i = 0; i < layout->field_count; i++) {
    const qa_q2_classic_save_field *field = &layout->fields[i];
    if (field->kind == QA_Q2_CLASSIC_FIELD_STRING) {
      const qa_q2_classic_string *found = NULL;
      for (size_t s = 0; s < record->string_count; s++)
        if (strcmp(record->strings[s].field, field->name) == 0) { found = &record->strings[s]; break; }
      if (found == NULL)
        qa_save_format_error(field->name, "missing native saved string");
      if (found->bytes != NULL &&
          (found->length == 0 || found->bytes[found->length - 1] != 0))
        qa_save_format_error(field->name, "source string lacks a NUL terminator");
      field_set_i32(bytes, record->byte_length, field,
                    found->bytes != NULL ? (int32_t)found->length : 0);
    } else {
      const qa_q2_classic_reference *found = NULL;
      for (size_t s = 0; s < record->reference_count; s++)
        if (strcmp(record->references[s].field, field->name) == 0) { found = &record->references[s]; break; }
      if (found == NULL)
        qa_save_format_error(field->name, "missing native saved reference");
      field_set_i32(bytes, record->byte_length, field, found->index);
    }
  }
  qa_binary_writer_bytes(writer, bytes, record->byte_length);
  free(bytes);
  for (size_t i = 0; i < layout->field_count; i++) {
    const qa_q2_classic_save_field *field = &layout->fields[i];
    if (field->kind != QA_Q2_CLASSIC_FIELD_STRING) continue;
    for (size_t s = 0; s < record->string_count; s++) {
      if (strcmp(record->strings[s].field, field->name) == 0 &&
          record->strings[s].bytes != NULL)
        qa_binary_writer_bytes(writer, record->strings[s].bytes, record->strings[s].length);
    }
  }
}

bool qa_decode_q2_classic_game(const uint8_t *bytes, size_t length,
                               const qa_q2_classic_save_layout *layout,
                               qa_q2_classic_game_save *out) {
  char source[1024];
  (void)snprintf(source, sizeof source, "%s/game.ssv", layout->module.id);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, source)) return false;
  memset(out, 0, sizeof *out);
  out->build_date = qa_binary_reader_bytes(&reader, 16);
  out->game = qa_binary_reader_bytes(&reader, layout->game_bytes);
  out->game_length = layout->game_bytes > 0 ? (size_t)layout->game_bytes : 0;
  if (out->build_date == NULL || (out->game_length > 0 && out->game == NULL)) goto oom;
  {
    qa_q2_classic_save_field count_field = {
        "game.maxclients", layout->client_count_offset, QA_Q2_CLASSIC_FIELD_CLIENT};
    int32_t count = field_i32(out->game, out->game_length, &count_field);
    if (count < 0 || (layout->client.byte_length > 0 &&
            count > (int32_t)(qa_binary_reader_remaining(&reader) /
                              (size_t)layout->client.byte_length)))
      qa_save_format_error("game.maxclients", "client count exceeds save bytes");
    /* TS divides by byteLength even when it is 0 (Infinity bound). */
    out->clients = count > 0
        ? (qa_q2_classic_record *)calloc((size_t)count, sizeof *out->clients) : NULL;
    if (count > 0 && out->clients == NULL) goto oom;
    out->client_count = count > 0 ? (size_t)count : 0;
    for (size_t i = 0; i < out->client_count; i++)
      if (!read_record(&reader, &layout->client, &out->clients[i])) goto oom;
  }
  if (qa_binary_reader_remaining(&reader) != 0) {
    qa_binary_reader_destroy(&reader);
    qa_save_format_error("game.ssv", "unconsumed bytes indicate a mismatched native layout");
  }
  qa_binary_reader_destroy(&reader);
  return true;
oom:
  qa_binary_reader_destroy(&reader);
  qa_q2_classic_game_save_free(out);
  return false;
}
uint8_t *qa_encode_q2_classic_game(const qa_q2_classic_game_save *save,
                                   const qa_q2_classic_save_layout *layout,
                                   size_t *out_length) {
  if (save->build_date == NULL || (int64_t)save->game_length != layout->game_bytes)
    qa_save_format_error("game.ssv", "native game header size mismatch");
  uint8_t *game = save->game_length > 0 ? (uint8_t *)malloc(save->game_length) : NULL;
  if (save->game_length > 0 && game == NULL) return NULL;
  if (save->game_length > 0) memcpy(game, save->game, save->game_length);
  qa_q2_classic_save_field count_field = {
      "game.maxclients", layout->client_count_offset, QA_Q2_CLASSIC_FIELD_CLIENT};
  field_set_i32(game, save->game_length, &count_field, (int32_t)save->client_count);
  size_t total = 16 + save->game_length;
  for (size_t i = 0; i < save->client_count; i++) total += record_size(&save->clients[i]);
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)total)) { free(game); return NULL; }
  qa_binary_writer_bytes(&writer, save->build_date, 16);
  qa_binary_writer_bytes(&writer, game, save->game_length);
  free(game);
  for (size_t i = 0; i < save->client_count; i++)
    write_record(&writer, &save->clients[i], &layout->client);
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
bool qa_decode_q2_classic_level(const uint8_t *bytes, size_t length,
                                const qa_q2_classic_save_layout *layout,
                                qa_q2_classic_level_save *out) {
  char source[1024];
  (void)snprintf(source, sizeof source, "%s/level.sav", layout->module.id);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)bytes, length, source)) return false;
  memset(out, 0, sizeof *out);
  if (qa_binary_reader_i32(&reader) != layout->entity.byte_length)
    qa_save_format_error("level.sav", "native edict size mismatch");
  uint32_t low = qa_binary_reader_u32(&reader);
  out->function_base = (uint64_t)low |
      (layout->pointer_bytes == 8 ? (uint64_t)qa_binary_reader_u32(&reader) << 32 : 0);
  if (!read_record(&reader, &layout->level, &out->level)) goto oom;
  for (;;) {
    int32_t slot = qa_binary_reader_i32(&reader);
    if (slot == -1) break;
    if (slot < 0) qa_save_format_error("level.sav", "invalid native edict slot");
    qa_q2_classic_level_entity *entities = (qa_q2_classic_level_entity *)realloc(
        out->entities, (out->entity_count + 1) * sizeof *entities);
    if (entities == NULL) goto oom;
    out->entities = entities;
    memset(&out->entities[out->entity_count], 0, sizeof out->entities[0]);
    out->entities[out->entity_count].slot = slot;
    if (!read_record(&reader, &layout->entity, &out->entities[out->entity_count].record))
      goto oom;
    out->entity_count++;
  }
  if (qa_binary_reader_remaining(&reader) != 0) {
    qa_binary_reader_destroy(&reader);
    qa_save_format_error("level.sav", "unconsumed bytes indicate a mismatched native layout");
  }
  qa_binary_reader_destroy(&reader);
  return true;
oom:
  qa_binary_reader_destroy(&reader);
  qa_q2_classic_level_save_free(out);
  return false;
}
uint8_t *qa_encode_q2_classic_level(const qa_q2_classic_level_save *save,
                                    const qa_q2_classic_save_layout *layout,
                                    size_t *out_length) {
  size_t total = 8 + (size_t)layout->pointer_bytes + record_size(&save->level);
  for (size_t i = 0; i < save->entity_count; i++)
    total += 4 + record_size(&save->entities[i].record);
  qa_binary_writer writer;
  if (!qa_binary_writer_init(&writer, (int64_t)total)) return NULL;
  qa_binary_writer_i32(&writer, layout->entity.byte_length);
  qa_binary_writer_u32(&writer, (int64_t)(uint32_t)save->function_base);
  if (layout->pointer_bytes == 8)
    qa_binary_writer_u32(&writer, (int64_t)(uint32_t)(save->function_base >> 32));
  write_record(&writer, &save->level, &layout->level);
  for (size_t i = 0; i < save->entity_count; i++) {
    qa_binary_writer_i32(&writer, save->entities[i].slot);
    write_record(&writer, &save->entities[i].record, &layout->entity);
  }
  qa_binary_writer_i32(&writer, -1);
  uint8_t *result = qa_binary_writer_finish(&writer, out_length);
  qa_binary_writer_destroy(&writer);
  return result;
}
uint8_t *qa_restore_q2_classic_record(const qa_q2_classic_record *record,
                                      const qa_q2_classic_record_layout *layout,
                                      const qa_q2_classic_relocations *relocations) {
  if ((int64_t)record->byte_length != layout->byte_length)
    qa_save_format_error("q2-record", "native struct size mismatch");
  uint8_t *bytes = record->byte_length > 0
      ? (uint8_t *)malloc(record->byte_length) : NULL;
  if (record->byte_length > 0 && bytes == NULL) return NULL;
  if (record->byte_length > 0) memcpy(bytes, record->bytes, record->byte_length);
  for (size_t i = 0; i < layout->field_count; i++) {
    const qa_q2_classic_save_field *field = &layout->fields[i];
    uint64_t pointer;
    if (field->kind == QA_Q2_CLASSIC_FIELD_STRING) {
      const qa_q2_classic_string *found = NULL;
      for (size_t s = 0; s < record->string_count; s++)
        if (strcmp(record->strings[s].field, field->name) == 0) { found = &record->strings[s]; break; }
      if (found == NULL)
        qa_save_format_error(field->name, "missing source string relocation");
      pointer = relocations->string(relocations->ctx, field->name,
                                    found->bytes, found->length, found->bytes == NULL);
    } else {
      const qa_q2_classic_reference *found = NULL;
      for (size_t s = 0; s < record->reference_count; s++)
        if (strcmp(record->references[s].field, field->name) == 0) { found = &record->references[s]; break; }
      if (found == NULL)
        qa_save_format_error(field->name, "missing source pointer relocation");
      pointer = relocations->reference(relocations->ctx, *field, found->index);
    }
    uint64_t limit = relocations->pointer_bytes == 4 ? 0x100000000ULL : 0;
    if (relocations->pointer_bytes == 4 && pointer >= limit)
      qa_save_format_error(field->name, "restored pointer exceeds guest ABI");
    if (field->offset < 0 ||
        (size_t)field->offset + (size_t)relocations->pointer_bytes > record->byte_length)
      qa_save_format_error(field->name, "save field is outside its native struct");
    for (int b = 0; b < relocations->pointer_bytes; b++)
      bytes[(size_t)field->offset + (size_t)b] =
          (uint8_t)(pointer >> (8 * b));
  }
  return bytes;
}
