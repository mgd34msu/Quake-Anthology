/*
 * Test fixture: a Q1 foundation over the real shared world tables, as the
 * TS harnesses build it (tests/gameplay/q1-base/base.test.ts createGame,
 * tests/gameplay/foundation/q1/foundation.test.ts gameFor):
 * SessionActorRegistry, SharedBodyTable, ActorCallbackTable,
 * GameplayAuthority with the Q1 combat policy (context = game.combatContext,
 * native victim armor), SharedInventoryTable, and a Q1FoundationHost whose
 * methods are test doubles. Header-only; not a test_*.c.
 *
 * API (kept backward compatible):
 *   q1_world_fixture_options()          TS createGame options (rerelease,
 *                                       skill 1, gravity 800, maxClients 4,
 *                                       q1:combat / q1:inventory / q2:movement).
 *   q1_world_fixture_create(fx, opts)   build the world, host and game
 *                                       (fx->game, qa_q1_services_init); false on OOM.
 *   q1_world_fixture_destroy(fx)        free everything.
 *   q1_world_fixture_admit_player(fx, origin, health)
 *                                       TS createGame player: allocateAtSource
 *                                       ("q3:character", 1 + n, "q3:sarge"), body with
 *                                       PLAYER_BOUNDS, damageable combat state, empty
 *                                       inventory, listed in host.players(). Does not
 *                                       call qa_q1_attach_player.
 *   q1_world_fixture_advance(fx, seconds)
 *                                       TS due(seconds): runs every think scheduled at
 *                                       or before `seconds` through host.callbacks.
 *   q1_world_fixture_of(ctx)            the fixture behind a host ctx (for test
 *                                       doubles that replace fx->game->host.X).
 *
 * Every host double receives fx->game->host.ctx == fx and checks it; a wrong
 * ctx increments fx->ctx_errors (tests CHECK it stays 0). Doubles record:
 * events (shallow copies, valid until the next emit), scheduled thinks,
 * transitions. random() returns fx->random_value (TS 0.4), trace() is an
 * open world (fraction 1), contents() "empty", classname() the Q1 entity's
 * classname or "player". Optional authority probe: fx->damage_allowed
 * (GameplayHooks.damageAllowed; NULL = allowed) sees the request after the
 * actor's damage adjustment.
 */
#ifndef QA_TESTS_CONTENT_Q1_WORLD_FIXTURE_H
#define QA_TESTS_CONTENT_Q1_WORLD_FIXTURE_H

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/policies.h"

#define Q1_WORLD_FIXTURE_MAGIC 0x51315746u

typedef struct q1_world_fixture_think { qa_owned_actor actor; double due; } q1_world_fixture_think;

typedef struct q1_world_fixture {
  unsigned magic;
  qa_identity_owner identity;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_q1_services *game;
  int ctx_errors;
  double random_value;
  qa_q1_event *events; size_t event_count, event_capacity;
  q1_world_fixture_think *thinks; size_t think_count, think_capacity;
  qa_actor_id players[16]; size_t player_count;
  int transitions; char last_transition_map[128];
  bool (*damage_allowed)(void *ctx, qa_damage_request request);
  void *damage_allowed_ctx;
  /* The world module has no release for combat/armor policies (TS GC):
   * the fixture frees the ctx blocks qa_create_q1_combat_policy and
   * qa_native_victim_armor allocate. */
  void *policy_ctx, *armor_ctx;
} q1_world_fixture;

static inline q1_world_fixture *q1_world_fixture_of(void *ctx) {
  q1_world_fixture *fx = ctx;
  if (fx == NULL || fx->magic != Q1_WORLD_FIXTURE_MAGIC) {
    fprintf(stderr, "q1_world_fixture: host double received a foreign ctx\n");
    abort();
  }
  return fx;
}
static inline q1_world_fixture *q1_wf_checked(void *ctx) { return q1_world_fixture_of(ctx); }

static inline void *q1_wf_grow(void *array, size_t *capacity, size_t count, size_t element) {
  if (count < *capacity) return array;
  const size_t next = *capacity ? *capacity * 2 : 16;
  void *grown = realloc(array, next * element);
  if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}

/* --- host doubles --- */
static inline double q1_wf_random(void *ctx) { return q1_wf_checked(ctx)->random_value; }
static inline qa_q1_trace q1_wf_trace(void *ctx, qa_q1_trace_request request) {
  (void)q1_wf_checked(ctx);
  qa_q1_trace trace;
  memset(&trace, 0, sizeof trace);
  trace.fraction = 1;
  trace.end = request.end;
  trace.in_open = true;
  return trace;
}
static inline qa_q1_contents q1_wf_contents(void *ctx, qa_vec3 point) { (void)point; (void)q1_wf_checked(ctx); return "empty"; }
static inline bool q1_wf_walk_move(void *ctx, qa_owned_actor actor, double yaw, double distance) {
  (void)actor; (void)yaw; (void)distance; (void)q1_wf_checked(ctx); return false;
}
static inline void q1_wf_change_yaw(void *ctx, qa_owned_actor actor) { (void)actor; (void)q1_wf_checked(ctx); }
static inline void q1_wf_move_to_goal(void *ctx, qa_owned_actor actor, qa_actor_id goal, double distance, const char *mode) {
  (void)actor; (void)goal; (void)distance; (void)mode; (void)q1_wf_checked(ctx);
}
static inline bool q1_wf_check_bottom(void *ctx, qa_actor_id actor) { (void)actor; (void)q1_wf_checked(ctx); return false; }
static inline qa_q1_pusher_services q1_wf_pusher_services(void *ctx, qa_q1_services *game) {
  (void)game; (void)q1_wf_checked(ctx);
  qa_range_error("This fixture does not step native pushers"); /* TS fixture throws */
}
static inline void q1_wf_schedule_think(void *ctx, qa_owned_actor actor, double due) {
  q1_world_fixture *fx = q1_wf_checked(ctx);
  for (size_t i = 0; i < fx->think_count; i++)
    if (qa_same_owned_actor(fx->thinks[i].actor, actor)) { fx->thinks[i].due = due; return; }
  fx->thinks = q1_wf_grow(fx->thinks, &fx->think_capacity, fx->think_count, sizeof *fx->thinks);
  fx->thinks[fx->think_count].actor = actor;
  fx->thinks[fx->think_count].due = due;
  fx->think_count++;
}
static inline void q1_wf_cancel_think(void *ctx, qa_owned_actor actor) {
  q1_world_fixture *fx = q1_wf_checked(ctx);
  for (size_t i = 0; i < fx->think_count; i++)
    if (qa_same_owned_actor(fx->thinks[i].actor, actor)) {
      memmove(&fx->thinks[i], &fx->thinks[i + 1], (fx->think_count - i - 1) * sizeof *fx->thinks);
      fx->think_count--;
      return;
    }
}
static inline void q1_wf_emit(void *ctx, qa_q1_event event) {
  q1_world_fixture *fx = q1_wf_checked(ctx);
  fx->events = q1_wf_grow(fx->events, &fx->event_capacity, fx->event_count, sizeof *fx->events);
  fx->events[fx->event_count++] = event;
}
static inline void q1_wf_transition(void *ctx, qa_transition_intent intent) {
  q1_world_fixture *fx = q1_wf_checked(ctx);
  fx->transitions++;
  snprintf(fx->last_transition_map, sizeof fx->last_transition_map, "%s", intent.map != NULL ? intent.map : "");
}
static inline const qa_actor_id *q1_wf_players(void *ctx, size_t *out_count) {
  q1_world_fixture *fx = q1_wf_checked(ctx);
  *out_count = fx->player_count;
  return fx->players;
}
static inline bool q1_wf_check_client(void *ctx, qa_owned_actor observer, qa_actor_id *out) {
  (void)observer; (void)out; (void)q1_wf_checked(ctx); return false;
}
static inline const char *q1_wf_classname(void *ctx, qa_actor_id actor) {
  q1_world_fixture *fx = q1_wf_checked(ctx);
  qa_q1_actor *entity = qa_q1_get_entity(fx->game, &actor);
  return entity != NULL ? entity->classname : "player";
}
static inline void q1_wf_powerup(void *ctx, qa_owned_actor actor, qa_q1_powerup powerup, double expires) {
  (void)actor; (void)powerup; (void)expires; (void)q1_wf_checked(ctx);
}

/* --- world table hooks --- */
static inline qa_bounds q1_wf_absolute_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  (void)ctx; return qa_translated_body_bounds(actor, state);
}
static inline void q1_wf_on_link(void *ctx, qa_linked_body body) { (void)ctx; (void)body; }
static inline void q1_wf_on_unlink(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; }
static inline void q1_wf_impulse(void *ctx, qa_owned_actor actor, qa_vec3 impulse, qa_provider_id movement) {
  (void)movement;
  q1_world_fixture *fx = q1_wf_checked(ctx);
  const qa_body_state_result body = qa_shared_body_table_read(fx->bodies, actor.id);
  if (!body.has_value) return;
  qa_body_state next = body.value;
  next.velocity = qa_q1_vadd(next.velocity, impulse);
  qa_shared_body_table_write(fx->bodies, actor, next);
}
static inline void q1_wf_before_reaction(void *ctx, qa_owned_actor actor, qa_damage_decision decision) {
  (void)actor; (void)decision; (void)q1_wf_checked(ctx);
}
static inline void q1_wf_confirmed(void *ctx, qa_damage_outcome outcome) { (void)outcome; (void)q1_wf_checked(ctx); }
static inline bool q1_wf_damage_allowed(void *ctx, qa_damage_request request) {
  q1_world_fixture *fx = q1_wf_checked(ctx);
  return fx->damage_allowed == NULL || fx->damage_allowed(fx->damage_allowed_ctx, request);
}
static inline qa_q1_combat_context q1_wf_combat_context(void *ctx, qa_damage_request request, qa_combat_state target,
                                                 const qa_combat_state *attacker) {
  (void)target; (void)attacker;
  return qa_q1_combat_context_for(q1_wf_checked(ctx)->game, request);
}
static inline qa_victim_armor_context q1_wf_armor_context(void *ctx, qa_damage_request request) {
  (void)ctx; (void)request;
  qa_victim_armor_context context;
  memset(&context, 0, sizeof context);
  context.arithmetic = "binary32";
  context.screen_facing_dot = 0;
  return context;
}

static inline qa_q1_options q1_world_fixture_options(void) {
  qa_q1_options options;
  memset(&options, 0, sizeof options);
  options.edition = "rerelease";
  options.skill = 1;
  options.deathmatch = 0;
  options.gravity = 800;
  options.has_max_clients = true;
  options.max_clients = 4;
  options.campaign = "q1:id1";
  options.combat_provider = "q1:combat";
  options.inventory_provider = "q1:inventory";
  options.movement_provider = "q2:movement";
  return options;
}

/* When set before q1_world_fixture_create, the combat policy carries the game's
 * damage source effects (TS q1TestHost world harness: sourceEffects:
 * game.damageSourceEffects). Off by default. */
static bool q1_wf_with_source_effects;

static inline bool q1_world_fixture_create(q1_world_fixture *fx, qa_q1_options options) {
  memset(fx, 0, sizeof *fx);
  fx->magic = Q1_WORLD_FIXTURE_MAGIC;
  fx->random_value = 0.4;
  if (!qa_identity_owner_create(&fx->identity, "q1-world-fixture")) return false;
  fx->actors = qa_session_actor_registry_create(&fx->identity, 4096);
  if (fx->actors == NULL) return false;
  const qa_body_link_hooks link = {fx, q1_wf_absolute_bounds, q1_wf_on_link, q1_wf_on_unlink};
  fx->bodies = qa_shared_body_table_create(fx->actors, link);
  fx->callbacks = qa_actor_callback_table_create(fx->actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = fx;
  hooks.damage_allowed = q1_wf_damage_allowed;
  hooks.impulse = q1_wf_impulse;
  hooks.before_reaction = q1_wf_before_reaction;
  hooks.confirmed = q1_wf_confirmed;
  fx->combat = fx->callbacks != NULL ? qa_gameplay_authority_create(fx->actors, fx->callbacks, hooks) : NULL;
  fx->inventory = qa_shared_inventory_table_create(fx->actors);
  fx->game = calloc(1, sizeof *fx->game);
  if (fx->bodies == NULL || fx->callbacks == NULL || fx->combat == NULL || fx->inventory == NULL || fx->game == NULL)
    return false;
  qa_q1_host host;
  memset(&host, 0, sizeof host);
  host.actors = fx->actors;
  host.bodies = fx->bodies;
  host.callbacks = fx->callbacks;
  host.combat = fx->combat;
  host.inventory = fx->inventory;
  host.ctx = fx;
  host.random = q1_wf_random;
  host.trace = q1_wf_trace;
  host.contents = q1_wf_contents;
  host.walk_move = q1_wf_walk_move;
  host.change_yaw = q1_wf_change_yaw;
  host.move_to_goal = q1_wf_move_to_goal;
  host.check_bottom = q1_wf_check_bottom;
  host.pusher_services = q1_wf_pusher_services;
  host.schedule_think = q1_wf_schedule_think;
  host.cancel_think = q1_wf_cancel_think;
  host.emit = q1_wf_emit;
  host.transition = q1_wf_transition;
  host.players = q1_wf_players;
  host.check_client = q1_wf_check_client;
  host.classname = q1_wf_classname;
  host.powerup = q1_wf_powerup;
  if (!qa_q1_services_init(fx->game, host, options)) return false;
  qa_q1_combat_policy_options policy;
  memset(&policy, 0, sizeof policy);
  policy.id = options.combat_provider != NULL ? options.combat_provider : "q1:combat";
  policy.ctx = fx;
  policy.context = q1_wf_combat_context;
  policy.armor = qa_native_victim_armor(q1_wf_armor_context, fx);
  fx->armor_ctx = policy.armor.ctx;
  if (q1_wf_with_source_effects) {
    policy.has_source_effects = true;
    policy.source_effects = qa_q1_damage_source_effects_of(fx->game);
  }
  const qa_combat_policy registered = qa_create_q1_combat_policy(policy);
  fx->policy_ctx = registered.ctx;
  (void)qa_gameplay_authority_register(fx->combat, registered);
  return true;
}

static inline void q1_world_fixture_destroy(q1_world_fixture *fx) {
  if (fx->game != NULL) {
    if (fx->game->named != NULL) qa_q1_services_free(fx->game);
    free(fx->game);
  }
  if (fx->combat != NULL) qa_gameplay_authority_destroy(fx->combat);
  if (fx->inventory != NULL) qa_shared_inventory_table_destroy(fx->inventory);
  if (fx->bodies != NULL) qa_shared_body_table_destroy(fx->bodies);
  if (fx->callbacks != NULL) qa_actor_callback_table_destroy(fx->callbacks);
  if (fx->actors != NULL) qa_session_actor_registry_destroy(fx->actors);
  qa_identity_owner_destroy(&fx->identity);
  free(fx->policy_ctx);
  free(fx->armor_ctx);
  free(fx->events);
  free(fx->thinks);
  memset(fx, 0, sizeof *fx);
}

static inline qa_owned_actor q1_world_fixture_admit_player(q1_world_fixture *fx, qa_vec3 origin, double health) {
  if (fx->player_count == sizeof fx->players / sizeof fx->players[0]) qa_range_error("fixture player limit");
  const qa_owned_actor player = qa_session_actor_registry_allocate_at_source(
      fx->actors, "q3:character", (int64_t)fx->player_count + 1, "q3:sarge");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.origin = origin;
  body.bounds = QA_Q1_PLAYER_BOUNDS;
  qa_shared_body_table_create_body(fx->bodies, player, body);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  combat.health = health;
  combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  combat.armor.powered.kind = "none";
  combat.mass = 100;
  combat.can_take_damage = true;
  qa_gameplay_authority_create_state(fx->combat, player, combat, NULL, NULL);
  qa_shared_inventory_table_create_entries(fx->inventory, player, NULL, 0);
  fx->players[fx->player_count++] = player.id;
  return player;
}

static inline void q1_world_fixture_advance(q1_world_fixture *fx, double seconds) {
  for (;;) {
    size_t at = SIZE_MAX;
    for (size_t i = 0; i < fx->think_count; i++)
      if (fx->thinks[i].due <= seconds && (at == SIZE_MAX || fx->thinks[i].due < fx->thinks[at].due)) at = i;
    if (at == SIZE_MAX) return;
    const qa_owned_actor actor = fx->thinks[at].actor;
    const double due = fx->thinks[at].due;
    memmove(&fx->thinks[at], &fx->thinks[at + 1], (fx->think_count - at - 1) * sizeof *fx->thinks);
    fx->think_count--;
    if (!qa_session_actor_registry_is_live(fx->actors, actor.id)) continue;
    qa_frame_context frame;
    memset(&frame, 0, sizeof frame);
    frame.time.kind = QA_SOURCE_TIME_SECONDS;
    frame.time.value = due;
    frame.elapsed.kind = QA_SOURCE_TIME_SECONDS;
    frame.elapsed.value = 0;
    (void)qa_actor_callback_table_think(fx->callbacks, actor, frame);
  }
}

#endif
