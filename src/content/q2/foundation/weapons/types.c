/* Port of src/content/q2/foundation/weapons/types.ts (+ host mirrors). GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/types.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"

noreturn void qa_q2f_error(const char *format, ...) {
  va_list args;
  fputs("Error: ", stderr);
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputc('\n', stderr);
  abort();
}

const char *qa_q2f_spawn_value(const qa_q2f_spawn_fields *fields, const char *key) {
  for (size_t i = 0; i < fields->count; i++)
    if (strcmp(fields->keys[i], key) == 0) return fields->values[i];
  return NULL;
}
double qa_q2f_number_field(const qa_q2f_spawn_fields *fields, const char *key, double fallback) {
  const char *value = qa_q2f_spawn_value(fields, key);
  return value == NULL ? fallback : qa_native_atof(value);
}

bool qa_q2f_think_eq(qa_q2f_think a, qa_q2f_think b) { return a.fn == b.fn && a.ctx == b.ctx; }

static void free_think(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)ctx;
  game->remove(game->ctx, entity);
}
static void free_die(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  game->remove(game->ctx, entity);
}
qa_q2f_think qa_q2f_free_entity_think(void) { qa_q2f_think t = {NULL, free_think}; return t; }
qa_q2f_die qa_q2f_free_entity_die(void) { qa_q2f_die t = {NULL, free_die}; return t; }

void qa_q2f_entity_init(qa_q2f_entity *e, qa_owned_actor actor, qa_q2f_spawn_fields spawn) {
  memset(e, 0, sizeof *e);
  e->actor = actor;
  e->spawn = spawn;
  const char *v;
  e->classname = spawn.classname;
  e->target = (v = qa_q2f_spawn_value(&spawn, "target")) ? v : "";
  e->targetname = (v = qa_q2f_spawn_value(&spawn, "targetname")) ? v : "";
  e->killtarget = (v = qa_q2f_spawn_value(&spawn, "killtarget")) ? v : "";
  e->combat_target = (v = qa_q2f_spawn_value(&spawn, "combattarget")) ? v : "";
  e->death_target = (v = qa_q2f_spawn_value(&spawn, "deathtarget")) ? v : "";
  e->health_target = (v = qa_q2f_spawn_value(&spawn, "healthtarget")) ? v : "";
  e->item_target = (v = qa_q2f_spawn_value(&spawn, "itemtarget")) ? v : "";
  e->message = (v = qa_q2f_spawn_value(&spawn, "message")) ? v : "";
  e->model = (v = qa_q2f_spawn_value(&spawn, "model")) ? v : "";
  e->model2 = ""; e->model3 = ""; e->model4 = "";
  e->noise = ""; e->sound_text = ""; e->map = "";
  e->old_frame = -1; e->scale = 1; e->alpha = 1; e->light_level = 128;
  e->clip_mask = 0x6000003;
  e->visible = true;
  e->solid = QA_Q2F_SOLID_NONE;
  e->motion = QA_Q2F_MOTION_STATIONARY;
  e->gravity = 1;
  e->gravity_vector = (qa_vec3){0, 0, -1};
}

bool qa_q2f_save_actor(const qa_actor_id *actor, qa_saved_actor_id *out) {
  if (actor == NULL) return false;
  out->slot = (double)actor->slot;
  out->generation = (double)actor->generation;
  return true;
}
qa_owned_actor qa_q2f_restore_actor(qa_q2f_game *game, qa_saved_actor_id actor) {
  qa_q2f_owned_result owner = game->host->actors.resolve_saved(game->host->actors.ctx, actor);
  if (!owner.has_value)
    qa_q2f_error("Q2 checkpoint references missing actor %g:%g", actor.slot, actor.generation);
  return owner.value;
}

const char *qa_q2w_phase_name(qa_q2w_phase phase) {
  switch (phase) {
    case QA_Q2W_ACTIVATING: return "activating";
    case QA_Q2W_READY: return "ready";
    case QA_Q2W_FIRING: return "firing";
    case QA_Q2W_DROPPING: return "dropping";
  }
  return "activating";
}

void qa_q2w_state_init(qa_q2w_state *s, qa_q2w_name weapon) {
  memset(s, 0, sizeof *s);
  s->primary_handoff = QA_Q2W_HANDOFF_ACTIVE;
  s->weapon = weapon;
  s->phase = QA_Q2W_ACTIVATING;
  s->hand_reservation = QA_Q2W_RES_NONE;
  s->kick_duration = 0.2;
  s->loop_sound = "";
  s->gun_rate = 10;
}
void qa_q2w_state_init_default(qa_q2w_state *state) { qa_q2w_state_init(state, "blaster"); }
