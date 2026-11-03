/* Port of src/app/bootstrap/simulation/arsenal/q3-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q3_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/death.h"
#include "qa/content/q3/base/game/grapple.h"
#include "qa/content/q3/base/game/item_pickup.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/save_state.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/team_arena/client_admission.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/content/q3/team_arena/client_events.h"
#include "qa/content/q3/team_arena/client_think.h"
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/movement/q3/move.h"
#include "qa/movement/q3/postures.h"
#include "qa/movement/q3/weapon.h"
#include "qa/persistence/save_image.h"

typedef qa_app_q3_selected_source source_t;
typedef qa_app_q3_selected_source_entry entry_t;
typedef qa_app_q3_selected_source_map map_t;

/* One TS step closure per track(): host.execute keeps it for the source's life. */
struct qa_app_q3_selected_source_step {
  source_t *source;
  qa_owned_actor actor;
  struct qa_app_q3_selected_source_step *next;
};

static const qa_vec3 zero = {0, 0, 0};
enum { MASK_SHOT = 0x6000001, CONTENTS_TRIGGER = 0x40000000 };

/* ---- insertion-ordered OwnedActor maps (TS Map/Set; deletion during
 * iteration leaves a hole the iterator skips, as JS Map iteration does). ---- */
static entry_t *map_find(map_t *map, qa_owned_actor actor) {
  for (size_t i = 0; i < map->count; i++)
    if (map->items[i].present && qa_same_owned_actor(map->items[i].actor, actor)) return &map->items[i];
  return NULL;
}
static entry_t *map_set(map_t *map, qa_owned_actor actor) {
  entry_t *found = map_find(map, actor);
  if (found != NULL) return found;
  if (map->count == map->capacity) {
    size_t capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    entry_t *grown = realloc(map->items, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    map->items = grown; map->capacity = capacity;
  }
  entry_t *entry = &map->items[map->count++];
  memset(entry, 0, sizeof *entry);
  entry->actor = actor; entry->present = true; map->live++;
  return entry;
}
static bool map_delete(map_t *map, qa_owned_actor actor) {
  entry_t *entry = map_find(map, actor);
  if (entry == NULL) return false;
  entry->present = false; map->live--;
  if (map->live == 0) map->count = 0;
  return true;
}
static void map_clear(map_t *map) {
  for (size_t i = 0; i < map->count; i++) map->items[i].present = false;
  map->count = 0; map->live = 0;
}
static void map_free(map_t *map) { free(map->items); memset(map, 0, sizeof *map); }

/* ---- small helpers ---- */
static bool provider_equal(qa_provider_id a, qa_provider_id b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static double now_of(source_t *s) { return s->has_clock ? s->clock : s->host.now(s->host.ctx); }
static qa_q3_game_combat_context *combat_of(source_t *s) { return qa_q3_base_combat_bridge_context(&s->bridge); }
static void assert_open(source_t *s) { if (s->closed) qa_range_error("Selected Q3 source is closed"); }
static qa_q3_game_entity *native_of(source_t *s, qa_actor_id actor) {
  return qa_q3_base_entity_records_native_by_actor(s->records, true, actor);
}
static bool owns(source_t *s, qa_owned_actor actor) {
  return provider_equal(actor.owner, s->host.provider) && native_of(s, actor.id) != NULL;
}
static bool is_live(source_t *s, qa_actor_id actor) { return qa_session_actor_registry_is_live(s->host.actors, actor); }
static bool body_of(source_t *s, qa_actor_id actor, qa_body_state *out) {
  qa_body_state_result body = qa_shared_body_table_read(s->host.bodies, actor);
  if (body.has_value && out != NULL) *out = body.value;
  return body.has_value;
}
static bool pose_of(source_t *s, qa_actor_id actor, qa_app_q3_selected_client_pose *out) {
  qa_app_q3_selected_client_pose ignored;
  return s->host.player(s->host.ctx, actor, out != NULL ? out : &ignored);
}
static const qa_q3_base_stat_schema *schema_of(source_t *s) { return qa_q3_base_stat_schema_for(s->host.product); }
static double stat_get(qa_q3_base_player_state_slots *slots, double index) { return qa_q3_base_player_state_slots_get(slots, index); }
static void stat_set(qa_q3_base_player_state_slots *slots, double index, double value) { qa_q3_base_player_state_slots_set(slots, index, value); }
static void call_think(qa_q3_entity_callback callback, qa_q3_game_entity *entity) {
  qa_q3_entity_think_fn fn;
  memcpy(&fn, &callback.fn, sizeof fn);
  fn(callback.ctx, entity);
}
static qa_q3_ta_event_context event_context(source_t *s) { qa_q3_ta_event_context context = {combat_of(s)}; return context; }

/* TS clientEffects(client) / sameEffects. */
static qa_app_q3_selected_client_effects effects_of(qa_q3_game_client *client) {
  qa_app_q3_selected_client_effects effects;
  effects.teleport_bit = (double)(qa_js_to_int32(client->ps.e_flags) & 4);
  effects.view_angles = client->ps.viewangles; effects.delta_angles = client->ps.delta_angles;
  effects.pm_flags = client->ps.pm_flags; effects.pm_time = client->ps.pm_time;
  effects.invulnerability_time = client->invulnerability_time;
  effects.max_health = stat_get(&client->ps.stats, qa_q3_base_stat_schema_for(qa_q3_base_player_state_record_product(&client->ps))->max_health);
  return effects;
}
static bool same_effects(qa_app_q3_selected_client_effects a, qa_app_q3_selected_client_effects b) {
  return a.teleport_bit == b.teleport_bit && a.pm_flags == b.pm_flags && a.pm_time == b.pm_time
      && a.invulnerability_time == b.invulnerability_time && a.max_health == b.max_health
      && a.view_angles.x == b.view_angles.x && a.view_angles.y == b.view_angles.y && a.view_angles.z == b.view_angles.z
      && a.delta_angles.x == b.delta_angles.x && a.delta_angles.y == b.delta_angles.y && a.delta_angles.z == b.delta_angles.z;
}

/* ---- run(): depth bookkeeping and publication after each source call ---- */
static void publish(source_t *s);
static void run_unwind(void *arg) { ((source_t *)arg)->depth--; }
typedef struct run_frame { qa_cleanup node; } run_frame;
static void run_begin(source_t *s, run_frame *frame) {
  assert_open(s); s->depth++;
  qa_cleanup_push(&frame->node, run_unwind, s);
  (void)combat_of(s);
}
static void run_end(source_t *s, run_frame *frame) {
  publish(s);
  qa_cleanup_pop(&frame->node);
  s->depth--;
}

/* ---- TS track / project / refresh / player ---- */
static void step_fn(void *ctx, double previous, double time);
static void track(source_t *s, qa_owned_actor actor) {
  if (!owns(s, actor) || map_find(&s->executing, actor) != NULL) return;
  map_set(&s->executing, actor);
  struct qa_app_q3_selected_source_step *step = calloc(1, sizeof *step);
  if (step == NULL) qa_range_error("Out of memory");
  step->source = s; step->actor = actor; step->next = s->steps; s->steps = step;
  s->host.execute(s->host.ctx, actor, step_fn, step);
}

static void publish_client(source_t *s, qa_q3_game_entity *entity, qa_q3_game_client *client) {
  qa_owned_actor actor = qa_q3_game_entity_actor(entity);
  qa_app_q3_selected_client_effects after = effects_of(client);
  entry_t *existing = map_find(&s->client_effects, actor);
  bool had = existing != NULL;
  qa_app_q3_selected_client_effects before = had ? existing->effects : after;
  map_set(&s->client_effects, actor)->effects = after;
  if (had && !same_effects(before, after)) s->host.client_changed(s->host.ctx, actor, before, after);
}

static void set_netname(qa_q3_game_client *client, char *value) {
  free(client->pers.netname);
  client->pers.netname = value;
}
static bool same_vector(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static void forget_presented(source_t *s, double slot) {
  if (!(slot >= 0) || (size_t)slot >= s->presented_count) return;
  qa_app_q3_presented_client *shown = &s->presented[(size_t)slot];
  free(shown->userinfo); memset(shown, 0, sizeof *shown);
}
static void clear_presented(source_t *s) {
  for (size_t i = 0; i < s->presented_count; i++) free(s->presented[i].userinfo);
  free(s->presented); s->presented = NULL; s->presented_count = 0;
}
static qa_app_q3_presented_client *presented_at(source_t *s, double slot) {
  size_t index = (size_t)slot;
  if (index >= s->presented_count) {
    qa_app_q3_presented_client *grown = realloc(s->presented, (index + 1) * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    memset(grown + s->presented_count, 0, (index + 1 - s->presented_count) * sizeof *grown);
    s->presented = grown; s->presented_count = index + 1;
  }
  return &s->presented[index];
}
static void refresh(source_t *s, qa_q3_game_entity *entity) {
  qa_owned_actor actor = qa_q3_game_entity_actor(entity);
  qa_body_state body;
  if (!body_of(s, actor.id, &body)) qa_range_error("Selected Q3 projected body was removed");
  if (entity->s.pos.type != QA_Q3_BASE_TR_STATIONARY || !same_vector(entity->s.pos.base, body.origin)) {
    qa_q3_base_trajectory pos = {QA_Q3_BASE_TR_STATIONARY, 0, 0, body.origin, zero};
    entity->s.pos = pos;
  }
  if (entity->s.apos.type != QA_Q3_BASE_TR_STATIONARY || !same_vector(entity->s.apos.base, body.angles)) {
    qa_q3_base_trajectory apos = {QA_Q3_BASE_TR_STATIONARY, 0, 0, body.angles, zero};
    entity->s.apos = apos;
  }
  qa_q3_game_client *client = entity->client;
  qa_app_q3_selected_client_pose pose;
  bool has_pose = pose_of(s, actor.id, &pose);
  if (client == NULL || !has_pose) return;
  publish_client(s, entity, client);
  const qa_q3_base_stat_schema *schema = schema_of(s);
  client->ps.viewangles = pose.angles; client->ps.viewheight = pose.view_height; client->sess.session_team = pose.team;
  client->pers.max_health = pose.max_health; stat_set(&client->ps.persistant, QA_Q3_BASE_PERS_TEAM, pose.team);
  stat_set(&client->ps.stats, schema->max_health, pose.max_health);
  stat_set(&client->ps.powerups, QA_Q3_PW_QUAD, pose.quad_until); stat_set(&client->ps.powerups, QA_Q3_PW_HASTE, pose.haste_until);
  const char *userinfo = s->host.userinfo(s->host.ctx, actor.id);
  double game_type = s->host.game_type(s->host.ctx);
  qa_app_q3_presented_client *shown = presented_at(s, entity->slot);
  /* The client presentation configstring is rebuilt only when one of its inputs changes. */
  if (!shown->set || strcmp(shown->userinfo, userinfo) != 0 || shown->team != client->sess.session_team
      || shown->max_health != client->pers.max_health || shown->wins != client->sess.wins || shown->losses != client->sess.losses
      || shown->team_leader != client->sess.team_leader || shown->game_type != game_type) {
    char *name = qa_q3_ta_client_info_value(userinfo, "name");
    if (name == NULL) qa_range_error("Out of memory");
    char *clean = qa_q3_ta_clean_client_name(name);
    free(name);
    if (clean == NULL) qa_range_error("Out of memory");
    set_netname(client, clean);
    char *config = qa_q3_ta_client_presentation_config(client, userinfo, game_type, false, QA_Q3_BASE_TEAM_FREE);
    if (config == NULL) qa_range_error("Out of memory");
    s->host.configstrings.set(s->host.configstrings.ctx, 544 + entity->slot, config);
    free(config);
    char *kept = strdup(userinfo);
    if (kept == NULL) qa_range_error("Out of memory");
    free(shown->userinfo);
    shown->set = true; shown->userinfo = kept; shown->team = client->sess.session_team; shown->max_health = client->pers.max_health;
    shown->wins = client->sess.wins; shown->losses = client->sess.losses; shown->team_leader = client->sess.team_leader; shown->game_type = game_type;
  }
  map_set(&s->client_effects, actor)->effects = effects_of(client);
}

/* TS Object.assign(client, new GameClient(product), kept) then
 * ps.copyFrom(fresh.ps, "preserve-authority"): every GameClient field takes
 * its fresh value except the kept owners; the fresh ammoTimes has no
 * ammoTimerStored hook. */
static void assign_fresh_client(source_t *s, qa_q3_game_client *client, bool keep_pers_sess_accuracy) {
  qa_q3_base_player_state ps = client->ps;
  qa_q3_client_persistant pers = client->pers;
  qa_q3_client_session sess = client->sess;
  double hits = client->accuracy_hits, shots = client->accuracy_shots;
  qa_q3_base_player_state_slots fresh_ammo;
  if (!qa_q3_base_player_state_slots_init(&fresh_ammo, qa_q3_base_weapon_count(s->host.product), NULL, 0, false, NULL, NULL, NULL))
    qa_range_error("Out of memory");
  qa_q3_base_player_state_slots_destroy(&client->ammo_times);
  free(client->areabits);
  if (!keep_pers_sess_accuracy) qa_q3_client_persistant_destroy(&pers);
  memset(client, 0, sizeof *client);
  client->ps = ps;
  client->ammo_times = fresh_ammo;
  if (keep_pers_sess_accuracy) {
    client->pers = pers; client->sess = sess; client->accuracy_hits = hits; client->accuracy_shots = shots;
  } else {
    qa_q3_client_persistant_init(&client->pers); qa_q3_client_session_init(&client->sess);
  }
  qa_q3_base_player_state fresh;
  if (!qa_q3_base_player_state_init(&fresh, s->host.product, NULL)) qa_range_error("Out of memory");
  qa_q3_base_player_state_record_copy_from(&client->ps, &fresh, QA_Q3_BASE_PRESERVE_AUTHORITY);
  qa_q3_base_player_state_record_destroy(&fresh);
}

static qa_q3_game_entity *project(source_t *s, qa_actor_id actor) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(s->host.actors, actor, &owned)) return NULL;
  qa_q3_game_entity *current = native_of(s, actor);
  if (current != NULL) return current;
  if (!body_of(s, actor, NULL)) qa_range_error("Q3 source projection requires the existing actor body");
  bool player = pose_of(s, actor, NULL);
  double slot = player ? 0 : QA_Q3_MAX_CLIENTS;
  double end = player ? s->host.max_clients : 1022;
  while (slot < end && qa_q3_game_entity_inuse(qa_q3_entity_pool_at(&s->pool, slot))) slot++;
  if (slot >= end) qa_range_error("Selected Q3 source projection capacity exceeded");
  qa_q3_game_entity *entity = qa_q3_base_entity_records_attach(s->records, slot, owned, player);
  map_set(&s->projected, owned)->slot = slot;
  if (slot >= s->pool.num_entities) {
    qa_q3_pool_counts counts = {slot + 1, s->host.max_clients};
    qa_q3_entity_pool_restore_counts(&s->pool, counts);
  }
  if (player) {
    qa_q3_game_client *client = qa_q3_base_entity_records_client(s->records, slot);
    assign_fresh_client(s, client, false);
    const double zeros[16] = {0};
    qa_q3_base_entity_records_restore_client_backing(s->records, slot, zeros, 16, zeros, 16);
    entity->s.e_type = QA_Q3_BASE_ET_PLAYER; entity->s.client_num = slot; client->ps.client_num = slot;
  }
  refresh(s, entity);
  return entity;
}
qa_q3_game_entity *qa_app_q3_selected_source_project(qa_app_q3_selected_source *source, qa_actor_id actor) {
  return project(source, actor);
}

static void bind_world(source_t *s) {
  qa_owned_actor world = s->host.world_actor(s->host.ctx);
  qa_q3_game_entity *current = native_of(s, world.id);
  if (current == NULL) (void)qa_q3_base_entity_records_attach(s->records, 1022, world, false);
  else if (current->slot != 1022) qa_range_error("Selected Q3 world actor changed its source slot");
}

static qa_q3_game_entity *player_of(source_t *s, qa_owned_actor actor) {
  assert_open(s);
  qa_session_actor_registry_assert_owned(s->host.actors, actor);
  bind_world(s);
  qa_q3_game_entity *entity = project(s, actor.id);
  if (entity == NULL || entity->client == NULL) qa_range_error("Selected Q3 arsenal requires an admitted player");
  refresh(s, entity);
  return entity;
}

/* TS returnPersistent(entity). */
typedef struct return_ctx { source_t *source; } return_ctx;
static void return_pickup(void *ctx, qa_q3_game_entity *pickup) {
  source_t *s = ((return_ctx *)ctx)->source;
  if (!qa_q3_game_entity_inuse(pickup)) return;
  qa_owned_actor actor = qa_q3_game_entity_actor(pickup);
  if (owns(s, actor)) qa_q3_return_persistent_powerup(pickup, s->world_server);
  else s->host.return_pickup(s->host.ctx, actor.id);
}
static void return_persistent(source_t *s, qa_q3_game_entity *entity) {
  return_ctx ctx = {s};
  qa_q3_toss_client_persistent_powerup(entity, return_pickup, &ctx);
}

/* TS publish(). */
static void publish(source_t *s) {
  for (double slot = 0; slot < s->pool.num_entities; slot++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(&s->pool, slot);
    if (!qa_q3_game_entity_inuse(entity)) continue;
    qa_owned_actor actor = qa_q3_game_entity_actor(entity);
    if (entity->client != NULL) {
      publish_client(s, entity, entity->client);
      qa_q3_ta_update_client_powerups(event_context(s), entity->client);
      qa_q3_game_player_state_to_entity_state(&entity->client->ps, &entity->s, true, false, 0);
    } else {
      if (!owns(s, actor)) continue;
      track(s, actor);
    }
    double event = entity->s.e_type >= QA_Q3_BASE_ET_EVENTS ? entity->s.e_type - QA_Q3_BASE_ET_EVENTS : entity->s.event;
    entry_t *previous = map_find(&s->published, actor);
    if (event == 0 || (previous != NULL && previous->event == event && previous->time == entity->event_time)) continue;
    entry_t *published = map_set(&s->published, actor);
    published->event = event; published->time = entity->event_time;
    qa_q3_base_entity_state copy;
    qa_q3_entity_state_record_copy(&copy, &entity->s);
    s->host.event(s->host.ctx, actor, &copy, entity->event_time);
  }
}

/* ---- TS step(actor, previous, time) ---- */
typedef struct step_restore { qa_cleanup node; source_t *source; bool has_clock; double clock, previous; } step_restore;
static void step_unwind(void *arg) {
  step_restore *saved = arg;
  saved->source->has_clock = saved->has_clock; saved->source->clock = saved->clock;
  saved->source->previous = saved->previous; saved->source->missiles.host.previous_time = saved->previous;
}
static void step_fn(void *ctx, double previous, double time) {
  struct qa_app_q3_selected_source_step *step = ctx;
  source_t *s = step->source;
  qa_owned_actor actor = step->actor;
  if (s->closed || !is_live(s, actor.id) || !owns(s, actor)) return;
  step_restore saved = {{0}, s, s->has_clock, s->clock, s->previous};
  qa_cleanup_push(&saved.node, step_unwind, &saved);
  s->has_clock = true; s->clock = time; s->previous = previous; s->missiles.host.previous_time = previous;
  for (size_t i = 0; i < s->projected.count; i++) {
    entry_t *entry = &s->projected.items[i];
    if (!entry->present || !is_live(s, entry->actor.id)) continue;
    refresh(s, qa_q3_entity_pool_at(&s->pool, entry->slot));
  }
  qa_q3_game_entity *entity = native_of(s, actor.id);
  run_frame frame;
  run_begin(s, &frame);
  if (entity != NULL && qa_q3_entity_pool_expire_events(&s->pool, entity) == QA_Q3_EVENT_ACTIVE
      && !qa_q3_missile_runtime_run_owned(&s->missiles, actor))
    qa_q3_run_think(entity, time);
  run_end(s, &frame);
  qa_cleanup_pop(&saved.node);
  step_unwind(&saved);
}

/* ---- Q3EntityRecords host ---- */
static void records_schedule(void *ctx, qa_owned_actor actor, bool has_due, double due) {
  (void)has_due; (void)due; /* TS schedule: actor => { this.track(actor) } ignores the due time. */
  track(ctx, actor);
}
typedef struct clock_restore { qa_cleanup node; source_t *source; bool has_clock; double clock; } clock_restore;
static void clock_unwind(void *arg) {
  clock_restore *saved = arg; saved->source->has_clock = saved->has_clock; saved->source->clock = saved->clock;
}
static void records_run_think(void *ctx, qa_owned_actor actor, double time) {
  source_t *s = ctx;
  qa_q3_game_entity *entity = native_of(s, actor.id);
  if (entity == NULL || !owns(s, actor)) qa_range_error("Selected Q3 think actor is retired");
  clock_restore saved = {{0}, s, s->has_clock, s->clock};
  qa_cleanup_push(&saved.node, clock_unwind, &saved);
  s->has_clock = true; s->clock = time;
  (void)combat_of(s);
  qa_q3_game_entity_set_nextthink(entity, 0);
  if (entity->think.fn == NULL) qa_range_error("NULL ent->think");
  call_think(entity->think, entity);
  qa_cleanup_pop(&saved.node);
  clock_unwind(&saved);
}
static bool records_damage_call(void *ctx, qa_q3_damage_call *out) {
  return qa_q3_base_combat_bridge_current_call(&((source_t *)ctx)->bridge, out);
}
static qa_q3_game_entity *records_foreign(void *ctx, qa_actor_id actor) { return project(ctx, actor); }
static bool records_is_player(void *ctx, qa_actor_id actor) { return pose_of(ctx, actor, NULL); }

/* ---- world: Q3WorldAdapter with the projecting traceActor override ---- */
static void world_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  source_t *s = ctx;
  if (owns(s, actor)) s->host.collision(s->host.ctx, actor, collision);
}
static bool world_curves(void *ctx) { source_t *s = ctx; return s->host.curves(s->host.ctx); }
static bool world_player_curve_clip(void *ctx) { source_t *s = ctx; return s->host.player_curve_clip(s->host.ctx); }
static qa_q3_base_actor_trace_result world_trace_actor(void *ctx, qa_q3_base_actor_trace_query input) {
  source_t *s = ctx;
  qa_q3_base_actor_trace_result trace = qa_q3_base_world_adapter_trace_actor(&s->world, input);
  if (trace.hit_kind == QA_Q3_BASE_HIT_ACTOR) (void)project(s, trace.hit_actor);
  return trace;
}
/* Q3WorldAdapter.trace: this.traceActor dispatches to the override. */
static qa_q3_base_server_trace_result world_trace(void *ctx, qa_q3_base_server_trace_query input) {
  source_t *s = ctx;
  qa_actor_id pass;
  bool has_pass = false;
  if (!(input.pass_entity_num == 1022 || input.pass_entity_num == 1023 || input.pass_entity_num < 0)) {
    qa_q3_game_entity *entity = qa_q3_base_entity_records_get(s->records, input.pass_entity_num);
    if (entity != NULL && qa_q3_game_entity_inuse(entity)) { pass = qa_q3_game_entity_actor(entity).id; has_pass = true; }
  }
  qa_q3_base_actor_trace_query query = {input.start, input.end, input.shape, input.mask, has_pass ? &pass : NULL};
  qa_q3_base_actor_trace_result result = world_trace_actor(s, query);
  double entity_num = result.hit_kind == QA_Q3_BASE_HIT_NONE ? 1023 : 1022;
  if (result.hit_kind == QA_Q3_BASE_HIT_ACTOR) {
    qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(s->records, true, result.hit_actor);
    if (entity != NULL) entity_num = entity->slot;
  }
  qa_q3_base_server_trace_result out;
  memset(&out, 0, sizeof out);
  out.fraction = result.fraction; out.end = result.end; out.entity_num = entity_num;
  out.contact_is_plane = result.contact_is_plane; out.contact_plane = result.contact_plane;
  out.solidity = result.solidity; out.contents = result.contents; out.surface_flags = result.surface_flags;
  return out;
}
static bool world_entity_contact(void *ctx, qa_bounds bounds, double number, bool capsule) {
  return qa_q3_base_world_adapter_entity_contact(&((source_t *)ctx)->world, bounds, number, capsule);
}
static const double *world_area_entities(void *ctx, qa_bounds bounds, bool has_maximum, double maximum, size_t *out_count) {
  return qa_q3_base_world_adapter_area_entities(&((source_t *)ctx)->world, bounds, has_maximum, maximum, out_count);
}
static double world_point_contents(void *ctx, qa_vec3 point, double pass) {
  return qa_q3_base_world_adapter_point_contents(&((source_t *)ctx)->world, point, pass);
}
static bool world_link_state(void *ctx, double number, qa_q3_base_link_state *out) {
  return qa_q3_base_world_adapter_link_state(&((source_t *)ctx)->world, number, out);
}
static void world_link(void *ctx, qa_q3_game_entity *entity) { qa_q3_base_world_adapter_link(&((source_t *)ctx)->world, entity); }
static void world_unlink(void *ctx, double number) { qa_q3_base_world_adapter_unlink(&((source_t *)ctx)->world, number); }
static const qa_actor_id *world_area_actors(void *ctx, qa_bounds bounds, double maximum, size_t *out_count) {
  return qa_q3_base_world_adapter_area_actors(&((source_t *)ctx)->world, bounds, maximum, out_count);
}
static bool world_contact_actor(void *ctx, qa_bounds bounds, qa_actor_id actor, bool capsule) {
  return qa_q3_base_world_adapter_contact_actor(&((source_t *)ctx)->world, bounds, actor, capsule);
}

/* ---- EntityPool options ---- */
static double pool_time(void *ctx) { return now_of(ctx); }
static void pool_print(void *ctx, const char *text) { source_t *s = ctx; s->host.print(s->host.ctx, text); }
static void pool_link(void *ctx, qa_q3_game_entity *entity) {
  source_t *s = ctx;
  qa_q3_base_world_adapter_link(&s->world, entity);
  track(s, qa_q3_game_entity_actor(entity));
}
static void pool_unlink(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_base_world_adapter_unlink(&((source_t *)ctx)->world, entity->slot);
}

/* ---- Q3CombatBridge host ---- */
static qa_victim_armor_context bridge_armor(void *ctx, qa_damage_request request) {
  source_t *s = ctx; return s->host.armor_context(s->host.ctx, request);
}
static double bridge_time(void *ctx) { return now_of(ctx); }
static double bridge_intermission(void *ctx) { source_t *s = ctx; return s->host.intermission_queued(s->host.ctx); }
static double bridge_game_type(void *ctx) { source_t *s = ctx; return s->host.game_type(s->host.ctx); }
static bool bridge_friendly_fire(void *ctx) { source_t *s = ctx; return s->host.friendly_fire(s->host.ctx); }
static double bridge_knockback(void *ctx) { source_t *s = ctx; return s->host.knockback(s->host.ctx); }
static void bridge_hurt_carrier(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  source_t *s = ctx; s->host.check_hurt_carrier(s->host.ctx, target, attacker);
}
static bool bridge_log_accuracy(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  source_t *s = ctx; return qa_q3_log_accuracy_hit(s->host.game_type(s->host.ctx), target, attacker);
}
static bool bridge_is_live(void *ctx, qa_actor_id actor) { return is_live(ctx, actor); }
static void bridge_feedback(void *ctx, qa_q3_damage_call call, qa_damage_decision decision) {
  qa_q3_damage_feedback(combat_of(ctx), call, decision);
}
static void bridge_foreign_feedback(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker, qa_damage_decision decision) {
  qa_q3_foreign_damage_feedback(combat_of(ctx), target, attacker, decision);
}
static bool bridge_projectile_parent(void *ctx, qa_actor_id actor, qa_actor_id *out) {
  return qa_q3_missile_runtime_owner_of(&((source_t *)ctx)->missiles, actor, out);
}
static bool bridge_obelisk(void *ctx, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  source_t *s = ctx; return s->host.check_obelisk_attack(s->host.ctx, target, attacker);
}
static void bridge_invulnerability(void *ctx, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point) {
  (void)qa_q3_invulnerability_effect(&((source_t *)ctx)->pool, target, direction, point);
}

/* ---- MissileRuntime host ---- */
static qa_q3_game_combat_context *missile_combat(void *ctx) { return combat_of(ctx); }
static double missile_prox_timeout(void *ctx) { source_t *s = ctx; return s->host.proximity_timeout(s->host.ctx); }
static double missile_sound_index(void *ctx, const char *path) { source_t *s = ctx; return s->host.sound_index(s->host.ctx, path); }
static qa_q3_reflection_result missile_invulnerability(void *ctx, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point) {
  qa_q3_weapon_invuln_impact impact = qa_q3_invulnerability_effect(&((source_t *)ctx)->pool, target, direction, point);
  qa_q3_reflection_result out;
  memset(&out, 0, sizeof out);
  out.kind = impact.kind == QA_Q3_WEAPON_INVULN_HIT ? QA_Q3_REFLECTION_HIT : QA_Q3_REFLECTION_MISS;
  out.bounce_direction = impact.bounce_direction;
  return out;
}

/* ---- WeaponRuntime host ({missiles, random, unlink, quadFactor, damageFactor};
 * the C weapon host reaches missiles.host.combat through the fields below). ---- */
static double weapon_damage_factor(void *ctx, qa_q3_game_entity *entity) {
  source_t *s = ctx;
  qa_actor_id actor = qa_q3_game_entity_actor(entity).id;
  if (s->host.has_weapon_effects) {
    double source_factor = qa_app_q3_selected_source_owns_equipment(s) && entity->client != NULL
        && entity->client->persistant_powerup != NULL && entity->client->persistant_powerup->item != NULL
        && entity->client->persistant_powerup->item->tag == QA_Q3_PW_DOUBLER ? 2 : 1;
    return (double)(float)(s->host.weapon_damage_factor(s->host.ctx, actor) * source_factor);
  }
  if (entity->client == NULL) qa_range_error("Selected weapon damage has no source client");
  return qa_q3_weapon_damage_factor(entity->client, s->host.quad_factor(s->host.ctx, &actor), s->host.product);
}
#define FIRE(name, call)                                                                                  \
  static qa_q3_game_entity *name(void *ctx, qa_q3_game_entity *entity, qa_vec3 origin, qa_q3_missile_direction direction) { \
    return call(&((source_t *)ctx)->missiles, entity, origin, &direction);                                  \
  }
FIRE(weapon_fire_grenade, qa_q3_missile_runtime_fire_grenade)
FIRE(weapon_fire_rocket, qa_q3_missile_runtime_fire_rocket)
FIRE(weapon_fire_plasma, qa_q3_missile_runtime_fire_plasma)
FIRE(weapon_fire_bfg, qa_q3_missile_runtime_fire_bfg)
FIRE(weapon_fire_grapple, qa_q3_missile_runtime_fire_grapple)
FIRE(weapon_fire_prox, qa_q3_missile_runtime_fire_prox)
#undef FIRE
static qa_q3_game_entity *weapon_fire_nail(void *ctx, qa_q3_game_entity *entity, qa_vec3 origin,
    qa_q3_missile_direction direction, qa_vec3 right, qa_vec3 up) {
  qa_vec3 forward = {direction.x, direction.y, direction.z};
  return qa_q3_missile_runtime_fire_nail(&((source_t *)ctx)->missiles, entity, origin, forward, right, up);
}
/* combat.spatial.traceActor({start, end, shape: point, passActor, mask: MASK_SHOT}). */
static qa_q3_base_actor_trace_result weapon_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass) {
  qa_q3_game_combat_context *combat = combat_of(ctx);
  qa_q3_base_actor_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start; query.end = end; query.shape.kind = QA_Q3_BASE_SHAPE_POINT; query.mask = MASK_SHOT; query.pass_actor = pass;
  return combat->spatial.trace_actor(combat->spatial.ctx, query);
}
/* combat.authority.read(actor). */
static bool weapon_authority_read(void *ctx, qa_actor_id actor, double *health, bool *can_take_damage, const char **team) {
  qa_combat_state_result state = qa_gameplay_authority_read(((source_t *)ctx)->host.combat, actor);
  if (!state.has_value) return false;
  *health = state.value.health; *can_take_damage = state.value.can_take_damage; *team = state.value.team;
  return true;
}
static qa_q3_damage_participant weapon_participant(void *ctx, qa_actor_id actor) {
  qa_q3_game_combat_context *combat = combat_of(ctx);
  return combat->participant(combat->services_ctx, actor);
}
static qa_q3_game_entity *weapon_native(void *ctx, qa_actor_id actor) { return native_of(ctx, actor); }
static bool weapon_is_player(void *ctx, qa_actor_id actor) {
  qa_q3_game_combat_context *combat = combat_of(ctx);
  return combat->is_player(combat->services_ctx, actor);
}
static void *weapon_unlink(void *ctx, qa_actor_id actor) {
  return (void *)qa_q3_base_world_adapter_unlink_actor(&((source_t *)ctx)->world, actor);
}
static void weapon_restore(void *ctx, void *token) {
  (void)ctx;
  qa_q3_base_relink_closure *closure = token;
  if (closure != NULL) { qa_q3_base_relink_closure_call(closure); qa_q3_base_relink_closure_destroy(closure); }
}
static size_t weapon_area_entities(void *ctx, qa_bounds bounds, double *out, size_t capacity) {
  size_t count = 0;
  const double *numbers = qa_q3_base_world_adapter_area_entities(&((source_t *)ctx)->world, bounds, false, 0, &count);
  for (size_t i = 0; i < count && i < capacity; i++) out[i] = numbers[i];
  return count < capacity ? count : capacity;
}
static bool weapon_link_bounds(void *ctx, double number, qa_bounds *out) {
  qa_q3_base_link_state state;
  if (!qa_q3_base_world_adapter_link_state(&((source_t *)ctx)->world, number, &state)) return false;
  *out = state.absbounds;
  return true;
}

/* ---- killBox over the combat context (TS killBox(context, entity)) ---- */
static size_t kill_area_actors(void *ctx, qa_bounds bounds, double maximum, qa_actor_id *out, size_t capacity) {
  qa_q3_game_combat_context *combat = combat_of(ctx);
  size_t count = 0;
  const qa_actor_id *actors = combat->spatial.area_actors(combat->spatial.ctx, bounds, maximum, &count);
  for (size_t i = 0; i < count && i < capacity; i++) out[i] = actors[i];
  return count < capacity ? count : capacity;
}
static bool kill_linked_bounds(void *ctx, qa_actor_id actor) {
  qa_q3_game_combat_context *combat = combat_of(ctx);
  qa_bounds bounds;
  return combat->linked_bounds(combat->services_ctx, actor, &bounds);
}
static qa_q3_kill_box_host kill_box_of(source_t *s) {
  qa_q3_kill_box_host host = {s, kill_area_actors, kill_linked_bounds, weapon_is_player, weapon_participant};
  return host;
}
static void teleport(source_t *s, qa_q3_game_entity *entity, qa_vec3 origin, qa_vec3 angles) {
  qa_q3_teleport_context context = {combat_of(s), kill_box_of(s), s->world_server};
  qa_q3_teleport_player(context, entity, origin, angles);
}

/* ---- PersonalPortalRuntime host (mapTravel) ---- */
static double portal_model_index(void *ctx, const char *name) { source_t *s = ctx; return s->host.model_index(s->host.ctx, name); }
static double portal_random(void *ctx) { return (double)qa_game_random_random(&((source_t *)ctx)->random); }
static void portal_drop_flag(void *ctx, qa_q3_game_entity *player) {
  source_t *s = ctx; s->host.drop_objectives(s->host.ctx, qa_q3_game_entity_actor(player));
}
static void portal_teleport(void *ctx, qa_q3_game_entity *player, qa_vec3 origin, qa_vec3 angles) { teleport(ctx, player, origin, angles); }

/* ---- actors.onRelease ---- */
static void released(void *ctx, qa_owned_actor actor) {
  source_t *s = ctx;
  map_delete(&s->executing, actor); map_delete(&s->published, actor); map_delete(&s->client_effects, actor);
  entry_t *entry = map_find(&s->projected, actor);
  bool had = entry != NULL;
  double slot = had ? entry->slot : 0;
  map_delete(&s->projected, actor);
  if (!had) return;
  if (slot < s->host.max_clients) {
    return_persistent(s, qa_q3_entity_pool_at(&s->pool, slot));
    s->host.configstrings.set(s->host.configstrings.ctx, 544 + slot, "");
  }
  forget_presented(s, slot);
  qa_q3_base_entity_records_release(s->records, qa_q3_entity_pool_at(&s->pool, slot));
}

/* ---- constructor ---- */
qa_app_q3_selected_source *qa_app_q3_selected_source_create(qa_app_q3_selected_source_host host, const qa_save_value *restored) {
  source_t *s = calloc(1, sizeof *s);
  if (s == NULL) return NULL;
  s->host = host;
  qa_game_random_init(&s->random, (int64_t)host.seed);
  qa_q3_base_record_host records_host;
  memset(&records_host, 0, sizeof records_host);
  records_host.actors = host.actors; records_host.bodies = host.bodies; records_host.combat = host.combat;
  records_host.inventory = host.inventory; records_host.callbacks = host.callbacks; records_host.ctx = s;
  records_host.schedule = records_schedule; records_host.run_think = records_run_think;
  records_host.damage_call = records_damage_call; records_host.foreign = records_foreign; records_host.is_player = records_is_player;
  s->records = qa_q3_base_entity_records_create(records_host, host.provider, host.product);
  if (s->records == NULL) { free(s); return NULL; }
  qa_q3_base_world_adapter_host world_host = {host.queries, host.bodies, s, world_collision, world_curves, world_player_curve_clip};
  qa_q3_base_world_adapter_init(&s->world, world_host, s->records);
  qa_q3_base_server_world server = {s, world_entity_contact, world_trace, world_area_entities, world_point_contents,
      world_link_state, world_link, world_unlink};
  s->world_server = server;
  qa_q3_base_actor_spatial_queries spatial = {s, world_area_actors, world_trace_actor, world_contact_actor};
  s->world_spatial = spatial;
  qa_q3_entity_pool_options pool_options;
  memset(&pool_options, 0, sizeof pool_options);
  pool_options.records = qa_q3_base_entity_records_pool_table(s->records);
  pool_options.product = host.product; pool_options.max_clients = host.max_clients;
  pool_options.map_start_time = host.now(host.ctx);
  pool_options.ctx = s; pool_options.time = pool_time; pool_options.print = pool_print;
  pool_options.link = pool_link; pool_options.unlink = pool_unlink;
  qa_q3_entity_pool_init(&s->pool, pool_options);
  qa_q3_base_combat_bridge_host bridge_host;
  memset(&bridge_host, 0, sizeof bridge_host);
  bridge_host.product = host.product; bridge_host.authority = host.combat; bridge_host.entities = &s->pool;
  bridge_host.records = s->records; bridge_host.world = &s->world_server; bridge_host.spatial = &s->world_spatial;
  bridge_host.has_damage_powerup_owner = host.has_damage_powerup_owner; bridge_host.damage_powerup_owner = host.damage_powerup_owner;
  bridge_host.has_source_damage_modifier = host.has_source_damage_modifier; bridge_host.source_damage_modifier = host.source_damage_modifier;
  bridge_host.weapon_provider = host.provider; bridge_host.combat_provider = host.combat_provider;
  bridge_host.inventory_provider = host.inventory_provider; bridge_host.movement_provider = host.movement_provider;
  bridge_host.ctx = s; bridge_host.armor_context = bridge_armor; bridge_host.time = bridge_time;
  bridge_host.intermission_queued = bridge_intermission; bridge_host.game_type = bridge_game_type;
  bridge_host.friendly_fire = bridge_friendly_fire; bridge_host.knockback = bridge_knockback;
  bridge_host.debug_damage = NULL; bridge_host.check_hurt_carrier = bridge_hurt_carrier;
  bridge_host.check_obelisk_attack = bridge_obelisk; bridge_host.log_accuracy_hit = bridge_log_accuracy;
  bridge_host.projectile_parent = bridge_projectile_parent; bridge_host.invulnerability_effect = bridge_invulnerability;
  bridge_host.is_live = bridge_is_live; bridge_host.damage_feedback = bridge_feedback;
  bridge_host.foreign_damage_feedback = bridge_foreign_feedback;
  qa_q3_base_combat_bridge_init(&s->bridge, bridge_host);
  qa_q3_base_combat_bridge_bind_game_services(&s->bridge);
  qa_q3_game_combat_context *combat = combat_of(s);
  qa_q3_missile_host missile_host;
  memset(&missile_host, 0, sizeof missile_host);
  missile_host.ctx = s; missile_host.product = host.product; missile_host.previous_time = s->previous;
  missile_host.combat = missile_combat; missile_host.weapon_behavior = host.weapon_behavior;
  missile_host.actors = host.actors; missile_host.bodies = host.bodies;
  missile_host.spatial = &s->world_spatial; missile_host.world = s->world_server;
  missile_host.has_missionpack = combat->product != QA_Q3_BASE_PRODUCT_BASEQ3;
  if (missile_host.has_missionpack) {
    missile_host.missionpack.ctx = s; missile_host.missionpack.prox_mine_timeout = missile_prox_timeout;
    missile_host.missionpack.random = qa_q3_game_random_pick_from(&s->random);
    missile_host.missionpack.sound_index = missile_sound_index; missile_host.missionpack.invulnerability_impact = missile_invulnerability;
  }
  qa_q3_missile_runtime_init(&s->missiles, missile_host);
  qa_q3_weapon_host weapon_host;
  memset(&weapon_host, 0, sizeof weapon_host);
  /* damage_factor is always present, so the weapon runtime's quadFactor
   * fallback (q3WeaponDamageFactor over host.quadFactor()) is never read. */
  weapon_host.ctx = s; weapon_host.combat = combat; weapon_host.world = s->world_server; weapon_host.random = &s->random;
  weapon_host.has_damage_factor = true; weapon_host.damage_factor = weapon_damage_factor;
  weapon_host.fire_grenade = weapon_fire_grenade; weapon_host.fire_rocket = weapon_fire_rocket;
  weapon_host.fire_plasma = weapon_fire_plasma; weapon_host.fire_bfg = weapon_fire_bfg;
  weapon_host.fire_grapple = weapon_fire_grapple; weapon_host.fire_prox = weapon_fire_prox; weapon_host.fire_nail = weapon_fire_nail;
  weapon_host.trace = weapon_trace; weapon_host.authority_read = weapon_authority_read;
  weapon_host.participant = weapon_participant; weapon_host.native_by_actor = weapon_native; weapon_host.is_player = weapon_is_player;
  weapon_host.unlink = weapon_unlink; weapon_host.restore = weapon_restore;
  weapon_host.area_entities = weapon_area_entities; weapon_host.link_bounds = weapon_link_bounds;
  qa_q3_weapon_runtime_init(&s->weapons, weapon_host);
  s->personal_portal = NULL;
  if (combat->product != QA_Q3_BASE_PRODUCT_BASEQ3) {
    qa_q3_personal_portal_host portal_host;
    memset(&portal_host, 0, sizeof portal_host);
    portal_host.ctx = s; portal_host.combat = combat; portal_host.world = s->world_server; portal_host.kill_box = kill_box_of(s);
    portal_host.model_index = portal_model_index; portal_host.random_value = portal_random;
    portal_host.has_map_travel = true; portal_host.map_drop_carried_flag = portal_drop_flag; portal_host.map_teleport = portal_teleport;
    portal_host.spatial_is_world = true;
    qa_q3_personal_portal_runtime_init(&s->portal_storage, portal_host);
    s->personal_portal = &s->portal_storage;
  }
  s->unobserve = qa_session_actor_registry_on_release(host.actors, released, s);
  if (restored != NULL) qa_app_q3_selected_source_restore(s, restored);
  return s;
}

void qa_app_q3_selected_source_destroy(qa_app_q3_selected_source *source) {
  if (source == NULL) return;
  qa_app_q3_selected_source_close(source);
  qa_q3_entity_pool_destroy(&source->pool);
  qa_q3_base_combat_bridge_destroy(&source->bridge);
  qa_q3_base_world_adapter_destroy(&source->world);
  qa_q3_base_entity_records_free(source->records);
  map_free(&source->projected); map_free(&source->client_effects); map_free(&source->executing); map_free(&source->published);
  clear_presented(source);
  qa_app_q3_source_presentation_state_destroy(source->restored_presentation);
  while (source->steps != NULL) { struct qa_app_q3_selected_source_step *next = source->steps->next; free(source->steps); source->steps = next; }
  free(source);
}

/* ---- getters ---- */
double qa_app_q3_selected_source_generation(const qa_app_q3_selected_source *source) { return source->revision; }
bool qa_app_q3_selected_source_owns_equipment(const qa_app_q3_selected_source *source) { return source->host.equipment_source; }
const qa_app_q3_source_presentation_state *qa_app_q3_selected_source_presentation_baseline(const qa_app_q3_selected_source *source) {
  return source->restored_presentation;
}
bool qa_app_q3_selected_source_active(const qa_app_q3_selected_source *source) { return !source->closed; }
qa_q3_base_combat_bridge *qa_app_q3_selected_source_bridge(qa_app_q3_selected_source *source) { return &source->bridge; }
/* TS selectedQ3Source.records.nativeByActor(actor)?.classname: NULL is TS undefined (no native record);
 * the classname getter resolves the client-name kind. */
const char *qa_app_q3_selected_source_native_classname(qa_app_q3_selected_source *source, qa_actor_id actor) {
  qa_q3_game_entity *entity = native_of(source, actor);
  return entity == NULL ? NULL : qa_q3_game_entity_classname(entity);
}
bool qa_app_q3_selected_source_actor(qa_app_q3_selected_source *source, double slot, qa_actor_id *out) {
  if (source->closed) return false;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_get(source->records, slot);
  if (entity == NULL || !qa_q3_game_entity_inuse(entity)) return false;
  qa_actor_id actor = qa_q3_game_entity_actor(entity).id;
  if (!is_live(source, actor)) return false;
  *out = actor;
  return true;
}
bool qa_app_q3_selected_source_live(qa_app_q3_selected_source *source, qa_actor_id actor) {
  return !source->closed && is_live(source, actor) && native_of(source, actor) != NULL;
}

static bool owner_actor(source_t *s, qa_q3_game_entity *entity, qa_actor_id *out) {
  qa_q3_game_entity *owner = qa_q3_base_entity_records_get(s->records, entity->r.owner_num);
  if (owner == NULL || !qa_q3_game_entity_inuse(owner)) return false;
  *out = qa_q3_game_entity_actor(owner).id;
  return true;
}
qa_q2_motion qa_app_q3_selected_source_motion(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_body_state body) {
  qa_q3_game_entity *entity = native_of(source, actor.id);
  if (entity == NULL || !owns(source, actor)) qa_range_error("Selected Q3 motion actor is retired");
  qa_q2_motion motion;
  memset(&motion, 0, sizeof motion);
  motion.actor = actor; motion.kind = "stationary"; motion.velocity = body.velocity; motion.angular_velocity = zero;
  motion.gravity = 1; motion.gravity_vector = (qa_vec3){0, 0, -1}; motion.clip_mask = entity->clipmask;
  motion.has_owner = owner_actor(source, entity, &motion.owner);
  return motion;
}
qa_app_sim_shared_solid qa_app_q3_selected_source_collision(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  qa_q3_game_entity *entity = native_of(source, actor.id);
  if (entity == NULL || !owns(source, actor)) qa_range_error("Selected Q3 collision actor is retired");
  qa_app_sim_shared_solid solid;
  memset(&solid, 0, sizeof solid);
  solid.family = "q3";
  bool inline_model = entity->r.model.kind == QA_Q3_BASE_MODEL_INLINE;
  solid.solid = entity->r.contents == 0 ? "none" : entity->r.contents == CONTENTS_TRIGGER ? "trigger" : inline_model ? "brush" : "box";
  solid.has_model = inline_model;
  if (inline_model) solid.model = entity->r.model.index;
  solid.owner = owner_actor(source, entity, &source->solid_owner) ? &source->solid_owner : NULL;
  return solid;
}
bool qa_app_q3_selected_source_fixed_pose(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_q3_postures postures, qa_fixed_movement_pose *out) {
  qa_q3_game_entity *entity = player_of(source, actor);
  qa_q3_game_client *client = entity->client;
  if (client == NULL) qa_range_error("Selected Q3 pose has no source client");
  if (client->invulnerability_time <= now_of(source)) {
    client->ps.pm_flags = (double)(qa_js_to_int32(client->ps.pm_flags) & ~QA_Q3_FLAG_INVULEXPAND);
    return false;
  }
  qa_q3_ta_update_client_powerups(event_context(source), client);
  qa_q3_ta_expand_invulnerability(&source->pool, &source->world_server, entity);
  *out = qa_q3_invulnerability_pose((qa_js_to_int32(client->ps.pm_flags) & QA_Q3_FLAG_INVULEXPAND) != 0, postures);
  return true;
}
void qa_app_q3_selected_source_advance_movement(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds) {
  qa_q3_game_entity *entity = player_of(source, actor);
  if (entity->client == NULL) qa_range_error("Selected Q3 movement clock has no source client");
  /* dropQ3MovementTimers reads and writes only pmTime and pmFlags. */
  qa_q3_motion timers;
  memset(&timers, 0, sizeof timers);
  timers.pm_time = entity->client->ps.pm_time; timers.pm_flags = entity->client->ps.pm_flags;
  qa_q3_drop_movement_timers(&timers, milliseconds);
  entity->client->ps.pm_time = timers.pm_time; entity->client->ps.pm_flags = timers.pm_flags;
  publish_client(source, entity, entity->client);
}
void qa_app_q3_selected_source_end_command(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds) {
  if (!qa_app_q3_selected_source_owns_equipment(source)) return;
  qa_q3_game_entity *entity = player_of(source, actor);
  if (qa_q3_game_entity_health(entity) <= 0) return;
  qa_q3_ta_client_timer_ownership ownership = {false, false, NULL, 0};
  run_frame frame;
  run_begin(source, &frame);
  qa_q3_ta_client_timer_actions(event_context(source), entity, milliseconds, &ownership);
  run_end(source, &frame);
}
bool qa_app_q3_selected_source_blocks_damage(qa_app_q3_selected_source *source, qa_damage_request request) {
  assert_open(source);
  qa_q3_game_entity *target = native_of(source, request.target);
  if (target == NULL) return false;
  qa_vec3 direction = request.direction, point = request.point;
  double method = request.attack.cause.kind == QA_ATTACK_CAUSE_Q3 ? request.attack.cause.means_of_death : -1;
  run_frame frame;
  run_begin(source, &frame);
  bool blocked = qa_q3_invulnerability_blocks(combat_of(source), target, &direction, &point, method);
  run_end(source, &frame);
  return blocked;
}
void qa_app_q3_selected_source_admit(qa_app_q3_selected_source *source, qa_owned_actor actor) { (void)player_of(source, actor); }

void qa_app_q3_selected_source_respawn(qa_app_q3_selected_source *source, qa_actor_id actor) {
  assert_open(source);
  qa_q3_game_entity *entity = native_of(source, actor);
  qa_q3_game_client *client = entity != NULL ? entity->client : NULL;
  if (entity == NULL || client == NULL) return;
  qa_app_q3_selected_source_release_hook(source, actor);
  return_persistent(source, entity);
  run_frame frame;
  run_begin(source, &frame);
  int32_t *persistant = qa_q3_base_player_state_slots_copy(&client->ps.persistant);
  if (persistant == NULL) qa_range_error("Out of memory");
  double length = client->ps.persistant.length;
  double event_sequence = client->ps.event_sequence, ping = client->ps.ping;
  assign_fresh_client(source, client, true);
  for (double index = 0; index < length; index++) stat_set(&client->ps.persistant, index, persistant[(size_t)index]);
  free(persistant);
  client->ps.event_sequence = event_sequence; client->ps.ping = ping; client->ps.client_num = entity->slot;
  map_delete(&source->client_effects, qa_q3_game_entity_actor(entity));
  refresh(source, entity);
  run_end(source, &frame);
}
bool qa_app_q3_selected_source_gauntlet_hit(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  qa_q3_game_entity *player = player_of(source, actor);
  run_frame frame;
  run_begin(source, &frame);
  bool hit = qa_q3_weapon_check_gauntlet_attack(&source->weapons, player);
  run_end(source, &frame);
  return hit;
}
void qa_app_q3_selected_source_command(qa_app_q3_selected_source *source, qa_owned_actor actor, bool attack, bool selected, bool alive) {
  qa_q3_game_entity *player = player_of(source, actor);
  qa_q3_game_client *client = player->client;
  if (client == NULL) qa_range_error("Missing selected Q3 client");
  run_frame frame;
  run_begin(source, &frame);
  if (!attack || !alive) client->fire_held = false;
  if ((!attack || !selected || !alive) && client->hook != NULL) qa_q3_missile_runtime_hook_free(&source->missiles, client->hook);
  run_end(source, &frame);
}
void qa_app_q3_selected_source_release_hook(qa_app_q3_selected_source *source, qa_actor_id actor) {
  assert_open(source);
  qa_q3_game_entity *entity = native_of(source, actor);
  qa_q3_game_entity *hook = entity != NULL && entity->client != NULL ? entity->client->hook : NULL;
  if (hook == NULL) return;
  run_frame frame;
  run_begin(source, &frame);
  qa_q3_missile_runtime_hook_free(&source->missiles, hook);
  run_end(source, &frame);
}
bool qa_app_q3_selected_source_grapple_point(qa_app_q3_selected_source *source, qa_actor_id actor, qa_vec3 *out) {
  qa_q3_game_entity *entity = native_of(source, actor);
  qa_q3_game_client *client = entity != NULL ? entity->client : NULL;
  if (client == NULL || (qa_js_to_int32(client->ps.pm_flags) & QA_Q3_FLAG_GRAPPLE_PULL) == 0) return false;
  *out = client->ps.grapple_point;
  return true;
}
bool qa_app_q3_selected_source_pull(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_vec3 *out) {
  qa_vec3 point;
  bool has_point = qa_app_q3_selected_source_grapple_point(source, actor.id, &point);
  qa_body_state body;
  bool has_body = body_of(source, actor.id, &body);
  qa_app_q3_selected_client_pose pose;
  bool has_pose = pose_of(source, actor.id, &pose);
  if (!has_point || !has_body || !has_pose) return false;
  *out = qa_q3_grapple_velocity(body.origin, point, qa_qvm_angle_vectors(pose.angles).forward);
  return true;
}
qa_app_q3_selected_equipment_state qa_app_q3_selected_source_equipment(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  qa_q3_game_entity *entity = player_of(source, actor);
  qa_q3_game_client *client = entity->client;
  if (client == NULL) qa_range_error("Missing selected Q3 client");
  const qa_q3_base_stat_schema *schema = schema_of(source);
  double holdable = stat_get(&client->ps.stats, schema->holdable_item);
  qa_app_q3_selected_equipment_state state;
  state.max_health = stat_get(&client->ps.stats, schema->max_health);
  state.persistent_powerup_tag = schema->product == QA_Q3_BASE_PRODUCT_BASEQ3 ? QA_Q3_PW_NONE
      : qa_q3_base_item_at(source->host.product, stat_get(&client->ps.stats, schema->persistent_powerup))->tag;
  state.holdable_item = holdable;
  state.holdable_tag = qa_q3_base_item_at(source->host.product, holdable)->tag;
  return state;
}
size_t qa_app_q3_selected_source_inventory(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_app_q3_source_inventory_row *out, size_t capacity) {
  if (!qa_app_q3_selected_source_owns_equipment(source)) return 0;
  qa_app_q3_selected_equipment_state equipment = qa_app_q3_selected_source_equipment(source, actor);
  size_t count = 0, rows = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(source->host.product, &count);
  for (size_t index = 0; index < count; index++) {
    const qa_q3_base_item_definition *item = &items[index];
    bool held = item->type == QA_Q3_BASE_IT_HOLDABLE && (double)index == equipment.holdable_item;
    bool persistent = item->type == QA_Q3_BASE_IT_PERSISTANT_POWERUP && item->tag == equipment.persistent_powerup_tag;
    if ((!held && !persistent) || item->class_name == NULL || item->pickup_name == NULL) continue;
    if (rows < capacity) {
      qa_app_q3_source_inventory_row *row = &out[rows];
      snprintf(row->item, sizeof row->item, "q3:%s", item->class_name);
      row->label = item->pickup_name; row->count = 1; row->usable = held; row->icon = item->icon;
    }
    rows++;
  }
  return rows;
}
double qa_app_q3_selected_source_firing_delay(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds) {
  qa_q3_game_entity *entity = player_of(source, actor);
  qa_q3_game_client *client = entity->client;
  if (client == NULL) qa_range_error("Selected firing delay requires its source client");
  double persistent = qa_app_q3_selected_source_owns_equipment(source) && client->persistant_powerup != NULL
      && client->persistant_powerup->item != NULL ? client->persistant_powerup->item->tag : 0;
  if (source->host.has_weapon_effects)
    return source->host.weapon_firing_delay(source->host.ctx, actor.id, milliseconds, persistent);
  return qa_q3_weapon_delay(milliseconds, persistent, stat_get(&client->ps.powerups, QA_Q3_PW_HASTE) != 0);
}
double qa_app_q3_selected_source_speed_multiplier(qa_app_q3_selected_source *source, qa_actor_id actor) {
  qa_q3_game_entity *entity = native_of(source, actor);
  qa_q3_game_client *client = entity != NULL ? entity->client : NULL;
  return client == NULL ? 1 : qa_q3_ta_client_speed_multiplier(&client->ps);
}
bool qa_app_q3_selected_source_pickup_allowed(qa_app_q3_selected_source *source, qa_original_pickup_offer offer) {
  if (!qa_app_q3_selected_source_owns_equipment(source)) return true;
  if (offer.default_resource == NULL || offer.default_resource->kind != QA_PICKUP_RESOURCE_PROTECTION
      || offer.default_resource->channel != QA_PROTECTION_REGULAR) return true;
  qa_q3_game_entity *entity = native_of(source, offer.recipient);
  qa_q3_game_client *client = entity != NULL ? entity->client : NULL;
  if (client == NULL || qa_q3_base_player_state_record_product(&client->ps) != QA_Q3_BASE_PRODUCT_MISSIONPACK) return true;
  qa_q3_base_player_inventory inventory = qa_q3_item_inventory(client);
  if (inventory.product != QA_Q3_BASE_PRODUCT_MISSIONPACK) return true;
  double tag = qa_q3_base_item_at(QA_Q3_BASE_PRODUCT_MISSIONPACK, inventory.persistent_powerup_index)->tag;
  return (tag != QA_Q3_PW_SCOUT && tag != QA_Q3_PW_GUARD) || qa_q3_base_can_q3_armor_be_grabbed(&inventory);
}
static bool same_class(const char *a, const char *b) { return a == NULL ? b == NULL : b != NULL && strcmp(a, b) == 0; }
qa_q3_source_pickup_admission qa_app_q3_selected_source_take_pickup(qa_app_q3_selected_source *source,
    qa_q3_source_pickup_descriptor offer) {
  qa_q3_source_pickup_admission native = {QA_Q3_SOURCE_PICKUP_NATIVE, 0}, rejected = {QA_Q3_SOURCE_PICKUP_REJECTED, 0};
  if (!qa_app_q3_selected_source_owns_equipment(source)) return native;
  if (offer.item->type != QA_Q3_BASE_IT_HOLDABLE && offer.item->type != QA_Q3_BASE_IT_PERSISTANT_POWERUP) return native;
  size_t count = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(source->host.product, &count);
  const qa_q3_base_item_definition *item = NULL;
  size_t model_index = 0;
  for (size_t i = 0; i < count && item == NULL; i++)
    if (same_class(items[i].class_name, offer.item->class_name)) { item = &items[i]; model_index = i; }
  if (item == NULL) return rejected;
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_owned(source->host.actors, offer.player_actor, &actor)
      || !is_live(source, offer.item_actor)) return rejected;
  qa_q3_game_entity *player = player_of(source, actor);
  qa_q3_game_client *client = player->client;
  if (client == NULL) qa_range_error("Original equipment pickup requires a source client");
  qa_q3_base_pickup_entity grabbed = {(double)model_index, offer.dropped ? 1 : 0, offer.generic1};
  qa_q3_base_player_inventory inventory = qa_q3_item_inventory(client);
  if (!qa_q3_base_can_item_be_grabbed(offer.game_type, grabbed, &inventory)) return rejected;
  qa_q3_game_entity *pickup = project(source, offer.item_actor);
  if (pickup == NULL) return rejected;
  pickup->item = item; pickup->count = offer.count; pickup->s.modelindex = (double)model_index; pickup->s.generic1 = offer.generic1;
  run_frame frame;
  run_begin(source, &frame);
  double respawn;
  if (item->type == QA_Q3_BASE_IT_HOLDABLE) respawn = qa_q3_pickup_holdable(pickup, player);
  else {
    char *handicap = qa_q3_ta_client_info_value(source->host.userinfo(source->host.ctx, actor.id), "handicap");
    if (handicap == NULL) qa_range_error("Out of memory");
    respawn = qa_q3_pickup_persistent_powerup(pickup, player, handicap);
    free(handicap);
  }
  run_end(source, &frame);
  qa_q3_source_pickup_admission picked = {QA_Q3_SOURCE_PICKUP_PICKED, respawn};
  return picked;
}
void qa_app_q3_selected_source_died(qa_app_q3_selected_source *source, qa_actor_id actor) {
  qa_q3_game_entity *entity = native_of(source, actor);
  if (entity == NULL || entity->client == NULL) return;
  run_frame frame;
  run_begin(source, &frame);
  return_persistent(source, entity);
  run_end(source, &frame);
}
void qa_app_q3_selected_source_consume(qa_app_q3_selected_source *source, qa_owned_actor actor, double item) {
  qa_q3_game_entity *entity = player_of(source, actor);
  qa_q3_game_client *client = entity->client;
  const qa_q3_base_stat_schema *schema = schema_of(source);
  if (client == NULL || stat_get(&client->ps.stats, schema->holdable_item) != item
      || qa_q3_base_item_at(source->host.product, item)->type != QA_Q3_BASE_IT_HOLDABLE)
    qa_range_error("Selected Q3 holdable consumption differs from its current source item");
  stat_set(&client->ps.stats, schema->holdable_item, 0);
}
static bool lower_equal(const char *value, const char *wanted_lower) {
  if (value == NULL) return false;
  char *lower = qa_js_to_lower_case(value);
  if (lower == NULL) qa_range_error("Out of memory");
  bool equal = strcmp(lower, wanted_lower) == 0;
  free(lower);
  return equal;
}
bool qa_app_q3_selected_source_give_holdable(qa_app_q3_selected_source *source, qa_owned_actor actor, const char *name) {
  if (!qa_app_q3_selected_source_owns_equipment(source)) return false;
  char *wanted = qa_js_to_lower_case(name);
  if (wanted == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(source->host.product, &count);
  const qa_q3_base_item_definition *item = NULL;
  for (size_t i = 0; i < count && item == NULL; i++)
    if (items[i].type == QA_Q3_BASE_IT_HOLDABLE && (lower_equal(items[i].class_name, wanted) || lower_equal(items[i].pickup_name, wanted)))
      item = &items[i];
  free(wanted);
  if (item == NULL) return false;
  qa_q3_game_entity *player = player_of(source, actor);
  run_frame frame;
  run_begin(source, &frame);
  qa_q3_game_entity *pickup = qa_q3_entity_pool_spawn(&source->pool);
  pickup->item = item;
  (void)qa_q3_pickup_holdable(pickup, player);
  if (qa_q3_game_entity_inuse(pickup)) qa_q3_entity_pool_free(&source->pool, pickup);
  run_end(source, &frame);
  return true;
}
void qa_app_q3_selected_source_restore_equipment(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_app_q3_selected_equipment_state saved) {
  assert_open(source);
  qa_session_actor_registry_assert_owned(source->host.actors, actor);
  qa_q3_game_entity *entity = native_of(source, actor.id);
  qa_q3_game_client *client = entity != NULL ? entity->client : NULL;
  if (client == NULL) qa_range_error("Restored selected equipment has no source client");
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(source->host.product, saved.holdable_item);
  if (saved.holdable_item != 0 && (item->type != QA_Q3_BASE_IT_HOLDABLE || item->tag != saved.holdable_tag))
    qa_range_error("Restored selected holdable differs from its source item");
  stat_set(&client->ps.stats, schema_of(source)->holdable_item, saved.holdable_item);
}
void qa_app_q3_selected_source_fire(qa_app_q3_selected_source *source, qa_owned_actor actor, double weapon, qa_weapon_step_input input) {
  (void)input; /* TS fire(actor, weapon, _input): the step input is unused. */
  qa_q3_game_entity *entity = player_of(source, actor);
  entity->s.weapon = weapon;
  if (entity->client == NULL) qa_range_error("Missing selected Q3 client");
  entity->client->ps.weapon = weapon;
  qa_q3_base_predictable_event added;
  (void)qa_q3_entity_pool_add_predictable_event(&source->pool, entity, QA_Q3_EV_FIRE_WEAPON, 0, &added);
  entity->event_time = now_of(source);
  run_frame frame;
  run_begin(source, &frame);
  qa_q3_weapon_fire(&source->weapons, entity);
  run_end(source, &frame);
}
static void holdable_teleport(void *ctx, qa_q3_game_entity *player) {
  source_t *s = ctx;
  qa_owned_actor actor = qa_q3_game_entity_actor(player);
  s->host.drop_objectives(s->host.ctx, actor);
  qa_vec3 origin, angles;
  s->host.spawn_point(s->host.ctx, actor, &origin, &angles);
  teleport(s, player, origin, angles);
}
static void holdable_kamikaze(void *ctx, qa_q3_game_entity *entity) {
  (void)qa_q3_weapon_start_kamikaze(&((source_t *)ctx)->weapons, entity);
}
static void holdable_portal_source(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_personal_portal_drop_source(((source_t *)ctx)->personal_portal, entity);
}
static void holdable_portal_destination(void *ctx, qa_q3_game_entity *entity) {
  qa_q3_personal_portal_drop_destination(((source_t *)ctx)->personal_portal, entity);
}
void qa_app_q3_selected_source_use_holdable(qa_app_q3_selected_source *source, qa_owned_actor actor, double event) {
  qa_q3_game_entity *entity = player_of(source, actor);
  qa_q3_game_combat_context *combat = combat_of(source);
  qa_q3_base_predictable_event added;
  (void)qa_q3_entity_pool_add_predictable_event(&source->pool, entity, event, 0, &added);
  entity->event_time = now_of(source);
  run_frame frame;
  run_begin(source, &frame);
  qa_q3_ta_holdable_context context;
  memset(&context, 0, sizeof context);
  context.product = combat->product; context.combat = combat; context.ctx = source;
  context.teleport = holdable_teleport; context.start_kamikaze = holdable_kamikaze;
  if (combat->product != QA_Q3_BASE_PRODUCT_BASEQ3) {
    if (source->personal_portal == NULL) qa_range_error("Missionpack holdable has no portal service");
    context.drop_portal_source = holdable_portal_source; context.drop_portal_destination = holdable_portal_destination;
  }
  qa_q3_ta_use_holdable(&context, entity, event);
  run_end(source, &frame);
}
void qa_app_q3_selected_source_synchronize(qa_app_q3_selected_source *source) { assert_open(source); publish(source); }

qa_app_q3_source_presentation_state *qa_app_q3_selected_source_source_state(qa_app_q3_selected_source *source) {
  return qa_app_q3_pool_presentation_state(&source->pool, source->host.product, now_of(source), source->host.configstrings);
}
size_t qa_app_q3_selected_source_presentations(qa_app_q3_selected_source *source, qa_app_simulation_presentation *out, size_t capacity) {
  return qa_app_q3_pool_models(&source->pool, source->host.product, now_of(source), source->host.content,
      source->host.configstrings, out, capacity);
}

/* ---- capture / restore ---- */
static qa_save_value *saved_actor_value(qa_actor_id actor) {
  qa_saved_actor_id saved = qa_saved_actor_id_of(actor);
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  if (!qa_save_value_set(object, "slot", qa_save_value_number(saved.slot))
      || !qa_save_value_set(object, "generation", qa_save_value_number(saved.generation))) { qa_save_value_free(object); return NULL; }
  return object;
}
static qa_q3_graph_services graph_services(source_t *s) {
  qa_q3_graph_services services = {s->records, &s->pool, s->host.actors};
  return services;
}
qa_save_value *qa_app_q3_selected_source_capture(qa_app_q3_selected_source *source) {
  assert_open(source);
  if (source->depth != 0) qa_range_error("Cannot save selected Q3 during a source call");
  bind_world(source);
  qa_save_value *root = qa_save_value_object();
  if (root == NULL) return NULL;
  qa_q3_graph_state graph_state = qa_q3_capture_q3_graph(graph_services(source));
  qa_save_value *graph = qa_q3_graph_state_to_value(&graph_state);
  qa_q3_graph_state_destroy(&graph_state);
  qa_q3_base_combat_bridge_save_state bridge_state = qa_q3_base_combat_bridge_capture_save_state(&source->bridge);
  qa_save_value *bridge = qa_save_value_object();
  bool ok = graph != NULL && bridge != NULL && qa_save_value_set(bridge, "sequence", qa_save_value_number(bridge_state.sequence));
  qa_save_value *missiles = ok ? qa_q3_missile_runtime_capture_save_state(&source->missiles) : NULL;
  qa_save_value *portal = NULL;
  ok = ok && missiles != NULL;
  if (ok && source->personal_portal != NULL) {
    portal = qa_save_value_object();
    ok = portal != NULL && qa_save_value_set(portal, "portalSequence",
        qa_save_value_number(qa_q3_personal_portal_capture_sequence(source->personal_portal)));
  } else if (ok) {
    portal = qa_save_value_null();
    ok = portal != NULL;
  }
  qa_save_value *published = ok ? qa_save_value_array() : NULL;
  ok = ok && published != NULL;
  for (size_t i = 0; ok && i < source->published.count; i++) {
    entry_t *entry = &source->published.items[i];
    if (!entry->present) continue;
    qa_save_value *row = qa_save_value_object();
    ok = row != NULL && qa_save_value_set(row, "actor", saved_actor_value(entry->actor.id))
        && qa_save_value_set(row, "event", qa_save_value_number(entry->event))
        && qa_save_value_set(row, "time", qa_save_value_number(entry->time));
    if (ok) ok = qa_save_value_push(published, row);
    else qa_save_value_free(row);
  }
  if (!ok) {
    qa_save_value_free(root); qa_save_value_free(graph); qa_save_value_free(bridge);
    qa_save_value_free(missiles); qa_save_value_free(portal); qa_save_value_free(published);
    return NULL;
  }
  ok = qa_save_value_set(root, "version", qa_save_value_number(1))
      && qa_save_value_set(root, "provider", qa_save_value_string(source->host.provider))
      && qa_save_value_set(root, "product", qa_save_value_string(qa_q3_base_product_name(source->host.product)))
      && qa_save_value_set(root, "graph", graph)
      && qa_save_value_set(root, "bridge", bridge)
      && qa_save_value_set(root, "missiles", missiles)
      && qa_save_value_set(root, "personalPortal", portal)
      && qa_save_value_set(root, "random", qa_save_value_number(source->random.seed))
      && qa_save_value_set(root, "published", published);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}

static qa_save_reader field_of(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static const qa_save_value *field_value(const qa_save_value *value, const char *name) {
  return value == NULL ? NULL : qa_save_value_get(value, name);
}
static qa_actor_id reference_saved(void *ctx, qa_saved_actor_id saved) {
  return qa_session_actor_registry_reference_saved(((source_t *)ctx)->host.actors, saved, "checkpoint");
}
static void adopt_ownership(source_t *s) {
  qa_q3_base_ownership_state *ownership = qa_q3_base_entity_records_capture_ownership(s->records);
  if (ownership == NULL) qa_range_error("Out of memory");
  for (size_t slot = 0; slot < QA_Q3_MAX_GENTITIES; slot++) {
    qa_q3_base_ownership_state *record = &ownership[slot];
    if (!record->has_actor) continue;
    if (record->borrowed && slot != 1022) {
      map_set(&s->projected, record->actor)->slot = (double)slot;
      qa_q3_game_client *client = qa_q3_entity_pool_at(&s->pool, (double)slot)->client;
      if (client != NULL) map_set(&s->client_effects, record->actor)->effects = effects_of(client);
    } else if (!record->borrowed) {
      track(s, record->actor);
    }
  }
  free(ownership);
}
static void set_baseline(source_t *s) {
  qa_app_q3_source_presentation_state *state = qa_app_q3_selected_source_source_state(s);
  if (state == NULL) qa_range_error("Out of memory");
  qa_app_q3_source_presentation_state_destroy(s->restored_presentation);
  s->restored_presentation = state;
  s->revision++;
}
void qa_app_q3_selected_source_restore(qa_app_q3_selected_source *source, const qa_save_value *value) {
  assert_open(source);
  if (source->depth != 0 || source->projected.live != 0 || source->executing.live != 0)
    qa_range_error("Selected Q3 restore requires an unused source owner");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "selected Q3 source")) qa_range_error("Out of memory");
  qa_save_reader r = field_of(&reader, "version"); (void)qa_save_reader_literal_number(&r, 1); qa_save_reader_destroy(&r);
  r = field_of(&reader, "provider"); (void)qa_save_reader_literal_string(&r, source->host.provider); qa_save_reader_destroy(&r);
  r = field_of(&reader, "product");
  qa_q3_base_product product = QA_Q3_BASE_PRODUCT_MISSIONPACK;
  if (!qa_save_reader_is_undefined(&r)) {
    const char *choices[2] = {"baseq3", "missionpack"};
    product = strcmp(qa_save_reader_choice_string(&r, choices, 2), "baseq3") == 0 ? QA_Q3_BASE_PRODUCT_BASEQ3 : QA_Q3_BASE_PRODUCT_MISSIONPACK;
  }
  qa_save_reader_destroy(&r);
  if (product != source->host.product) qa_save_reader_fail(&reader, "Saved Q3 equipment product changed");
  qa_q3_graph_state graph = qa_q3_read_complete_graph(field_value(value, "graph"));
  qa_q3_prepare_q3_graph(graph_services(source), &graph);
  qa_q3_restore_q3_graph(graph_services(source), &graph);
  qa_q3_graph_state_destroy(&graph);
  qa_q3_base_combat_bridge_restore_save_state(&source->bridge, field_value(value, "bridge"));
  r = field_of(&reader, "personalPortal");
  if (source->personal_portal != NULL) {
    /* TS PersonalPortalRuntime.restoreSaveState. */
    qa_save_reader portal;
    if (!qa_save_reader_init(&portal, field_value(value, "personalPortal"), "q3.portals")) qa_range_error("Out of memory");
    qa_save_reader sequence = field_of(&portal, "portalSequence");
    double number = qa_save_reader_integer_min(&sequence, -2147483648.0);
    if (number > 2147483647.0) qa_save_reader_fail(&sequence, "portal sequence exceeds source integer range");
    qa_q3_personal_portal_restore_sequence(source->personal_portal, number);
    qa_save_reader_destroy(&sequence); qa_save_reader_destroy(&portal);
  } else if (!qa_save_reader_is_null(&r)) {
    qa_save_reader_fail(&reader, "Base Q3 equipment has missionpack portal state");
  }
  qa_save_reader_destroy(&r);
  qa_q3_missile_runtime_restore_save_state(&source->missiles, field_value(value, "missiles"), reference_saved, source);
  r = field_of(&reader, "random"); qa_game_random_reset(&source->random, (int64_t)qa_save_reader_integer(&r)); qa_save_reader_destroy(&r);
  qa_save_reader list = field_of(&reader, "published");
  size_t count = qa_save_reader_list_count(&list);
  entry_t *rows = calloc(count ? count : 1, sizeof *rows);
  if (rows == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader actor_reader = field_of(&entry, "actor");
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_owned_actor actor;
    if (!qa_session_actor_registry_resolve_saved(source->host.actors, saved, &actor) || native_of(source, actor.id) == NULL)
      qa_save_reader_fail(&entry, "Published event has no live selected source actor");
    rows[i].actor = actor;
    r = field_of(&entry, "event"); rows[i].event = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    r = field_of(&entry, "time"); rows[i].time = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
    qa_save_reader_destroy(&entry);
  }
  for (size_t i = 0; i < count; i++) {
    if (map_find(&source->published, rows[i].actor) != NULL) qa_save_reader_fail(&reader, "Duplicate published source event");
    entry_t *entry = map_set(&source->published, rows[i].actor);
    entry->event = rows[i].event; entry->time = rows[i].time;
  }
  free(rows);
  qa_save_reader_destroy(&list);
  qa_q3_game_entity *world = native_of(source, source->host.world_actor(source->host.ctx).id);
  if (world == NULL || world->slot != 1022) qa_save_reader_fail(&reader, "Restored selected Q3 world actor differs from the current map");
  qa_save_reader_destroy(&reader);
  adopt_ownership(source);
  set_baseline(source);
}

static qa_q3_game_entity *legacy_project(void *ctx, qa_actor_id actor) { return project(ctx, actor); }
void qa_app_q3_selected_source_restore_legacy(qa_app_q3_selected_source *source, const qa_app_legacy_q3_source *saved,
    const qa_owned_actor *client_actors, const qa_app_q3_selected_arsenal_checkpoint *client_states, size_t client_count) {
  assert_open(source);
  if (source->depth != 0 || source->projected.live != 0 || source->executing.live != 0 || source->host.product != QA_Q3_BASE_PRODUCT_BASEQ3)
    qa_range_error("Legacy Q3 restore requires an unused base source owner");
  bind_world(source);
  for (size_t i = 0; i < client_count; i++) {
    const qa_app_q3_selected_arsenal_checkpoint *state = &client_states[i];
    if (state->runtime.product == NULL || strcmp(state->runtime.product, "baseq3") != 0
        || !provider_equal(state->arsenal.provider, source->host.provider)
        || state->arsenal.state.kind == NULL || strcmp(state->arsenal.state.kind, "q3") != 0)
      qa_range_error("Legacy Q3 player arsenal differs from its source");
    qa_q3_game_entity *entity = player_of(source, client_actors[i]);
    qa_q3_game_client *client = entity->client;
    if (client == NULL) qa_range_error("Legacy Q3 player lost its source client");
    qa_app_q3_selected_equipment_state equipment = {state->runtime.max_health, state->runtime.persistent_powerup_tag,
        state->runtime.holdable_item, state->runtime.holdable_tag};
    qa_app_q3_selected_source_restore_equipment(source, client_actors[i], equipment);
    const qa_weapon_state *weapon = &state->arsenal.state;
    client->ps.weapon = weapon->source_weapon; client->ps.weapon_state = weapon->state; client->ps.weapon_time = weapon->time_milliseconds;
    client->ps.torso_anim = state->torso_animation; client->ps.event_sequence = state->runtime.event_sequence;
    client->ps.entity_event_sequence = state->runtime.event_sequence;
    entity->s.weapon = weapon->source_weapon;
  }
  qa_app_restore_legacy_q3_source(source, saved, legacy_project, source);
  for (size_t i = 0; i < saved->projectile_count; i++) {
    qa_owned_actor actor = saved->projectiles[i].base.actor;
    qa_q3_game_entity *entity = native_of(source, actor.id);
    if (entity == NULL) qa_range_error("Imported Q3 projectile disappeared");
    track(source, actor);
    if (entity->s.event != 0) {
      entry_t *entry = map_set(&source->published, actor);
      entry->event = entity->s.event; entry->time = entity->event_time;
    }
  }
  set_baseline(source);
}

void qa_app_q3_selected_source_close(qa_app_q3_selected_source *source) {
  if (source->closed) return;
  if (source->depth != 0) qa_range_error("Cannot close selected Q3 during a source call");
  for (size_t i = 0; i < source->projected.count; i++) {
    entry_t *entry = &source->projected.items[i];
    if (entry->present && entry->slot < source->host.max_clients)
      return_persistent(source, qa_q3_entity_pool_at(&source->pool, entry->slot));
  }
  source->closed = true;
  qa_q3_base_ownership_state *ownership = qa_q3_base_entity_records_capture_ownership(source->records);
  if (ownership == NULL) qa_range_error("Out of memory");
  for (size_t slot = 0; slot < QA_Q3_MAX_GENTITIES; slot++) {
    qa_q3_base_ownership_state *record = &ownership[slot];
    if (record->has_actor && !record->borrowed && is_live(source, record->actor.id))
      qa_session_actor_registry_release(source->host.actors, record->actor);
  }
  free(ownership);
  qa_q3_missile_runtime_close(&source->missiles);
  qa_q3_base_entity_records_close(source->records);
  qa_session_actor_registry_off_release(source->host.actors, source->unobserve);
  map_clear(&source->projected); map_clear(&source->executing); map_clear(&source->published); map_clear(&source->client_effects);
  clear_presented(source);
}
