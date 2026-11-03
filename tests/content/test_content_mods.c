/*
 * Port of key cases from tests/content/catalog/gameplay-mods.test.ts,
 * monster-roster.test.ts and monsters.test.ts (the catalog/corpus
 * cases that need installed game files are not ported), plus direct
 * reader cases for the mods declaration readers. TypeScript
 * toThrow cases run in a child process and must abort.
 * GPL-2.0-or-later.
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

#include "qa/content/mods/callbacks.h"
#include "qa/content/mods/catalog.h"
#include "qa/content/mods/declaration.h"
#include "qa/content/mods/match.h"
#include "qa/content/mods/pickups.h"
#include "qa/content/mods/qvm_presentation.h"
#include "qa/content/mods/selection.h"
#include "qa/content/mods/source_call.h"
#include "qa/content/monsters/definitions.h"
#include "qa/content/monsters/roster.h"
#include "qa/content/monsters/target.h"

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

static qa_save_reader json_reader(const char *json) {
  qa_save_value *value =
      qa_parse_json_value((const uint8_t *)json, strlen(json), false);
  qa_save_reader reader;
  if (value == NULL || !qa_save_reader_init(&reader, value, NULL)) abort();
  return reader;
}

static void test_target(void) {
  qa_monster_target_observation observation;
  memset(&observation, 0, sizeof observation);
  CHECK(qa_monster_target_eligible(10, &observation));
  CHECK(!qa_monster_target_eligible(0, &observation));
  CHECK(!qa_monster_target_eligible(-1, &observation));
  CHECK(!qa_monster_target_eligible(10, NULL));
  /* bun: monsterTargetEligible(10, {notarget: true, ...}) === true; FL_NOTARGET
     is read only when acquiring a target, never by a monster already chasing. */
  observation.notarget = true;
  CHECK(qa_monster_target_eligible(10, &observation));
}

static void test_monster_sources(void) {
  CHECK(qa_monster_source_count == 16);
  qa_monster_definition_reference reference = {
      {"q1:monsters/classic/id1", "q1:classic:id1:test"}, "monster_army"};
  const qa_monster_source_definition *source = qa_monster_source(reference);
  CHECK(strcmp(source->family, "q1") == 0);
  CHECK(strcmp(source->program, "id1") == 0);
  const qa_monster_creature_entry *army =
      qa_monster_source_creature(source, "monster_army");
  CHECK(army != NULL);
  bool has_attack = false;
  for (size_t i = 0; i < army->resource_count; i++)
    if (strcmp(army->resources[i], "sound/soldier/sattck1.wav") == 0)
      has_attack = true;
  CHECK(has_attack);
  qa_provider_timing timing = qa_monster_timing(source);
  CHECK(timing.clock.kind == QA_CLOCK_Q1_NETQUAKE);
  CHECK(timing.clock.minimum_frame_seconds == 0.001);
  CHECK(timing.clock.maximum_frame_seconds == 0.1);
  qa_monster_definition_reference q2ref = {
      {"q2:monsters/classic/baseq2", "q2:classic:baseq2:test"}, "monster_infantry"};
  timing = qa_monster_timing(qa_monster_source(q2ref));
  CHECK(timing.clock.kind == QA_CLOCK_Q2_CLASSIC);
  CHECK(timing.clock.frame_milliseconds == 100);
  qa_monster_definition_reference q2rr = {
      {"q2:monsters/rerelease/baseq2", "q2:rerelease:baseq2:test"},
      "monster_infantry"};
  timing = qa_monster_timing(qa_monster_source(q2rr));
  CHECK(timing.clock.kind == QA_CLOCK_Q2_RERELEASE);
  CHECK(timing.clock.frame_milliseconds == 25);
  /* Every registered creature appears in its family slot catalog. */
  for (size_t i = 0; i < qa_monster_source_count; i++) {
    size_t slot_count = 0;
    const qa_monster_roster_slot *slots = qa_campaign_monster_slots(
        strcmp(qa_monster_sources[i].family, "q1") == 0 ? QA_GAME_FAMILY_Q1
                                                        : QA_GAME_FAMILY_Q2,
        &slot_count);
    for (size_t j = 0; j < qa_monster_sources[i].creature_count; j++) {
      bool found = false;
      for (size_t k = 0; k < slot_count; k++)
        if (strcmp(slots[k].classname,
                   qa_monster_sources[i].creatures[j].classname) == 0)
          found = true;
      CHECK(found);
    }
  }
  qa_monster_definition_reference bad = {
      {"q1:monsters/classic/id1", "x"}, "monster_nope"};
  CHECK_ABORTS((void)qa_monster_source(bad));
}

static const qa_monster_selection_target *roster_target(
    const qa_enemy_selection *roster, const char *classname) {
  for (size_t i = 0; i < roster->by_classname_count; i++)
    if (strcmp(roster->by_classname[i].classname, classname) == 0)
      return &roster->by_classname[i].target;
  return NULL;
}
static void check_replacement(const char *authored, qa_provider_reference target,
                              const char *classname, const char *replacement) {
  qa_enemy_selection roster;
  CHECK(qa_default_monster_roster(authored, target, NULL, 0, &roster));
  const qa_monster_selection_target *found = roster_target(&roster, classname);
  CHECK(found != NULL);
  if (replacement == NULL) {
    CHECK(found->is_map_defined);
  } else {
    CHECK(!found->is_map_defined);
    CHECK(strcmp(found->reference.classname, replacement) == 0);
    CHECK(strcmp(found->reference.source.provider, target.provider) == 0);
  }
  qa_default_monster_roster_free(&roster);
}

static void test_roster(void) {
  size_t count = 0;
  const qa_monster_roster_slot *slots =
      qa_campaign_monster_slots(QA_GAME_FAMILY_Q3, &count);
  CHECK(count == 0 && slots == NULL);
  (void)qa_campaign_monster_slots(QA_GAME_FAMILY_Q1, &count);
  CHECK(count == 44);
  (void)qa_campaign_monster_slots(QA_GAME_FAMILY_Q2, &count);
  CHECK(count == 45);
  qa_provider_reference q1 = {"q1:monsters/rerelease/id1", "q1:rerelease:id1:test"};
  qa_provider_reference q2 = {"q2:monsters/rerelease/baseq2",
                              "q2:rerelease:baseq2:test"};
  check_replacement("q2", q1, "monster_soldier", "monster_army");
  check_replacement("q2", q1, "monster_soldier_light", "monster_army");
  check_replacement("q2", q1, "monster_infantry", "monster_enforcer");
  check_replacement("q2", q1, "monster_berserk", "monster_demon1");
  check_replacement("q2", q1, "monster_gunner", "monster_ogre");
  check_replacement("q2", q1, "monster_flyer", "monster_wizard");
  check_replacement("q2", q1, "monster_flipper", "monster_fish");
  check_replacement("q2", q1, "monster_tank", "monster_shambler");
  check_replacement("q2", q1, "monster_parasite", "monster_dog");
  check_replacement("q2", q1, "monster_medic", NULL);
  check_replacement("q2", q1, "monster_widow2", NULL);
  check_replacement("q1", q2, "monster_fish", "monster_flipper");
  check_replacement("q1", q2, "monster_dog", "monster_parasite");
  check_replacement("q1", q2, "monster_wrath", "monster_flyer");
  check_replacement("q1", q2, "monster_boss", NULL);
  check_replacement("q1", q2, "monster_zombie", NULL);
  check_replacement("q2", q2, "monster_parasite", NULL);
  check_replacement("q1", q1, "monster_dog", "monster_dog");
  /* Overrides win, including new classnames. */
  qa_monster_by_classname overrides[] = {
      {"monster_soldier",
       {false, {{"q1:monsters/rerelease/id1", "q1:rerelease:id1:test"},
                "monster_ogre"}}},
      {"monster_flipper", {true, {{NULL, NULL}, NULL}}},
      {"monster_custom",
       {false, {{"q1:monsters/rerelease/id1", "q1:rerelease:id1:test"},
                "monster_ogre"}}},
  };
  qa_enemy_selection roster;
  CHECK(qa_default_monster_roster("q2", q1, overrides, 3, &roster));
  CHECK(roster.kind == QA_ENEMY_SELECTION_REPLACE);
  CHECK(roster.default_target.is_map_defined);
  const qa_monster_selection_target *found = roster_target(&roster, "monster_soldier");
  CHECK(found != NULL && !found->is_map_defined &&
        strcmp(found->reference.classname, "monster_ogre") == 0);
  found = roster_target(&roster, "monster_flipper");
  CHECK(found != NULL && found->is_map_defined);
  found = roster_target(&roster, "monster_custom");
  CHECK(found != NULL && !found->is_map_defined);
  qa_default_monster_roster_free(&roster);
  qa_provider_reference unknown = {"q9:monsters/classic/nope", "x"};
  qa_enemy_selection out;
  CHECK_ABORTS((void)qa_default_monster_roster("q1", unknown, NULL, 0, &out));
}

static void test_source_call(void) {
  qa_save_reader reader = json_reader(
      "{\"function\":\"W_Fire\",\"arguments\":[{\"kind\":\"input\",\"name\":\"amount\"},"
      "{\"kind\":\"float\",\"value\":2.5}],\"globals\":[{\"name\":\"time\","
      "\"value\":{\"kind\":\"string\",\"value\":\"x\"}}]}");
  qa_mod_source_call call = qa_read_mod_source_call(&reader);
  qa_save_reader_destroy(&reader);
  CHECK(strcmp(call.function, "W_Fire") == 0);
  CHECK(call.argument_count == 2);
  CHECK(strcmp(call.arguments[0].kind, "input") == 0);
  CHECK(strcmp(call.arguments[0].name, "amount") == 0);
  CHECK(call.arguments[1].number_value == 2.5);
  CHECK(call.global_count == 1);
  CHECK(strcmp(call.globals[0].value.string_value, "x") == 0);
  qa_save_reader bad = json_reader("{\"kind\":\"bogus\"}");
  CHECK_ABORTS((void)qa_read_mod_source_value(&bad));
  qa_save_reader_destroy(&bad);
}

static const char *const HEALTHY_DECL =
    "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\","
    "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
    "\"actorFields\":[],\"callbacks\":[]}";

static void test_quakec_declaration(void) {
  qa_mod_declaration declaration = qa_parse_gameplay_mod_declaration(
      (const uint8_t *)HEALTHY_DECL, strlen(HEALTHY_DECL));
  CHECK(declaration.runtime == QA_MOD_RUNTIME_QUAKEC);
  CHECK(declaration.as.quakec != NULL);
  CHECK(declaration.as.quakec->version == 1);
  CHECK(strcmp(declaration.as.quakec->program.path, "progs.dat") == 0);
  CHECK(declaration.as.quakec->actor_field_count == 0);
  /* transform on an actor operation is rejected */
  static const char *bad =
      "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\","
      "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
      "\"actorFields\":[],\"callbacks\":[{\"id\":\"m:cb\",\"function\":\"f\","
      "\"arguments\":[],\"globals\":[],\"operation\":\"actor.think\","
      "\"stage\":\"transform\",\"result\":\"amount\"}]}";
  CHECK_ABORTS((void)qa_read_mod_callbacks((const uint8_t *)bad, strlen(bad)));
  /* constant actor fields cannot reference callback inputs */
  static const char *bad_field =
      "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"progs.dat\","
      "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
      "\"actorFields\":[{\"field\":\"health\",\"binding\":\"constant\","
      "\"value\":{\"kind\":\"input\",\"name\":\"self\"}}],\"callbacks\":[]}";
  CHECK_ABORTS(
      (void)qa_read_mod_callbacks((const uint8_t *)bad_field, strlen(bad_field)));
  /* invalid resource path */
  static const char *bad_path =
      "{\"version\":1,\"runtime\":\"quakec\",\"program\":{\"path\":\"../progs.dat\","
      "\"digest\":\"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},"
      "\"actorFields\":[],\"callbacks\":[]}";
  CHECK_ABORTS(
      (void)qa_read_mod_callbacks((const uint8_t *)bad_path, strlen(bad_path)));
}

static void read_call_adapter(void *ctx, const qa_save_reader *reader, void *out) {
  (void)ctx;
  *(qa_mod_source_call *)out = qa_read_mod_source_call(reader);
}

static void test_match_and_pickups(void) {
  qa_save_reader reader =
      json_reader("[{\"value\":0,\"team\":null},{\"value\":1,\"team\":\"red\"}]");
  size_t count = 0;
  qa_source_team_value *values = qa_read_source_team_values(&reader, &count);
  qa_save_reader_destroy(&reader);
  CHECK(count == 2 && values != NULL);
  CHECK(values[0].team == NULL);
  CHECK(strcmp(values[1].team, "red") == 0);
  qa_save_reader dup =
      json_reader("[{\"value\":0,\"team\":\"red\"},{\"value\":0,\"team\":\"blue\"}]");
  size_t dup_count = 0;
  CHECK_ABORTS((void)qa_read_source_team_values(&dup, &dup_count));
  qa_save_reader_destroy(&dup);

  qa_save_reader rule_reader = json_reader(
      "{\"id\":\"shells\",\"offered\":[\"q1:item_shells\"],"
      "\"resource\":{\"kind\":\"inventory\",\"item\":\"q1:ammo_shells\"},"
      "\"operation\":{\"kind\":\"boolean-grant\",\"grant\":{\"function\":\"give\","
      "\"arguments\":[],\"globals\":[]}}}");
  qa_mod_pickup_rule rule = qa_read_mod_pickup_rule(&rule_reader, read_call_adapter,
                                                    NULL, sizeof(qa_mod_source_call));
  qa_save_reader_destroy(&rule_reader);
  CHECK(strcmp(rule.id, "shells") == 0);
  CHECK(rule.writes.count == 1);
  CHECK(strcmp(rule.writes.values[0].fields, "count") == 0);
  CHECK(!rule.operation.is_gate_then_grant);
  qa_save_reader both = json_reader(
      "{\"id\":\"x\",\"offered\":[\"q1:item_shells\"],"
      "\"resource\":{\"kind\":\"inventory\",\"item\":\"q1:ammo_shells\"},"
      "\"writes\":[{\"kind\":\"inventory\",\"item\":\"q1:ammo_shells\","
      "\"fields\":\"count\"}],\"operation\":{\"kind\":\"boolean-grant\","
      "\"grant\":{\"function\":\"g\",\"arguments\":[],\"globals\":[]}}}");
  CHECK_ABORTS((void)qa_read_mod_pickup_rule(&both, read_call_adapter, NULL,
                                             sizeof(qa_mod_source_call)));
  qa_save_reader_destroy(&both);
}

static qa_mod_description make_description(const char *product, const char *id,
                                           const char *title, bool available) {
  qa_mod_description description;
  memset(&description, 0, sizeof description);
  description.selection.product = product;
  description.selection.id = id;
  description.title = title;
  description.purpose = "addition";
  description.is_available = available;
  description.unavailable_reason = available ? NULL : "Missing callback declaration absent.json";
  return description;
}

static void test_selection_set(void) {
  qa_mod_description descriptions[] = {
      make_description("q1-classic-id1", "healthy", "Healthy", true),
      make_description("q1-classic-id1", "missing", "Missing", false),
  };
  qa_mod_selection_set set;
  CHECK(qa_mod_selection_set_init(&set, descriptions, 2, NULL, 0));
  qa_mod_selection healthy = {"q1-classic-id1", "healthy"};
  qa_mod_selection_set_set_enabled(&set, healthy, true);
  size_t count = 0;
  const qa_mod_selection *enabled = qa_mod_selection_set_enabled(&set, &count);
  CHECK(count == 1);
  CHECK(strcmp(enabled[0].id, "healthy") == 0);
  CHECK(qa_mod_selection_set_has(&set, healthy));
  qa_mod_selection missing = {"q1-classic-id1", "missing"};
  CHECK_ABORTS(qa_mod_selection_set_set_enabled(&set, missing, true));
  qa_mod_selection_set_set_enabled(&set, healthy, false);
  (void)qa_mod_selection_set_enabled(&set, &count);
  CHECK(count == 0);
  qa_mod_selection_set_destroy(&set);
  /* Duplicate declarations abort. */
  qa_mod_description dupes[] = {
      make_description("p", "a", "A", true), make_description("p", "a", "A2", true)};
  qa_mod_selection_set dup_set;
  CHECK_ABORTS((void)qa_mod_selection_set_init(&dup_set, dupes, 2, NULL, 0));
}

/* Fake MountedContent for catalog discovery. */
struct fake_mount {
  const char *components;
  const char *program_digest;
};
static bool fake_open(void *ctx, const char *path, const char *filter,
                      qa_mod_catalog_file *out) {
  struct fake_mount *mount = (struct fake_mount *)ctx;
  (void)filter;
  memset(out, 0, sizeof *out);
  if (strcmp(path, "gameplay-mods.json") == 0) {
    out->bytes = (const uint8_t *)mount->components;
    out->length = strlen(mount->components);
    out->digest = "sha256:bbbb";
    return true;
  }
  if (strcmp(path, "healthy.json") == 0) {
    out->bytes = (const uint8_t *)HEALTHY_DECL;
    out->length = strlen(HEALTHY_DECL);
    out->digest = "sha256:cccc";
    return true;
  }
  if (strcmp(path, "progs.dat") == 0) {
    static const uint8_t bytes[] = {1, 2, 3};
    out->bytes = bytes;
    out->length = sizeof bytes;
    out->digest = mount->program_digest;
    return true;
  }
  return false;
}

static void test_catalog(void) {
  static const char *components =
      "{\"version\":1,\"components\":["
      "{\"id\":\"rules\",\"title\":\"Whole game rules\",\"purpose\":\"game-type\"},"
      "{\"id\":\"missing\",\"title\":\"missing\",\"purpose\":\"addition\","
      "\"callbacks\":\"absent.json\",\"requires\":[],\"conflicts\":[]},"
      "{\"id\":\"healthy\",\"title\":\"healthy\",\"purpose\":\"addition\","
      "\"callbacks\":\"healthy.json\",\"requires\":[],\"conflicts\":[]}]}";
  struct fake_mount mount = {components,
                             "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
  qa_mod_mounted_content mounted = {&mount, fake_open};
  qa_mod_catalog_product product;
  memset(&product, 0, sizeof product);
  product.id = "q1:classic:id1:installed";
  product.expectation.id = "q1-classic-id1";
  product.expectation.title = "Quake";
  product.expectation.family = "q1";
  product.expectation.edition = "classic";
  size_t count = 0;
  qa_discovered_gameplay_mod *found =
      qa_discover_gameplay_mods(&product, &mounted, &count);
  CHECK(found != NULL);
  CHECK(count == 2);
  CHECK(!found[0].is_available);
  CHECK(strcmp(found[0].description.selection.id, "missing") == 0);
  CHECK(strcmp(found[0].description.unavailable_reason,
               "Missing callback declaration absent.json") == 0);
  CHECK(found[1].is_available);
  CHECK(strcmp(found[1].mod.selection.id, "healthy") == 0);
  CHECK(strcmp(found[1].mod.source_title, "Quake (Q1)") == 0);
  CHECK(strcmp(found[1].mod.source.provider, "q1:official") == 0);
  /* Stale executable digest becomes unavailable. */
  mount.program_digest = "sha256:dddd";
  found = qa_discover_gameplay_mods(&product, &mounted, &count);
  CHECK(count == 2);
  CHECK(!found[1].is_available);
  CHECK(strcmp(found[1].description.unavailable_reason,
               "Executable differs from its callback declaration") == 0);
}

int main(void) {
  test_target();
  test_monster_sources();
  test_roster();
  test_source_call();
  test_quakec_declaration();
  test_match_and_pickups();
  test_selection_set();
  test_catalog();
  if (failures == 0) printf("test_content_mods: %d checks passed\n", checks);
  return failures == 0 ? 0 : 1;
}
