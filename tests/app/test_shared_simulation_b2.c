/* Phase B2 checks for the SharedSimulation port (runtime.ts, arsenal,
 * equipment and presentation region, shared_simulation_arsenal.c).
 *
 * Expected values come from the TS methods run under bun on the same
 * inputs: tests/app/oracles/shared_simulation_b2.ts calls the
 * SharedSimulation.prototype methods on fake `this` records
 * (`bun tests/app/oracles/shared_simulation_b2.ts`). Methods that need a
 * constructed simulation (the constructor calls phase B1 and B3 methods) are
 * exercised on a zeroed simulation record holding only the members the
 * method reads. Callbacks the methods hand to other modules are checked with
 * sentinel contexts. Linked through the normal Makefile rule.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"
#include "qa/content/composition/q1_q2_supply.h"
#include "qa/content/composition/q2_q3_supply.h"
#include "qa/content/composition/q3_q1_supply.h"
#include "qa/foundation/support.h"

static int failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)
/* Exact double comparison that treats NaN as equal to NaN (the oracle prints NaN). */
static bool same(double a, double b) { return (isnan(a) && isnan(b)) || a == b; }

static qa_actor_id actor_id(int64_t slot) { qa_actor_id id; memset(&id, 0, sizeof id); id.slot = slot; id.generation = 1; return id; }

/* A zeroed simulation with the maps the B2 presentation methods read. */
static qa_app_shared_simulation *new_sim(void) {
  qa_app_shared_simulation *sim = calloc(1, sizeof *sim);
  qa_ss_map_init(&sim->presentation_snapshot, QA_SS_KEY_ACTOR, sizeof(qa_ss_presentation_pose));
  qa_ss_map_init(&sim->view_models, QA_SS_KEY_ACTOR, sizeof(qa_ss_view_model));
  qa_ss_map_init(&sim->weapon_slots, QA_SS_KEY_ACTOR, sizeof(qa_app_sim_weapon_slot *));
  sim->source_frame.time.kind = QA_SOURCE_TIME_SECONDS;
  return sim;
}
static void free_sim(qa_app_shared_simulation *sim) {
  qa_ss_map_free(&sim->presentation_snapshot); qa_ss_map_free(&sim->view_models); qa_ss_map_free(&sim->weapon_slots);
  for (size_t i = 0; i < sim->allocation_count; i++) free(sim->allocations[i]);
  free(sim->allocations);
  free(sim);
}

/* presentationFraction: oracle "fraction" lines. */
static void test_presentation_fraction(void) {
  struct { bool has_tick; double tick; size_t size; double seconds, scheduling; bool ahead; double fraction, back; } cases[] = {
    {false, 0, 1, 1000, 1000, true, 1, 0}, {true, 0, 1, 1000, 1000, true, 1, 0}, {true, 25, 0, 1000, 990, true, 1, 0},
    {true, 25, 1, 1000, 990, true, 0, 1}, {true, 25, 1, 1000, 990, false, 0, 1}, {true, 25, 1, 1000, 1030, true, 0, 1},
    {true, 25, 1, 1000, 960, true, 0, 1}, {true, 100, 1, 1.5, 1450, true, 0.5, 0.5}, {true, 100, 1, 1.5, 1525, false, 0.25, 0.75},
    {true, 100, 1, 1.5, NAN, true, NAN, NAN},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    qa_app_shared_simulation *sim = new_sim();
    sim->has_presentation_tick_milliseconds = cases[i].has_tick; sim->presentation_tick_milliseconds = cases[i].tick;
    for (size_t j = 0; j < cases[i].size; j++) { qa_actor_id id = actor_id((int64_t)j); qa_ss_map_set(&sim->presentation_snapshot, &id, NULL); }
    sim->source_frame.time.value = cases[i].seconds;
    sim->source_scheduling_milliseconds = cases[i].scheduling; sim->presentation_ahead = cases[i].ahead;
    qa_ss_presentation_fraction result = qa_app_shared_sim_presentation_fraction(sim);
    CHECK(same(result.fraction, cases[i].fraction) && same(result.back_lerp, cases[i].back));
    free_sim(sim);
  }
}

/* interpolatedPose: oracle "pose" lines. */
static void test_interpolated_pose(void) {
  qa_app_shared_simulation *sim = new_sim();
  qa_actor_id a1 = actor_id(1);
  qa_ss_presentation_pose previous = {{10, -20, 30}, {350, -170, 0}, NULL, 0};
  qa_ss_map_set(&sim->presentation_snapshot, &a1, &previous);
  struct { int64_t slot; qa_vec3 origin, angles; double fraction; double expected[10]; } cases[] = {
    {1, {20, -10, 50}, {10, 170, 90}, 0.25, {12.5, -17.5, 35, 355, -175, 22.5, 10, -20, 30, 0.75}},
    {1, {20, -10, 50}, {10, 170, 90}, 1, {20, -10, 50, 10, 170, 90, 20, -10, 50, 0}},
    {2, {20, -10, 50}, {10, 170, 90}, 0.5, {20, -10, 50, 10, 170, 90, 20, -10, 50, 0}},
    {1, {600, -20, 30}, {0, 0, 0}, 0.5, {600, -20, 30, 0, 0, 0, 600, -20, 30, 0}},
    {1, {13, -24, 30.5}, {-720.5, 540, -181}, 0.75, {12.25, -23, 30.375, 357.125, -177.5, 134.25, 10, -20, 30, 0.25}},
    {1, {10, -20, 30}, {170, 10, 359}, 0, {10, -20, 30, 350, -170, 0, 10, -20, 30, 1}},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    qa_ss_interpolated_pose pose = qa_app_shared_sim_interpolated_pose(sim, actor_id(cases[i].slot), cases[i].origin, cases[i].angles, cases[i].fraction);
    double got[10] = {pose.origin.x, pose.origin.y, pose.origin.z, pose.angles.x, pose.angles.y, pose.angles.z,
                      pose.previous_origin.x, pose.previous_origin.y, pose.previous_origin.z, pose.back_lerp};
    for (int k = 0; k < 10; k++) CHECK(same(got[k], cases[i].expected[k]));
  }
  free_sim(sim);
}

/* q1VisualFields: oracle "visual" lines. */
static void test_q1_visual_fields(void) {
  struct { double alpha, scale, expected_alpha, expected_scale; } cases[] = {
    {0, 0, 1, 1}, {0.3, 1.5, 0.30000001192092896, 1.5}, {2, -1, 1, -1}, {-0.5, 0.1, 0, 0.10000000149011612},
    {NAN, NAN, NAN, NAN}, {0.1, 3.3, 0.10000000149011612, 3.299999952316284},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    qa_ss_visual_fields fields = qa_app_shared_sim_q1_visual_fields(cases[i].alpha, cases[i].scale);
    CHECK(same(fields.alpha, cases[i].expected_alpha) && same(fields.scale, cases[i].expected_scale));
  }
}

/* replacedSupplyItems: oracle "replaced" lines. */
static void check_replaced(const qa_pickup_supply_profile *profile, const char *expected) {
  size_t count = 0;
  qa_item_id *items = qa_ss_replaced_supply_items(profile, &count);
  char joined[2048] = "";
  for (size_t i = 0; i < count; i++) { if (i) strcat(joined, ","); strcat(joined, items[i]); }
  if (strcmp(joined, expected) != 0) fprintf(stderr, "replaced %s: %s\n", profile->id, joined);
  CHECK(strcmp(joined, expected) == 0);
  free(items);
}
static void test_replaced_supply_items(void) {
  check_replaced(&QA_COMPOSITION_Q3_Q1_SUPPLY_PROFILE, "q3:ammo/shotgun,q3:ammo/machinegun,q3:ammo/grenadelauncher,q3:ammo/rocketlauncher,"
      "q3:ammo/lightning,q3:ammo/railgun,q3:ammo/plasmagun,q3:ammo/bfg,q3:weapon/gauntlet,q3:weapon/shotgun,q3:weapon/machinegun,"
      "q3:weapon/grenadelauncher,q3:weapon/rocketlauncher,q3:weapon/lightning,q3:weapon/railgun,q3:weapon/plasmagun,q3:weapon/bfg");
  check_replaced(&QA_COMPOSITION_Q1_Q2_SUPPLY_PROFILE, "q1:ammo/shells,q1:ammo/nails,q1:ammo/rockets,q1:ammo/cells,q1:weapon/axe,"
      "q1:weapon/shotgun,q1:weapon/supershotgun,q1:weapon/nailgun,q1:weapon/supernailgun,q1:weapon/grenadelauncher,"
      "q1:weapon/rocketlauncher,q1:weapon/lightning");
  check_replaced(&QA_COMPOSITION_Q2_Q3_SUPPLY_PROFILE, "q2:ammo_shells,q2:ammo_bullets,q2:ammo_rockets,q2:ammo_slugs,q2:weapon_shotgun,"
      "q2:weapon_supershotgun,q2:weapon_machinegun,q2:weapon_chaingun,q2:weapon_grenadelauncher,q2:weapon_rocketlauncher,"
      "q2:weapon_hyperblaster,q2:weapon_railgun,q2:weapon_bfg");
}

/* weaponPresentationClock: oracle "clock" lines. */
static void test_weapon_presentation_clock(void) {
  struct { const char *family; double milliseconds; bool has; double expected; } cases[] = {
    {"q3", 1234.7, true, 1234}, {"q3", -5.5, true, -5}, {"q1", 99, false, 0}, {NULL, 1, false, 0},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    qa_app_shared_simulation *sim = new_sim();
    sim->weapon_provider.content = "q3:baseq3"; sim->weapon_provider.provider = "q3:baseq3";
    sim->has_selected_arsenal = cases[i].family != NULL; sim->selected_arsenal.family = cases[i].family;
    sim->selected_milliseconds = cases[i].milliseconds;
    qa_content_id content = NULL; double milliseconds = 0;
    bool has = qa_app_shared_sim_weapon_presentation_clock(sim, &content, &milliseconds);
    CHECK(has == cases[i].has);
    if (has) CHECK(strcmp(content, "q3:baseq3") == 0 && milliseconds == cases[i].expected);
    free_sim(sim);
  }
}

/* ---- sentinel contexts ---- */
static int sentinel_storage;
#define SENTINEL ((void *)&sentinel_storage)
typedef struct source_log { int reads, writes, capacity_queries, wrong_ctx; qa_inventory_entry entries[4]; size_t count; } source_log;
static source_log source;
static const qa_inventory_entry *source_read(void *ctx, size_t *out_count) {
  if (ctx != SENTINEL) source.wrong_ctx++;
  source.reads++; *out_count = source.count; return source.entries;
}
static void source_write(void *ctx, qa_inventory_entry entry) {
  if (ctx != SENTINEL) source.wrong_ctx++;
  source.writes++;
  for (size_t i = 0; i < source.count; i++) if (strcmp(source.entries[i].item, entry.item) == 0) source.entries[i] = entry;
}
static bool source_mutable_capacity(void *ctx, qa_item_id item) {
  if (ctx != SENTINEL) source.wrong_ctx++;
  source.capacity_queries++;
  return strcmp(item, "q1:ammo/shells") == 0;
}
typedef struct duplicate_call { qa_app_shared_simulation *sim; qa_owned_actor actor; qa_inventory_state_binding binding; } duplicate_call;
static void bind_duplicates(void *ctx) {
  duplicate_call *call = ctx;
  qa_inventory_entry saved[2] = {{"rogue:ammo/lava", 1, 5, false, {0}}, {"rogue:ammo/lava", 2, 5, false, {0}}};
  qa_app_shared_sim_bind_source_inventory(call->sim, call->actor, call->binding, saved, 2);
}
/* bindSourceInventory forwards the caller's binding with its own ctx;
 * supplemental rows stay with the simulation. Also the inventory access
 * adapter (ctx = simulation) the selected arsenals read through. */
static void test_bind_source_inventory(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "shared-simulation-b2"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 64);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_owned_actor owner = qa_session_actor_registry_allocate(actors, "q1:character", "q1:player");
  qa_app_shared_simulation *sim = new_sim();
  sim->actors = actors; sim->inventory = inventory; sim->source.kind = QA_SS_SOURCE_Q1;
  memset(&source, 0, sizeof source);
  source.entries[0] = (qa_inventory_entry){"q1:ammo/shells", 10, 100, false, {0}};
  source.entries[1] = (qa_inventory_entry){"q1:weapon/shotgun", 1, 1, false, {0}};
  source.count = 2;
  qa_inventory_state_binding binding = {SENTINEL, source_read, NULL, source_write, source_mutable_capacity};
  qa_inventory_entry saved[2] = {{"rogue:ammo/lava", 5, 50, false, {0}}, {"q1:ammo/shells", 99, 100, false, {0}}};
  qa_app_shared_sim_bind_source_inventory(sim, owner, binding, saved, 2);
  CHECK(qa_shared_inventory_table_count(inventory, owner.id, "q1:ammo/shells") == 10);
  CHECK(qa_shared_inventory_table_count(inventory, owner.id, "rogue:ammo/lava") == 5);
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(inventory, owner.id, &count);
  CHECK(count == 3 && strcmp(entries[0].item, "q1:ammo/shells") == 0 && strcmp(entries[2].item, "rogue:ammo/lava") == 0);
  int writes = source.writes;
  qa_shared_inventory_table_configure(inventory, owner, (qa_inventory_entry){"q1:ammo/shells", 25, 100, false, {0}}, NULL, NULL);
  CHECK(source.writes == writes + 1 && source.entries[0].count == 25);
  qa_shared_inventory_table_configure(inventory, owner, (qa_inventory_entry){"rogue:ammo/lava", 7, 50, false, {0}}, NULL, NULL);
  CHECK(source.writes == writes + 1 && qa_shared_inventory_table_count(inventory, owner.id, "rogue:ammo/lava") == 7);
  CHECK(qa_shared_inventory_table_mutable_capacity(inventory, owner.id, "q1:ammo/shells"));
  CHECK(qa_shared_inventory_table_mutable_capacity(inventory, owner.id, "rogue:ammo/lava"));
  CHECK(!qa_shared_inventory_table_mutable_capacity(inventory, owner.id, "q1:weapon/shotgun"));
  CHECK(source.reads > 0 && source.capacity_queries > 0 && source.wrong_ctx == 0);
  /* The selected arsenals' inventory surface reads through the simulation. */
  qa_app_inventory_access access = qa_app_shared_sim_inventory_access(sim);
  CHECK(access.ctx == sim && access.count(access.ctx, owner.id, "q1:ammo/shells") == 25);
  size_t access_count = 0;
  CHECK(access.entries(access.ctx, owner.id, &access_count) != NULL && access_count == 3);
  /* "Saved source inventory has duplicate items" */
  qa_owned_actor second = qa_session_actor_registry_allocate(actors, "q1:character", "q1:player");
  duplicate_call call = {sim, second, binding};
  qa_caught_error caught;
  CHECK(!qa_try(bind_duplicates, &call, &caught) && strcmp(caught.message, "Saved source inventory has duplicate items") == 0);
  free_sim(sim);
  qa_shared_inventory_table_destroy(inventory);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

/* sharedGrapple(): the control's callbacks run with the simulation as ctx. */
static void test_shared_grapple(void) {
  qa_app_shared_simulation *sim = new_sim();
  sim->recipe.equipment.grapple.is_enabled = true; sim->recipe.equipment.grapple.mechanic = "q2-ctf";
  const qa_shared_grapple_control *control = qa_app_shared_sim_shared_grapple(sim);
  CHECK(control->ctx == sim && control->selection.is_enabled && strcmp(control->selection.mechanic, "q2-ctf") == 0);
  CHECK(!control->native_slot(control->ctx, "q2-ctf"));
  CHECK(!control->pulling(control->ctx, actor_id(3)) && control->gravity_scale(control->ctx, actor_id(3)) == 1);
  control->release(control->ctx, actor_id(3));
  /* grappleSlotHost: no weapon slot means the equipment is not selected. */
  qa_app_sim_grapple_slot_host host = qa_app_shared_sim_grapple_slot_host(sim);
  CHECK(host.ctx == sim && !host.selected(host.ctx, actor_id(3)));
  CHECK(!qa_app_shared_sim_has_weapon_slot(sim, actor_id(3)));
  qa_ss_grapple_state state;
  CHECK(!qa_app_shared_sim_grapple_state(sim, actor_id(3), &state));
  qa_hand_grenade_equipment_state grenades;
  CHECK(!qa_app_shared_sim_hand_grenade_state(sim, actor_id(3), &grenades));
  free_sim(sim);
}


/* ---- runtime-b2b rows: oracle "spawn".."native-behavior" and "pickup" lines ---- */
typedef struct b2b_call { qa_app_shared_simulation *sim; int which; } b2b_call;
static void b2b_run(void *ctx) {
  b2b_call *call = ctx; qa_app_shared_simulation *sim = call->sim;
  qa_owned_actor owner; memset(&owner, 0, sizeof owner); owner.id = actor_id(1);
  qa_vec3 origin, angles; qa_app_prepared_weapon_behavior entry; memset(&entry, 0, sizeof entry);
  switch (call->which) {
  case 0: qa_app_shared_sim_selected_q3_spawn(sim, owner, &origin, &angles); break;
  case 1: qa_app_shared_sim_drop_selected_q3_objectives(sim, owner); break;
  case 2: (void)qa_app_shared_sim_selected_native_player(sim); break;
  case 3: (void)qa_app_shared_sim_native_drop_protection(sim, actor_id(1), "debit", NULL, NULL); break;
  case 4: (void)qa_app_shared_sim_native_drop_projection(sim, actor_id(1), "q2:weapon_shotgun"); break;
  case 5: qa_app_shared_sim_initialize_qvm_grapple(sim); break;
  case 6: entry.kind = "qvm"; (void)qa_app_shared_sim_create_qvm_weapon_behavior(sim, entry); break;
  case 7: entry.kind = "rerelease-native"; (void)qa_app_shared_sim_create_native_weapon_behavior(sim, entry, NULL, NULL); break;
  default: break;
  }
}
static void b2b_expect(qa_app_shared_simulation *sim, int which, const char *message) {
  b2b_call call = {sim, which}; qa_caught_error caught;
  bool returned = qa_try(b2b_run, &call, &caught);
  if (returned || strcmp(caught.message, message) != 0) fprintf(stderr, "case %d: %s\n", which, returned ? "returned" : caught.message);
  CHECK(!returned && strcmp(caught.message, message) == 0);
}
static int execute_calls, execute_wrong_ctx;
static int execute_result = 42;
static void *b2b_execute(void *ctx) { if (ctx != SENTINEL) execute_wrong_ctx++; execute_calls++; return &execute_result; }
static void test_b2b_error_paths(void) {
  qa_app_shared_simulation *sim = new_sim();
  sim->source.kind = QA_SS_SOURCE_LOADING;
  b2b_expect(sim, 0, "Selected map travel is unavailable while the world is loading");
  b2b_expect(sim, 1, "Selected objective drop has no source map owner");
  sim->source.kind = QA_SS_SOURCE_QUAKEC;
  b2b_expect(sim, 2, "Selected native equipment requires its source player");
  sim->source.kind = QA_SS_SOURCE_Q1;
  b2b_expect(sim, 3, "Original inventory drop lost its native source");
  /* A classic native source runs the effect directly, with the caller's ctx. */
  sim->source.kind = QA_SS_SOURCE_Q2_NATIVE; sim->source.edition = "classic";
  execute_calls = 0; execute_wrong_ctx = 0;
  int *result = qa_app_shared_sim_native_drop_protection(sim, actor_id(1), "consume", b2b_execute, SENTINEL);
  CHECK(result == &execute_result && *result == 42 && execute_calls == 1 && execute_wrong_ctx == 0);
  b2b_expect(sim, 4, "Native drop has no declared original item prototypes");
  /* Disabled equipment and the QVM grapple (initialized separately) construct nothing. */
  CHECK(qa_app_shared_sim_create_hand_grenades(sim) == NULL);
  CHECK(qa_app_shared_sim_create_grapple(sim) == NULL);
  qa_app_shared_sim_initialize_qvm_grapple(sim);
  sim->recipe.equipment.grapple.is_enabled = true; sim->recipe.equipment.grapple.mechanic = "q3-qvm";
  CHECK(qa_app_shared_sim_create_grapple(sim) == NULL);
  b2b_expect(sim, 5, "Selected QVM grapple requires its exact prepared source and host clock");
  b2b_expect(sim, 6, "QVM weapon components require host calendar services");
  b2b_expect(sim, 7, "Native weapon components require host clock capabilities");
  free_sim(sim);
}
/* nativeInventoryRows: null off a native source; Q3 source pickups' native/rejected branches. */
static void test_b2b_pickups(void) {
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "shared-simulation-b2b"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 64);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_app_shared_simulation *sim = new_sim();
  sim->actors = actors; sim->inventory = inventory; sim->source.kind = QA_SS_SOURCE_Q1;
  size_t count = 7;
  CHECK(qa_app_shared_sim_native_inventory_rows(sim, actor_id(1), &count) == NULL && count == 0);
  static int supply_storage; /* a supply that these branches never consult */
  struct { const char *family; double type, tag; qa_q3_source_pickup_preview_kind preview; qa_q3_source_pickup_admission_kind admit; } cases[] = {
    {NULL, 1, 2, QA_Q3_SOURCE_PREVIEW_NATIVE, QA_Q3_SOURCE_PICKUP_NATIVE},
    {"q1", 4, 0, QA_Q3_SOURCE_PREVIEW_NATIVE, QA_Q3_SOURCE_PICKUP_NATIVE},
    {"q1", 1, 0, QA_Q3_SOURCE_PREVIEW_REJECTED, QA_Q3_SOURCE_PICKUP_REJECTED},
    {"q2", 2, 2, QA_Q3_SOURCE_PREVIEW_REJECTED, QA_Q3_SOURCE_PICKUP_REJECTED},
    {"q3", 1, 2, QA_Q3_SOURCE_PREVIEW_NATIVE, QA_Q3_SOURCE_PICKUP_NATIVE},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    sim->has_selected_arsenal = cases[i].family != NULL; sim->selected_arsenal.family = cases[i].family;
    sim->selected_supply = cases[i].family != NULL ? (qa_shared_pickup_admission *)&supply_storage : NULL;
    qa_q3_base_item_definition item; memset(&item, 0, sizeof item);
    item.type = cases[i].type; item.tag = cases[i].tag; item.quantity = 10;
    qa_q3_source_pickup_descriptor pickup; memset(&pickup, 0, sizeof pickup);
    pickup.item = &item; pickup.player_actor = actor_id(1);
    qa_q3_source_pickup_preview preview = qa_app_shared_sim_preview_selected_q3_pickup(sim, pickup);
    qa_q3_source_pickup_admission admission = qa_app_shared_sim_admit_selected_q3_pickup(sim, pickup);
    CHECK(preview.kind == cases[i].preview && admission.kind == cases[i].admit);
  }
  free_sim(sim);
  qa_shared_inventory_table_destroy(inventory);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

int main(void) {
  test_presentation_fraction();
  test_interpolated_pose();
  test_q1_visual_fields();
  test_replaced_supply_items();
  test_weapon_presentation_clock();
  test_bind_source_inventory();
  test_shared_grapple();
  test_b2b_error_paths();
  test_b2b_pickups();
  if (failures) { fprintf(stderr, "%d failures\n", failures); return 1; }
  printf("test_shared_simulation_b2: ok\n");
  return 0;
}
