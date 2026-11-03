/* Port of src/content/q3/base/game/state.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/state.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
void qa_q3_player_team_state_init(qa_q3_player_team_state *s) { memset(s, 0, sizeof *s); s->state = QA_Q3_TEAM_STATE_BEGIN; }
void qa_q3_client_session_init(qa_q3_client_session *s) { memset(s, 0, sizeof *s); s->session_team = 0; s->spectator_state = QA_Q3_SPEC_NOT; }
void qa_q3_client_persistant_init(qa_q3_client_persistant *p) {
  memset(p, 0, sizeof *p); p->connected = QA_Q3_CONN_DISCONNECTED; p->netname = NULL; qa_q3_player_team_state_init(&p->team_state);
}
void qa_q3_client_persistant_destroy(qa_q3_client_persistant *p) { free(p->netname); p->netname = NULL; }
bool qa_q3_game_client_init_bound(qa_q3_game_client *c, qa_q3_base_product product,
    const qa_q3_base_player_authority_binding *authority,
    void *ammo_timer_ctx, void (*ammo_timer_stored)(void *ctx, double index, double value)) {
  memset(c, 0, sizeof *c);
  if (!qa_q3_base_player_state_init(&c->ps, product, authority)) return false;
  qa_q3_client_persistant_init(&c->pers); qa_q3_client_session_init(&c->sess);
  if (!qa_q3_base_player_state_slots_init(&c->ammo_times, qa_q3_base_weapon_count(product), NULL, 0, false, NULL,
      ammo_timer_ctx, ammo_timer_stored)) {
    qa_q3_base_player_state_record_destroy(&c->ps); return false;
  }
  return true;
}
bool qa_q3_game_client_init(qa_q3_game_client *c, qa_q3_base_product product) {
  return qa_q3_game_client_init_bound(c, product, NULL, NULL, NULL);
}
void qa_q3_game_client_destroy(qa_q3_game_client *c) {
  qa_q3_client_persistant_destroy(&c->pers); free(c->areabits); c->areabits = NULL;
  qa_q3_base_player_state_record_destroy(&c->ps); qa_q3_base_player_state_slots_destroy(&c->ammo_times);
}
void qa_q3_game_player_state_reset(qa_q3_base_player_state *ps, qa_q3_base_copy_stores stores) {
  qa_q3_base_player_state fresh;
  if (!qa_q3_base_player_state_init(&fresh, qa_q3_base_player_state_record_product(ps), NULL)) qa_range_error("out of memory");
  qa_q3_base_player_state_record_copy_from(ps, &fresh, stores);
  qa_q3_base_player_state_record_destroy(&fresh);
}
qa_q3_damage_participant qa_q3_participant_entity(qa_q3_game_entity *e) { qa_q3_damage_participant p; memset(&p, 0, sizeof p); p.entity = e; return p; }
bool qa_q3_game_entity_inuse(const qa_q3_game_entity *e) { return e->binding.active ? e->binding.active(e->binding.ctx) : false; }
qa_owned_actor qa_q3_game_entity_actor(const qa_q3_game_entity *e) {
  qa_owned_actor a; memset(&a, 0, sizeof a);
  return e->binding.actor ? e->binding.actor(e->binding.ctx) : a;
}
const char *qa_q3_game_entity_classname(const qa_q3_game_entity *e) {
  if (e->classname_client) return e->classname_client->pers.netname ? e->classname_client->pers.netname : "";
  return e->classname;
}
static char *dupstr(const char *v) { if (!v) return NULL; size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
void qa_q3_game_entity_set_classname(qa_q3_game_entity *e, const char *v) { free(e->classname); e->classname = dupstr(v); e->classname_client = NULL; }
void qa_q3_game_entity_bind_client_name(qa_q3_game_entity *e, qa_q3_game_client *c) { e->classname_client = c; }
qa_q3_game_classname_capture qa_q3_game_entity_capture_classname(const qa_q3_game_entity *e) {
  qa_q3_game_classname_capture out; memset(&out, 0, sizeof out);
  if (e->classname_client) { out.kind = 1; out.client = e->classname_client; } else out.value = e->classname;
  return out;
}
double qa_q3_game_entity_health(const qa_q3_game_entity *e) { return e->binding.health(e->binding.ctx); }
void qa_q3_game_entity_set_health(qa_q3_game_entity *e, double v) { e->binding.set_health(e->binding.ctx, v); }
bool qa_q3_game_entity_takedamage(const qa_q3_game_entity *e) { return e->binding.takedamage(e->binding.ctx); }
void qa_q3_game_entity_set_takedamage(qa_q3_game_entity *e, bool v) { e->binding.set_takedamage(e->binding.ctx, v); }
void qa_q3_game_entity_set_nextthink(qa_q3_game_entity *e, double v) { e->nextthink = v; if (e->binding.schedule) e->binding.schedule(e->binding.ctx, v); }
void qa_q3_game_entity_restore_next_think(qa_q3_game_entity *e, double v) { e->nextthink = v; }
void qa_q3_game_entity_init(qa_q3_game_entity *e, double slot, qa_q3_game_entity_binding binding) {
  if (slot != trunc(slot) || slot < 0 || slot >= QA_Q3_MAX_GENTITIES) qa_range_error("Game entity slot outside 0..1023");
  memset(e, 0, sizeof *e); e->slot = slot; e->binding = binding; e->mover_state = QA_Q3_MOVER_POS1;
  qa_q3_base_entity_shared_init(&e->r, binding.body);
}
void qa_q3_game_entity_destroy(qa_q3_game_entity *e) {
  free(e->classname); free(e->model); free(e->model2); free(e->message); free(e->target);
  free(e->targetname); free(e->team); free(e->target_shader_name); free(e->target_shader_new_name);
  e->classname = e->model = e->model2 = e->message = e->target = e->targetname = e->team = NULL;
  e->target_shader_name = e->target_shader_new_name = NULL;
}
static double snap_component(double component) {
  double value = (float)component;
  return value >= -2147483648.0 && value < 2147483648.0 ? trunc(value) + 0.0 : -2147483648.0;
}
static qa_vec3 copy_position(qa_vec3 value, bool snap) {
  if (!snap) return value;
  return (qa_vec3){snap_component(value.x), snap_component(value.y), snap_component(value.z)};
}
void qa_q3_game_player_state_to_entity_state(qa_q3_base_player_state *ps, qa_q3_entity_state_record *s,
    bool snap, bool has_extrapolation, double extrapolation_time) {
  double health = qa_q3_base_player_state_record_health(ps);
  s->e_type = ps->pm_type == QA_Q3_PM_INTERMISSION || ps->pm_type == QA_Q3_PM_SPECTATOR || health <= -40 ? 10 : 1;
  s->number = ps->client_num;
  s->pos.type = has_extrapolation ? QA_Q3_BASE_TR_LINEAR_STOP : QA_Q3_BASE_TR_INTERPOLATE;
  s->pos.base = copy_position(qa_q3_base_player_state_record_origin(ps), snap);
  s->pos.delta = qa_q3_base_player_state_record_velocity(ps);
  if (has_extrapolation) { s->pos.time = extrapolation_time; s->pos.duration = 50; }
  s->apos.type = QA_Q3_BASE_TR_INTERPOLATE;
  s->apos.base = copy_position(ps->viewangles, snap);
  s->angles2.y = (float)ps->movement_dir;
  s->legs_anim = ps->legs_anim;
  s->torso_anim = ps->torso_anim;
  s->client_num = ps->client_num;
  s->e_flags = qa_q3_base_player_state_record_health(ps) <= 0 ? (qa_int32(ps->e_flags) | 1) : (qa_int32(ps->e_flags) & ~1);
  if (ps->external_event != 0) {
    s->event = ps->external_event;
    s->event_parm = ps->external_event_parm;
  } else if (ps->entity_event_sequence < ps->event_sequence) {
    double oldest = qa_int32(ps->event_sequence - 2);
    if (ps->entity_event_sequence < oldest) ps->entity_event_sequence = oldest;
    int32_t sequence = qa_int32(ps->entity_event_sequence), slot = sequence & 1;
    s->event = qa_int32(qa_q3_base_player_state_slots_get(&ps->events, slot)) | ((sequence & 3) << 8);
    s->event_parm = qa_q3_base_player_state_slots_get(&ps->event_parms, slot);
    ps->entity_event_sequence = qa_int32(ps->entity_event_sequence + 1);
  }
  s->weapon = ps->weapon;
  s->ground_entity_num = ps->ground_entity_num;
  int32_t powerups = 0;
  for (double index = 0; index < ps->powerups.length; index++)
    if (qa_q3_base_player_state_slots_get(&ps->powerups, index) != 0) powerups |= (int32_t)(1u << (unsigned)index);
  s->powerups = powerups;
  s->loop_sound = ps->loop_sound;
  s->generic1 = ps->generic1;
}
