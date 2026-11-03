/* Port of src/content/q3/base/game/entities.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/entities.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
const char *qa_q3_entity_event_status_name(qa_q3_entity_event_status s) {
  switch (s) { case QA_Q3_EVENT_INACTIVE: return "inactive"; case QA_Q3_EVENT_ACTIVE: return "active";
    case QA_Q3_EVENT_WAITING: return "waiting"; default: return "freed"; }
}
static double check_time(double time) {
  if (time != trunc(time) || time < -2147483648 || time > 2147483647) qa_range_error("Game time must be a signed 32-bit millisecond value");
  return time;
}
void qa_q3_init_game_entity(qa_q3_game_entity *e) {
  qa_q3_game_entity_set_classname(e, "noclass"); e->s.number = e->slot; e->r.owner_num = QA_Q3_BASE_ENTITYNUM_NONE;
}
void qa_q3_set_origin(qa_q3_game_entity *e, qa_vec3 origin) {
  qa_q3_base_trajectory pos = {QA_Q3_BASE_TR_STATIONARY, 0, 0, origin, {0, 0, 0}};
  e->s.pos = pos; qa_q3_base_entity_shared_set_current_origin(&e->r, origin);
}
void qa_q3_run_think(qa_q3_game_entity *e, double time) {
  double think_time = (float)e->nextthink; if (think_time <= 0 || think_time > time) return;
  if (e->binding.run_think) e->binding.run_think(e->binding.ctx, time);
}
static void pool_print(qa_q3_entity_pool *p, char *msg) { if (msg) { if (p->options.print) p->options.print(p->options.ctx, msg); free(msg); } }
void qa_q3_entity_pool_init(qa_q3_entity_pool *pool, qa_q3_entity_pool_options options) {
  if (options.max_clients != trunc(options.max_clients) || options.max_clients < 1 || options.max_clients > QA_Q3_MAX_CLIENTS)
    qa_range_error("Configured clients must be within 1..64");
  check_time(options.map_start_time); pool->options = options; pool->max_clients = options.max_clients;
  pool->num_entities = QA_Q3_MAX_CLIENTS; qa_q3_ranking_reports_init(&pool->rankings); qa_q3_callback_catalog_init(&pool->callbacks);
  pool->rankings.grapple_death = options.product == QA_Q3_BASE_PRODUCT_MISSIONPACK ? 28 : 23;
  pool->utilities = malloc(sizeof *pool->utilities); if (!pool->utilities) qa_range_error("out of memory");
  qa_q3_game_utility_scratch_init(pool->utilities, options.ctx, options.print);
  for (int slot = 0; slot < QA_Q3_MAX_CLIENTS; slot++) pool->clients[slot] = options.records.client(options.records.ctx, slot);
  if (options.event_debug != NULL)
    for (int slot = 0; slot < QA_Q3_MAX_CLIENTS; slot++) qa_q3_base_player_state_record_set_event_debug(&pool->clients[slot]->ps, options.event_debug);
  for (int i = 0; i < options.max_clients; i++) qa_q3_entity_pool_at(pool, i)->client = qa_q3_entity_pool_client_at(pool, i);
}
void qa_q3_entity_pool_destroy(qa_q3_entity_pool *pool) { free(pool->utilities); pool->utilities = NULL; qa_q3_callback_catalog_destroy(&pool->callbacks); }
qa_q3_game_entity *qa_q3_entity_pool_activate_client(qa_q3_entity_pool *pool, double slot) {
  if (slot < 0 || slot >= pool->max_clients) qa_range_error("Client slot outside configured clients");
  qa_q3_game_entity *e = pool->options.records.activate(pool->options.records.ctx, slot);
  e->client = qa_q3_entity_pool_client_at(pool, slot); return e;
}
void qa_q3_entity_pool_deactivate_client(qa_q3_entity_pool *pool, double slot) { pool->options.records.deactivate_client(pool->options.records.ctx, slot); }
void qa_q3_entity_pool_restore_counts(qa_q3_entity_pool *pool, qa_q3_pool_counts s) {
  if (s.num_entities != trunc(s.num_entities) || s.num_entities < QA_Q3_MAX_CLIENTS || s.num_entities > QA_Q3_BASE_ENTITYNUM_WORLD
      || s.max_clients != trunc(s.max_clients) || s.max_clients < 1 || s.max_clients > QA_Q3_MAX_CLIENTS)
    qa_range_error("Invalid restored Q3 pool counts");
  pool->num_entities = s.num_entities; pool->max_clients = s.max_clients;
}
void qa_q3_entity_pool_clear_level(qa_q3_entity_pool *pool) { pool->num_entities = 0; pool->max_clients = 0; }
void qa_q3_entity_pool_clear_entities(qa_q3_entity_pool *pool) {
  for (int slot = 0; slot < QA_Q3_MAX_GENTITIES; slot++) { qa_q3_game_entity *e = qa_q3_entity_pool_get(pool, slot); if (e && qa_q3_game_entity_inuse(e)) pool->options.records.release(pool->options.records.ctx, e); }
}
void qa_q3_entity_pool_initialize_clients(qa_q3_entity_pool *pool, double max_clients) {
  if (max_clients != trunc(max_clients) || max_clients < 1 || max_clients > QA_Q3_MAX_CLIENTS) qa_range_error("Configured clients must be within 1..64");
  pool->max_clients = max_clients;
  for (int i = 0; i < QA_Q3_MAX_CLIENTS; i++) {
    /* TS keeps the ps/pers/sess/ammoTimes objects: ps.copyFrom(fresh.ps)
     * preserves the authority binding; ammoTimes.set(i, 0) notifies its hook. */
    qa_q3_game_client *c = pool->clients[i];
    qa_q3_game_player_state_reset(&c->ps, QA_Q3_BASE_PRESERVE_AUTHORITY);
    qa_q3_client_persistant_destroy(&c->pers); qa_q3_client_persistant_init(&c->pers); qa_q3_client_session_init(&c->sess);
    for (double index = 0; index < c->ammo_times.length; index++) qa_q3_base_player_state_slots_set(&c->ammo_times, index, 0);
    qa_q3_base_player_state ps = c->ps; qa_q3_client_persistant pers = c->pers; qa_q3_client_session sess = c->sess;
    qa_q3_base_player_state_slots ammo_times = c->ammo_times;
    free(c->areabits);
    memset(c, 0, sizeof *c); c->ps = ps; c->pers = pers; c->sess = sess; c->ammo_times = ammo_times;
  }
  for (int i = 0; i < max_clients; i++) qa_q3_entity_pool_at(pool, i)->client = qa_q3_entity_pool_client_at(pool, i);
  pool->num_entities = QA_Q3_MAX_CLIENTS;
}
qa_q3_game_entity *qa_q3_entity_pool_get(qa_q3_entity_pool *pool, double n) { return pool->options.records.get(pool->options.records.ctx, n); }
qa_q3_game_entity *qa_q3_entity_pool_at(qa_q3_entity_pool *pool, double n) {
  qa_q3_game_entity *e = qa_q3_entity_pool_get(pool, n); if (!e) qa_range_error("Game entity %s is unavailable", QA_JS_NUMBER(n)); return e;
}
qa_q3_game_client *qa_q3_entity_pool_client_at(qa_q3_entity_pool *pool, double n) {
  if (n < 0 || n >= QA_Q3_MAX_CLIENTS || !pool->clients[qa_js_to_int32(n)]) qa_range_error("Game client %s is unavailable", QA_JS_NUMBER(n));
  return pool->clients[qa_js_to_int32(n)];
}
static void owned(qa_q3_entity_pool *pool, qa_q3_game_entity *e) {
  if (qa_q3_entity_pool_get(pool, e->slot) != e) qa_range_error("Game entity does not belong to this pool");
}
static qa_q3_game_entity *initialize_slot(qa_q3_entity_pool *pool, double slot) {
  qa_q3_game_entity *e = pool->options.records.activate(pool->options.records.ctx, slot); qa_q3_init_game_entity(e); return e;
}
qa_q3_game_entity *qa_q3_entity_pool_spawn(qa_q3_entity_pool *pool) {
  double now = check_time(pool->options.time(pool->options.ctx));
  for (int i = QA_Q3_MAX_CLIENTS; i < pool->num_entities; i++) {
    qa_q3_game_entity *e = qa_q3_entity_pool_at(pool, i); if (qa_q3_game_entity_inuse(e)) continue;
    if (e->freetime > qa_int32(pool->options.map_start_time + 2000) && qa_int32(now - e->freetime) < 1000) continue;
    return initialize_slot(pool, i);
  }
  if (pool->num_entities == QA_Q3_BASE_ENTITYNUM_WORLD) {
    for (int i = 0; i < QA_Q3_MAX_GENTITIES; i++) {
      qa_q3_game_format_arg a[2] = {qa_q3_game_format_number(i), qa_q3_game_format_string(qa_q3_game_entity_classname(qa_q3_entity_pool_at(pool, i)))};
      if (!qa_q3_game_entity_classname(qa_q3_entity_pool_at(pool, i))) a[1] = qa_q3_game_format_null();
      pool_print(pool, qa_q3_game_format("%4i: %s\n", a, 2, 32000));
    }
    qa_range_error("G_Spawn: no free entities");
  }
  double slot = pool->num_entities; pool->num_entities++; return initialize_slot(pool, slot);
}
bool qa_q3_entity_pool_entities_free(qa_q3_entity_pool *pool) {
  for (int i = QA_Q3_MAX_CLIENTS; i < pool->num_entities; i++) if (!qa_q3_game_entity_inuse(qa_q3_entity_pool_at(pool, i))) return true;
  return false;
}
void qa_q3_entity_pool_free(qa_q3_entity_pool *pool, qa_q3_game_entity *e) {
  owned(pool, e); if (pool->options.unlink) pool->options.unlink(pool->options.ctx, e);
  if (e->never_free) return;
  double now = check_time(pool->options.time(pool->options.ctx));
  pool->options.records.release(pool->options.records.ctx, e);
  qa_q3_game_entity_set_classname(e, "freed"); e->freetime = now;
}
qa_q3_game_entity *qa_q3_entity_pool_temp_entity(qa_q3_entity_pool *pool, qa_vec3 origin, double event) {
  qa_q3_game_entity *e = qa_q3_entity_pool_spawn(pool); e->s.e_type = 13 + event;
  qa_q3_game_entity_set_classname(e, "tempEntity"); e->event_time = check_time(pool->options.time(pool->options.ctx));
  e->free_after_event = true;
  qa_vec3 snapped = {(double)qa_qvm_float_to_int(origin.x), (double)qa_qvm_float_to_int(origin.y), (double)qa_qvm_float_to_int(origin.z)};
  qa_q3_set_origin(e, snapped); if (pool->options.link) pool->options.link(pool->options.ctx, e); return e;
}
bool qa_q3_entity_pool_add_predictable_event(qa_q3_entity_pool *pool, qa_q3_game_entity *e, double event, double parameter, qa_q3_base_predictable_event *out) {
  owned(pool, e); if (!e->client) return false;
  qa_q3_base_predictable_event added = qa_q3_base_player_state_record_add_event(&e->client->ps, event, parameter);
  if (out) *out = added;
  return true;
}
void qa_q3_entity_pool_add_event(qa_q3_entity_pool *pool, qa_q3_game_entity *e, double event, double parameter) {
  owned(pool, e);
  if (event == 0) { qa_q3_game_format_arg a = qa_q3_game_format_number(e->s.number); pool_print(pool, qa_q3_game_format("G_AddEvent: zero event added for entity %i\n", &a, 1, 32000)); return; }
  double now = check_time(pool->options.time(pool->options.ctx));
  if (e->client) {
    double bits = (qa_js_to_int32(e->client->ps.external_event) & 0x300) + 0x100; bits = qa_js_to_int32(bits) & 0x300;
    e->client->ps.external_event = qa_js_to_int32(event) | qa_js_to_int32(bits); e->client->ps.external_event_parm = parameter; e->client->ps.external_event_time = now;
  } else {
    double bits = ((qa_js_to_int32(e->s.event) & 0x300) + 0x100) & 0x300; e->s.event = qa_js_to_int32(event) | qa_js_to_int32(bits); e->s.event_parm = parameter;
  }
  e->event_time = now;
}
qa_q3_entity_event_status qa_q3_entity_pool_expire_events(qa_q3_entity_pool *pool, qa_q3_game_entity *e) {
  owned(pool, e); if (!qa_q3_game_entity_inuse(e)) return QA_Q3_EVENT_INACTIVE;
  if (qa_int32(check_time(pool->options.time(pool->options.ctx)) - e->event_time) > 300) {
    if (e->s.event != 0) { e->s.event = 0; if (e->client) e->client->ps.external_event = 0; }
    if (e->free_after_event) { qa_q3_entity_pool_free(pool, e); return qa_q3_game_entity_inuse(e) ? QA_Q3_EVENT_WAITING : QA_Q3_EVENT_FREED; }
    if (e->unlink_after_event) { e->unlink_after_event = false; if (pool->options.unlink) pool->options.unlink(pool->options.ctx, e); }
  }
  return e->free_after_event ? QA_Q3_EVENT_WAITING : QA_Q3_EVENT_ACTIVE;
}
