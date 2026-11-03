/*
 * C checks for tests/gameplay/composition/q2/composition.test.ts (Q2
 * composition: createQ2ProductRuntime, captureQ2Product,
 * restoreQ2Product). compose() mirrors the TS helper: a rerelease baseq2
 * runtime over SessionActorRegistry, SharedBodyTable, GameplayAuthority
 * (createQ2CombatPolicy with the match's source effects and
 * nativeVictimArmor) and SharedInventoryTable, the same host stubs (the
 * CTF/LMCTF ground probe trace returns its start), two or four players
 * allocated at source "q2:players" slot n+1. Expected values are the TS
 * test's, which pass under bun (15 of 16; "retail rerelease base1" needs
 * retail rerelease data and fails under bun without it, so it has no C
 * check). Each callback asserts the context its author gave it.
 * GPL-2.0-or-later.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/cvars/index.h"
#include "qa/settings/server/q2_owner.h"

#include "qa/content/composition/q2/index.h"
#include "qa/content/composition/q2/save.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/policies.h"

static int checks = 0, failures = 0;
#define CHECK(cond)                                                                                   \
  do {                                                                                                \
    checks++;                                                                                         \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); }     \
  } while (0)

#define MAX_CLIENTS 4
typedef struct cw cw;
/* Per-role contexts: each callback checks it got the one it was given. */
typedef struct role { cw *world; int tag; } role;
enum { ROLE_HOST = 1, ROLE_LINK, ROLE_COMBAT, ROLE_POLICY, ROLE_ARMOR, ROLE_PLAYER, ROLE_RR, ROLE_ITEM, ROLE_ENTITY,
       ROLE_SERVICES, ROLE_WEAPONS, ROLE_FLAGS };
static int ctx_failures = 0;
static cw *ctx_world(void *ctx, int tag) {
  role *r = ctx;
  if (r == NULL || r->tag != tag) { ctx_failures++; fprintf(stderr, "FAIL foreign ctx (want %d)\n", tag); abort(); }
  return r->world;
}

struct cw {
  role roles[16];
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_actor_callback_table *callbacks;
  qa_shared_body_table *bodies;
  qa_shared_inventory_table *inventory;
  qa_gameplay_authority *combat;
  qa_combat_policy policy;
  qa_victim_armor_policy armor;
  qa_q2_product_match_effects_binding effects;
  qa_q2w_weapons weapons;
  qa_q2_foundation_host host;
  qa_q2_composition_options options;
  qa_q2_product_runtime *composition;
  qa_q2_game_services *game;
  qa_q2_spawn_report report;
  qa_q2_match_selection match;
  qa_actor_id ids[MAX_CLIENTS]; size_t id_count;
  qa_q2_player_movement movements[MAX_CLIENTS];
  double now;
  double *flags; /* TS deathmatchFlags {read, write} target, or NULL */
  qa_q2_entity *first, *second;
  int messages;
  qa_q2_rerelease_item_state *item_services; /* TS q2RereleaseItemServices(cvars) spread, or NULL */
};

/* ---- host ---- */
static double h_now(void *c) { return ctx_world(c, ROLE_HOST)->now; }
static double h_gravity(void *c) { (void)ctx_world(c, ROLE_HOST); return 800; }
static double h_frame(void *c) { (void)ctx_world(c, ROLE_HOST); return 0.025; }
static double h_random(void *c) { (void)ctx_world(c, ROLE_HOST); return 0.5; }
static void h_schedule(void *c, qa_owned_actor a, const double *due) { (void)ctx_world(c, ROLE_HOST); (void)a; (void)due; }
static void h_touch_triggers(void *c, qa_owned_actor a) { (void)ctx_world(c, ROLE_HOST); (void)a; }
static void h_key(void *c, qa_actor_id a) { (void)ctx_world(c, ROLE_HOST); (void)a; }
static qa_trace_result h_trace(void *c, qa_q2_trace_request r) {
  cw *w = ctx_world(c, ROLE_HOST);
  qa_trace_result out;
  memset(&out, 0, sizeof out);
  out.kind = "q2";
  out.fraction = 1;
  bool probe = (w->match.kind == QA_Q2_MATCH_CTF || w->match.kind == QA_Q2_MATCH_LMCTF) && r.end.z == r.start.z - 128;
  out.end = probe ? r.start : r.end;
  out.hit.kind = QA_TRACE_HIT_NONE;
  return out;
}
static double h_contents(void *c, qa_vec3 p) { (void)ctx_world(c, ROLE_HOST); (void)p; return 0; }
static bool h_true2(void *c, qa_vec3 a, qa_vec3 b) { (void)ctx_world(c, ROLE_HOST); (void)a; (void)b; return true; }
static const qa_actor_id *h_nearby(void *c, qa_vec3 o, double r, size_t *n) {
  (void)ctx_world(c, ROLE_HOST); (void)o; (void)r; *n = 0; return NULL;
}
static const qa_actor_id *h_players(void *c, size_t *n) { cw *w = ctx_world(c, ROLE_HOST); *n = w->id_count; return w->ids; }
static bool h_is_player(void *c, qa_actor_id a) {
  cw *w = ctx_world(c, ROLE_HOST);
  for (size_t i = 0; i < w->id_count; i++) if (qa_same_actor(w->ids[i], a)) return true;
  return false;
}
static bool h_is_monster(void *c, qa_actor_id a) { (void)ctx_world(c, ROLE_HOST); (void)a; return false; }
static qa_actor_id h_world_actor(void *c) {
  cw *w = ctx_world(c, ROLE_HOST);
  for (size_t i = 0; i < qa_q2_game_entity_count(w->game); i++) {
    qa_q2_entity *e = qa_q2_game_entity_at(w->game, i);
    if (strcmp(e->classname, "worldspawn") == 0) return e->actor.id;
  }
  qa_raise("Error", 0, 0, "world missing");
}
static qa_bounds h_inline(void *c, double m) {
  (void)ctx_world(c, ROLE_HOST); (void)m;
  qa_bounds b = {{-16, -16, -24}, {16, 16, 32}};
  return b;
}
static void h_set_solid(void *c, qa_owned_actor a, const char *s, const double *m) { (void)ctx_world(c, ROLE_HOST); (void)a; (void)s; (void)m; }
static void h_set_motion(void *c, qa_q2_motion m) { (void)ctx_world(c, ROLE_HOST); (void)m; }
static void h_portal(void *c, double p, bool o) { (void)ctx_world(c, ROLE_HOST); (void)p; (void)o; }
static void h_emit(void *c, qa_q2_presentation_event e) {
  cw *w = ctx_world(c, ROLE_HOST);
  if (strcmp(e.kind, "centerprint") == 0) w->messages++;
}
static qa_q2_player_view_result h_view(void *c, qa_actor_id a) {
  (void)ctx_world(c, ROLE_HOST); (void)a;
  qa_q2_player_view_result r;
  memset(&r, 0, sizeof r);
  r.has_value = true;
  return r;
}
static void h_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { (void)ctx_world(c, ROLE_HOST); (void)m; (void)l; (void)f; }
static void h_transition(void *c, qa_transition_intent t) { (void)ctx_world(c, ROLE_HOST); (void)t; }
static void h_diagnostic(void *c, const char *m) { (void)ctx_world(c, ROLE_HOST); qa_raise("Error", 0, 0, "%s", m); }

/* ---- tables ---- */
static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) { (void)ctx_world(c, ROLE_LINK); return qa_translated_body_bounds(a, b); }
static void link_on(void *c, qa_linked_body b) { (void)ctx_world(c, ROLE_LINK); (void)b; }
static void link_off(void *c, qa_actor_id a) { (void)ctx_world(c, ROLE_LINK); (void)a; }
static void combat_impulse(void *c, qa_owned_actor a, qa_vec3 i, qa_provider_id m) { (void)ctx_world(c, ROLE_COMBAT); (void)a; (void)i; (void)m; }
static void combat_before(void *c, qa_owned_actor a, qa_damage_decision d) { (void)ctx_world(c, ROLE_COMBAT); (void)a; (void)d; }
static void combat_confirmed(void *c, qa_damage_outcome o) { (void)ctx_world(c, ROLE_COMBAT); (void)o; }
static qa_q2_combat_context policy_context(void *c, qa_damage_request r, qa_combat_state t, const qa_combat_state *a) {
  (void)ctx_world(c, ROLE_POLICY); (void)r; (void)t; (void)a;
  qa_q2_combat_context out;
  memset(&out, 0, sizeof out);
  out.arithmetic = "binary64"; out.player = true; out.no_knockback = true; out.movable = true;
  return out;
}
static qa_victim_armor_context armor_context(void *c, qa_damage_request r) {
  (void)ctx_world(c, ROLE_ARMOR); (void)r;
  qa_victim_armor_context out;
  memset(&out, 0, sizeof out);
  out.screen_facing_dot = 1; out.arithmetic = "binary64";
  out.has_q2 = true; out.q2_product = "rerelease"; out.q2_ctf = false; out.q2_alive = true;
  return out;
}

/* ---- Q2Weapons hooks ---- */
static void w_emit(void *c, qa_q2w_event e) { (void)ctx_world(c, ROLE_WEAPONS); (void)e; }
static void w_noise(void *c, qa_actor_id a, qa_vec3 o, bool s) { (void)ctx_world(c, ROLE_WEAPONS); (void)a; (void)o; (void)s; }
static void w_dodge(void *c, qa_actor_id m, qa_actor_id a, double eta, qa_trace_result t) { (void)ctx_world(c, ROLE_WEAPONS); (void)m; (void)a; (void)eta; (void)t; }
static void w_ammo(void *c, qa_actor_id a, qa_item_id i) { (void)ctx_world(c, ROLE_WEAPONS); (void)a; (void)i; }
static bool w_can_target(void *c, const qa_actor_id *a, qa_actor_id t) { (void)ctx_world(c, ROLE_WEAPONS); (void)a; (void)t; return true; }

/* ---- player hooks ---- */
static qa_q2_player_movement p_movement(void *c, qa_actor_id actor) {
  cw *w = ctx_world(c, ROLE_PLAYER);
  for (size_t i = 0; i < w->id_count; i++) if (qa_same_actor(w->ids[i], actor)) return w->movements[i];
  qa_raise("Error", 0, 0, "missing player movement");
}
static void p_set_movement(void *c, qa_actor_id a, qa_q2_player_movement_change m) { (void)ctx_world(c, ROLE_PLAYER); (void)a; (void)m; }
static void p_emit(void *c, qa_q2_player_event e) { (void)ctx_world(c, ROLE_PLAYER); (void)e; }
static void p_noise(void *c, qa_actor_id a, qa_vec3 o) { (void)ctx_world(c, ROLE_PLAYER); (void)a; (void)o; }
static bool p_banned(void *c, const char *address) { (void)ctx_world(c, ROLE_PLAYER); (void)address; return false; }
static qa_q2w_input zero_input(void) {
  qa_q2w_input input;
  memset(&input, 0, sizeof input);
  input.hand = QA_Q2W_HAND_RIGHT; input.players_collide = true; input.gravity = 800;
  return input;
}
static qa_q2w_input p_weapon_input(void *c, qa_actor_id a) { (void)ctx_world(c, ROLE_PLAYER); (void)a; return zero_input(); }

/* ---- rerelease hooks ---- */
static void rr_emit(void *c, qa_q2_rerelease_event e) { (void)ctx_world(c, ROLE_RR); (void)e; }
static qa_q2rr_player_identity rr_identity(void *c, qa_actor_id actor) {
  cw *w = ctx_world(c, ROLE_RR);
  qa_q2rr_player_identity identity = {-1, ""};
  for (size_t i = 0; i < w->id_count; i++) if (qa_same_actor(w->ids[i], actor)) identity.seat = (double)i;
  return identity;
}
static bool rr_clip(void *c, qa_q2_entity *t, qa_actor_id p, qa_q2_game_services *g) { (void)ctx_world(c, ROLE_RR); (void)t; (void)p; (void)g; return true; }
static qa_q2rr_navigation rr_navigation(void *c, qa_vec3 s, qa_vec3 g, const qa_actor_id *a) {
  (void)ctx_world(c, ROLE_RR); (void)s; (void)g; (void)a;
  qa_q2rr_navigation n;
  memset(&n, 0, sizeof n);
  n.kind = "no-navigation";
  return n;
}
static bool rr_searching(void *c, const qa_actor_id *p) { (void)ctx_world(c, ROLE_RR); (void)p; return false; }
static bool rr_grounded(void *c, qa_actor_id p) { (void)ctx_world(c, ROLE_RR); (void)p; return true; }
static void rr_push(void *c, qa_actor_id p, qa_vec3 v) { (void)ctx_world(c, ROLE_RR); (void)p; (void)v; }
static void rr_actor_gravity(void *c, qa_actor_id a, double g) { (void)ctx_world(c, ROLE_RR); (void)a; (void)g; }
static void rr_world_gravity(void *c, double g) { (void)ctx_world(c, ROLE_RR); (void)g; }
static const char *rr_light_style(void *c, double s) { (void)ctx_world(c, ROLE_RR); (void)s; return "m"; }

/* ---- item, entity, services hooks ---- */
static void i_weapon_picked(void *c, qa_actor_id a, qa_item_id i, bool f) { (void)ctx_world(c, ROLE_ITEM); (void)a; (void)i; (void)f; }
static void i_silencer(void *c, qa_actor_id a, double n) { (void)ctx_world(c, ROLE_ITEM); (void)a; (void)n; }
static void i_power_armor(void *c, qa_actor_id a, const char *k) { (void)ctx_world(c, ROLE_ITEM); (void)a; (void)k; }
static void e_push(void *c, qa_actor_id a, qa_vec3 v) { (void)ctx_world(c, ROLE_ENTITY); (void)a; (void)v; }
static void e_gravity(void *c, qa_actor_id a, double g) { (void)ctx_world(c, ROLE_ENTITY); (void)a; (void)g; }
static qa_q2_base_local_time e_time(void *c) { (void)ctx_world(c, ROLE_ENTITY); qa_q2_base_local_time t = {12, 0, 0}; return t; }
static double s_gravity(void *c) { (void)ctx_world(c, ROLE_SERVICES); return 800; }
static void s_emit(void *c, qa_q2_composition_event e) { (void)ctx_world(c, ROLE_SERVICES); (void)e; }
static qa_q2_composition_powerups s_foreign(void *c, qa_actor_id a) {
  (void)ctx_world(c, ROLE_SERVICES); (void)a;
  qa_q2_composition_powerups p = {0, 0, 0};
  return p;
}
/* TS deathmatchFlags {read, write} over a test-owned number; the C
 * services share their ctx, so these leaves check the services role. */
static double f_read(void *c) { return *ctx_world(c, ROLE_SERVICES)->flags; }
static void f_write(void *c, double value) { *ctx_world(c, ROLE_SERVICES)->flags = value; }
static qa_q2mp_random_item_settings s_random_items(void *c) {
  cw *w = ctx_world(c, ROLE_SERVICES);
  return qa_q2_rerelease_random_items(w->item_services);
}
static bool s_drop_quad_fire(void *c) { return qa_q2_rerelease_drop_quad_fire(ctx_world(c, ROLE_SERVICES)->item_services); }

static const char *const PLAIN_MAP = "{ \"classname\" \"worldspawn\" } { \"classname\" \"info_player_start\" }";
static const char *const CTF_MAP =
    "{ \"classname\" \"worldspawn\" } { \"classname\" \"info_player_start\" } { \"classname\" \"info_player_deathmatch\" "
    "\"origin\" \"400 0 0\" } { \"classname\" \"info_player_team1\" \"origin\" \"-400 0 0\" } { \"classname\" "
    "\"info_player_team2\" \"origin\" \"400 0 0\" } { \"classname\" \"item_flag_team1\" \"origin\" \"-500 0 0\" } { "
    "\"classname\" \"item_flag_team2\" \"origin\" \"500 0 0\" }";

typedef struct compose_extra {
  double *flags;                              /* TS deathmatchFlags */
  qa_q2_rerelease_item_state *item_services;  /* TS options */
} compose_extra;

/* TS compose(initializeInventory, entities, match, clientCount, deathmatchFlags, options). */
static cw *compose(bool initialize_inventory, const char *entities, qa_q2_match_selection match, size_t client_count,
                   const compose_extra *extra) {
  cw *w = calloc(1, sizeof *w);
  for (int i = 0; i < 16; i++) { w->roles[i].world = w; w->roles[i].tag = i; }
  w->match = match;
  qa_identity_owner_create(&w->identities, "rr-source-check");
  w->actors = qa_session_actor_registry_create(&w->identities, 4096);
  w->callbacks = qa_actor_callback_table_create(w->actors);
  qa_body_link_hooks link = {&w->roles[ROLE_LINK], link_bounds, link_on, link_off};
  w->bodies = qa_shared_body_table_create(w->actors, link);
  w->inventory = qa_shared_inventory_table_create(w->actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &w->roles[ROLE_COMBAT]; hooks.impulse = combat_impulse; hooks.before_reaction = combat_before;
  hooks.confirmed = combat_confirmed;
  w->combat = qa_gameplay_authority_create(w->actors, w->callbacks, hooks);
  qa_q2w_hooks wh;
  memset(&wh, 0, sizeof wh);
  wh.ctx = &w->roles[ROLE_WEAPONS]; wh.emit = w_emit; wh.noise = w_noise; wh.dodge = w_dodge;
  wh.ammo_changed = w_ammo; wh.can_target = w_can_target;
  CHECK(qa_q2w_weapons_init(&w->weapons, wh));
  qa_q2_foundation_host *h = &w->host;
  h->ctx = &w->roles[ROLE_HOST];
  h->actors = w->actors; h->callbacks = w->callbacks; h->bodies = w->bodies; h->combat = w->combat;
  h->inventory = w->inventory;
  h->now = h_now; h->gravity = h_gravity; h->frame_seconds = h_frame; h->random = h_random; h->schedule = h_schedule;
  h->touch_triggers = h_touch_triggers; h->key_consumed = h_key; h->trace = h_trace; h->point_contents = h_contents;
  h->in_pvs = h_true2; h->in_phs = h_true2; h->areas_connected = h_true2; h->nearby = h_nearby; h->players = h_players;
  h->is_player = h_is_player; h->is_monster = h_is_monster; h->world_actor = h_world_actor;
  h->inline_model_bounds = h_inline; h->set_solid = h_set_solid; h->set_motion = h_set_motion;
  h->set_area_portal = h_portal; h->emit = h_emit; h->player_view_state = h_view; h->prepare_level_change = h_prepare;
  h->transition = h_transition; h->diagnostic = h_diagnostic;

  qa_q2_composition_options *o = &w->options;
  o->edition = QA_Q2_EDITION_RERELEASE;
  o->program = QA_Q2_PROGRAM_BASEQ2;
  o->host = h;
  o->weapons = &w->weapons;
  o->match = &w->match;
  o->options.map_name = "base1"; o->options.skill = 1;
  o->options.mode = match.kind == QA_Q2_MATCH_STANDARD ? QA_Q2_MODE_COOP : QA_Q2_MODE_DEATHMATCH;
  o->options.deathmatch_flags = 0; o->options.max_clients = 4; o->options.provider = "q2:game";
  o->options.campaign = "q2:base"; o->options.combat_provider = "q2:combat";
  o->options.inventory_provider = "q2:inventory"; o->options.movement_provider = "q2:movement";
  qa_q2_player_hooks *ph = &o->player_hooks;
  ph->ctx = &w->roles[ROLE_PLAYER]; ph->movement = p_movement; ph->set_movement = p_set_movement; ph->emit = p_emit;
  ph->noise = p_noise; ph->banned = p_banned; ph->weapon_input = p_weapon_input;
  o->item_hooks.ctx = &w->roles[ROLE_ITEM]; o->item_hooks.weapon_picked = i_weapon_picked;
  o->item_hooks.silencer = i_silencer; o->item_hooks.power_armor = i_power_armor;
  o->entity_hooks.ctx = &w->roles[ROLE_ENTITY]; o->entity_hooks.player_push = e_push;
  o->entity_hooks.set_actor_gravity = e_gravity; o->entity_hooks.local_time = e_time;
  qa_q2_composition_services *s = &o->services;
  s->ctx = &w->roles[ROLE_SERVICES]; s->gravity = s_gravity; s->emit = s_emit; s->foreign_powerups = s_foreign;
  if (extra != NULL && extra->flags != NULL) {
    w->flags = extra->flags;
    s->deathmatch_flags_read = f_read;
    s->deathmatch_flags_write = f_write;
  }
  if (extra != NULL && extra->item_services != NULL) {
    w->item_services = extra->item_services;
    s->random_items = s_random_items;
    s->drop_quad_fire = s_drop_quad_fire;
  }
  qa_q2_rerelease_hooks *rr = &o->rerelease_hooks;
  rr->ctx = &w->roles[ROLE_RR]; rr->emit = rr_emit; rr->player_identity = rr_identity; rr->clip_trigger = rr_clip;
  rr->navigation = rr_navigation; rr->monsters_searching = rr_searching; rr->grounded_on_world = rr_grounded;
  rr->push_player = rr_push; rr->set_actor_gravity = rr_actor_gravity; rr->set_world_gravity = rr_world_gravity;
  rr->light_style = rr_light_style;
  w->composition = qa_create_q2_product_runtime(o);
  CHECK(w->composition != NULL);
  w->game = w->composition->services;
  qa_q2_combat_policy_options policy;
  memset(&policy, 0, sizeof policy);
  policy.id = "q2:combat";
  policy.ctx = &w->roles[ROLE_POLICY];
  policy.context = policy_context;
  policy.has_source_effects = true;
  policy.source_effects = qa_q2_product_match_source_effects(&w->effects, &w->composition->match, w->game);
  w->armor = policy.armor = qa_native_victim_armor(armor_context, &w->roles[ROLE_ARMOR]);
  w->policy = qa_create_q2_combat_policy(policy);
  (void)qa_gameplay_authority_register(w->combat, w->policy);
  CHECK(w->composition->rerelease != NULL);
  w->report = qa_q2_foundation_load(w->composition->game, entities, NULL, 0);
  qa_q2_product_runtime_after_spawn(w->composition);
  qa_vec3 zero = {0, 0, 0};
  qa_bounds bounds = {{-16, -16, -24}, {16, 16, 32}};
  for (size_t slot = 0; slot < client_count; slot++) {
    qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(w->actors, "q2:players", (int64_t)slot + 1, "q2:male");
    w->ids[w->id_count++] = actor.id;
    qa_body_state body;
    memset(&body, 0, sizeof body);
    body.origin = zero; body.angles = zero; body.velocity = zero; body.bounds = bounds;
    qa_shared_body_table_create_body(w->bodies, actor, body);
    qa_q2_player_movement movement;
    memset(&movement, 0, sizeof movement);
    movement.grounded = true; movement.standing_bounds = bounds;
    w->movements[slot] = movement;
    char userinfo[32];
    snprintf(userinfo, sizeof userinfo, "\\name\\Player%zu", slot);
    qa_q2_player_admission admission;
    memset(&admission, 0, sizeof admission);
    admission.slot = (double)slot; admission.userinfo = userinfo; admission.initialize_inventory = initialize_inventory;
    admission.has_use_q2_weapons = true; admission.use_q2_weapons = false;
    if (match.kind == QA_Q2_MATCH_CTF || match.kind == QA_Q2_MATCH_LMCTF) {
      (void)qa_q2_product_runtime_admit(w->composition, actor, admission, NULL);
    } else {
      qa_q2_entity *entity = qa_q2_entity_services_attach_player(w->composition->game, actor);
      (void)qa_q2_players_attach(w->composition->players, entity, w->game, admission);
    }
  }
  w->first = qa_q2_game_entity_of(w->game, &w->ids[0]);
  w->second = qa_q2_game_entity_of(w->game, &w->ids[1]);
  CHECK(w->first != NULL && w->second != NULL);
  return w;
}

static void release(cw *w) {
  qa_q2_product_runtime_destroy(w->composition);
  qa_q2w_weapons_destroy(&w->weapons);
  qa_shared_inventory_table_destroy(w->inventory);
  qa_gameplay_authority_destroy(w->combat);
  qa_combat_policy_free(w->policy);
  qa_victim_armor_policy_free(w->armor);
  qa_actor_callback_table_destroy(w->callbacks);
  qa_shared_body_table_destroy(w->bodies);
  qa_session_actor_registry_close(w->actors);
  qa_session_actor_registry_destroy(w->actors);
  qa_identity_owner_destroy(&w->identities);
  free(w);
}

static qa_q2_match_selection selection(qa_q2_match_kind kind) {
  qa_q2_match_selection value;
  memset(&value, 0, sizeof value);
  value.kind = kind;
  return value;
}
static qa_q2_entity *spawn(cw *w, const char *classname, double ordinal, const char *key, const char *value) {
  qa_q2_spawn_fields fields;
  memset(&fields, 0, sizeof fields);
  fields.ordinal = ordinal;
  fields.classname = classname;
  CHECK(qa_q2_spawn_set(&fields, "classname", classname));
  if (key != NULL) CHECK(qa_q2_spawn_set(&fields, key, value));
  qa_q2_entity *entity = w->game->spawn(w->game->ctx, fields);
  return entity;
}
static qa_q2_entity *find(cw *w, const char *classname) {
  for (size_t i = 0; i < qa_q2_game_entity_count(w->game); i++) {
    qa_q2_entity *e = qa_q2_game_entity_at(w->game, i);
    if (strcmp(e->classname, classname) == 0) return e;
  }
  return NULL;
}
static size_t count_named(cw *w, const char *classname) {
  size_t n = 0;
  for (size_t i = 0; i < qa_q2_game_entity_count(w->game); i++)
    if (strcmp(qa_q2_game_entity_at(w->game, i)->classname, classname) == 0) n++;
  return n;
}
static void configure(cw *w, qa_q2_entity *player, qa_item_id item, double count, double capacity) {
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = item; entry.count = count; entry.capacity = capacity;
  qa_shared_inventory_table_configure(w->inventory, player->actor, entry, NULL, NULL);
}
static double health_of(cw *w, qa_q2_entity *e) {
  qa_combat_state_result state = qa_gameplay_authority_read(w->combat, e->actor.id);
  return state.has_value ? state.value.health : -1e9;
}
static void think(cw *w, qa_q2_entity *e) { if (e->think.fn != NULL) e->think.fn(e->think.ctx, e, w->game); }
static void touch(cw *w, qa_q2_entity *e, qa_q2_entity *other) {
  qa_touch_contact contact;
  memset(&contact, 0, sizeof contact);
  contact.self = e->actor;
  contact.other = other->actor.id;
  e->touch.fn(e->touch.ctx, e, w->game, contact);
}
/* TS captureQ2Product(active) then restoreQ2Product(restored, checkpoint). */
static void save_into(cw *from, cw *to) {
  size_t count = 0;
  qa_provider_checkpoint *records = qa_capture_q2_product(from->composition, &count);
  CHECK(records != NULL && count > 0);
  qa_restore_q2_product(to->composition, records, count);
  qa_q2_product_checkpoints_free(records, count);
  /* The C restore rebuilds entity records; TS tests keep using the
   * players' actor ids, so look the entities up again by id. */
  to->first = qa_q2_game_entity_of(to->game, &to->ids[0]);
  to->second = qa_q2_game_entity_of(to->game, &to->ids[1]);
}
static qa_death_reaction killed_by(cw *w, qa_q2_entity *victim, qa_q2_entity *attacker, double damage) {
  qa_death_reaction reaction;
  memset(&reaction, 0, sizeof reaction);
  reaction.pain.self = victim->actor;
  reaction.pain.attacker = &attacker->actor.id;
  reaction.inflictor = &attacker->actor.id;
  reaction.pain.damage = damage;
  reaction.point = qa_q2_game_body(w->game, victim).origin;
  return reaction;
}

static qa_q2_rerelease_players *rr_players(cw *w) { return &w->composition->rerelease->players; }
static qa_q2_base_player_state *state_of(cw *w, qa_q2_entity *e) { return qa_q2_players_get_state(w->composition->players, e->actor.id); }
static bool command(cw *w, qa_q2_entity *e, const char *name, const char *arg) {
  const char *args[1] = {arg};
  return qa_q2_players_client_command(w->composition->players, e, w->game, name, args, arg != NULL ? 1 : 0);
}
static bool ctf_command(cw *w, qa_q2_entity *e, const char *name, const char *arg0, const char *arg1) {
  const char *args[2] = {arg0, arg1};
  return qa_q2_ctf_command(w->composition->match.ctf, e, w->game, name, args, arg1 != NULL ? 2 : arg0 != NULL ? 1 : 0);
}
static bool lmctf_command(cw *w, qa_q2_entity *e, const char *name, const char *arg) {
  const char *args[1] = {arg};
  return qa_q2_lmctf_command(w->composition->match.lmctf, e, w->game, name, args, arg != NULL ? 1 : 0);
}
static qa_q2_ctf_player_state *ctf_state(cw *w, qa_q2_entity *e) {
  return qa_q2_ctf_context_get(qa_q2_ctf_get_context(w->composition->match.ctf), e->actor.id);
}
static qa_lmctf_player_state *lmctf_state(cw *w, qa_q2_entity *e) {
  return qa_lmctf_context_get(qa_q2_lmctf_get_context(w->composition->match.lmctf), e->actor.id);
}
static double statistic(qa_lmctf_player_state *state, const char *key) {
  for (size_t i = 0; i < state->statistic_count; i++)
    if (strcmp(state->statistics[i].key, key) == 0) return state->statistics[i].count;
  return -1;
}
static void body_armor(cw *w, qa_q2_entity *e) {
  qa_armor_state armor;
  memset(&armor, 0, sizeof armor);
  armor.regular.kind = QA_REGULAR_ARMOR_Q2; armor.regular.points = 200; armor.regular.normal_protection = 0.8;
  armor.regular.energy_protection = 0.6; armor.regular.item = "q2:item_armor_body";
  armor.powered.kind = "none";
  qa_gameplay_authority_set_armor(w->combat, e->actor, armor);
}
static void no_armor(cw *w, qa_q2_entity *e) {
  qa_armor_state armor;
  memset(&armor, 0, sizeof armor);
  armor.regular.kind = QA_REGULAR_ARMOR_NONE; armor.powered.kind = "none";
  qa_gameplay_authority_set_armor(w->combat, e->actor, armor);
}
static double armor_points(cw *w, qa_q2_entity *e) {
  qa_combat_state_result state = qa_gameplay_authority_read(w->combat, e->actor.id);
  return state.has_value && state.value.armor.regular.kind == QA_REGULAR_ARMOR_Q2 ? state.value.armor.regular.points : -1;
}
/* TS game.damage(target, first, first.actor.id, amount, 0, zero, zero, zero, 1, 0). */
static void damage(cw *w, qa_q2_entity *target, qa_q2_entity *attacker, double amount) {
  qa_vec3 zero = {0, 0, 0};
  (void)w->game->damage(w->game->ctx, target->actor.id, attacker, attacker->actor.id, &attacker->actor.id, amount, 0,
                        zero, zero, zero, 1, 0, NULL);
}

/* combined rerelease source preserves private seats, expansion powers and named entity saves */
static void test_combined_rerelease(void) {
  cw *active = compose(true, PLAIN_MAP, selection(QA_Q2_MATCH_STANDARD), 2, NULL);
  CHECK(strcmp(qa_q2w_weapons_definition(active->composition->weapons, "ionripper")->name, "ionripper") == 0);
  CHECK(strcmp(qa_q2w_weapons_definition(active->composition->weapons, "disintegrator")->name, "disintegrator") == 0);
  CHECK(qa_q2_rerelease_players_get_extra(rr_players(active), active->first->actor.id)->seat == 0);
  CHECK(qa_q2_rerelease_players_get_extra(rr_players(active), active->second->actor.id)->seat == 1);
  qa_q2_entity *gekk = spawn(active, "monster_gekk", 3, NULL, NULL);
  qa_q2_entity *stalker = spawn(active, "monster_stalker", 4, NULL, NULL);
  qa_q2_entity *guardian = spawn(active, "monster_guardian", 5, NULL, NULL);
  qa_q2m_context *context = qa_q2m_monsters_context(&active->composition->monsters, gekk->actor.id);
  CHECK(context != NULL && strcmp(context->state->kind, "gekk") == 0);
  context = qa_q2m_monsters_context(&active->composition->monsters, stalker->actor.id);
  CHECK(context != NULL && strcmp(context->state->kind, "stalker") == 0);
  context = qa_q2m_monsters_context(&active->composition->monsters, guardian->actor.id);
  CHECK(context != NULL && strcmp(context->state->kind, "guardian") == 0);
  qa_q2_entity *target = spawn(active, "target_story", 2, "message", "source private story");
  CHECK(target->use.fn != NULL);
  target->use.fn(target->use.ctx, target, active->game, NULL, &active->first->actor.id);
  configure(active, active->first, "q2:item_double", 1, 2);
  (void)qa_q2_item_module_use(active->composition->items, active->first->actor, "q2:item_double", active->game, 30);
  CHECK(qa_q2mp_items_powerups(&active->composition->armory->items, active->first->actor.id).double_until == 30);
  cw *restored = compose(false, PLAIN_MAP, selection(QA_Q2_MATCH_STANDARD), 2, NULL);
  (void)spawn(restored, "monster_gekk", 3, NULL, NULL);
  (void)spawn(restored, "monster_stalker", 4, NULL, NULL);
  (void)spawn(restored, "monster_guardian", 5, NULL, NULL);
  (void)spawn(restored, "target_story", 2, "message", "source private story");
  save_into(active, restored);
  const char *story = restored->composition->rerelease->entities.base.story;
  CHECK(story != NULL && strcmp(story, "source private story") == 0);
  CHECK(qa_q2mp_items_powerups(&restored->composition->armory->items, restored->first->actor.id).double_until == 30);
  CHECK(qa_q2mp_items_powerups(&restored->composition->armory->items, restored->second->actor.id).double_until == 0);
  release(active);
  release(restored);
}

/* Tag source scoring transfers the live token before player death drops inventory */
static void test_tag_scoring(void) {
  cw *active = compose(true, PLAIN_MAP, selection(QA_Q2_MATCH_TAG), 2, NULL);
  CHECK(active->composition->match.source_kind == QA_Q2_MATCH_TAG);
  qa_q2_entity *token = find(active, "dm_tag_token");
  CHECK(token != NULL);
  if (token == NULL) { release(active); return; }
  qa_q2_item_module_touch(active->composition->items, token, active->game, active->first->actor.id);
  qa_actor_id owner;
  CHECK(qa_q2mp_tag_owner_actor(&active->composition->match.tag, &owner) && qa_same_actor(owner, active->first->actor.id));
  static qa_attack_provenance attack;
  attack = active->game->attack(active->game->ctx, active->second, active->second->actor.id, &active->second->actor.id, 1, 0, NULL);
  active->first->last_attack = &attack;
  (void)qa_q2_players_record_death(active->composition->players, active->first, active->game,
                                   killed_by(active, active->first, active->second, 110));
  CHECK(qa_q2mp_tag_owner_actor(&active->composition->match.tag, &owner) && qa_same_actor(owner, active->second->actor.id));
  CHECK(state_of(active, active->second)->score == 5);
  CHECK(qa_q2_product_match_damage(&active->composition->match, active->first->actor.id, NULL, 100) == 75);
  active->first->last_attack = NULL;
  release(active);
}

/* CTF composes shared admission, team commands, source flag capture and checkpoint */
static void test_ctf_admission(void) {
  cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_CTF), 2, NULL);
  CHECK(active->composition->match.source_kind == QA_Q2_MATCH_CTF);
  CHECK(active->report.unsupported_count == 0);
  CHECK(state_of(active, active->first)->spectator);
  CHECK(command(active, active->first, "team", "red"));
  CHECK(command(active, active->second, "team", "blue"));
  CHECK(qa_q2_game_body(active->game, active->first).origin.x == -400);
  qa_q2_entity *red = find(active, "item_flag_team1"), *blue = find(active, "item_flag_team2");
  CHECK(red != NULL && blue != NULL);
  think(active, red); think(active, blue);
  touch(active, blue, active->first);
  CHECK(qa_shared_inventory_table_count(active->inventory, active->first->actor.id, "q2:item_flag_team2") == 1);
  touch(active, red, active->first);
  CHECK(qa_q2_ctf_get_context(active->composition->match.ctf)->match.team1 == 1);
  cw *restored = compose(false, CTF_MAP, selection(QA_Q2_MATCH_CTF), 2, NULL);
  save_into(active, restored);
  CHECK(qa_q2_ctf_get_context(restored->composition->match.ctf)->match.team1 == 1);
  qa_q2_ctf_player_state *state = ctf_state(restored, restored->first);
  CHECK(state != NULL && state->team == 1);
  release(active);
  release(restored);
}

/* LMCTF composes team admission, flag capture and checkpoint */
static void test_lmctf_admission(void) {
  cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 2, NULL);
  CHECK(active->composition->match.source_kind == QA_Q2_MATCH_LMCTF);
  CHECK(active->report.unsupported_count == 0);
  CHECK(lmctf_state(active, active->first)->team == 1);
  CHECK(lmctf_state(active, active->second)->team == 2);
  qa_lmctf_flags *flags = qa_q2_lmctf_get_flags(active->composition->match.lmctf);
  qa_q2_entity *red = qa_lmctf_flags_flag(flags, 1, active->game), *blue = qa_lmctf_flags_flag(flags, 2, active->game);
  CHECK(red != NULL && blue != NULL);
  (void)qa_lmctf_flags_pickup(flags, blue, active->game, active->first->actor.id);
  CHECK(qa_lmctf_flags_carried(flags, active->first->actor.id, active->game) != NULL);
  (void)qa_lmctf_flags_pickup(flags, red, active->game, active->first->actor.id);
  CHECK(statistic(lmctf_state(active, active->first), "captures") == 1);
  cw *restored = compose(false, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 2, NULL);
  save_into(active, restored);
  CHECK(statistic(lmctf_state(restored, restored->first), "captures") == 1);
  CHECK(qa_lmctf_flags_flag(qa_q2_lmctf_get_flags(restored->composition->match.lmctf), 1, restored->game) != NULL);
  release(active);
  release(restored);
}

/* CTF strength and resistance execute at native shared armor stages */
static void test_ctf_techs(void) {
  cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_CTF), 2, NULL);
  (void)command(active, active->first, "team", "red");
  (void)command(active, active->second, "team", "blue");
  configure(active, active->first, "q2:item_tech2", 1, 1);
  configure(active, active->second, "q2:item_tech1", 1, 1);
  body_armor(active, active->second);
  damage(active, active->second, active->first, 100);
  CHECK(health_of(active, active->second) == 80);
  CHECK(armor_points(active, active->second) == 40);
  release(active);
}

/* LMCTF runes execute between shared armor stages and heal vampire on committed damage */
static void test_lmctf_runes(void) {
  cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 2, NULL);
  qa_lmctf_runes *runes = qa_q2_lmctf_get_runes(active->composition->match.lmctf);
  qa_q2_entity *damage_rune = spawn(active, "damage_rune", 100, NULL, NULL);
  qa_q2_entity *resist_rune = spawn(active, "resist_rune", 101, NULL, NULL);
  CHECK(qa_lmctf_runes_pickup(runes, damage_rune, active->game, active->first->actor.id));
  CHECK(qa_lmctf_runes_pickup(runes, resist_rune, active->game, active->second->actor.id));
  body_armor(active, active->second);
  damage(active, active->second, active->first, 100);
  CHECK(health_of(active, active->second) == 80);
  CHECK(armor_points(active, active->second) == 120);
  (void)qa_lmctf_runes_drop(runes, active->first->actor.id, active->game);
  (void)qa_lmctf_runes_drop(runes, active->second->actor.id, active->game);
  qa_q2_entity *vampire = spawn(active, "vampire_rune", 102, NULL, NULL);
  CHECK(qa_lmctf_runes_pickup(runes, vampire, active->game, active->first->actor.id));
  qa_gameplay_authority_set_health(active->combat, active->first->actor, 200);
  no_armor(active, active->second);
  damage(active, active->second, active->first, 40);
  CHECK(health_of(active, active->second) == 40);
  CHECK(health_of(active, active->first) == 220);
  release(active);
}

/* CTF menu and admin settings commands validate source actions */
static void test_ctf_menu(void) {
  cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_CTF), 2, NULL);
  CHECK(!ctf_command(active, active->first, "ctf-menu", "invalid-action", NULL));
  CHECK(ctf_command(active, active->first, "ctf-menu", "join-red", NULL));
  CHECK(ctf_state(active, active->first)->team == 1);
  CHECK(!ctf_command(active, active->first, "ctf-settings", "matchMinutes", "25"));
  ctf_state(active, active->first)->admin = true;
  CHECK(!ctf_command(active, active->first, "ctf-settings", "matchMinutes", "Infinity"));
  CHECK(ctf_command(active, active->first, "ctf-settings", "matchMinutes", "25"));
  CHECK(qa_q2_ctf_get_context(active->composition->match.ctf)->rules.match_minutes == 25);
  CHECK(ctf_command(active, active->first, "ctf-settings", "weaponsStay", "true"));
  CHECK((qa_js_to_int32(qa_q2_options_deathmatch_flags(&active->game->options)) & 4) == 4);
  release(active);
}

/* CTF competition setup gates shared item pickups and resumes them in play */
static void test_ctf_competition(void) {
  cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_CTF), 2, NULL);
  (void)ctf_command(active, active->first, "ctf-menu", "join-red", NULL);
  qa_gameplay_authority_set_health(active->combat, active->first->actor, 50);
  qa_q2_entity *health = spawn(active, "item_health_small", 110, NULL, NULL);
  think(active, health);
  qa_q2_ctf_context *context = qa_q2_ctf_get_context(active->composition->match.ctf);
  context->match.phase = "setup";
  qa_q2_item_module_touch(active->composition->items, health, active->game, active->first->actor.id);
  CHECK(health_of(active, active->first) == 50);
  context->match.phase = "none";
  qa_q2_item_module_touch(active->composition->items, health, active->game, active->first->actor.id);
  CHECK(health_of(active, active->first) == 52);
  release(active);
}

static void set_owned(char **slot, const char *value) {
  free(*slot);
  *slot = strdup(value);
}
/* LMCTF referee countdown and team lock survive the shared checkpoint */
static void test_lmctf_referee(void) {
  cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 2, NULL);
  qa_q2_lmctf *mode = active->composition->match.lmctf;
  qa_lmctf_rules *rules = qa_q2_lmctf_get_rules(mode);
  set_owned(&rules->ref_password, "match-ref");
  rules->countdown_seconds = 1; rules->time_limit_minutes = 1; rules->auto_lock = true;
  (void)lmctf_command(active, active->first, "startmatch", NULL);
  qa_lmctf_match *match = qa_q2_lmctf_get_match(mode);
  CHECK(strcmp(qa_lmctf_match_phase(match), "none") == 0);
  (void)lmctf_command(active, active->first, "referee", "match-ref");
  (void)lmctf_command(active, active->first, "startmatch", NULL);
  CHECK(strcmp(qa_lmctf_match_phase(match), "countdown") == 0);
  qa_lmctf_context *context = qa_q2_lmctf_get_context(mode);
  CHECK(!qa_lmctf_context_can_score(context));
  CHECK(!qa_lmctf_context_flags_touchable(context));
  CHECK(qa_lmctf_match_teams_locked(match));
  qa_q2_lmctf_join(mode, active->second, active->game, 1);
  CHECK(lmctf_state(active, active->second)->team == 2);
  active->now = 1;
  qa_q2_lmctf_player_frame(mode, active->first, active->game);
  cw *restored = compose(false, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 2, NULL);
  save_into(active, restored);
  qa_q2_lmctf *restored_mode = restored->composition->match.lmctf;
  qa_lmctf_match *restored_match = qa_q2_lmctf_get_match(restored_mode);
  qa_lmctf_match_checkpoint a = qa_lmctf_match_capture(match), b = qa_lmctf_match_capture(restored_match);
  CHECK(a.has_pending_map == b.has_pending_map && strcmp(a.phase, b.phase) == 0 && a.remaining == b.remaining &&
        a.next_think == b.next_think && a.paused == b.paused && a.teams_locked == b.teams_locked);
  const char *password = qa_q2_lmctf_get_rules(restored_mode)->ref_password;
  CHECK(password != NULL && strcmp(password, "") == 0);
  restored->now = 2;
  qa_q2_lmctf_player_frame(restored_mode, restored->first, restored->game);
  CHECK(strcmp(qa_lmctf_match_phase(restored_match), "inplay") == 0);
  CHECK(qa_lmctf_match_remaining(restored_match) == 59);
  CHECK(qa_lmctf_context_can_score(qa_q2_lmctf_get_context(restored_mode)));
  (void)lmctf_command(restored, restored->first, "stopmatch", NULL);
  CHECK(strcmp(qa_lmctf_match_phase(restored_match), "none") == 0);
  CHECK(!qa_lmctf_match_teams_locked(restored_match));
  release(active);
  release(restored);
}

/* LMCTF plasma retains signed native cell debits and infinite-ammo counters through saves */
static void test_lmctf_plasma(void) {
  static const struct { double cells; bool infinite; double expected; } scenarios[] = {
      {1, false, -9}, {1, true, -8}, {10, false, 0}, {10, true, 1}};
  for (size_t i = 0; i < sizeof scenarios / sizeof scenarios[0]; i++) {
    cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 2, NULL);
    qa_q2w_weapons *weapons = active->composition->weapons;
    qa_q2_product_runtime_set_deathmatch_flags(active->composition, scenarios[i].infinite ? 8192 : 0);
    configure(active, active->first, "q2:ammo_cells", scenarios[i].cells, 200);
    qa_q2w_state *state = qa_q2w_weapons_bind(weapons, active->first->actor, active->game, NULL);
    state->weapon = "lmctf:plasma"; state->phase = QA_Q2W_FIRING; state->frame = 4;
    qa_q2w_input input = zero_input();
    input.attack = true;
    qa_q2w_owner owner = {active->first->actor, active->first->view_height};
    qa_q2w_weapons_tick(weapons, owner, active->game, input);
    CHECK(qa_shared_inventory_table_count(active->inventory, active->first->actor.id, "q2:ammo_cells") == scenarios[i].expected);
    CHECK(state->frame == 5);
    CHECK(count_named(active, "goop") == 3);
    size_t n = 0;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(active->inventory, active->first->actor.id, &n);
    qa_inventory_entry entry;
    bool found = false;
    for (size_t j = 0; j < n; j++) if (strcmp(entries[j].item, "q2:ammo_cells") == 0) { entry = entries[j]; found = true; }
    CHECK(found && entry.capacity == 200 && entry.has_count_policy && strcmp(entry.count_policy.kind, "source-counter") == 0 &&
          strcmp(entry.count_policy.arithmetic, "int32") == 0);
    if (found) {
      configure(active, active->first, "q2:ammo_cells", 100, 200);
      qa_shared_inventory_table_configure(active->inventory, active->first->actor, entry, NULL, NULL);
      CHECK(qa_shared_inventory_table_count(active->inventory, active->first->actor.id, "q2:ammo_cells") == scenarios[i].expected);
    }
    active->now = 0.1;
    qa_q2w_weapons_tick(weapons, owner, active->game, input);
    CHECK(qa_shared_inventory_table_count(active->inventory, active->first->actor.id, "q2:ammo_cells") == scenarios[i].expected);
    CHECK(count_named(active, "goop") == 3);
    release(active);
  }
}

/* LMCTF native skip vote resumes ballots and strict deadline through a checkpoint */
static void test_lmctf_vote(void) {
  cw *too_few = compose(true, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 2, NULL);
  double started = -1;
  (void)lmctf_command(too_few, too_few->first, "lmctf-vote", "skip");
  CHECK(!qa_lmctf_vote_started_at(qa_q2_lmctf_get_vote(too_few->composition->match.lmctf), &started));
  release(too_few);
  cw *active = compose(true, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 4, NULL);
  qa_q2_lmctf *mode = active->composition->match.lmctf;
  (void)lmctf_command(active, active->first, "lmctf-vote", "skip");
  CHECK(qa_lmctf_vote_started_at(qa_q2_lmctf_get_vote(mode), &started) && started == 0);
  CHECK((qa_js_to_int32(lmctf_state(active, active->first)->extra_flags) & 192) == 64);
  (void)lmctf_command(active, active->second, "voteno", NULL);
  (void)lmctf_command(active, active->second, "voteyes", NULL);
  CHECK((qa_js_to_int32(lmctf_state(active, active->second)->extra_flags) & 192) == 64);
  cw *restored = compose(false, CTF_MAP, selection(QA_Q2_MATCH_LMCTF), 4, NULL);
  save_into(active, restored);
  qa_q2_lmctf *vote_mode = restored->composition->match.lmctf;
  CHECK(qa_lmctf_vote_started_at(qa_q2_lmctf_get_vote(vote_mode), &started) && started == 0);
  CHECK((qa_js_to_int32(lmctf_state(restored, restored->second)->extra_flags) & 192) == 64);
  restored->now = 30;
  qa_q2_lmctf_player_frame(vote_mode, restored->first, restored->game);
  CHECK(qa_lmctf_vote_started_at(qa_q2_lmctf_get_vote(vote_mode), &started) && started == 0);
  CHECK(strcmp(qa_q2_players_intermission(restored->composition->players).kind, "playing") == 0);
  restored->now = 30.1;
  qa_q2_lmctf_player_frame(vote_mode, restored->first, restored->game);
  CHECK(!qa_lmctf_vote_started_at(qa_q2_lmctf_get_vote(vote_mode), &started));
  CHECK(strcmp(qa_q2_players_intermission(restored->composition->players).kind, "intermission") == 0);
  release(active);
  release(restored);
}

/* DeathBall initializes required flags through the external owner and preserves its configured bits */
static void test_deathball_flags(void) {
  double flags = 16;
  compose_extra extra = {&flags, NULL};
  qa_q2_match_selection ball = selection(QA_Q2_MATCH_DEATHBALL);
  ball.team1_skin = "male/ctf_r"; ball.team2_skin = "male/ctf_b"; ball.goal_limit = 5;
  cw *active = compose(true, "{ \"classname\" \"worldspawn\" } { \"classname\" \"info_player_start\" } { \"classname\" \"dm_dball_ball_start\" }",
                       ball, 2, &extra);
  double required = 0x20000 | 0x80000 | 0x40000 | 256 | 64;
  CHECK(flags == (double)(16 | (int)required));
  CHECK(qa_q2_options_deathmatch_flags(&active->game->options) == (double)(16 | (int)required));
  CHECK(active->composition->has_movement_stop_speed && active->composition->movement_stop_speed == 0);
  release(active);
}

static bool set_cvar(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_set(cvars, name, value, false, &snapshot)) return false;
  qa_cvar_read_free(&snapshot);
  return true;
}
static qa_cvar_registry *item_cvars(qa_identity_owner *id, const char *name) {
  qa_identity_owner_create(id, name);
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = QA_COMMAND_DIALECT_Q2_RERELEASE;
  options.context.session = id->session;
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry *cvars = qa_cvar_registry_create(&options);
  qa_register_q2_server_cvars(cvars, "q2:tag");
  return cvars;
}

/* rerelease live random-item policy replaces the existing pickup at respawn */
static void test_random_items(void) {
  qa_identity_owner id;
  qa_cvar_registry *cvars = item_cvars(&id, "random-items");
  qa_q2_rerelease_item_state services;
  CHECK(qa_q2_rerelease_item_services(cvars, &services));
  compose_extra extra = {NULL, &services};
  cw *active = compose(true, PLAIN_MAP, selection(QA_Q2_MATCH_TAG), 2, &extra);
  qa_q2_entity *pickup = spawn(active, "ammo_shells", 9, NULL, NULL);
  qa_actor_id actor = pickup->actor.id;
  think(active, pickup);
  qa_q2_item_module_touch(active->composition->items, pickup, active->game, active->first->actor.id);
  active->now = 30;
  think(active, pickup);
  CHECK(strcmp(pickup->classname, "ammo_shells") == 0);
  CHECK(set_cvar(cvars, "g_dm_random_items", "1"));
  qa_q2_item_module_touch(active->composition->items, pickup, active->game, active->second->actor.id);
  active->now = 60;
  think(active, pickup);
  CHECK(qa_same_actor(pickup->actor.id, actor));
  CHECK(strcmp(pickup->classname, "ammo_shells") != 0);
  const qa_q2_item_definition *definition = qa_q2_item_module_item_definition(active->composition->items, pickup);
  CHECK(definition != NULL && strcmp(definition->classname, pickup->classname) == 0);
  release(active);
  qa_cvar_registry_destroy(cvars);
  qa_identity_owner_destroy(&id);
}

/* rerelease DualFire death drop preserves remaining duration and obeys live suppression */
static void test_dual_fire_drop(void) {
  for (int allow = 1; allow >= 0; allow--) {
    qa_identity_owner id;
    qa_cvar_registry *cvars = item_cvars(&id, "dual-fire-drop");
    CHECK(set_cvar(cvars, "g_dm_no_quadfire_drop", allow ? "0" : "1"));
    qa_q2_rerelease_item_state services;
    CHECK(qa_q2_rerelease_item_services(cvars, &services));
    compose_extra extra = {NULL, &services};
    cw *active = compose(true, PLAIN_MAP, selection(QA_Q2_MATCH_TAG), 2, &extra);
    state_of(active, active->first)->use_q2_weapons = true;
    configure(active, active->first, "q2:item_quadfire", 1, 2);
    (void)qa_q2_item_module_use(active->composition->items, active->first->actor, "q2:item_quadfire", active->game, 30);
    active->now = 10;
    (void)qa_q2_players_record_death(active->composition->players, active->first, active->game,
                                     killed_by(active, active->first, active->second, 110));
    qa_q2_entity *drop = find(active, "item_quadfire");
    if (!allow) {
      CHECK(drop == NULL);
    } else {
      CHECK(drop != NULL);
      if (drop != NULL) {
        CHECK(drop->has_next_think && drop->next_think == 30);
        CHECK((qa_js_to_int32(drop->spawnflags) & 0x20000) != 0);
        qa_q2_item_module_touch(active->composition->items, drop, active->game, active->second->actor.id);
        CHECK(qa_q2mp_items_powerups(&active->composition->armory->items, active->second->actor.id).quad_fire_until == 30);
        CHECK(qa_q2mp_items_powerups(&active->composition->armory->items, active->first->actor.id).quad_fire_until == 0);
      }
    }
    release(active);
    qa_cvar_registry_destroy(cvars);
    qa_identity_owner_destroy(&id);
  }
}

/* Horde match checkpoint restores wave progress */
static void test_horde_checkpoint(void) {
  cw *active = compose(true, PLAIN_MAP, selection(QA_Q2_MATCH_HORDE), 2, NULL);
  CHECK(active->composition->match.source_kind == QA_Q2_MATCH_HORDE);
  /* TS horde.wave = 7 (the C horde is opaque: set it through its checkpoint). */
  qa_q2_horde_checkpoint state;
  CHECK(qa_q2_horde_capture(active->composition->match.horde, &state));
  state.wave = 7;
  qa_q2_horde_restore(active->composition->match.horde, active->game, state);
  free(state.monsters);
  cw *restored = compose(false, PLAIN_MAP, selection(QA_Q2_MATCH_HORDE), 2, NULL);
  save_into(active, restored);
  CHECK(restored->composition->match.source_kind == QA_Q2_MATCH_HORDE);
  CHECK(qa_q2_horde_wave(restored->composition->match.horde) == 7);
  release(active);
  release(restored);
}

int main(void) {
  test_combined_rerelease();
  test_tag_scoring();
  test_ctf_admission();
  test_lmctf_admission();
  test_ctf_techs();
  test_lmctf_runes();
  test_ctf_menu();
  test_ctf_competition();
  test_lmctf_referee();
  test_lmctf_plasma();
  test_lmctf_vote();
  test_deathball_flags();
  test_random_items();
  test_dual_fire_drop();
  test_horde_checkpoint();
  CHECK(ctx_failures == 0);
  printf("test_q2_composition: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
