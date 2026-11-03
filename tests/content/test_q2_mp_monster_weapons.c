/*
 * qa_q2mp_projectiles_monster_weapons: TS passes Q2MissionPackProjectiles
 * wherever Q2MissionPackMonsterWeapons is expected (registerQ2MissionPack
 * Monsters / registerQ2RereleaseMonsters `weapons: armory.projectiles`), so
 * each leaf of the C view must be the projectile's own fire method.
 *
 * Expected values come from the same scenario run in TS under bun:
 * Q2Foundation (no modules) over SessionActorRegistry/SharedBodyTable/
 * GameplayAuthority/SharedInventoryTable, a trace stub that never hits (kind
 * q2, fraction 1), Q2Ballistics + new Q2MissionPackProjectiles({ base,
 * monster: () => null, playerEffect }), a monster_dummy at (10,20,30) with
 * bounds +-16/-24..32, start (10,20,40), direction (3,4,0) (heat rockets
 * (1,0,0)). Printed (classname model velocity damage damageRadius
 * radiusDamage accel effects owner-slot):
 *   ion       ion boomrang 300.00000000000006,400,0 15 100 0 0 64 0
 *   blue      bolt blaser  classic 600.0000000000001,800,0 / rerelease 3000,4000,0  20 0 0 0 8 0
 *   heat      rocket 500,0,0 50 120 50 0.075 16 0  (turnFraction omitted)
 *   heat2     rocket 500,0,0 50 120 50 0.25 16 0   (turnFraction 0.25)
 *   plasma    plasma s_photon 2175,2900,0 60 100 50 0 16785408 0
 *   blaster2  bolt laser2 (rerelease objects/laser) 600.0000000000001,800,0 7 128 0 0 67108872 0
 *   tracker   tracker disintegrator 600.0000000000001,800,0 20 0 0 0 67108864 0 (enemy null)
 *   flechette flechette 690.0000000000001,920,0 10 7 0 0 0 0
 *   heat beam: no host events.
 * Every callback checks its sentinel ctx. GPL-2.0-or-later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/runtime.h"
#include "qa/content/q2/missionpacks/projectiles.h"
#include "qa/world/gameplay/policies.h"

static int checks = 0, failures = 0;
#define CHECK(cond)                                                                   \
  do {                                                                                \
    checks++;                                                                         \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static int HOST_SENTINEL, LINK_SENTINEL, COMBAT_SENTINEL, POLICY_SENTINEL, ARMOR_SENTINEL, WEAPON_SENTINEL, PROJECTILE_SENTINEL;
static int ctx_failures = 0;
#define EXPECT_CTX(ctx, sentinel) do { if ((ctx) != (void *)&(sentinel)) { ctx_failures++; fprintf(stderr, "FAIL ctx %s:%d\n", __FILE__, __LINE__); } } while (0)

typedef struct world {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_combat_policy policy;
  qa_victim_armor_policy armor;
  qa_shared_inventory_table *inventory;
  qa_q2_foundation_host host;
  qa_q2_foundation *foundation;
  qa_q2_game_services *game;
  bool has_player; qa_actor_id player;
  char emits[8][48]; size_t emit_count;
} world;
static world *W;

static double h_now(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0; }
static double h_frame(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0.1; }
static double h_gravity(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 800; }
static double h_random(void *c) { EXPECT_CTX(c, HOST_SENTINEL); return 0.5; }
static void h_schedule(void *c, qa_owned_actor actor, const double *due) { EXPECT_CTX(c, HOST_SENTINEL); (void)actor; (void)due; }
static void h_touch_triggers(void *c, qa_owned_actor a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static qa_trace_result h_trace(void *c, qa_q2_trace_request r) {
  EXPECT_CTX(c, HOST_SENTINEL);
  qa_trace_result out;
  memset(&out, 0, sizeof out);
  out.kind = "q2"; out.fraction = 1; out.end = r.end; out.hit.kind = QA_TRACE_HIT_NONE;
  return out;
}
static double h_contents(void *c, qa_vec3 p) { EXPECT_CTX(c, HOST_SENTINEL); (void)p; return 0; }
static bool h_true2(void *c, qa_vec3 a, qa_vec3 b) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; (void)b; return true; }
static const qa_actor_id *h_nearby(void *c, qa_vec3 o, double r, size_t *n) { EXPECT_CTX(c, HOST_SENTINEL); (void)o; (void)r; *n = 0; return NULL; }
static const qa_actor_id *h_players(void *c, size_t *n) {
  EXPECT_CTX(c, HOST_SENTINEL);
  *n = W->has_player ? 1 : 0;
  return W->has_player ? &W->player : NULL;
}
static bool h_is_player(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); return W->has_player && qa_same_actor(a, W->player); }
static bool h_is_monster(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; return true; }
static qa_bounds h_inline_bounds(void *c, double m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; qa_bounds b; memset(&b, 0, sizeof b); return b; }
static void h_set_solid(void *c, qa_owned_actor a, const char *s, const double *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; (void)s; (void)m; }
static void h_set_motion(void *c, qa_q2_motion m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; }
static void h_portal(void *c, double p, bool o) { EXPECT_CTX(c, HOST_SENTINEL); (void)p; (void)o; }
static void h_emit(void *c, qa_q2_presentation_event e) {
  EXPECT_CTX(c, HOST_SENTINEL);
  if (e.kind != NULL && W->emit_count < 8) snprintf(W->emits[W->emit_count++], sizeof W->emits[0], "%s", e.kind);
}
static qa_q2_player_view_result h_view(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; qa_q2_player_view_result r; memset(&r, 0, sizeof r); r.has_value = true; return r; }
static void h_key(void *c, qa_actor_id a) { EXPECT_CTX(c, HOST_SENTINEL); (void)a; }
static void h_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; (void)l; (void)f; }
static void h_transition(void *c, qa_transition_intent t) { EXPECT_CTX(c, HOST_SENTINEL); (void)t; }
static void h_diagnostic(void *c, const char *m) { EXPECT_CTX(c, HOST_SENTINEL); (void)m; }

static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) { EXPECT_CTX(c, LINK_SENTINEL); return qa_translated_body_bounds(a, b); }
static void link_on(void *c, qa_linked_body b) { EXPECT_CTX(c, LINK_SENTINEL); (void)b; }
static void link_off(void *c, qa_actor_id a) { EXPECT_CTX(c, LINK_SENTINEL); (void)a; }
static void combat_impulse(void *c, qa_owned_actor a, qa_vec3 i, qa_provider_id m) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)a; (void)i; (void)m; }
static void combat_before(void *c, qa_owned_actor a, qa_damage_decision d) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)a; (void)d; }
static void combat_confirmed(void *c, qa_damage_outcome o) { EXPECT_CTX(c, COMBAT_SENTINEL); (void)o; }
static qa_q2_combat_context policy_context(void *c, qa_damage_request r, qa_combat_state t, const qa_combat_state *a) {
  EXPECT_CTX(c, POLICY_SENTINEL); (void)t; (void)a;
  qa_q2_combat_context out;
  memset(&out, 0, sizeof out);
  (void)r; out.arithmetic = "binary64"; out.monster = true; out.no_knockback = true;
  return out;
}
static qa_victim_armor_context armor_context(void *c, qa_damage_request r) {
  EXPECT_CTX(c, ARMOR_SENTINEL); (void)r;
  qa_victim_armor_context out;
  memset(&out, 0, sizeof out);
  out.screen_facing_dot = 1; out.arithmetic = "binary64";
  return out;
}

/* ---- weapon and projectile hooks ---- */
static void wh_emit(void *c, qa_q2w_event e) { EXPECT_CTX(c, WEAPON_SENTINEL); (void)e; }
static void wh_noise(void *c, qa_actor_id a, qa_vec3 o, bool s) { EXPECT_CTX(c, WEAPON_SENTINEL); (void)a; (void)o; (void)s; }
static void wh_dodge(void *c, qa_actor_id m, qa_actor_id a, double eta, qa_trace_result t) { EXPECT_CTX(c, WEAPON_SENTINEL); (void)m; (void)a; (void)eta; (void)t; }
static void wh_ammo(void *c, qa_actor_id a, qa_item_id i) { EXPECT_CTX(c, WEAPON_SENTINEL); (void)a; (void)i; }
static bool wh_can_target(void *c, const qa_actor_id *a, qa_actor_id t) { EXPECT_CTX(c, WEAPON_SENTINEL); (void)a; (void)t; return true; }
static struct qa_q2m_context *ph_monster(void *c, qa_actor_id a) { EXPECT_CTX(c, PROJECTILE_SENTINEL); (void)a; return NULL; }
static void ph_effect(void *c, qa_q2mp_player_effect e) { EXPECT_CTX(c, PROJECTILE_SENTINEL); (void)e; }

static void world_create(qa_q2_edition edition) {
  W = calloc(1, sizeof *W);
  qa_identity_owner_create(&W->identities, "q2-mw");
  W->actors = qa_session_actor_registry_create(&W->identities, 4096);
  qa_body_link_hooks link = {&LINK_SENTINEL, link_bounds, link_on, link_off};
  W->bodies = qa_shared_body_table_create(W->actors, link);
  W->callbacks = qa_actor_callback_table_create(W->actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = &COMBAT_SENTINEL; hooks.impulse = combat_impulse; hooks.before_reaction = combat_before; hooks.confirmed = combat_confirmed;
  W->combat = qa_gameplay_authority_create(W->actors, W->callbacks, hooks);
  W->inventory = qa_shared_inventory_table_create(W->actors);
  qa_q2_combat_policy_options policy;
  memset(&policy, 0, sizeof policy);
  policy.id = "q2:combat"; policy.ctx = &POLICY_SENTINEL; policy.context = policy_context;
  W->armor = policy.armor = qa_native_victim_armor(armor_context, &ARMOR_SENTINEL);
  W->policy = qa_create_q2_combat_policy(policy);
  (void)qa_gameplay_authority_register(W->combat, W->policy);
  qa_q2_foundation_host *h = &W->host;
  h->ctx = &HOST_SENTINEL;
  h->actors = W->actors; h->bodies = W->bodies; h->callbacks = W->callbacks; h->combat = W->combat; h->inventory = W->inventory;
  h->now = h_now; h->frame_seconds = h_frame; h->gravity = h_gravity; h->random = h_random; h->schedule = h_schedule;
  h->touch_triggers = h_touch_triggers; h->trace = h_trace; h->point_contents = h_contents; h->in_pvs = h_true2;
  h->in_phs = h_true2; h->areas_connected = h_true2; h->nearby = h_nearby; h->players = h_players;
  h->is_player = h_is_player; h->is_monster = h_is_monster; h->inline_model_bounds = h_inline_bounds;
  h->set_solid = h_set_solid; h->set_motion = h_set_motion; h->set_area_portal = h_portal; h->emit = h_emit;
  h->player_view_state = h_view; h->key_consumed = h_key; h->prepare_level_change = h_prepare;
  h->transition = h_transition; h->diagnostic = h_diagnostic;
  qa_q2_game_options options;
  memset(&options, 0, sizeof options);
  options.edition = edition; options.map_name = "base1"; options.skill = 1;
  options.mode = QA_Q2_MODE_SINGLEPLAYER; options.max_clients = 4; options.provider = "q2:official";
  options.campaign = "q2:base"; options.combat_provider = "q2:combat"; options.inventory_provider = "q2:inventory";
  options.movement_provider = "q1:movement";
  W->foundation = qa_q2_entity_services_create(&W->host, options, NULL, 0);
  W->game = qa_q2_entity_services_game(W->foundation);
}
static void world_destroy(void) {
  qa_q2_entity_services_destroy(W->foundation);
  qa_shared_inventory_table_destroy(W->inventory);
  qa_gameplay_authority_destroy(W->combat);
  qa_combat_policy_free(W->policy);
  qa_victim_armor_policy_free(W->armor);
  qa_actor_callback_table_destroy(W->callbacks);
  qa_shared_body_table_destroy(W->bodies);
  qa_session_actor_registry_close(W->actors);
  qa_session_actor_registry_destroy(W->actors);
  qa_identity_owner_destroy(&W->identities);
  free(W);
  W = NULL;
}

static bool bolt_is(qa_q2_entity *e, const char *classname, const char *model, double vx, double vy, double vz, double damage,
                    double damage_radius, double radius_damage, double accel, double effects) {
  if (e == NULL) return false;
  qa_body_state body = qa_q2_game_body(W->game, e);
  return strcmp(e->classname, classname) == 0 && strcmp(e->model, model) == 0 && body.velocity.x == vx &&
         body.velocity.y == vy && body.velocity.z == vz && e->damage == damage && e->damage_radius == damage_radius &&
         e->radius_damage == radius_damage && e->accel == accel && e->effects == effects && e->has_owner &&
         e->owner.slot == 0 && !e->has_enemy;
}

static void test_edition(qa_q2_edition edition) {
  world_create(edition);
  bool rerelease = edition == QA_Q2_EDITION_RERELEASE;
  qa_q2w_hooks wh;
  memset(&wh, 0, sizeof wh);
  wh.ctx = &WEAPON_SENTINEL; wh.emit = wh_emit; wh.noise = wh_noise; wh.dodge = wh_dodge; wh.ammo_changed = wh_ammo;
  wh.can_target = wh_can_target;
  qa_q2w_ballistics ballistics;
  CHECK(qa_q2w_ballistics_init(&ballistics, wh));
  qa_q2mp_projectile_hooks ph;
  memset(&ph, 0, sizeof ph);
  ph.base = &ballistics; ph.ctx = &PROJECTILE_SENTINEL; ph.monster = ph_monster; ph.player_effect = ph_effect;
  qa_q2mp_projectiles projectiles;
  qa_q2mp_projectiles_init(&projectiles, ph);
  qa_q2mpm_weapons weapons = qa_q2mp_projectiles_monster_weapons(&projectiles);
  CHECK(weapons.ctx == &projectiles);

  qa_q2_entity *self = W->game->create(W->game->ctx, "monster_dummy", NULL, 0);
  qa_q2_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_origin = true; changes.origin = (qa_vec3){10, 20, 30};
  changes.has_bounds = true; changes.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  W->game->move(W->game->ctx, self, changes, true);
  CHECK(self->actor.id.slot == 0);
  qa_vec3 start = {10, 20, 40}, dir = {3, 4, 0}, east = {1, 0, 0};
  const double v600 = 600.0000000000001, v300 = 300.00000000000006;
  CHECK(bolt_is(weapons.fire_ion_ripper(weapons.ctx, self, W->game, start, dir, 15, 500, 64),
                "ion", "models/objects/boomrang/tris.md2", v300, 400, 0, 15, 100, 0, 0, 64));
  CHECK(bolt_is(weapons.fire_blue_blaster(weapons.ctx, self, W->game, start, dir, 20, 1000, 8),
                "bolt", "models/objects/blaser/tris.md2", rerelease ? 3000 : v600, rerelease ? 4000 : 800, 0, 20, 0, 0, 0, 8));
  CHECK(bolt_is(weapons.fire_heat_rocket(weapons.ctx, self, W->game, start, east, 50, 500, 120, 50, false, 0),
                "rocket", "models/objects/rocket/tris.md2", 500, 0, 0, 50, 120, 50, 0.075, 16));
  CHECK(bolt_is(weapons.fire_heat_rocket(weapons.ctx, self, W->game, start, east, 50, 500, 120, 50, true, 0.25),
                "rocket", "models/objects/rocket/tris.md2", 500, 0, 0, 50, 120, 50, 0.25, 16));
  CHECK(bolt_is(weapons.fire_plasma(weapons.ctx, self, W->game, start, dir, 60, 725, 100, 50),
                "plasma", "sprites/s_photon.sp2", 2175, 2900, 0, 60, 100, 50, 0, 16785408));
  CHECK(bolt_is(weapons.fire_blaster2(weapons.ctx, self, W->game, start, dir, 7, 1000, 8),
                "bolt", rerelease ? "models/objects/laser/tris.md2" : "models/proj/laser2/tris.md2", v600, 800, 0, 7, 128, 0, 0,
                67108872));
  CHECK(bolt_is(weapons.fire_tracker(weapons.ctx, self, W->game, start, dir, 20, 1000, NULL),
                "tracker", "models/proj/disintegrator/tris.md2", v600, 800, 0, 20, 0, 0, 0, 67108864));
  CHECK(bolt_is(weapons.fire_flechette(weapons.ctx, self, W->game, start, dir, 10, 1150, 7),
                "flechette", "models/proj/flechette/tris.md2", 690.0000000000001, 920, 0, 10, 7, 0, 0, 0));
  W->emit_count = 0;
  weapons.fire_heat_beam(weapons.ctx, self, W->game, start, dir, (qa_vec3){0, 0, 0}, 15, 0);
  CHECK(W->emit_count == 0);
  /* Ballistics unsubscribe from the registry's releases, so they go first. */
  qa_q2w_ballistics_destroy(&ballistics);
  world_destroy();
}

int main(void) {
  test_edition(QA_Q2_EDITION_CLASSIC);
  test_edition(QA_Q2_EDITION_RERELEASE);
  CHECK(ctx_failures == 0);
  printf("test_q2_mp_monster_weapons: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
