/* Port of src/app/bootstrap/network/q2-effects.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_effects.h"
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/q2/temp_types.h"
static const struct { qa_q2_temp_type type; const char *effect; } trails[] = {
  {QA_Q2_TE_RAILTRAIL, "rail"}, {QA_Q2_TE_BUBBLETRAIL, "bubble-trail"},
  {QA_Q2_TE_BFG_LASER, "bfg-laser"}, {QA_Q2_TE_BFG_ZAP, "bfg-zap"}};
enum shape { SH_POSITION, SH_DIRECTION, SH_SPLASH };
static const struct { const char *name; qa_q2_temp_type type; enum shape shape; } effects[] = {
  {"gunshot", QA_Q2_TE_GUNSHOT, SH_DIRECTION}, {"blood", QA_Q2_TE_BLOOD, SH_DIRECTION},
  {"blaster", QA_Q2_TE_BLASTER, SH_DIRECTION}, {"shotgun", QA_Q2_TE_SHOTGUN, SH_DIRECTION},
  {"sparks", QA_Q2_TE_SPARKS, SH_DIRECTION}, {"screen-sparks", QA_Q2_TE_SCREEN_SPARKS, SH_DIRECTION},
  {"shield-sparks", QA_Q2_TE_SHIELD_SPARKS, SH_DIRECTION},
  {"bullet-sparks", QA_Q2_TE_BULLET_SPARKS, SH_DIRECTION},
  {"greenblood", QA_Q2_TE_GREENBLOOD, SH_DIRECTION}, {"blaster2", QA_Q2_TE_BLASTER2, SH_DIRECTION},
  {"flechette", QA_Q2_TE_FLECHETTE, SH_DIRECTION}, {"moreblood", QA_Q2_TE_MOREBLOOD, SH_DIRECTION},
  {"electric-sparks", QA_Q2_TE_ELECTRIC_SPARKS, SH_DIRECTION},
  {"splash", QA_Q2_TE_SPLASH, SH_SPLASH}, {"laser-sparks", QA_Q2_TE_LASER_SPARKS, SH_SPLASH},
  {"welding-sparks", QA_Q2_TE_WELDING_SPARKS, SH_SPLASH},
  {"tunnel-sparks", QA_Q2_TE_TUNNEL_SPARKS, SH_SPLASH},
  {"explosion1", QA_Q2_TE_EXPLOSION1, SH_POSITION}, {"explosion2", QA_Q2_TE_EXPLOSION2, SH_POSITION},
  {"rocket-explosion", QA_Q2_TE_ROCKET_EXPLOSION, SH_POSITION},
  {"grenade-explosion", QA_Q2_TE_GRENADE_EXPLOSION, SH_POSITION},
  {"rocket-explosion-water", QA_Q2_TE_ROCKET_EXPLOSION_WATER, SH_POSITION},
  {"grenade-explosion-water", QA_Q2_TE_GRENADE_EXPLOSION_WATER, SH_POSITION},
  {"bfg-explosion", QA_Q2_TE_BFG_EXPLOSION, SH_POSITION},
  {"bfg-bigexplosion", QA_Q2_TE_BFG_BIGEXPLOSION, SH_POSITION},
  {"boss-teleport", QA_Q2_TE_BOSSTPORT, SH_POSITION},
  {"other-teleport", QA_Q2_TE_TELEPORT_EFFECT, SH_POSITION}};
#define EFFECT_COUNT (sizeof effects / sizeof effects[0])
static const qa_q2_temp_field *find_field(const qa_q2_temp_entity *value,
    qa_q2_temp_field_kind kind, qa_q2_temp_field_name name) {
  for (size_t i = 0; i < value->field_count; i++)
    if (value->fields[i].kind == kind && value->fields[i].name == name) return &value->fields[i];
  return NULL;
}
bool qa_app_q2_beam_from_wire(const qa_q2_temp_entity *value, qa_q2w_event *out) {
  const char *effect = NULL;
  for (size_t i = 0; i < sizeof trails / sizeof trails[0]; i++)
    if ((double)trails[i].type == value->type) effect = trails[i].effect;
  if (!effect) return false;
  const qa_q2_temp_field *start = find_field(value, QA_Q2_TEMP_FIELD_VECTOR, QA_Q2_TEMP_NAME_POSITION1);
  const qa_q2_temp_field *end = find_field(value, QA_Q2_TEMP_FIELD_VECTOR, QA_Q2_TEMP_NAME_POSITION2);
  if (!start || !end) qa_range_error("Q2 trail requires both source endpoints");
  memset(out, 0, sizeof *out);
  out->kind = QA_Q2W_EVENT_BEAM;
  out->effect = effect; out->actor = NULL;
  out->start = start->vector_value; out->end = end->vector_value; out->duration = 0.1;
  return true;
}
static void push_field(qa_q2_temp_entity *out, qa_q2_temp_field field) {
  out->fields[out->field_count++] = field;
}
bool qa_app_q2_effect_to_wire(const qa_q2_presentation_event *event, qa_q2_temp_entity *out) {
  const char *name = event->effect;
  if (strncmp(name, "q2:", 3) == 0) name += 3;
  size_t found = EFFECT_COUNT;
  for (size_t i = 0; i < EFFECT_COUNT; i++)
    if (strcmp(effects[i].name, name) == 0) found = i;
  if (found == EFFECT_COUNT) return false;
  memset(out, 0, sizeof *out);
  out->type = effects[found].type;
  if (effects[found].shape == SH_SPLASH) {
    qa_q2_temp_field f = {QA_Q2_TEMP_FIELD_INTEGER, QA_Q2_TEMP_NAME_COUNT, event->count, {0, 0, 0}};
    push_field(out, f);
  }
  qa_q2_temp_field pos = {QA_Q2_TEMP_FIELD_VECTOR, QA_Q2_TEMP_NAME_POSITION1, 0, event->origin};
  push_field(out, pos);
  if (effects[found].shape != SH_POSITION) {
    qa_q2_temp_field dir = {QA_Q2_TEMP_FIELD_VECTOR, QA_Q2_TEMP_NAME_DIRECTION, 0, event->direction};
    push_field(out, dir);
  }
  if (effects[found].shape == SH_SPLASH) {
    qa_q2_temp_field f = {QA_Q2_TEMP_FIELD_INTEGER, QA_Q2_TEMP_NAME_COLOR, event->color, {0, 0, 0}};
    push_field(out, f);
  }
  return true;
}
bool qa_app_q2_effect_from_wire(const qa_q2_temp_entity *value, qa_q2_presentation_event *out) {
  size_t found = EFFECT_COUNT;
  for (size_t i = 0; i < EFFECT_COUNT; i++)
    if ((double)effects[i].type == value->type) found = i;
  if (found == EFFECT_COUNT) return false;
  const qa_q2_temp_field *pos = find_field(value, QA_Q2_TEMP_FIELD_VECTOR, QA_Q2_TEMP_NAME_POSITION1);
  const qa_q2_temp_field *dir = find_field(value, QA_Q2_TEMP_FIELD_VECTOR, QA_Q2_TEMP_NAME_DIRECTION);
  const qa_q2_temp_field *count = find_field(value, QA_Q2_TEMP_FIELD_INTEGER, QA_Q2_TEMP_NAME_COUNT);
  const qa_q2_temp_field *color = find_field(value, QA_Q2_TEMP_FIELD_INTEGER, QA_Q2_TEMP_NAME_COLOR);
  qa_vec3 zero = {0, 0, 0};
  memset(out, 0, sizeof *out);
  out->kind = "effect";
  out->effect = effects[found].name;
  out->origin = pos ? pos->vector_value : zero;
  out->direction = dir ? dir->vector_value : zero;
  out->count = count ? count->integer_value : 0;
  out->color = color ? color->integer_value : 0;
  return true;
}
