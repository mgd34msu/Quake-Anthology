/*
 * Port of src/network/q2/temp-entities.ts. TS field name literals
 * become enums with exact name strings available via helpers; the
 * raw byte span is copied. GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_TEMP_ENTITIES_H
#define QA_NETWORK_Q2_TEMP_ENTITIES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/network/q2/message.h"

/* TS Q2TempField kinds and names. */
typedef enum qa_q2_temp_field_kind {
  QA_Q2_TEMP_FIELD_INTEGER, QA_Q2_TEMP_FIELD_VECTOR
} qa_q2_temp_field_kind;
typedef enum qa_q2_temp_field_name {
  QA_Q2_TEMP_NAME_ENTITY1, QA_Q2_TEMP_NAME_ENTITY2, QA_Q2_TEMP_NAME_COUNT,
  QA_Q2_TEMP_NAME_COLOR, QA_Q2_TEMP_NAME_TIME, QA_Q2_TEMP_NAME_POSITION1,
  QA_Q2_TEMP_NAME_POSITION2, QA_Q2_TEMP_NAME_DIRECTION, QA_Q2_TEMP_NAME_OFFSET
} qa_q2_temp_field_name;

const char *qa_q2_temp_field_name_text(qa_q2_temp_field_name name);

/* TS Q2TempField. */
typedef struct qa_q2_temp_field {
  qa_q2_temp_field_kind kind;
  qa_q2_temp_field_name name;
  double integer_value;  /* kind INTEGER */
  qa_vec3 vector_value;  /* kind VECTOR */
} qa_q2_temp_field;

#define QA_Q2_TEMP_FIELDS_MAX 8

/* TS Q2TempEntity (raw is a borrowed span copy). */
typedef struct qa_q2_temp_entity {
  double type;
  qa_q2_temp_field fields[QA_Q2_TEMP_FIELDS_MAX];
  size_t field_count;
  uint8_t *raw; /* owned */
  size_t raw_length;
} qa_q2_temp_entity;

void qa_q2_temp_entity_free(qa_q2_temp_entity *entity);

/* TS readGamePosition(message, floating, int23=false). */
qa_vec3 qa_q2_read_game_position(qa_q2_sizebuf *message, bool floating, bool int23);
/* TS readTempEntity(message, floating, q2proExtended=false, int23=false).
 * False only on OOM. */
bool qa_q2_read_temp_entity(qa_q2_sizebuf *message, bool floating,
                            bool q2pro_extended, bool int23,
                            qa_q2_temp_entity *out);

#endif
