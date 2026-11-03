/*
 * Port of key cases from tests/composition/ (q1-supply, q2-q3-supply,
 * q3-q2-supply profile data + q1Q2PickupSelect) and direct checks of
 * the expansion transforms, loadouts, Q1 source clients, and the Q1 source
 * composition (src/content/composition/q1/runtime.ts) over the real one Q1
 * game, ported from tests/composition/q1/source.test.ts where the case does
 * not need a corpus map. GPL-2.0-or-later.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "q1_world_fixture.h"
#include "qa/content/composition/expansion_source_supply.h"
#include "qa/content/composition/expansion_supply.h"
#include "qa/content/composition/q1/clients.h"
#include "qa/content/composition/q1/runtime.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/composition/q1_hipnotic_supply.h"
#include "qa/content/composition/q1_q2_supply.h"
#include "qa/content/composition/q1_q3_supply.h"
#include "qa/content/composition/q2_q1_supply.h"
#include "qa/content/composition/q2_q3_supply.h"
#include "qa/content/composition/q3_q1_supply.h"
#include "qa/content/composition/q3_q2_supply.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static const qa_pickup_supply_mapping *find_row(const qa_pickup_supply_mapping *rows, size_t count,
                                                const char *source) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(rows[i].source, source) == 0) return &rows[i];
  return NULL;
}
static bool row_has(const qa_pickup_supply_mapping *row, const char *destination) {
  if (row == NULL) return false;
  for (size_t i = 0; i < row->destination_count; i++)
    if (strcmp(row->destinations[i], destination) == 0) return true;
  return false;
}
static bool owners_has(const qa_pickup_supply_profile *profile, bool ammo, const char *item, const char *source) {
  const qa_pickup_supply_owner *owners = ammo ? profile->ammo_owners : profile->weapon_owners;
  size_t count = ammo ? profile->ammo_owner_count : profile->weapon_owner_count;
  if (owners == NULL) return false;
  for (size_t i = 0; i < count; i++)
    if (strcmp(owners[i].item, item) == 0 && strcmp(owners[i].source, source) == 0) return true;
  return false;
}

/* tests/composition/q1-supply.test.ts: pickup rank cases. */
static void test_pickup_select(void) {
  CHECK(!qa_composition_q1_q2_pickup_select("q2:weapon_hyperblaster", "q2:weapon_shotgun", QA_PICKUP_SELECTION_BETTER));
  CHECK(qa_composition_q1_q2_pickup_select("q2:weapon_shotgun", "q2:weapon_hyperblaster", QA_PICKUP_SELECTION_BETTER));
  CHECK(!qa_composition_q1_q2_pickup_select("q2:weapon_shotgun", "q2:weapon_shotgun", QA_PICKUP_SELECTION_BETTER));
  CHECK(qa_composition_q1_q2_pickup_select("q2:weapon_hyperblaster", "q2:weapon_shotgun", QA_PICKUP_SELECTION_ALWAYS));
  CHECK(!qa_composition_q1_q2_pickup_select("q2:weapon_shotgun", "q2:weapon_hyperblaster", QA_PICKUP_SELECTION_NEVER));
  CHECK(qa_composition_q1_q2_pickup_select(NULL, "q2:weapon_blaster", QA_PICKUP_SELECTION_BETTER));
  CHECK_ABORTS((void)qa_composition_q1_q2_pickup_select(NULL, "q2:weapon_boomer", QA_PICKUP_SELECTION_BETTER));
  CHECK_ABORTS((void)qa_composition_q1_q2_pickup_select("q3:weapon/shotgun", "q2:weapon_shotgun", QA_PICKUP_SELECTION_BETTER));
}

static void test_profiles(void) {
  const qa_pickup_supply_profile *p = &QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q1-base-q2-supply") == 0);
  CHECK(p->ammo_count == 4 && p->weapon_count == 8 && p->weapon_owners == NULL && p->ammo_owners == NULL);
  const qa_pickup_supply_mapping *row = find_row(p->ammo, p->ammo_count, "q1:ammo/rockets");
  CHECK(row != NULL && row->destination_count == 2 && row_has(row, "q2:ammo_rockets") && row_has(row, "q2:ammo_grenades"));
  /* TS test: every Q1_Q2 weapon destination is a base Q2 weapon. */
  for (size_t i = 0; i < p->weapon_count; i++)
    for (size_t d = 0; d < p->weapons[i].destination_count; d++)
      CHECK(qa_composition_q1_q2_pickup_select(NULL, p->weapons[i].destinations[d], QA_PICKUP_SELECTION_BETTER));

  p = &QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q2-base-q1-supply") == 0);
  CHECK(p->weapon_owner_count == 8 && owners_has(p, false, "q1:weapon/lightning", "q2:weapon_hyperblaster"));
  row = find_row(p->ammo, p->ammo_count, "q2:ammo_cells");
  CHECK(row_has(row, "q2:ammo_cells") && row_has(row, "q1:ammo/cells"));

  p = &QA_COMPOSITION_Q2_HIPNOTIC_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q2-hipnotic-supply") == 0);
  CHECK(p->weapon_owner_count == 11 && owners_has(p, false, "q1:weapon/hipnotic:mjolnir", "q2:weapon_bfg"));
  row = find_row(p->weapons, p->weapon_count, "q2:weapon_grenadelauncher");
  CHECK(row_has(row, "q1:weapon/hipnotic:proximity"));

  p = &QA_COMPOSITION_Q1_HIPNOTIC_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q1-hipnotic-supply") == 0);
  row = find_row(p->ammo, p->ammo_count, "q1:ammo/cells");
  CHECK(row != NULL && row->destination_count == 1 && row_has(row, "q1:ammo/cells"));
  row = find_row(p->weapons, p->weapon_count, "q1:weapon/lightning");
  CHECK(row != NULL && row->destination_count == 3 && row_has(row, "q1:weapon/hipnotic:laser"));

  p = &QA_COMPOSITION_Q2_Q3_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q2-base-q3-supply") == 0);
  row = find_row(p->ammo, p->ammo_count, "q2:ammo_grenades");
  CHECK(row_has(row, "q2:ammo_grenades") && row_has(row, "q3:ammo/grenadelauncher"));
  row = find_row(p->weapons, p->weapon_count, "q2:weapon_hyperblaster");
  CHECK(row_has(row, "q3:weapon/plasmagun") && row_has(row, "q3:weapon/lightning"));

  p = &QA_COMPOSITION_Q3_Q2_SUPPLY_PROFILE;
  CHECK(p->ammo_owner_count == 6 && owners_has(p, true, "q2:ammo_slugs", "q3:ammo/railgun"));
  row = find_row(p->weapons, p->weapon_count, "q3:weapon/machinegun");
  CHECK(row_has(row, "q2:weapon_machinegun") && row_has(row, "q2:weapon_chaingun"));

  p = &QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE;
  CHECK(p->ammo_owner_count == 4 && owners_has(p, true, "q1:ammo/cells", "q3:ammo/lightning"));
  p = &QA_COMPOSITION_Q3_HIPNOTIC_SUPPLY_PROFILE;
  row = find_row(p->weapons, p->weapon_count, "q3:weapon/bfg");
  CHECK(row_has(row, "q1:weapon/hipnotic:mjolnir"));

  p = &QA_COMPOSITION_Q1_Q3_SUPPLY_PROFILE;
  CHECK(strcmp(p->id, "composition:q1-base-q3-supply") == 0 && p->weapon_count == 8);

  qa_composition_supply_loadout loadout = qa_composition_q1_q2_supply_loadout();
  CHECK(strcmp(loadout.weapon, "q2:weapon_shotgun") == 0 && loadout.inventory_count == 3);
  CHECK(strcmp(loadout.inventory[2].item, "q2:ammo_shells") == 0 && loadout.inventory[2].count == 25);
  loadout = qa_composition_q3_q2_supply_loadout();
  CHECK(strcmp(loadout.weapon, "q2:weapon_machinegun") == 0 && loadout.inventory_count == 4);
  CHECK(loadout.inventory[3].count == 100);
}

static qa_inventory_entry SPAWN_AMMO[] = {
    {"q3:weapon/gauntlet", 5, 1, false, {0}}, {"q3:weapon/shotgun", 0, 1, false, {0}},
    {"q3:ammo/shotgun", 7, 200, false, {0}}, {"q3:ammo/machinegun", 9, 200, false, {0}}};
static qa_arsenal_state fake_spawn(void *ctx, qa_provider_id provider, const char *product, bool mission_pack) {
  (void)ctx;
  (void)mission_pack;
  qa_arsenal_state state = {0};
  state.provider = provider;
  state.active_weapon = "q3:weapon/machinegun";
  state.state.kind = strcmp(product, "foreign") == 0 ? "q1" : "q3";
  state.ammo = SPAWN_AMMO;
  state.ammo_count = 4;
  return state;
}

static void test_q1_q3_loadout(void) {
  qa_inventory_entry ammo[4];
  qa_arsenal_state out;
  CHECK(qa_composition_q1_q3_supply_loadout("q3:game", NULL, fake_spawn, NULL, ammo, 4, &out));
  CHECK(strcmp(out.active_weapon, "q3:weapon/shotgun") == 0);
  CHECK(out.state.source_weapon == 3);
  CHECK(ammo[0].count == 1 && ammo[1].count == 1 && ammo[2].count == 25 && ammo[3].count == 0);
  CHECK(!qa_composition_q1_q3_supply_loadout("q3:game", NULL, fake_spawn, NULL, ammo, 3, &out));
  CHECK_ABORTS((void)qa_composition_q1_q3_supply_loadout("q3:game", "foreign", fake_spawn, NULL, ammo, 4, &out));
}

static void test_expansions(void) {
  qa_composition_owned_profile owned;
  qa_composition_expansion_supply_kind rogue[] = {QA_COMPOSITION_EXPANSION_Q2_ROGUE};
  CHECK(qa_composition_expansion_supply(&QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE, rogue, 1, &owned));
  qa_pickup_supply_profile view = qa_composition_owned_profile_view(&owned);
  CHECK(strcmp(view.id, "composition:q1-base-q2-supply/q2-rogue") == 0);
  CHECK(view.weapon_owners == NULL && view.ammo_owners == NULL);
  const qa_pickup_supply_mapping *row = find_row(view.weapons, view.weapon_count, "q1:weapon/nailgun");
  CHECK(row_has(row, "q2:weapon_machinegun") && row_has(row, "q2:weapon_etf_rifle"));
  row = find_row(view.ammo, view.ammo_count, "q1:ammo/nails");
  CHECK(row_has(row, "q2:ammo_bullets") && row_has(row, "q2:ammo_flechettes"));
  /* Rockets: first stays q2:ammo_rockets; grenades expand to prox/tesla. */
  row = find_row(view.ammo, view.ammo_count, "q1:ammo/rockets");
  CHECK(row != NULL && strcmp(row->destinations[0], "q2:ammo_rockets") == 0 && row_has(row, "q2:ammo_prox") &&
        row_has(row, "q2:ammo_tesla"));
  qa_composition_owned_profile_destroy(&owned);

  qa_composition_expansion_supply_kind q1rogue[] = {QA_COMPOSITION_EXPANSION_Q1_ROGUE};
  CHECK(qa_composition_expansion_supply(&QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE, q1rogue, 1, &owned));
  view = qa_composition_owned_profile_view(&owned);
  CHECK(view.weapon_owners != NULL && view.ammo_owners == NULL);
  CHECK(owners_has(&view, false, "q1:weapon/rogue:plasma", "q2:weapon_hyperblaster"));
  CHECK(owners_has(&view, false, "q1:weapon/lightning", "q2:weapon_hyperblaster"));
  qa_composition_owned_profile_destroy(&owned);

  qa_composition_expansion_supply_kind mp[] = {QA_COMPOSITION_EXPANSION_Q3_MISSIONPACK};
  CHECK(qa_composition_expansion_supply(&QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE, mp, 1, &owned));
  view = qa_composition_owned_profile_view(&owned);
  CHECK(owners_has(&view, true, "q3:ammo/nailgun", "q3:ammo/machinegun"));
  CHECK(view.ammo_owner_count == 7);
  qa_composition_owned_profile_destroy(&owned);

  /* Empty expansion list still rewrites the id with a trailing slash. */
  CHECK(qa_composition_expansion_supply(&QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE, NULL, 0, &owned));
  CHECK(strcmp(owned.id, "composition:q1-base-q2-supply/") == 0);
  qa_composition_owned_profile_destroy(&owned);

  CHECK(qa_composition_expansion_source_supply(&QA_COMPOSITION_Q2_Q1_SUPPLY_PROFILE, &owned));
  view = qa_composition_owned_profile_view(&owned);
  CHECK(strcmp(view.id, "composition:q2-base-q1-supply/expansion-sources") == 0);
  row = find_row(view.ammo, view.ammo_count, "q2:ammo_magslug");
  CHECK(row_has(row, "q1:ammo/nails"));
  row = find_row(view.weapons, view.weapon_count, "q2:weapon_phalanx");
  CHECK(row_has(row, "q1:weapon/supernailgun"));
  CHECK(owners_has(&view, true, "q1:ammo/shells", "q2:ammo_shells"));
  /* q1:ammo/nails is fed by bullets and slugs: no unique owner. */
  CHECK(!owners_has(&view, true, "q1:ammo/nails", "q2:ammo_bullets"));
  CHECK(!owners_has(&view, true, "q1:ammo/nails", "q2:ammo_slugs"));
  qa_composition_owned_profile_destroy(&owned);
}

/* --- Q1 source composition over the real one Q1 game ------------------------
 * A real qa_q1_services on the shared world tables (q1_world_fixture.h
 * pieces), built by qa_comp_create_q1_source_composition as TS
 * createQ1SourceComposition does. The world mirrors the TS sourceWorld()
 * harness of tests/composition/q1/source.test.ts, except that the map is a
 * synthetic entity list over the fixture's open world (worldspawn, an
 * info_player_start, two info_player_deathmatch, an info_player_coop, the two
 * CTF team starts) instead of a corpus BSP. Expected values come from the TS
 * Q1SourceComposition run over the same world under bun (scratchpad
 * q1comp_oracle.ts). Every services, campaign and host callback checks its
 * sentinel ctx. */
#define COMP_WORLD_MAGIC 0x434f4d50u
#define COMP_CAMPAIGN_MAGIC 0x43414d50u

typedef struct comp_campaign {
  unsigned magic;
  double flags;
  int skill;
} comp_campaign;

typedef struct comp_world {
  q1_world_fixture fx; /* first member: the host and gameplay ctx */
  unsigned magic;      /* services ctx sentinel */
  int ctx_errors;
  bool ready;
  qa_comp_q1_services services;
  qa_comp_q1_source_composition comp;
  comp_campaign campaign;
  struct { char name[32]; double value; } cvars[16];
  size_t cvar_count;
  char messages[16][128];
  size_t message_count;
  int event_counts[QA_COMP_Q1_EVENT_LEVEL_PRESENTATION + 1];
  int64_t last_left_slot;
  int restarts, places, disconnects, finishes;
  char restart_map[32];
  double restart_flags;
  char place_classname[48];
  qa_actor_id bots[4];
  size_t bot_count;
  qa_q1_entity entities[8];
  qa_q1_entity_property properties[8][2];
  size_t entity_count;
} comp_world;

static comp_world *world_of(void *ctx) {
  comp_world *w = ctx;
  if (w == NULL || w->magic != COMP_WORLD_MAGIC) {
    fprintf(stderr, "composition services received a foreign ctx\n");
    abort();
  }
  return w;
}
static comp_campaign *campaign_of(void *ctx) {
  comp_campaign *campaign = ctx;
  if (campaign == NULL || campaign->magic != COMP_CAMPAIGN_MAGIC) {
    fprintf(stderr, "campaign binding received a foreign ctx\n");
    abort();
  }
  return campaign;
}

static double *cvar_slot(comp_world *w, const char *name) {
  for (size_t i = 0; i < w->cvar_count; i++)
    if (strcmp(w->cvars[i].name, name) == 0) return &w->cvars[i].value;
  if (w->cvar_count == sizeof w->cvars / sizeof w->cvars[0]) abort();
  (void)snprintf(w->cvars[w->cvar_count].name, sizeof w->cvars[0].name, "%s", name);
  w->cvars[w->cvar_count].value = 0;
  return &w->cvars[w->cvar_count++].value;
}

/* Q1CampaignState() over the selection binding. */
static double cc_read_flags(void *ctx) { return campaign_of(ctx)->flags; }
static void cc_write_flags(void *ctx, double flags) { campaign_of(ctx)->flags = flags; }
static void cc_set_skill(void *ctx, int skill) { campaign_of(ctx)->skill = skill; }

/* Q1CompositionServices (TS sourceWorld services). */
static double cs_cvar(void *ctx, const char *name) {
  comp_world *w = world_of(ctx);
  for (size_t i = 0; i < w->cvar_count; i++)
    if (strcmp(w->cvars[i].name, name) == 0) return w->cvars[i].value;
  return 0;
}
static void cs_set_cvar(void *ctx, const char *name, const char *value) {
  *cvar_slot(world_of(ctx), name) = qa_js_string_to_number(value);
}
static void cs_emit(void *ctx, const qa_comp_q1_event *event) {
  comp_world *w = world_of(ctx);
  w->event_counts[event->kind]++;
  if (event->kind == QA_COMP_Q1_EVENT_CLIENT_LEFT) w->last_left_slot = event->slot;
}
static bool is_bot(comp_world *w, qa_actor_id actor) {
  for (size_t i = 0; i < w->bot_count; i++)
    if (qa_same_actor(w->bots[i], actor)) return true;
  return false;
}
static qa_comp_q1_selected_player cs_selected_player(void *ctx, qa_actor_id actor) {
  comp_world *w = world_of(ctx);
  qa_q1_services *game = w->fx.game;
  qa_comp_q1_selected_player selected;
  memset(&selected, 0, sizeof selected);
  selected.dead_flag = qa_q1_health(game, actor) > 0 ? 0 : 2;
  selected.is_bot = is_bot(w, actor);
  const qa_body_state_result body = qa_shared_body_table_read(w->fx.bodies, actor);
  selected.view_angles = body.has_value ? body.value.angles : QA_Q1_ZERO;
  selected.view_offset = (qa_vec3){0, 0, 26};
  selected.water_type = "empty";
  const qa_q1_player_state *player = qa_q1_player(game, actor);
  selected.teleport_until = player != NULL ? player->teleport_until : 0;
  return selected;
}
static void cs_set_observer(void *ctx, qa_actor_id actor, bool enabled) {
  comp_world *w = world_of(ctx);
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(w->fx.actors, actor, &owner)) abort();
  qa_combat_trait_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_can_take_damage = true;
  changes.can_take_damage = !enabled;
  qa_gameplay_authority_set_traits(w->fx.combat, owner, changes);
}
static void cs_place_player(void *ctx, qa_owned_actor actor, qa_q1_actor *spot, const qa_q1_travel_state *travel) {
  comp_world *w = world_of(ctx);
  const qa_body_state_result body = qa_shared_body_table_read(w->fx.bodies, actor.id);
  if (!body.has_value) abort();
  qa_body_state next = body.value;
  next.origin = qa_q1_get_body(w->fx.game, spot).origin;
  next.bounds = QA_Q1_PLAYER_BOUNDS;
  next.velocity = QA_Q1_ZERO;
  qa_shared_body_table_write(w->fx.bodies, actor, next);
  qa_combat_trait_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_can_take_damage = true;
  changes.can_take_damage = true;
  qa_gameplay_authority_set_traits(w->fx.combat, actor, changes);
  w->places++;
  (void)snprintf(w->place_classname, sizeof w->place_classname, "%s", spot->classname);
  qa_comp_q1_source_composition_admit_travel(&w->comp, actor, travel);
}
static void cs_disconnect(void *ctx, qa_actor_id actor) {
  comp_world *w = world_of(ctx);
  qa_owned_actor owner;
  if (qa_session_actor_registry_resolve_owned(w->fx.actors, actor, &owner))
    qa_session_actor_registry_release(w->fx.actors, owner);
  for (size_t i = 0; i < w->fx.player_count; i++)
    if (qa_same_actor(w->fx.players[i], actor)) {
      memmove(&w->fx.players[i], &w->fx.players[i + 1], (w->fx.player_count - i - 1) * sizeof w->fx.players[0]);
      w->fx.player_count--;
      break;
    }
  w->disconnects++;
}
static void cs_teleport(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until) {
  (void)actor; (void)origin; (void)angles; (void)velocity; (void)until;
  (void)world_of(ctx);
}
static qa_item_id cs_selected_weapon(void *ctx, qa_actor_id actor) {
  comp_world *w = world_of(ctx);
  const qa_q1_player_state *player = qa_q1_player(w->fx.game, actor);
  return player == NULL ? NULL : qa_q1_services_weapon_item(w->fx.game, player->weapon);
}
static qa_item_id cs_selected_ammo(void *ctx, qa_actor_id actor) {
  comp_world *w = world_of(ctx);
  const qa_q1_player_state *player = qa_q1_player(w->fx.game, actor);
  return player == NULL ? NULL : qa_q1_services_weapon_ammo(w->fx.game, player->weapon);
}
static bool cs_select_weapon(void *ctx, qa_actor_id actor, qa_item_id item) {
  comp_world *w = world_of(ctx);
  qa_q1_player_state *player = qa_q1_player(w->fx.game, actor);
  for (size_t i = 0; i < sizeof QA_Q1_WEAPON_IDS / sizeof QA_Q1_WEAPON_IDS[0]; i++)
    if (strcmp(qa_q1_services_weapon_item(w->fx.game, QA_Q1_WEAPON_IDS[i]), item) == 0)
      return player != NULL && qa_q1_select_weapon(w->fx.game, player->actor, QA_Q1_WEAPON_IDS[i]);
  return false;
}
static void cs_weapon_changed(void *ctx, qa_actor_id actor, qa_item_id acquired) {
  (void)actor; (void)acquired;
  (void)world_of(ctx);
}
static bool cs_prompt_supported(void *ctx, qa_actor_id actor) { (void)actor; (void)world_of(ctx); return true; }
static void cs_restart_session(void *ctx, const char *map, double flags) {
  comp_world *w = world_of(ctx);
  w->restarts++;
  (void)snprintf(w->restart_map, sizeof w->restart_map, "%s", map);
  w->restart_flags = flags;
}
static void cs_finish_campaign(void *ctx) { world_of(ctx)->finishes++; }

/* Host emit: records message texts with their arguments ("text|arg|arg"),
 * then the fixture double records the event. */
static void cw_emit(void *ctx, qa_q1_event event) {
  comp_world *world = (comp_world *)q1_world_fixture_of(ctx);
  if (world->magic != COMP_WORLD_MAGIC) world->ctx_errors++;
  if (event.kind != NULL && strcmp(event.kind, "message") == 0 &&
      world->message_count < sizeof world->messages / sizeof world->messages[0]) {
    char *out = world->messages[world->message_count++];
    size_t used = (size_t)snprintf(out, sizeof world->messages[0], "%s", event.text != NULL ? event.text : "");
    for (size_t i = 0; i < event.arg_count && used < sizeof world->messages[0]; i++) {
      const qa_q1_msg_arg *arg = &event.args[i];
      used += (size_t)snprintf(out + used, sizeof world->messages[0] - used, "|%s",
                               arg->is_number ? QA_JS_NUMBER(arg->number) : arg->text);
    }
  }
  q1_wf_emit(ctx, event);
}
/* GameplayAuthority.beforeReaction: composed.beforeReaction(actor, decision). */
static void cw_before_reaction(void *ctx, qa_owned_actor actor, qa_damage_decision decision) {
  comp_world *w = (comp_world *)q1_world_fixture_of(ctx);
  if (w->magic != COMP_WORLD_MAGIC) w->ctx_errors++;
  if (w->ready) qa_comp_q1_source_composition_before_reaction(&w->comp, actor, &decision);
}
/* context: { ...game.combatContext(request), teamplay: services.cvar("teamplay") } */
static qa_q1_combat_context cw_combat_context(void *ctx, qa_damage_request request, qa_combat_state target,
                                              const qa_combat_state *attacker) {
  (void)target; (void)attacker;
  comp_world *w = (comp_world *)q1_world_fixture_of(ctx);
  qa_q1_combat_context context = qa_q1_combat_context_for(w->fx.game, request);
  context.teamplay = cs_cvar(w, "teamplay");
  return context;
}

static void add_entity(comp_world *w, const char *classname, const char *key, const char *value) {
  const size_t i = w->entity_count++;
  w->properties[i][0] = (qa_q1_entity_property){(char *)"classname", (char *)classname};
  w->properties[i][1] = (qa_q1_entity_property){(char *)key, (char *)value};
  w->entities[i] = (qa_q1_entity){w->properties[i], 2};
}

typedef struct world_options {
  qa_comp_q1_program program;
  bool horde, coop;
  double deathmatch;
} world_options;

static void world_create(comp_world *w, world_options o) {
  memset(w, 0, sizeof *w);
  w->magic = COMP_WORLD_MAGIC;
  q1_world_fixture *fx = &w->fx;
  fx->magic = Q1_WORLD_FIXTURE_MAGIC;
  fx->random_value = 0.4;
  if (!qa_identity_owner_create(&fx->identity, "q1-composition-check")) abort();
  fx->actors = qa_session_actor_registry_create(&fx->identity, 4096);
  const qa_body_link_hooks link = {fx, q1_wf_absolute_bounds, q1_wf_on_link, q1_wf_on_unlink};
  fx->bodies = qa_shared_body_table_create(fx->actors, link);
  fx->callbacks = qa_actor_callback_table_create(fx->actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = fx;
  hooks.damage_allowed = q1_wf_damage_allowed;
  hooks.impulse = q1_wf_impulse;
  hooks.before_reaction = cw_before_reaction;
  hooks.confirmed = q1_wf_confirmed;
  fx->combat = qa_gameplay_authority_create(fx->actors, fx->callbacks, hooks);
  fx->inventory = qa_shared_inventory_table_create(fx->actors);
  if (fx->bodies == NULL || fx->callbacks == NULL || fx->combat == NULL || fx->inventory == NULL) abort();
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
  host.emit = cw_emit;
  host.transition = q1_wf_transition;
  host.players = q1_wf_players;
  host.check_client = q1_wf_check_client;
  host.classname = q1_wf_classname;
  host.powerup = q1_wf_powerup;
  *cvar_slot(w, "teamplay") = o.program == QA_COMP_Q1_CTF ? 130 : 0;
  *cvar_slot(w, "horde") = o.horde ? 1 : 0;
  *cvar_slot(w, "skill") = 1;
  *cvar_slot(w, "sv_gravity") = 800;
  qa_comp_q1_services *services = &w->services;
  services->ctx = w;
  services->cvar = cs_cvar;
  services->set_cvar = cs_set_cvar;
  services->emit = cs_emit;
  services->selected_player = cs_selected_player;
  services->set_observer = cs_set_observer;
  services->place_player = cs_place_player;
  services->disconnect = cs_disconnect;
  services->teleport = cs_teleport;
  services->selected_weapon = cs_selected_weapon;
  services->selected_ammo = cs_selected_ammo;
  services->select_weapon = cs_select_weapon;
  services->weapon_changed = cs_weapon_changed;
  services->prompt_supported = cs_prompt_supported;
  services->restart_session = cs_restart_session;
  services->finish_campaign = cs_finish_campaign;
  w->campaign.magic = COMP_CAMPAIGN_MAGIC;
  qa_comp_q1_source_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.program = o.program;
  selection.campaign = (qa_q1_campaign_binding){&w->campaign, cc_read_flags, cc_write_flags, cc_set_skill};
  selection.has_horde = true;
  selection.horde = o.horde;
  selection.registered = true;
  selection.official_campaign = true;
  static char campaign_names[7][16];
  char *campaign_name = campaign_names[o.program];
  (void)snprintf(campaign_name, sizeof campaign_names[0], "q1:%s", qa_comp_q1_program_name(o.program));
  qa_q1_options options = q1_world_fixture_options();
  options.coop = o.coop;
  options.deathmatch = o.program == QA_COMP_Q1_CTF ? 1 : o.deathmatch;
  options.campaign = campaign_name;
  options.skill = 3;     /* replaced by the cvar getter */
  options.gravity = 100; /* replaced by the cvar getter */
  CHECK(qa_comp_create_q1_source_composition(&w->comp, host, &options, &selection, services));
  fx->game = w->comp.game;
  w->ready = true;
  qa_q1_combat_policy_options policy;
  memset(&policy, 0, sizeof policy);
  policy.id = "q1:combat";
  policy.ctx = fx;
  policy.context = cw_combat_context;
  policy.armor = qa_native_victim_armor(q1_wf_armor_context, fx);
  fx->armor_ctx = policy.armor.ctx;
  policy.has_source_effects = true;
  policy.source_effects = qa_q1_damage_source_effects_of(fx->game);
  const qa_combat_policy registered = qa_create_q1_combat_policy(policy);
  fx->policy_ctx = registered.ctx;
  (void)qa_gameplay_authority_register(fx->combat, registered);
  add_entity(w, "worldspawn", "model", "*0");
  add_entity(w, "info_player_start", "origin", "64 32 24");
  add_entity(w, "info_player_deathmatch", "origin", "-400 32 24");
  add_entity(w, "info_player_deathmatch", "origin", "400 -32 24");
  add_entity(w, "info_player_coop", "origin", "128 0 24");
  if (o.program == QA_COMP_Q1_CTF) {
    add_entity(w, "info_player_team1", "origin", "200 0 24");
    add_entity(w, "info_player_team2", "origin", "-200 0 24");
  }
}

static size_t world_spawn(comp_world *w) {
  const qa_bounds world_bounds = {{-512, -512, -512}, {512, 512, 512}};
  qa_q1_spawn_report report;
  CHECK(qa_comp_q1_source_composition_spawn_entities(&w->comp, w->entities, w->entity_count, &world_bounds, 1, "e1m1",
                                                     NULL, 0, &report));
  const size_t spawned = report.spawned_count;
  qa_q1_spawn_report_free(&report);
  return spawned;
}

static void world_destroy(comp_world *w) {
  CHECK(w->ctx_errors == 0);
  CHECK(w->fx.ctx_errors == 0);
  w->ready = false;
  qa_comp_q1_source_composition_destroy(&w->comp);
  w->fx.game = NULL;
  q1_world_fixture_destroy(&w->fx);
}

/* TS admit(name, color, autoSwitch?). */
static qa_owned_actor admit(comp_world *w, const char *name, int color, const char *auto_switch) {
  q1_world_fixture *fx = &w->fx;
  const size_t slot = fx->player_count;
  const qa_owned_actor actor =
      qa_session_actor_registry_allocate_at_source(fx->actors, "q1:official", (int64_t)slot + 1, "q3:sarge");
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.bounds = QA_Q1_PLAYER_BOUNDS;
  qa_shared_body_table_create_body(fx->bodies, actor, body);
  qa_combat_state combat;
  memset(&combat, 0, sizeof combat);
  combat.health = 100;
  combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  combat.armor.powered.kind = "none";
  combat.mass = 100;
  combat.can_take_damage = true;
  qa_gameplay_authority_create_state(fx->combat, actor, combat, NULL, NULL);
  qa_shared_inventory_table_create_entries(fx->inventory, actor, NULL, 0);
  fx->players[fx->player_count++] = actor.id;
  char color_text[16];
  (void)snprintf(color_text, sizeof color_text, "%d", color);
  const qa_comp_userinfo_entry info[] = {{"name", name}, {"topcolor", color_text}, {"bottomcolor", color_text},
                                         {"qts_weapon_autoswitch", auto_switch}};
  const qa_comp_q1_client_admission admission = {(int64_t)slot, info, auto_switch != NULL ? 4u : 3u};
  CHECK(qa_comp_q1_source_composition_attach(&w->comp, actor, &admission));
  (void)qa_q1_attach_player(fx->game, actor, NULL, true, NULL);
  qa_q1_travel_state travel = qa_comp_q1_source_composition_new_travel(&w->comp);
  qa_comp_q1_source_composition_admit_travel(&w->comp, actor, &travel);
  qa_q1_travel_state_destroy(&travel);
  qa_comp_q1_source_composition_spawned(&w->comp, actor.id, true, NULL);
  qa_q1_actor *point = qa_comp_q1_source_composition_select_spawn(&w->comp, &actor.id, false);
  if (point != NULL) {
    const qa_body_state_result current = qa_shared_body_table_read(fx->bodies, actor.id);
    if (current.has_value) {
      qa_body_state next = current.value;
      next.origin = qa_q1_get_body(fx->game, point).origin;
      qa_shared_body_table_write(fx->bodies, actor, next);
    }
  }
  return actor;
}
static void impulse_input(comp_world *w, qa_actor_id actor, double impulse) {
  const qa_comp_q1_source_input input = {false, false, false, impulse};
  qa_comp_q1_source_composition_input(&w->comp, actor, input);
}
static bool origin_is(comp_world *w, qa_actor_id actor, double x, double y, double z) {
  const qa_body_state_result body = qa_shared_body_table_read(w->fx.bodies, actor);
  return body.has_value && body.value.origin.x == x && body.value.origin.y == y && body.value.origin.z == z;
}
static double inventory_count(comp_world *w, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(w->fx.inventory, actor, item);
}
static const char *combat_team(qa_gameplay_authority *combat, qa_actor_id actor) {
  const qa_combat_state_result state = qa_gameplay_authority_read(combat, actor);
  return state.has_value && state.value.team != NULL ? state.value.team : "(null)";
}

/* --- Q1 source clients over the real game (TS Q1SourceClients) ------------- */
static int clients_emits;
static bool clients_observer_seen;
static int clients_sentinel;
static void cl_emit(void *ctx, const qa_comp_q1_event *event) {
  CHECK(ctx == &clients_sentinel);
  if (event->kind == QA_COMP_Q1_EVENT_CLIENT) clients_emits++;
}
static void cl_set_observer(void *ctx, qa_actor_id actor, bool enabled) {
  (void)actor;
  CHECK(ctx == &clients_sentinel);
  clients_observer_seen = enabled;
}

static void test_clients(void) {
  q1_world_fixture fx;
  CHECK(q1_world_fixture_create(&fx, q1_world_fixture_options()));
  const qa_owned_actor actor_a = q1_world_fixture_admit_player(&fx, QA_Q1_ZERO, 100);
  const qa_owned_actor actor_b = q1_world_fixture_admit_player(&fx, QA_Q1_ZERO, 100);
  qa_q1_player_state *player_a = qa_q1_attach_player(fx.game, actor_a, NULL, true, NULL);
  qa_comp_q1_services services;
  memset(&services, 0, sizeof services);
  services.ctx = &clients_sentinel;
  services.emit = cl_emit;
  services.set_observer = cl_set_observer;

  qa_comp_q1_source_clients clients;
  qa_comp_q1_source_clients_init(&clients, fx.game, QA_COMP_Q1_ID1, &services);
  const qa_comp_userinfo_entry info[] = {{"name", "Ranger"}, {"topcolor", "99"}, {"bottomcolor", "4"},
                                         {"qts_weapon_autoswitch", "bogus"}};
  qa_comp_q1_client_admission admission = {0, info, 4};
  clients_emits = 0;
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_attach(&clients, actor_a, &admission);
  CHECK(client != NULL);
  CHECK(strcmp(qa_comp_q1_source_client_name(client), "Ranger") == 0);
  CHECK(qa_comp_q1_source_client_shirt(client) == 13); /* clamp 99 -> 13 */
  CHECK(qa_comp_q1_source_client_pants(client) == 4);
  CHECK(client->team == 5);
  CHECK(strcmp(combat_team(fx.combat, actor_a.id), "5") == 0);
  CHECK(strcmp(player_a->auto_switch, "always") == 0); /* bogus autoswitch -> always */
  CHECK(clients_emits == 1);
  CHECK(qa_comp_q1_source_clients_get(&clients, actor_b.id) == NULL);

  qa_comp_q1_source_clients_spawned(&clients, actor_a.id);
  CHECK(client->team == -1); /* rerelease id1 non-coop */
  qa_comp_q1_source_clients_add_score(&clients, actor_a.id, 16777217);
  CHECK(client->frags == 16777216); /* Math.fround staging */
  qa_comp_q1_source_clients_set_observer(&clients, actor_a.id, true);
  CHECK(clients_observer_seen && client->observer);

  size_t length = 0;
  uint8_t *bytes = qa_comp_q1_source_clients_capture(&clients, &length);
  CHECK(bytes != NULL && length > 0);
  client->frags = 0;
  clients.red_captures = 3;
  CHECK(qa_comp_q1_source_clients_restore(&clients, bytes, length));
  client = qa_comp_q1_source_clients_require(&clients, actor_a.id);
  CHECK(client->frags == 16777216 && client->observer && client->team == -1);
  CHECK(clients.red_captures == 0);
  free(bytes);

  admission.slot = 0;
  CHECK_ABORTS((void)qa_comp_q1_source_clients_attach(&clients, actor_b, &admission));
  admission.slot = 4;
  CHECK_ABORTS((void)qa_comp_q1_source_clients_attach(&clients, actor_b, &admission));
  CHECK_ABORTS((void)qa_comp_q1_source_clients_require(&clients, actor_b.id));
  CHECK(fx.ctx_errors == 0);
  qa_comp_q1_source_clients_destroy(&clients);
  q1_world_fixture_destroy(&fx);
}

/* "official campaign selection joins base, mission packs and rerelease
 * addons" and "horde match registers on non-MG1 Q1 programs": which layers
 * each program registers, and the authored spawns each admits. */
static void test_composition_programs(void) {
  static const struct { qa_comp_q1_program program; const char *pack, *addon; bool ctf, horde; size_t spawned; } CASES[] = {
      {QA_COMP_Q1_ID1, NULL, NULL, false, false, 5},      {QA_COMP_Q1_HIPNOTIC, "hipnotic", NULL, false, false, 5},
      {QA_COMP_Q1_ROGUE, "rogue", NULL, false, false, 5}, {QA_COMP_Q1_DOPA, NULL, "dopa", false, true, 5},
      {QA_COMP_Q1_MG1, NULL, "mg1", false, true, 4},      {QA_COMP_Q1_MG3, NULL, "mg3", false, false, 4},
      {QA_COMP_Q1_CTF, NULL, "ctf", true, false, 8}};
  for (size_t i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
    comp_world *w = calloc(1, sizeof *w);
    world_create(w, (world_options){CASES[i].program, false, false, 0});
    CHECK(qa_q1_option_skill(w->fx.game) == 1 && qa_q1_option_gravity(w->fx.game) == 800); /* TS getters */
    CHECK(w->fx.game->options.precache_id1 == (CASES[i].program == QA_COMP_Q1_ID1));
    CHECK(world_spawn(w) == CASES[i].spawned);
    CHECK(cs_cvar(w, "sv_gravity") == 800);
    {
      /* createQ1SourceComposition's skill/teamplay/gravity getters read the
       * cvars live: a cvar written outside the composition is seen at once.
       * Expected values: TS getters (skill clamps 2.5 to 2) under bun. */
      double *skill = cvar_slot(w, "skill"), *teamplay = cvar_slot(w, "teamplay"), *gravity = cvar_slot(w, "sv_gravity");
      const double saved[3] = {*skill, *teamplay, *gravity};
      *skill = 2.5; *teamplay = 3; *gravity = 100;
      CHECK(qa_q1_option_skill(w->fx.game) == 2 && qa_q1_option_teamplay(w->fx.game) == 3 &&
            qa_q1_option_gravity(w->fx.game) == 100);
      *skill = 7;
      CHECK(qa_q1_option_skill(w->fx.game) == 3);
      *skill = saved[0]; *teamplay = saved[1]; *gravity = saved[2];
    }
    qa_comp_q1_source_composition *comp = &w->comp;
    CHECK(CASES[i].pack == NULL ? comp->packs == NULL : comp->packs != NULL && strcmp(comp->packs->pack, CASES[i].pack) == 0);
    CHECK(CASES[i].addon == NULL ? comp->addon == NULL
                                 : comp->addon != NULL && strcmp(qa_q1ad_program_name(comp->addon->program), CASES[i].addon) == 0);
    CHECK((comp->ctf != NULL) == CASES[i].ctf);
    CHECK((comp->horde != NULL) == CASES[i].horde);
    CHECK(qa_q1_base_for(w->fx.game) == comp->base);
    world_destroy(w);
    free(w);
  }
  static const struct { qa_comp_q1_program program; const char *addon; } HORDE[] = {
      {QA_COMP_Q1_ID1, NULL}, {QA_COMP_Q1_HIPNOTIC, NULL}, {QA_COMP_Q1_MG3, "mg3"}};
  for (size_t i = 0; i < sizeof HORDE / sizeof HORDE[0]; i++) {
    comp_world *w = calloc(1, sizeof *w);
    world_create(w, (world_options){HORDE[i].program, true, false, 0});
    CHECK(w->comp.horde != NULL);
    CHECK(w->comp.horde_addon != NULL && w->comp.horde_addon->program == QA_Q1AD_MG1);
    CHECK(HORDE[i].addon == NULL ? w->comp.addon == NULL
                                 : w->comp.addon != NULL && strcmp(qa_q1ad_program_name(w->comp.addon->program), HORDE[i].addon) == 0);
    world_destroy(w);
    free(w);
  }
}

static bool has_message(comp_world *w, const char *text) {
  for (size_t i = 0; i < w->message_count; i++)
    if (strcmp(w->messages[i], text) == 0) return true;
  return false;
}

/* id1 case of "official campaign selection" (impulses 3, 1, 9) plus the
 * single-player notices, suicide, death drop, pose and disconnect. */
static void test_composition_id1(void) {
  comp_world *w = calloc(1, sizeof *w);
  world_create(w, (world_options){QA_COMP_Q1_ID1, false, false, 0});
  (void)world_spawn(w);
  qa_comp_q1_source_composition *comp = &w->comp;
  const qa_owned_actor actor = admit(w, "Ranger", 4, NULL);
  CHECK(w->message_count == 1 && has_message(w, "$qc_entered|Ranger"));
  CHECK(origin_is(w, actor.id, 64, 32, 24));
  qa_q1_player_state *player = qa_q1_player(w->fx.game, actor.id);
  CHECK(player != NULL);
  if (player == NULL) { world_destroy(w); free(w); return; }
  qa_shared_inventory_table_configure(w->fx.inventory, actor, (qa_inventory_entry){"q1:weapon/supershotgun", 1, 1, false, {0}}, NULL, NULL);
  qa_shared_inventory_table_configure(w->fx.inventory, actor, (qa_inventory_entry){"q1:ammo/shells", 1, 100, false, {0}}, NULL, NULL);
  impulse_input(w, actor.id, 3);
  CHECK(qa_comp_q1_source_composition_impulse(comp, actor.id));
  CHECK(strcmp(player->weapon, "shotgun") == 0);
  CHECK(has_message(w, "$qc_not_enough_ammo"));
  player->attack_finished = 1;
  impulse_input(w, actor.id, 1);
  CHECK(!qa_comp_q1_source_composition_impulse(comp, actor.id));
  impulse_input(w, actor.id, 0);
  CHECK(qa_comp_q1_source_clients_require(&comp->clients, actor.id)->impulse == 1);
  qa_q1_services_set_time(w->fx.game, 1);
  CHECK(qa_comp_q1_source_composition_impulse(comp, actor.id));
  CHECK(strcmp(player->weapon, "axe") == 0);
  impulse_input(w, actor.id, 9);
  CHECK(qa_comp_q1_source_composition_impulse(comp, actor.id));
  CHECK(inventory_count(w, actor.id, "q1:ammo/cells") == 200);
  const qa_combat_state_result armor = qa_gameplay_authority_read(w->fx.combat, actor.id);
  CHECK(armor.has_value && armor.value.armor.regular.kind == QA_REGULAR_ARMOR_Q1);
  CHECK(strcmp(player->weapon, "rocketlauncher") == 0);
  impulse_input(w, actor.id, 11);
  CHECK(qa_comp_q1_source_composition_impulse(comp, actor.id));
  CHECK(w->campaign.flags == 1);

  w->message_count = 0;
  qa_comp_q1_source_composition_suicide(comp, actor.id);
  CHECK(w->message_count == 1 && has_message(w, "$qc_suicides|Ranger"));
  CHECK(qa_comp_q1_source_clients_require(&comp->clients, actor.id)->frags == -2);
  CHECK(w->restarts == 1 && strcmp(w->restart_map, "e1m1") == 0 && w->restart_flags == 1);
  CHECK(qa_comp_q1_source_composition_drop_inventory(comp, actor) == NULL);
  CHECK(qa_comp_q1_source_composition_fall_damage_allowed(comp, actor.id));
  const qa_q1_character_source_pose pose = qa_comp_q1_source_composition_character_pose(comp, actor.id);
  CHECK(!pose.has_frame && !pose.has_axe_pose && pose.definition == NULL);

  w->message_count = 0;
  w->event_counts[QA_COMP_Q1_EVENT_CLIENT_LEFT] = 0;
  qa_comp_q1_source_composition_disconnect(comp, actor.id);
  CHECK(w->message_count == 1 && has_message(w, "$qc_left_game|Ranger|-2"));
  CHECK(w->event_counts[QA_COMP_Q1_EVENT_CLIENT_LEFT] == 1 && w->last_left_slot == 0);
  CHECK(w->disconnects == 1);
  CHECK(qa_comp_q1_source_clients_get(&comp->clients, actor.id) == NULL);
  world_destroy(w);
  free(w);
}

/* Deathmatch: obituary broadcast and score, -99 health floor, the base
 * backpack drop, and a respawn through placePlayer. */
static void test_composition_deathmatch(void) {
  comp_world *w = calloc(1, sizeof *w);
  world_create(w, (world_options){QA_COMP_Q1_ID1, false, false, 1});
  (void)world_spawn(w);
  qa_comp_q1_source_composition *comp = &w->comp;
  const qa_owned_actor a = admit(w, "Ranger", 4, NULL), b = admit(w, "Sarge", 13, NULL);
  CHECK(origin_is(w, a.id, 400, -32, 24));
  CHECK(origin_is(w, b.id, -400, 32, 24));
  w->message_count = 0;
  (void)qa_q1_damage(w->fx.game, b.id, a.id, &a.id, 1000, "shotgun", NULL, NULL, NULL);
  CHECK(w->message_count == 2 && has_message(w, "$qc_death_sg|Sarge|Ranger"));
  CHECK(qa_comp_q1_source_clients_require(&comp->clients, a.id)->frags == 1);
  CHECK(qa_comp_q1_source_clients_require(&comp->clients, b.id)->death_recorded);
  CHECK(qa_q1_health(w->fx.game, b.id) == -99);
  const qa_q1_actor *drop = qa_comp_q1_source_composition_drop_inventory(comp, b);
  CHECK(drop != NULL && strcmp(drop->classname, "item_backpack") == 0);
  qa_q1_services_set_time(w->fx.game, 2);
  qa_comp_q1_source_composition_request_respawn(comp, b.id, NULL);
  CHECK(w->places == 1 && strcmp(w->place_classname, "info_player_deathmatch") == 0);
  CHECK(!qa_comp_q1_source_clients_require(&comp->clients, b.id)->death_recorded);
  CHECK(origin_is(w, b.id, -400, 32, 24));
  qa_comp_q1_source_composition_fired(comp, a.id, "q1:weapon/axe");
  int before = 0;
  for (size_t i = 0; i <= QA_COMP_Q1_EVENT_LEVEL_PRESENTATION; i++) before += w->event_counts[i];
  qa_comp_q1_source_composition_pre_frame(comp, 0.1);
  int after = 0;
  for (size_t i = 0; i <= QA_COMP_Q1_EVENT_LEVEL_PRESENTATION; i++) after += w->event_counts[i];
  CHECK(after == before);
  world_destroy(w);
  free(w);
}

/* "pickup preference reaches actual Q1 source players on admission update
 * respawn" (the saved-restore leg needs a full world checkpoint; see report). */
static void test_composition_pickup_preference(void) {
  comp_world *w = calloc(1, sizeof *w);
  world_create(w, (world_options){QA_COMP_Q1_ID1, false, false, 0});
  (void)world_spawn(w);
  qa_comp_q1_source_composition *comp = &w->comp;
  const qa_owned_actor first = admit(w, "First", 0, "never"), second = admit(w, "Second", 0, "new");
  CHECK(strcmp(qa_q1_player(w->fx.game, first.id)->auto_switch, "never") == 0);
  CHECK(strcmp(qa_q1_player(w->fx.game, second.id)->auto_switch, "new") == 0);
  const qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&comp->clients, first.id);
  qa_comp_userinfo_entry values[8];
  size_t count = 0;
  for (size_t i = 0; i < client->userinfo_count && count < 8; i++) values[count++] = client->userinfo[i];
  /* new Map(userinfo).set(...): copies, since update replaces the client's storage. */
  char keys[8][32], texts[8][32];
  for (size_t i = 0; i < count; i++) {
    (void)snprintf(keys[i], sizeof keys[i], "%s", values[i].key);
    (void)snprintf(texts[i], sizeof texts[i], "%s",
                   strcmp(values[i].key, "qts_weapon_autoswitch") == 0 ? "new" : values[i].value);
    values[i] = (qa_comp_userinfo_entry){keys[i], texts[i]};
  }
  CHECK(qa_comp_q1_source_composition_userinfo(comp, first.id, values, count));
  CHECK(strcmp(qa_q1_player(w->fx.game, first.id)->auto_switch, "new") == 0);
  (void)qa_q1_attach_player(w->fx.game, first, NULL, true, NULL);
  qa_comp_q1_source_composition_spawned(comp, first.id, false, NULL);
  CHECK(strcmp(qa_q1_player(w->fx.game, first.id)->auto_switch, "new") == 0);
  world_destroy(w);
  free(w);
}

/* Rogue ClientKill: the composition asks the mission pack first; the fifth
 * suicide of a connection is refused with no obituary (TS packs.suicide
 * === false). RogueTeams.suicide under bun (build/agents/q1-sync/bun/
 * rogue_suicide.ts): true x4, then false with "$qc_suicided_too_much". */
static int rogue_gravity_calls;
static void rogue_set_gravity(void *ctx, qa_actor_id actor, double scale) {
  (void)q1_world_fixture_of(ctx); (void)actor; (void)scale;
  rogue_gravity_calls++;
}
static void test_composition_rogue_suicide(void) {
  comp_world *w = calloc(1, sizeof *w);
  world_create(w, (world_options){QA_COMP_Q1_ROGUE, false, false, 0});
  (void)world_spawn(w);
  /* Rogue travel admission resets antigrav through the movement host. */
  w->fx.game->host.set_gravity = rogue_set_gravity;
  const qa_owned_actor actor = admit(w, "Ranger", 4, NULL);
  for (int i = 0; i < 5; i++) {
    w->message_count = 0;
    qa_comp_q1_source_composition_suicide(&w->comp, actor.id);
    CHECK(i < 4 ? has_message(w, "$qc_suicides|Ranger") && !has_message(w, "$qc_suicided_too_much")
                : w->message_count == 1 && has_message(w, "$qc_suicided_too_much"));
  }
  CHECK(rogue_gravity_calls > 0);
  world_destroy(w);
  free(w);
}

/* Rerelease coop: the q1:coop-bots damage source effect cancels a bot's
 * damage to a human, not the reverse. */
static void test_composition_coop_bots(void) {
  comp_world *w = calloc(1, sizeof *w);
  world_create(w, (world_options){QA_COMP_Q1_ID1, false, true, 0});
  (void)world_spawn(w);
  const qa_owned_actor bot = admit(w, "Bot", 4, NULL);
  w->bots[w->bot_count++] = bot.id;
  const qa_owned_actor human = admit(w, "Human", 4, NULL);
  CHECK(qa_comp_q1_source_clients_require(&w->comp.clients, human.id)->team == 1);
  (void)qa_q1_damage(w->fx.game, human.id, bot.id, &bot.id, 50, "shotgun", NULL, NULL, NULL);
  CHECK(qa_q1_health(w->fx.game, human.id) == 100);
  (void)qa_q1_damage(w->fx.game, bot.id, human.id, &human.id, 50, "shotgun", NULL, NULL, NULL);
  CHECK(qa_q1_health(w->fx.game, bot.id) == 50);
  world_destroy(w);
  free(w);
}

static int reactions;
static int reaction_sentinel;
static void count_pain(void *ctx, qa_pain_reaction reaction) { (void)reaction; CHECK(ctx == &reaction_sentinel); reactions++; }
static void count_die(void *ctx, qa_death_reaction reaction) { (void)reaction; CHECK(ctx == &reaction_sentinel); reactions++; }

/* MG3 case of "official campaign selection": the buddha lethal-health effect. */
static void test_composition_mg3_buddha(void) {
  comp_world *w = calloc(1, sizeof *w);
  world_create(w, (world_options){QA_COMP_Q1_MG3, false, false, 0});
  (void)world_spawn(w);
  const qa_owned_actor actor = admit(w, "Ranger", 4, NULL);
  qa_q1ad_set_player_number(w->comp.addon, actor.id, "buddha", 1);
  reactions = 0;
  qa_actor_callbacks callbacks;
  memset(&callbacks, 0, sizeof callbacks);
  callbacks.ctx = &reaction_sentinel;
  callbacks.pain = count_pain;
  callbacks.die = count_die;
  qa_actor_callback_table_bind(w->fx.callbacks, actor, callbacks);
  (void)qa_q1_damage(w->fx.game, actor.id, actor.id, &actor.id, 200, "rocketlauncher", NULL, NULL, NULL);
  CHECK(qa_q1_health(w->fx.game, actor.id) == 1);
  CHECK(reactions == 0);
  CHECK(!qa_comp_q1_source_clients_require(&w->comp.clients, actor.id)->death_recorded);
  const qa_q1_character_source_pose pose = qa_comp_q1_source_composition_character_pose(&w->comp, actor.id);
  CHECK(!pose.has_frame);
  world_destroy(w);
  free(w);
}

static comp_world *ctf_world;
static qa_actor_id ctf_red;
static double score_at_death;
static void ctf_die(void *ctx, qa_death_reaction reaction) {
  (void)reaction;
  CHECK(ctx == &reaction_sentinel);
  score_at_death = qa_comp_q1_source_clients_require(&ctf_world->comp.clients, ctf_red)->frags;
}

/* "actual CTF composition joins client teams, capture score, death ordering":
 * teams, CTF spawn arsenal, death scoring before the character death
 * callback, the grapple impulse, pose and fall damage. */
static void test_composition_ctf(void) {
  comp_world *w = calloc(1, sizeof *w);
  world_create(w, (world_options){QA_COMP_Q1_CTF, false, false, 1});
  (void)world_spawn(w);
  qa_comp_q1_source_composition *comp = &w->comp;
  const qa_owned_actor red = admit(w, "Ranger", 4, NULL), blue = admit(w, "Sarge", 13, NULL);
  q1_world_fixture_advance(&w->fx, 10);
  qa_q1_services_set_time(w->fx.game, 10);
  CHECK(strcmp(combat_team(w->fx.combat, red.id), "red") == 0 && strcmp(combat_team(w->fx.combat, blue.id), "blue") == 0);
  CHECK(inventory_count(w, red.id, "q1:ammo/shells") == 40);
  ctf_world = w;
  ctf_red = red.id;
  score_at_death = -1;
  CHECK(qa_comp_q1_source_clients_require(&comp->clients, red.id)->frags == 0);
  qa_actor_callbacks callbacks;
  memset(&callbacks, 0, sizeof callbacks);
  callbacks.ctx = &reaction_sentinel;
  callbacks.die = ctf_die;
  qa_actor_callback_table_bind(w->fx.callbacks, blue, callbacks);
  (void)qa_q1_damage(w->fx.game, blue.id, red.id, &red.id, 1000, "shotgun", NULL, NULL, NULL);
  CHECK(score_at_death == 1);
  CHECK(qa_comp_q1_source_clients_require(&comp->clients, blue.id)->death_recorded);
  impulse_input(w, red.id, 22);
  CHECK(qa_comp_q1_source_composition_impulse(comp, red.id));
  CHECK(strcmp(qa_q1_player(w->fx.game, red.id)->weapon, "ctf:grapple") == 0);
  const qa_q1_character_source_pose pose = qa_comp_q1_source_composition_character_pose(comp, red.id);
  CHECK(pose.has_axe_pose && pose.axe_pose && !pose.has_frame);
  CHECK(qa_comp_q1_source_composition_fall_damage_allowed(comp, red.id));
  CHECK(w->event_counts[QA_COMP_Q1_EVENT_CTF_STATUS] > 0);
  world_destroy(w);
  free(w);
}

int main(void) {
  test_pickup_select();
  test_profiles();
  test_q1_q3_loadout();
  test_expansions();
  test_clients();
  test_composition_programs();
  test_composition_id1();
  test_composition_deathmatch();
  test_composition_pickup_preference();
  test_composition_coop_bots();
  test_composition_rogue_suicide();
  test_composition_mg3_buddha();
  test_composition_ctf();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
