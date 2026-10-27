/* Port of src/content/q3/base/game/state.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_error.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
const char *qa_q3_game_product_name(qa_q3_game_product p) { return p == QA_Q3_PRODUCT_BASEQ3 ? "baseq3" : "missionpack"; }
static float f32(double v) { return (float)v; }
static double seconds(double ms) { return f32(f32(ms) * f32(0.001)); }
static double periodic_radians(qa_q3_game_trajectory_mirror tr, double at) {
  double fraction = f32(f32((double)qa_int32(at - tr.time)) / f32(tr.duration));
  return f32(f32(fraction * f32(3.141592653589793)) * 2);
}
qa_vec3 qa_q3_game_evaluate_trajectory_mirror(qa_q3_game_trajectory_mirror tr, double at) {
  if (tr.type == QA_Q3_TR_STATIONARY || tr.type == QA_Q3_TR_INTERPOLATE) return tr.base;
  if (tr.type == QA_Q3_TR_LINEAR) return qa_add3(tr.base, qa_scale3(tr.delta, seconds(qa_int32(at - tr.time))));
  if (tr.type == QA_Q3_TR_SINE) return qa_add3(tr.base, qa_scale3(tr.delta, f32(sin(periodic_radians(tr, at)))));
  if (tr.type == QA_Q3_TR_LINEAR_STOP) {
    double end = qa_int32(tr.time + tr.duration), time = at > end ? end : at;
    double dt = seconds(qa_int32(time - tr.time)); if (dt < 0) dt = 0;
    return qa_add3(tr.base, qa_scale3(tr.delta, dt));
  }
  if (tr.type == QA_Q3_TR_GRAVITY) {
    double dt = seconds(qa_int32(at - tr.time)); qa_vec3 r = qa_add3(tr.base, qa_scale3(tr.delta, dt));
    double fall = f32(f32(0.5 * 800 * dt) * dt); return qa_math_vec3(r.x, r.y, r.z - fall);
  }
  { char buf[128]; snprintf(buf, sizeof buf, "BG_EvaluateTrajectory: unknown trType: %g", tr.time); qa_common_error(QA_COMMON_ERROR_DROP, buf); }
}
qa_vec3 qa_q3_game_evaluate_trajectory_delta_mirror(qa_q3_game_trajectory_mirror tr, double at) {
  qa_vec3 zero = {0, 0, 0};
  if (tr.type == QA_Q3_TR_STATIONARY || tr.type == QA_Q3_TR_INTERPOLATE) return zero;
  if (tr.type == QA_Q3_TR_LINEAR) return tr.delta;
  if (tr.type == QA_Q3_TR_SINE) return qa_scale3(tr.delta, f32(f32(cos(periodic_radians(tr, at))) * 0.5));
  if (tr.type == QA_Q3_TR_LINEAR_STOP) return at > qa_int32(tr.time + tr.duration) ? zero : tr.delta;
  if (tr.type == QA_Q3_TR_GRAVITY) { double dt = seconds(qa_int32(at - tr.time)); return qa_math_vec3(tr.delta.x, tr.delta.y, tr.delta.z - f32(800 * dt)); }
  { char buf[128]; snprintf(buf, sizeof buf, "BG_EvaluateTrajectoryDelta: unknown trType: %g", tr.time); qa_common_error(QA_COMMON_ERROR_DROP, buf); }
}
double qa_q3_game_slots_get_mirror(const qa_q3_game_player_slots_mirror *s, size_t i) {
  if (i >= s->length) qa_range_error("Player state slot %zu outside %zu", i, s->length);
  return s->values[i];
}
void qa_q3_game_slots_set_mirror(qa_q3_game_player_slots_mirror *s, size_t i, double v) {
  if (i >= s->length) qa_range_error("Player state slot %zu outside %zu", i, s->length);
  s->values[i] = qa_int32(v);
}
void qa_q3_game_player_state_add_event_mirror(qa_q3_game_player_state_mirror *ps, double ev, double parm) {
  /* Mirror simplification: ring into events slots by sequence in generic. */
  size_t i = ps->events.length ? (size_t)((int)ps->generic % (int)ps->events.length) : 0;
  if (ps->events.length) { qa_q3_game_slots_set_mirror(&ps->events, i, ev); qa_q3_game_slots_set_mirror(&ps->event_parms, i, parm); ps->generic += 1; }
}
void qa_q3_player_team_state_init(qa_q3_player_team_state *s) { memset(s, 0, sizeof *s); s->state = QA_Q3_TEAM_STATE_BEGIN; }
void qa_q3_client_session_init(qa_q3_client_session *s) { memset(s, 0, sizeof *s); s->session_team = 0; s->spectator_state = QA_Q3_SPEC_NOT; }
void qa_q3_client_persistant_init(qa_q3_client_persistant *p) {
  memset(p, 0, sizeof *p); p->connected = QA_Q3_CONN_DISCONNECTED; p->netname = NULL; qa_q3_player_team_state_init(&p->team_state);
}
void qa_q3_client_persistant_destroy(qa_q3_client_persistant *p) { free(p->netname); p->netname = NULL; }
static void slots_init(qa_q3_game_player_slots_mirror *s, size_t n) { memset(s->values, 0, sizeof s->values); s->length = n < QA_Q3_GAME_SLOTS_CAP ? n : QA_Q3_GAME_SLOTS_CAP; }
void qa_q3_game_client_init(qa_q3_game_client *c, qa_q3_game_product product) {
  memset(c, 0, sizeof *c); c->ps.product = product;
  slots_init(&c->ps.stats, 16); slots_init(&c->ps.persistant, 16); slots_init(&c->ps.powerups, 16);
  slots_init(&c->ps.ammo, 16); slots_init(&c->ps.events, 2); slots_init(&c->ps.event_parms, 2); slots_init(&c->ps.delta_angles, 3);
  qa_q3_client_persistant_init(&c->pers); qa_q3_client_session_init(&c->sess);
  slots_init(&c->ammo_times, product == QA_Q3_PRODUCT_BASEQ3 ? 11 : 14);
}
void qa_q3_game_client_destroy(qa_q3_game_client *c) { qa_q3_client_persistant_destroy(&c->pers); free(c->areabits); c->areabits = NULL; }
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
double qa_q3_game_entity_health(const qa_q3_game_entity *e) { return e->binding.body.health; }
void qa_q3_game_entity_set_health(qa_q3_game_entity *e, double v) { e->binding.body.health = v; }
bool qa_q3_game_entity_takedamage(const qa_q3_game_entity *e) { return e->binding.body.takedamage; }
void qa_q3_game_entity_set_takedamage(qa_q3_game_entity *e, bool v) { e->binding.body.takedamage = v; }
void qa_q3_game_entity_set_nextthink(qa_q3_game_entity *e, double v) { e->nextthink = v; if (e->binding.schedule) e->binding.schedule(e->binding.ctx, v); }
void qa_q3_game_entity_restore_next_think(qa_q3_game_entity *e, double v) { e->nextthink = v; }
void qa_q3_game_entity_init(qa_q3_game_entity *e, double slot, qa_q3_game_entity_binding binding) {
  if (slot != trunc(slot) || slot < 0 || slot >= QA_Q3_MAX_GENTITIES) qa_range_error("Game entity slot outside 0..1023");
  memset(e, 0, sizeof *e); e->slot = slot; e->binding = binding; e->mover_state = QA_Q3_MOVER_POS1;
}
void qa_q3_game_entity_destroy(qa_q3_game_entity *e) {
  free(e->classname); free(e->model); free(e->model2); free(e->message); free(e->target);
  free(e->targetname); free(e->team); free(e->target_shader_name); free(e->target_shader_new_name);
  e->classname = e->model = e->model2 = e->message = e->target = e->targetname = e->team = NULL;
  e->target_shader_name = e->target_shader_new_name = NULL;
}
