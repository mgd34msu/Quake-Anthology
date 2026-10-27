/*
 * Port of src/persistence/q2-classic.ts. Layouts borrow the module
 * identity and field tables. Decoded records own their bytes and
 * appended strings (qa_*_free helpers release them); encoders return
 * malloc'd bytes and bore the same checks as the TS. The relocation
 * interface becomes function pointers + ctx; returned pointers
 * (TS bigint) are uint64_t. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q2_CLASSIC_H
#define QA_PERSISTENCE_Q2_CLASSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/execution.h"

/* TS field kind: "string"|"entity"|"client"|"item"|"function"|"move". */
typedef enum qa_q2_classic_field_kind {
  QA_Q2_CLASSIC_FIELD_STRING, QA_Q2_CLASSIC_FIELD_ENTITY,
  QA_Q2_CLASSIC_FIELD_CLIENT, QA_Q2_CLASSIC_FIELD_ITEM,
  QA_Q2_CLASSIC_FIELD_FUNCTION, QA_Q2_CLASSIC_FIELD_MOVE
} qa_q2_classic_field_kind;
typedef struct qa_q2_classic_save_field {
  const char *name;
  int64_t offset;
  qa_q2_classic_field_kind kind;
} qa_q2_classic_save_field;
typedef struct qa_q2_classic_record_layout {
  int64_t byte_length;
  const qa_q2_classic_save_field *fields;
  size_t field_count;
} qa_q2_classic_record_layout;
typedef struct qa_q2_classic_save_layout {
  qa_module_identity module;
  int pointer_bytes; /* 4 | 8 */
  int64_t game_bytes;
  int64_t client_count_offset;
  qa_q2_classic_record_layout client, level, entity;
} qa_q2_classic_save_layout;

typedef struct qa_q2_classic_string {
  const char *field; /* borrowed layout name */
  const uint8_t *bytes; /* owned record copy; NULL = TS null */
  size_t length;
} qa_q2_classic_string;
typedef struct qa_q2_classic_reference {
  const char *field;
  int32_t index;
} qa_q2_classic_reference;
typedef struct qa_q2_classic_record {
  uint8_t *bytes; /* owned */
  size_t byte_length;
  qa_q2_classic_string *strings; size_t string_count;
  qa_q2_classic_reference *references; size_t reference_count;
} qa_q2_classic_record;
typedef struct qa_q2_classic_game_save {
  uint8_t *build_date; /* 16 bytes */
  uint8_t *game;
  size_t game_length;
  qa_q2_classic_record *clients; size_t client_count;
} qa_q2_classic_game_save;
typedef struct qa_q2_classic_level_entity {
  int32_t slot;
  qa_q2_classic_record record;
} qa_q2_classic_level_entity;
typedef struct qa_q2_classic_level_save {
  uint64_t function_base;
  qa_q2_classic_record level;
  qa_q2_classic_level_entity *entities; size_t entity_count;
} qa_q2_classic_level_save;

bool qa_decode_q2_classic_game(const uint8_t *bytes, size_t length,
                               const qa_q2_classic_save_layout *layout,
                               qa_q2_classic_game_save *out);
uint8_t *qa_encode_q2_classic_game(const qa_q2_classic_game_save *save,
                                   const qa_q2_classic_save_layout *layout,
                                   size_t *out_length);
bool qa_decode_q2_classic_level(const uint8_t *bytes, size_t length,
                                const qa_q2_classic_save_layout *layout,
                                qa_q2_classic_level_save *out);
uint8_t *qa_encode_q2_classic_level(const qa_q2_classic_level_save *save,
                                    const qa_q2_classic_save_layout *layout,
                                    size_t *out_length);
void qa_q2_classic_record_free(qa_q2_classic_record *record);
void qa_q2_classic_game_save_free(qa_q2_classic_game_save *save);
void qa_q2_classic_level_save_free(qa_q2_classic_level_save *save);

typedef struct qa_q2_classic_relocations {
  void *ctx;
  int pointer_bytes; /* 4 | 8 */
  uint64_t (*string)(void *ctx, const char *field,
                     const uint8_t *bytes, size_t length, bool is_null);
  uint64_t (*reference)(void *ctx, qa_q2_classic_save_field field, int32_t index);
} qa_q2_classic_relocations;
/* Malloc'd restored struct bytes; NULL only on OOM. */
uint8_t *qa_restore_q2_classic_record(const qa_q2_classic_record *record,
                                      const qa_q2_classic_record_layout *layout,
                                      const qa_q2_classic_relocations *relocations);

#endif
