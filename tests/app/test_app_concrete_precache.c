/*
 * precache.ts over the real SharedSimulation sources: a port of
 * ../quake-anthology-ts/tests/first-playable/precache.test.ts (transient Q2 sounds by source content,
 * deduplicated shared declarations, native medic reinforcements, selected character resources). The TS
 * tests pass a PrecacheSource whose sources are all null; here that is a SharedSimulation whose source is
 * still loading, the state in which every TS source getter returns null. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/precache.h"
#include "qa/content/catalog/products.h"
#include "qa/content/q2/foundation/host.h"
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

static const char *const product_ids[] = {"q1-classic-id1", "q2-classic-baseq2", "q2-classic-rogue", "q2-rerelease-baseq2", "q3-baseq3"};
#define PRODUCT_COUNT 5
static qa_catalog_discovered_product products[PRODUCT_COUNT];
static qa_content_catalog catalog;
static const char *content_of(const char *product) { return qa_content_catalog_product(&catalog, product)->id; }

static void make_catalog(void) {
  for (size_t i = 0; i < PRODUCT_COUNT; i++) {
    const qa_catalog_product_expectation *expectation = NULL;
    for (size_t e = 0; e < qa_expected_product_count; e++)
      if (strcmp(qa_expected_products[e].id, product_ids[i]) == 0) expectation = &qa_expected_products[e];
    memset(&products[i], 0, sizeof products[i]);
    products[i].id = qa_create_content_id((qa_content_identity){expectation->family, expectation->edition, expectation->campaign, "precache-test"});
    products[i].expectation = *expectation;
    products[i].availability.kind = QA_CATALOG_INSTALLED;
  }
  qa_content_catalog_init(&catalog, "/unused", products, PRODUCT_COUNT, NULL, 0, 0, NULL, NULL, 0);
}

static bool has_request(const qa_app_resource_requests *requests, const char *content, const char *path) {
  for (size_t i = 0; i < requests->count; i++)
    if ((content == NULL || strcmp(requests->items[i].content, content) == 0) && strcmp(requests->items[i].path, path) == 0) return true;
  return false;
}
static size_t count_request(const qa_app_resource_requests *requests, const char *content, const char *path) {
  size_t count = 0;
  for (size_t i = 0; i < requests->count; i++)
    if (strcmp(requests->items[i].content, content) == 0 && strcmp(requests->items[i].path, path) == 0) count++;
  return count;
}

static qa_app_shared_simulation *loading_simulation(void) {
  qa_app_shared_simulation *simulation = calloc(1, sizeof *simulation);
  simulation->source.kind = QA_SS_SOURCE_LOADING;
  return simulation;
}

static qa_provider_reference native;
static qa_monster_definition_reference gunner;
static qa_executable_recipe base_recipe(qa_provider_reference map) {
  qa_executable_recipe recipe;
  memset(&recipe, 0, sizeof recipe);
  recipe.map.entities = map;
  recipe.equipment = qa_disabled_equipment();
  recipe.character.definition = native;
  recipe.character.appearance = native;
  recipe.enemies.kind = QA_ENEMY_SELECTION_REPLACE;
  recipe.enemies.default_target.reference = gunner;
  return recipe;
}

static void test_transient_sounds(qa_app_shared_simulation *simulation) {
  const char *rows[] = {"q2-classic-baseq2", "q2-classic-rogue", "q2-rerelease-baseq2"};
  for (size_t r = 0; r < 3; r++) {
    qa_provider_reference source = {"q2:entities", content_of(rows[r])};
    qa_executable_recipe recipe = base_recipe(source);
    qa_app_resource_requests requests = qa_app_application_resource_requests((qa_app_precache_content){&catalog, &recipe}, simulation);
    CHECK(has_request(&requests, source.content, "sound/weapons/rocklx1a.wav"));
    bool expansion = strcmp(rows[r], "q2-classic-baseq2") != 0;
    CHECK(has_request(&requests, source.content, "sound/weapons/tesla.wav") == expansion);
    CHECK(has_request(&requests, source.content, "sound/weapons/disrupthit.wav") == expansion);
    qa_app_resource_requests_destroy(&requests);
  }
}

static bool resource_suffix(const char *path) {
  const char *dot = strrchr(path, '.');
  if (dot == NULL) return false;
  const char *ok[] = {".mdl", ".md2", ".md3", ".spr", ".sp2", ".wav", ".ogg"};
  for (size_t i = 0; i < 7; i++) if (strcmp(dot, ok[i]) == 0) return true;
  return false;
}

static void test_shared_dedupe(qa_app_shared_simulation *simulation) {
  qa_executable_recipe recipe = base_recipe(native);
  recipe.weapons = (qa_provider_reference_list){&native, 1};
  qa_monster_by_classname ogre = {"monster_ogre", {false, gunner}};
  recipe.enemies.by_classname = &ogre;
  recipe.enemies.by_classname_count = 1;
  recipe.equipment.hand_grenades = (qa_hand_grenade_selection){true, {qa_equipment_provider_ids.hand_grenades, gunner.source.content}, "rerelease", 2, 50};
  qa_app_resource_requests requests = qa_app_application_resource_requests((qa_app_precache_content){&catalog, &recipe}, simulation);
  CHECK(count_request(&requests, gunner.source.content, "models/objects/grenade/tris.md2") == 1);
  CHECK(has_request(&requests, native.content, "progs/grenade.mdl"));
  CHECK(has_request(&requests, gunner.source.content, "sound/gunner/gunatck3.wav"));
  CHECK(has_request(&requests, gunner.source.content, "sound/weapons/grenlx1a.wav"));
  bool unique = true, suffixed = true;
  for (size_t i = 0; i < requests.count; i++) {
    if (count_request(&requests, requests.items[i].content, requests.items[i].path) != 1) unique = false;
    if (!resource_suffix(requests.items[i].path)) suffixed = false;
  }
  CHECK(unique);
  CHECK(suffixed);
  qa_app_resource_requests_destroy(&requests);
}

static void set_reinforcements(qa_q2_spawn_fields *fields, const char *value) {
  for (size_t i = 0; i < fields->value_count; i++) { free(fields->values[i].key); free(fields->values[i].value); }
  fields->value_count = 0;
  if (value == NULL) return;
  fields->values[0].key = strdup("reinforcements");
  fields->values[0].value = strdup(value);
  fields->value_count = 1;
}

static void test_medic_reinforcements(void) {
  const qa_monster_source_definition *source = NULL;
  for (size_t i = 0; i < qa_monster_source_count; i++)
    if (strcmp(qa_monster_sources[i].provider, gunner.source.provider) == 0) source = &qa_monster_sources[i];
  CHECK(source != NULL);
  if (source == NULL) return;
  qa_q2_field_pair pairs[1];
  qa_q2_entity commander;
  memset(&commander, 0, sizeof commander);
  commander.classname = "monster_medic_commander";
  commander.spawn.ordinal = 4;
  commander.spawn.classname = "monster_medic_commander";
  commander.spawn.values = pairs;
  commander.spawn.value_capacity = 1;
  qa_q2_entity *entities[1] = {&commander};
  set_reinforcements(&commander.spawn, NULL);
  qa_app_resource_requests ordinary = qa_app_native_q2_monster_resources(gunner.source.content, source, entities, 1);
  set_reinforcements(&commander.spawn, " monster_arachnid 7; monster_guardian 11 ");
  qa_app_resource_requests custom = qa_app_native_q2_monster_resources(gunner.source.content, source, entities, 1);
  const char *added[] = {"monster_arachnid", "monster_guardian"};
  for (size_t c = 0; c < 2; c++) {
    const qa_monster_creature_entry *creature = qa_monster_source_creature(source, added[c]);
    CHECK(creature != NULL);
    for (size_t r = 0; creature != NULL && r < creature->resource_count; r++)
      CHECK(has_request(&custom, gunner.source.content, creature->resources[r]));
  }
  CHECK(!has_request(&ordinary, NULL, "models/monsters/arachnid/tris.md2"));
  CHECK(has_request(&custom, NULL, "models/monsters/arachnid/tris.md2"));
  set_reinforcements(&commander.spawn, "monster_shambler 3");
  qa_app_resource_requests restored = qa_app_native_q2_monster_resources(gunner.source.content, source, entities, 1);
  CHECK(!has_request(&restored, NULL, "models/monsters/arachnid/tris.md2"));
  CHECK(has_request(&restored, NULL, "models/monsters/shambler/tris.md2"));
  set_reinforcements(&commander.spawn, NULL);
  qa_app_resource_requests_destroy(&ordinary);
  qa_app_resource_requests_destroy(&custom);
  qa_app_resource_requests_destroy(&restored);
}

static void test_character_resources(void) {
  const char *rows[] = {"q1-classic-id1", "q2-rerelease-baseq2", "q3-baseq3"};
  for (size_t r = 0; r < 3; r++) {
    qa_provider_reference character = {native.provider, content_of(rows[r])};
    qa_executable_recipe recipe = base_recipe(native);
    recipe.enemies.kind = QA_ENEMY_SELECTION_MAP_DEFINED;
    recipe.character.definition = character;
    recipe.character.appearance = character;
    qa_app_resource_requests requests = qa_app_character_resource_requests((qa_app_precache_content){&catalog, &recipe});
    bool same = true;
    for (size_t i = 0; i < requests.count; i++) if (strcmp(requests.items[i].content, character.content) != 0) same = false;
    CHECK(same);
    if (r == 0) {
      CHECK(has_request(&requests, NULL, "sound/player/pain1.wav"));
      CHECK(has_request(&requests, NULL, "progs/gib1.mdl"));
      CHECK(!has_request(&requests, NULL, "progs/v_rock.mdl"));
    } else if (r == 1) {
      CHECK(has_request(&requests, NULL, "*pain100_1.wav"));
      CHECK(has_request(&requests, NULL, "models/objects/gibs/head2/tris.md2"));
    } else {
      CHECK(has_request(&requests, NULL, "*pain100_1.wav"));
      CHECK(has_request(&requests, NULL, "sound/world/telein.wav"));
      CHECK(!has_request(&requests, NULL, "sound/player/sarge/pain100_1.wav"));
    }
    qa_app_resource_requests_destroy(&requests);
  }
}

int main(void) {
  make_catalog();
  native = (qa_provider_reference){"q1:official", content_of("q1-classic-id1")};
  gunner = (qa_monster_definition_reference){{"q2:monsters/rerelease/baseq2", content_of("q2-rerelease-baseq2")}, "monster_gunner"};
  qa_app_shared_simulation *simulation = loading_simulation();
  test_transient_sounds(simulation);
  test_shared_dedupe(simulation);
  test_medic_reinforcements();
  test_character_resources();
  free(simulation);
  qa_content_catalog_destroy(&catalog);
  for (size_t i = 0; i < PRODUCT_COUNT; i++) free(products[i].id);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
