/* Port of src/network/q2/temp-entities.ts. GPL-2.0-or-later. */
#include "qa/network/q2/temp_entities.h"

#include <stdlib.h>
#include <string.h>

#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/temp_types.h"

const char *qa_q2_temp_field_name_text(qa_q2_temp_field_name name) {
  switch (name) {
    case QA_Q2_TEMP_NAME_ENTITY1: return "entity1";
    case QA_Q2_TEMP_NAME_ENTITY2: return "entity2";
    case QA_Q2_TEMP_NAME_COUNT: return "count";
    case QA_Q2_TEMP_NAME_COLOR: return "color";
    case QA_Q2_TEMP_NAME_TIME: return "time";
    case QA_Q2_TEMP_NAME_POSITION1: return "position1";
    case QA_Q2_TEMP_NAME_POSITION2: return "position2";
    case QA_Q2_TEMP_NAME_DIRECTION: return "direction";
    case QA_Q2_TEMP_NAME_OFFSET: return "offset";
  }
  return "";
}

void qa_q2_temp_entity_free(qa_q2_temp_entity *entity) {
  free(entity->raw);
  entity->raw = NULL;
  entity->raw_length = 0;
}

static double read_component(qa_q2_sizebuf *m, bool floating, bool int23) {
  if (int23) return (double)qa_q2pro_read_int23(m, 0) / 8.0;
  return floating ? qa_q2_msg_read_float(m) : qa_q2_msg_read_coord(m);
}

qa_vec3 qa_q2_read_game_position(qa_q2_sizebuf *message, bool floating, bool int23) {
  qa_vec3 v;
  v.x = read_component(message, floating, int23);
  v.y = read_component(message, floating, int23);
  v.z = read_component(message, floating, int23);
  return v;
}

typedef struct temp_builder {
  qa_q2_sizebuf *m;
  bool floating;
  bool int23;
  qa_q2_temp_entity *out;
} temp_builder;

static void push_field(temp_builder *b, qa_q2_temp_field field) {
  if (b->out->field_count < QA_Q2_TEMP_FIELDS_MAX) {
    b->out->fields[b->out->field_count++] = field;
  }
}
static double field_integer(temp_builder *b, qa_q2_temp_field_name name, int which) {
  double value = which == 2 ? (double)qa_q2_msg_read_long(b->m)
                 : which == 1 ? (double)qa_q2_msg_read_short(b->m)
                              : (double)qa_q2_msg_read_byte(b->m);
  qa_q2_temp_field f;
  memset(&f, 0, sizeof f);
  f.kind = QA_Q2_TEMP_FIELD_INTEGER;
  f.name = name;
  f.integer_value = value;
  push_field(b, f);
  return value;
}
static void field_position(temp_builder *b, qa_q2_temp_field_name name) {
  qa_q2_temp_field f;
  memset(&f, 0, sizeof f);
  f.kind = QA_Q2_TEMP_FIELD_VECTOR;
  f.name = name;
  f.vector_value = qa_q2_read_game_position(b->m, b->floating, b->int23);
  push_field(b, f);
}
static void field_direction(temp_builder *b) {
  float dir[3] = {0, 0, 0};
  qa_q2_msg_read_dir(b->m, dir);
  qa_q2_temp_field f;
  memset(&f, 0, sizeof f);
  f.kind = QA_Q2_TEMP_FIELD_VECTOR;
  f.name = QA_Q2_TEMP_NAME_DIRECTION;
  f.vector_value.x = (double)dir[0];
  f.vector_value.y = (double)dir[1];
  f.vector_value.z = (double)dir[2];
  push_field(b, f);
}

bool qa_q2_read_temp_entity(qa_q2_sizebuf *message, bool floating,
                            bool q2pro_extended, bool int23,
                            qa_q2_temp_entity *out) {
  memset(out, 0, sizeof *out);
  int64_t start = message->readcount;
  int32_t type = qa_q2_msg_read_byte(message);
  out->type = (double)type;
  temp_builder b = {message, floating, int23, out};
  switch (type) {
    case QA_Q2_TE_BLOOD: case QA_Q2_TE_GUNSHOT: case QA_Q2_TE_SPARKS:
    case QA_Q2_TE_BULLET_SPARKS: case QA_Q2_TE_SCREEN_SPARKS:
    case QA_Q2_TE_SHIELD_SPARKS: case QA_Q2_TE_SHOTGUN: case QA_Q2_TE_BLASTER:
    case QA_Q2_TE_GREENBLOOD: case QA_Q2_TE_BLASTER2: case QA_Q2_TE_FLECHETTE:
    case QA_Q2_TE_HEATBEAM_SPARKS: case QA_Q2_TE_HEATBEAM_STEAM:
    case QA_Q2_TE_MOREBLOOD: case QA_Q2_TE_ELECTRIC_SPARKS:
    case QA_Q2_TE_BLUEHYPERBLASTER_2: case QA_Q2_TE_BERSERK_SLAM:
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_direction(&b);
      break;
    case QA_Q2_TE_SPLASH: case QA_Q2_TE_LASER_SPARKS:
    case QA_Q2_TE_WELDING_SPARKS: case QA_Q2_TE_TUNNEL_SPARKS:
      field_integer(&b, QA_Q2_TEMP_NAME_COUNT, 0);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_direction(&b);
      field_integer(&b, QA_Q2_TEMP_NAME_COLOR, 0);
      break;
    case QA_Q2_TE_BLUEHYPERBLASTER: case QA_Q2_TE_RAILTRAIL:
    case QA_Q2_TE_RAILTRAIL2: case QA_Q2_TE_BUBBLETRAIL:
    case QA_Q2_TE_DEBUGTRAIL: case QA_Q2_TE_BUBBLETRAIL2:
    case QA_Q2_TE_BFG_LASER: case QA_Q2_TE_BFG_ZAP:
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION2);
      break;
    case QA_Q2_TE_GRENADE_EXPLOSION: case QA_Q2_TE_GRENADE_EXPLOSION_WATER:
    case QA_Q2_TE_EXPLOSION2: case QA_Q2_TE_PLASMA_EXPLOSION:
    case QA_Q2_TE_ROCKET_EXPLOSION: case QA_Q2_TE_ROCKET_EXPLOSION_WATER:
    case QA_Q2_TE_EXPLOSION1: case QA_Q2_TE_EXPLOSION1_NP:
    case QA_Q2_TE_EXPLOSION1_BIG: case QA_Q2_TE_BFG_EXPLOSION:
    case QA_Q2_TE_BFG_BIGEXPLOSION: case QA_Q2_TE_BOSSTPORT:
    case QA_Q2_TE_PLAIN_EXPLOSION: case QA_Q2_TE_CHAINFIST_SMOKE:
    case QA_Q2_TE_TRACKER_EXPLOSION: case QA_Q2_TE_TELEPORT_EFFECT:
    case QA_Q2_TE_DBALL_GOAL: case QA_Q2_TE_WIDOWSPLASH:
    case QA_Q2_TE_NUKEBLAST: case QA_Q2_TE_EXPLOSION1_NL:
    case QA_Q2_TE_EXPLOSION2_NL:
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      break;
    case QA_Q2_TE_PARASITE_ATTACK: case QA_Q2_TE_MEDIC_CABLE_ATTACK:
    case QA_Q2_TE_HEATBEAM: case QA_Q2_TE_MONSTER_HEATBEAM:
    case QA_Q2_TE_GRAPPLE_CABLE_2: case QA_Q2_TE_LIGHTNING_BEAM:
      field_integer(&b, QA_Q2_TEMP_NAME_ENTITY1, 1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION2);
      break;
    case QA_Q2_TE_GRAPPLE_CABLE:
      field_integer(&b, QA_Q2_TEMP_NAME_ENTITY1, 1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION2);
      field_position(&b, QA_Q2_TEMP_NAME_OFFSET);
      break;
    case QA_Q2_TE_LIGHTNING:
      field_integer(&b, QA_Q2_TEMP_NAME_ENTITY1, 1);
      field_integer(&b, QA_Q2_TEMP_NAME_ENTITY2, 1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION2);
      break;
    case QA_Q2_TE_FLASHLIGHT:
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_integer(&b, QA_Q2_TEMP_NAME_ENTITY1, 1);
      break;
    case QA_Q2_TE_FORCEWALL:
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION2);
      field_integer(&b, QA_Q2_TEMP_NAME_COLOR, 0);
      break;
    case QA_Q2_TE_STEAM: {
      double entity = field_integer(&b, QA_Q2_TEMP_NAME_ENTITY1, 1);
      field_integer(&b, QA_Q2_TEMP_NAME_COUNT, 0);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      field_direction(&b);
      field_integer(&b, QA_Q2_TEMP_NAME_COLOR, 0);
      field_integer(&b, QA_Q2_TEMP_NAME_ENTITY2, 1);
      if (entity != -1) field_integer(&b, QA_Q2_TEMP_NAME_TIME, 2);
      break;
    }
    case QA_Q2_TE_WIDOWBEAMOUT:
      field_integer(&b, QA_Q2_TEMP_NAME_ENTITY1, 1);
      field_position(&b, QA_Q2_TEMP_NAME_POSITION1);
      break;
    case QA_Q2_TE_POWER_SPLASH:
      field_integer(&b, QA_Q2_TEMP_NAME_ENTITY1, 1);
      field_integer(&b, QA_Q2_TEMP_NAME_COUNT, 0);
      break;
    case QA_Q2_TE_Q2PRO_DAMAGE_DEALT:
      if (!q2pro_extended) {
        qa_q2_throw_error("Q2PRO damage event needs its game message dialect");
      }
      field_integer(&b, QA_Q2_TEMP_NAME_COUNT, 1);
      break;
    default:
      qa_q2_throw_error("Unknown Q2 temporary entity %d", (int)type);
  }
  qa_q2_message_check_read(message);
  int64_t span = message->readcount - start;
  size_t raw_len = span > 0 ? (size_t)span : 0;
  uint8_t *raw = (uint8_t *)malloc(raw_len > 0 ? raw_len : 1);
  if (raw == NULL) return false;
  for (size_t i = 0; i < raw_len; i++) {
    int64_t at = start + (int64_t)i;
    raw[i] = at >= 0 && (uint64_t)at < (uint64_t)message->data_length
                 ? message->data[at]
                 : 0;
  }
  out->raw = raw;
  out->raw_length = raw_len;
  return true;
}
