/*
 * Port of tests/persistence/native-weapon.test.ts (the
 * readSavedNativeWeaponDeclaration cases, against fake profile
 * tables standing in for the unported compat/q2 profiles),
 * q1-source.test.ts (capture/encode/restore against a fake
 * machine+source instead of installed content), and the
 * selectQ1SaveProduct cases of saved-game.test.ts, plus synthetic
 * roundtrips for protection.ts and q1-foundation.ts derived from
 * the TS codecs. GPL-2.0-or-later.
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

#include "qa/contracts/identity.h"
#include "qa/persistence/native_weapon.h"
#include "qa/persistence/protection.h"
#include "qa/persistence/q1.h"
#include "qa/persistence/q1_foundation.h"
#include "qa/persistence/q1_quakec.h"
#include "qa/persistence/q1_selection.h"
#include "qa/persistence/q1_source.h"
#include "qa/persistence/value.h"

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

static bool str_eq(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  memcpy(copy, text, length + 1);
  return copy;
}

/* --- native-weapon.test.ts --- */
#define LEGACY_DIGEST \
  "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"

static qa_native_weapon_behavior_declaration fake_declaration(
    const char *digest) {
  qa_native_weapon_behavior_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.version = 1;
  declaration.kind = "q2-api2023-trajectory";
  declaration.abi = "windows-x86-64";
  declaration.artifact_path = "game_x64.dll";
  declaration.artifact_digest = digest;
  declaration.id = "test:weapon";
  declaration.title = "Test weapon";
  declaration.role = "rocket";
  declaration.aspect = "trajectory";
  declaration.time.rva = 0x2000;
  declaration.fire_rva = 0x1000;
  return declaration;
}
static qa_weapon_behavior_definition definition_for(
    qa_module_identity module,
    const qa_native_weapon_behavior_declaration *declaration) {
  qa_weapon_behavior_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.id = declaration->id;
  definition.title = declaration->title;
  definition.module = module;
  definition.role = declaration->role;
  definition.aspect = declaration->aspect;
  definition.fire.kind = QA_WEAPON_CALLBACK_NATIVE_ARTIFACT;
  definition.fire.module = module;
  definition.fire.image_offset = (int64_t)declaration->fire_rva;
  definition.fire.abi =
      (qa_native_call_abi){"windows-x86-64", "pe32+", 8, "microsoft-x64"};
  return definition;
}
static bool fake_built_in(void *ctx, qa_module_identity module,
                          qa_native_weapon_behavior_declaration *out) {
  (void)ctx;
  if (!str_eq(module.digest, LEGACY_DIGEST)) return false;
  *out = fake_declaration(module.digest);
  return true;
}
static qa_native_weapon_behavior_declaration fake_read_declaration(
    void *ctx, const qa_save_value *value, qa_module_identity module) {
  (void)ctx;
  qa_save_reader reader;
  qa_save_reader_init(&reader, value, "native-weapon-profile");
  const qa_save_value *digest = qa_save_value_get(value, "artifactDigest");
  const qa_save_value *path = qa_save_value_get(value, "artifactPath");
  if (digest == NULL || digest->kind != QA_SAVE_STRING ||
      !str_eq(digest->text, module.digest) || path == NULL ||
      path->kind != QA_SAVE_STRING || !str_eq(path->text, module.artifact_path))
    qa_save_reader_fail(&reader, "native profile artifact identity differs");
  qa_save_reader_destroy(&reader);
  return fake_declaration(module.digest);
}
static bool fake_weapon_definition(
    void *ctx, qa_module_identity module,
    const qa_native_weapon_behavior_declaration *declaration,
    qa_weapon_behavior_definition *out) {
  (void)ctx;
  *out = definition_for(module, declaration);
  return true;
}

static qa_save_value *declaration_value(const char *digest) {
  qa_save_value *obj = qa_save_value_object();
  qa_save_value_set(obj, "artifactDigest", qa_save_value_string(digest));
  qa_save_value_set(obj, "artifactPath", qa_save_value_string("game_x64.dll"));
  return obj;
}

static void test_native_weapon(void) {
  qa_native_weapon_profiles profiles = {NULL, fake_built_in,
                                        fake_read_declaration,
                                        fake_weapon_definition};
  qa_module_identity module = {"test:native", "game_x64.dll", LEGACY_DIGEST,
                               LEGACY_DIGEST};
  qa_native_weapon_behavior_declaration declaration =
      fake_declaration(LEGACY_DIGEST);
  qa_weapon_behavior_definition definition =
      definition_for(module, &declaration);
  qa_save_reader legacy;
  CHECK(qa_save_reader_init(&legacy, NULL, "legacy"));
  qa_native_weapon_behavior_declaration read =
      qa_read_saved_native_weapon_declaration(&legacy, &profiles, definition);
  CHECK(str_eq(read.id, declaration.id));
  CHECK(read.time.rva == declaration.time.rva);
  /* TS: changed saved behavior identity is rejected. */
  qa_weapon_behavior_definition changed = definition;
  changed.id = "test:different";
  qa_save_reader changed_reader;
  CHECK(qa_save_reader_init(&changed_reader, NULL, "legacy"));
  CHECK_ABORTS(qa_read_saved_native_weapon_declaration(&changed_reader,
                                                       &profiles, changed));
  qa_save_reader_destroy(&changed_reader);
  /* TS: an unknown native artifact requires its retained explicit
   * declaration. */
  qa_module_identity other = {"test:native", "game_x64.dll",
                              "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                              "declared-build"};
  qa_native_weapon_behavior_declaration declared =
      fake_declaration(other.digest);
  qa_weapon_behavior_definition selected = definition_for(other, &declared);
  qa_save_reader missing;
  CHECK(qa_save_reader_init(&missing, NULL, "legacy"));
  CHECK_ABORTS(qa_read_saved_native_weapon_declaration(&missing, &profiles,
                                                       selected));
  qa_save_reader_destroy(&missing);
  qa_save_value *explicit_value = declaration_value(other.digest);
  qa_save_reader explicit_reader;
  CHECK(qa_save_reader_init(&explicit_reader, explicit_value, "native"));
  read = qa_read_saved_native_weapon_declaration(&explicit_reader, &profiles,
                                                 selected);
  CHECK(str_eq(read.artifact_digest, other.digest));
  qa_save_reader_destroy(&explicit_reader);
  qa_save_value_free(explicit_value);
  /* TS: the legacy declaration does not validate a different
   * artifact. */
  qa_save_value *wrong_value = declaration_value(LEGACY_DIGEST);
  qa_save_reader wrong_reader;
  CHECK(qa_save_reader_init(&wrong_reader, wrong_value, "native"));
  CHECK_ABORTS(qa_read_saved_native_weapon_declaration(&wrong_reader,
                                                       &profiles, selected));
  qa_save_reader_destroy(&wrong_reader);
  qa_save_value_free(wrong_value);
  qa_save_reader_destroy(&legacy);
}

/* --- protection.ts (synthetic) --- */
typedef struct fake_world {
  qa_identity_owner identity;
  qa_owned_actor actors[2];
  qa_actor_observation observations[2];
  int inventory_calls;
} fake_world;

static const qa_actor_observation *fake_observations(void *ctx,
                                                     size_t *out_count) {
  fake_world *world = ctx;
  *out_count = 2;
  return world->observations;
}
static bool fake_resolve_owned(void *ctx, qa_actor_id actor,
                               qa_owned_actor *out) {
  fake_world *world = ctx;
  for (int i = 0; i < 2; i++) {
    if (qa_same_actor(world->actors[i].id, actor)) {
      *out = world->actors[i];
      return true;
    }
  }
  return false;
}
static bool fake_resolve_saved(void *ctx, qa_saved_actor_id saved,
                               qa_owned_actor *out) {
  fake_world *world = ctx;
  for (int i = 0; i < 2; i++) {
    if ((double)world->actors[i].id.slot == saved.slot &&
        (double)world->actors[i].id.generation == saved.generation) {
      *out = world->actors[i];
      return true;
    }
  }
  return false;
}
static void fake_assert_idle(void *ctx) { (void)ctx; }
static bool fake_primary_armor(void *ctx, qa_owned_actor actor,
                               qa_armor_state *out) {
  (void)ctx;
  memset(out, 0, sizeof *out);
  out->regular.kind = QA_REGULAR_ARMOR_NONE;
  out->powered.kind = "none";
  if (actor.id.slot == 1) {
    out->regular.kind = QA_REGULAR_ARMOR_Q1;
    out->regular.points = 80;
    out->regular.absorption = 0.3;
    out->regular.item = "q1:armor/green";
  } else if (actor.id.slot == 2) {
    out->powered.kind = "screen";
    out->powered.cells = 50;
  }
  return true;
}
static qa_provider_id fake_protection_owner(void *ctx, qa_owned_actor actor,
                                            qa_protection_channel channel) {
  (void)ctx;
  if (actor.id.slot == 1 && channel == QA_PROTECTION_REGULAR) return "q1:game";
  if (actor.id.slot == 2 && channel == QA_PROTECTION_POWERED) return "q1:game";
  return NULL;
}
static const qa_item_id *fake_inventory_items(void *ctx, qa_owned_actor actor,
                                              qa_protection_channel channel,
                                              size_t *out_count) {
  fake_world *world = ctx;
  (void)actor;
  (void)channel;
  world->inventory_calls++;
  *out_count = 0;
  return NULL;
}

static void test_protection(void) {
  fake_world world;
  memset(&world, 0, sizeof world);
  CHECK(qa_identity_owner_create(&world.identity, "protection-test"));
  for (int i = 0; i < 2; i++) {
    qa_actor_id id = qa_identity_owner_actor(&world.identity, i + 1, 0);
    world.actors[i] =
        qa_identity_owner_owned_actor(&world.identity, id, "q1:game");
    world.observations[i].id = id;
    world.observations[i].owner = "q1:game";
    world.observations[i].definition = "q1:actor";
  }
  qa_session_actor_registry actors = {.ctx = &world,
                                      .observations = fake_observations,
                                      .resolve_owned = fake_resolve_owned,
                                      .resolve_saved = fake_resolve_saved};
  qa_gameplay_authority combat = {
      .ctx = &world,
      .assert_idle = fake_assert_idle,
      .protection_owner = fake_protection_owner,
      .protection_inventory_items = fake_inventory_items,
      .copied_primary_armor = fake_primary_armor};
  qa_provider_checkpoint record;
  CHECK(qa_capture_primary_protection(&actors, &combat, &record));
  CHECK(str_eq(record.provider, "world:gameplay"));
  CHECK(str_eq(record.schema, "world:primary-protection"));
  CHECK(record.version == 1);
  CHECK(world.inventory_calls == 2);
  qa_save_image save;
  memset(&save, 0, sizeof save);
  save.providers = &record;
  save.provider_count = 1;
  qa_primary_protection read;
  CHECK(qa_read_primary_protection(&save, &actors, &read));
  CHECK(read.recorded);
  CHECK(read.entry_count == 2);
  const qa_primary_protection_entry *first = &read.entries[0];
  const qa_primary_protection_entry *second = &read.entries[1];
  if (first->saved.slot != 1) {
    const qa_primary_protection_entry *swap = first;
    first = second;
    second = swap;
  }
  CHECK(first->has_regular && !first->has_powered);
  CHECK(first->regular.kind == QA_REGULAR_ARMOR_Q1);
  CHECK(first->regular.points == 80);
  CHECK(str_eq(first->owner, "q1:game"));
  CHECK(second->has_powered && !second->has_regular);
  CHECK(str_eq(second->powered.kind, "screen"));
  CHECK(second->powered.cells == 50);
  qa_primary_protection_free(&read);
  /* No record: not recorded, empty entries. */
  qa_save_image empty;
  memset(&empty, 0, sizeof empty);
  CHECK(qa_read_primary_protection(&empty, &actors, &read));
  CHECK(!read.recorded && read.entry_count == 0);
  qa_primary_protection_free(&read);
  /* Duplicate and unsupported records abort. */
  qa_provider_checkpoint records[2] = {record, record};
  qa_save_image duplicate;
  memset(&duplicate, 0, sizeof duplicate);
  duplicate.providers = records;
  duplicate.provider_count = 2;
  CHECK_ABORTS(qa_read_primary_protection(&duplicate, &actors, &read));
  qa_provider_checkpoint unsupported = record;
  unsupported.version = 2;
  qa_save_image bad;
  memset(&bad, 0, sizeof bad);
  bad.providers = &unsupported;
  bad.provider_count = 1;
  CHECK_ABORTS(qa_read_primary_protection(&bad, &actors, &read));
  free((void *)record.bytes.bytes);
  qa_identity_owner_destroy(&world.identity);
}

/* --- saved-game.test.ts: selectQ1SaveProduct --- */
static qa_catalog_product make_product(const char *product_id, const char *id,
                                       const char *family, const char *edition,
                                       const char *directory,
                                       const char *availability,
                                       const qa_content_map *maps,
                                       size_t map_count) {
  qa_catalog_product product;
  memset(&product, 0, sizeof product);
  product.id = product_id;
  product.expectation.id = id;
  product.expectation.family = family;
  product.expectation.edition = edition;
  product.expectation.content_directory = directory;
  product.availability.kind = availability;
  product.maps = maps;
  product.map_count = map_count;
  return product;
}

static void test_q1_selection(void) {
  static const qa_content_map e1m1[] = {{"maps/e1m1.bsp", "pak0", true, 0}};
  static const qa_content_map e1m1_upper[] = {
      {"Maps/E1M1.BSP", "pak0", true, 0}};
  qa_catalog_product products[] = {
      make_product("q1:classic:id1", "q1-classic-id1", "q1", "classic",
                   "q1/id1", "installed", e1m1, 1),
      make_product("q1:rerelease:id1", "q1-rerelease-id1", "q1", "rerelease",
                   "q1/rerelease/id1", "installed", e1m1_upper, 1),
      make_product("q1:classic:hipnotic", "q1-classic-hipnotic", "q1",
                   "classic", "q1/hipnotic", "installed", e1m1, 1),
      make_product("q2:classic:baseq2", "q2-classic-baseq2", "q2", "classic",
                   "q2/baseq2", "installed", e1m1, 1),
      make_product("q1:classic:rogue", "q1-classic-rogue", "q1", "classic",
                   "q1/rogue", "missing", e1m1, 1),
  };
  qa_installed_catalog catalog = {products, 5};
  qa_q1_save_data save;
  memset(&save, 0, sizeof save);
  save.format_version = 5;
  save.map = "e1m1";
  /* TS: explicit native save content selection wins over a
   * directory label. */
  const qa_catalog_product *product = qa_select_q1_save_product(
      &catalog, &save, "q1-rerelease-id1/manual.sav", "q1-classic-id1");
  CHECK(str_eq(product->expectation.id, "q1-classic-id1"));
  /* Selection by product id works too. */
  product = qa_select_q1_save_product(&catalog, &save, "manual.sav",
                                      "q1:rerelease:id1");
  CHECK(str_eq(product->expectation.id, "q1-rerelease-id1"));
  /* TS: a selected source game that does not match aborts. */
  CHECK_ABORTS(qa_select_q1_save_product(&catalog, &save, "manual.sav",
                                         "q2-classic-baseq2"));
  /* The save's containing directory identifies the product. */
  product = qa_select_q1_save_product(&catalog, &save,
                                      "q1-rerelease-id1/manual.sav", NULL);
  CHECK(str_eq(product->expectation.id, "q1-rerelease-id1"));
  /* Ambiguous v5 saves never guess between mods. */
  CHECK_ABORTS(qa_select_q1_save_product(&catalog, &save, "manual.sav", NULL));
  /* Uninstalled map content aborts. */
  save.map = "dm6";
  CHECK_ABORTS(qa_select_q1_save_product(&catalog, &save, "manual.sav", NULL));
  save.map = "e1m1";
  /* Version 6 game directories narrow to the last directory. */
  save.format_version = 6;
  save.game_directories = "id1;hipnotic";
  product = qa_select_q1_save_product(&catalog, &save, "manual.sav", NULL);
  CHECK(str_eq(product->expectation.id, "q1-classic-hipnotic"));
  /* Version 6 edition ties resolve to the rerelease product. */
  save.game_directories = "id1";
  product = qa_select_q1_save_product(&catalog, &save, "manual.sav", NULL);
  CHECK(str_eq(product->expectation.id, "q1-rerelease-id1"));
  /* Malformed v6 directory lists abort. */
  save.game_directories = "id1;bad dir";
  CHECK_ABORTS(qa_select_q1_save_product(&catalog, &save, "manual.sav", NULL));
  save.game_directories = "";
  CHECK_ABORTS(qa_select_q1_save_product(&catalog, &save, "manual.sav", NULL));
}

/* --- q1-source.test.ts (fake machine/source) --- */
#define FAKE_SLOTS 4
#define FAKE_BYTES 16
typedef struct fake_machine {
  size_t count, capacity;
  uint8_t bytes[FAKE_SLOTS][FAKE_BYTES];
  int snapshots;
  char applied[FAKE_SLOTS][64]; /* last applied "key=value" per slot */
  bool saw_zeroed[FAKE_SLOTS];
} fake_machine;

static qa_q1_text_record record_of(const char *const *pairs, size_t count) {
  qa_q1_text_record record;
  record.pairs = count == 0 ? NULL : calloc(count, sizeof *record.pairs);
  record.pair_count = count;
  for (size_t i = 0; i < count; i++) {
    record.pairs[i].key = dup_text(pairs[2 * i]);
    record.pairs[i].value = dup_text(pairs[2 * i + 1]);
  }
  return record;
}
static qa_q1_text_record dup_record(const qa_q1_text_record *record,
                                    bool only_unknown) {
  qa_q1_text_record out = {NULL, 0};
  for (size_t i = 0; i < record->pair_count; i++) {
    const char *key = record->pairs[i].key;
    bool known = str_eq(key, "serverflags") || str_eq(key, "classname") ||
                 str_eq(key, "health") || str_eq(key, "origin");
    if (only_unknown == known) continue;
    out.pairs = realloc(out.pairs, (out.pair_count + 1) * sizeof *out.pairs);
    out.pairs[out.pair_count].key = dup_text(key);
    out.pairs[out.pair_count].value = dup_text(record->pairs[i].value);
    out.pair_count++;
  }
  return out;
}
static void machine_snapshot(void *ctx) { ((fake_machine *)ctx)->snapshots++; }
static size_t machine_count(void *ctx) { return ((fake_machine *)ctx)->count; }
static size_t machine_capacity(void *ctx) {
  return ((fake_machine *)ctx)->capacity;
}
static void machine_set_count(void *ctx, size_t count) {
  ((fake_machine *)ctx)->count = count;
}
static uint8_t *machine_bytes(void *ctx, size_t slot, size_t *out_length) {
  *out_length = FAKE_BYTES;
  return ((fake_machine *)ctx)->bytes[slot];
}
static qa_q1_text_record machine_save_globals(void *ctx) {
  (void)ctx;
  static const char *const pairs[] = {"serverflags", "1"};
  return record_of(pairs, 1);
}
static qa_q1_text_record machine_save_entity(void *ctx, size_t slot,
                                             bool is_free) {
  (void)ctx;
  if (is_free) return record_of(NULL, 0);
  if (slot == 0) {
    static const char *const pairs[] = {"classname", "worldspawn"};
    return record_of(pairs, 1);
  }
  static const char *const pairs[] = {"health", "73", "origin", "520 288 28"};
  return record_of(pairs, 2);
}
static qa_q1_text_record machine_apply_globals(
    void *ctx, const qa_q1_text_record *pairs) {
  (void)ctx;
  return dup_record(pairs, true);
}
static qa_qc_entity_parse_result machine_apply_entity(
    void *ctx, size_t slot, const qa_q1_text_record *pairs) {
  fake_machine *machine = ctx;
  bool zeroed = true;
  for (int i = 0; i < FAKE_BYTES; i++)
    if (machine->bytes[slot][i] != 0) zeroed = false;
  machine->saw_zeroed[slot] = zeroed;
  if (pairs->pair_count > 0) {
    (void)snprintf(machine->applied[slot], sizeof machine->applied[slot],
                   "%s=%s", pairs->pairs[0].key, pairs->pairs[0].value);
  }
  qa_qc_entity_parse_result result;
  result.empty = pairs->pair_count == 0;
  result.unknown = dup_record(pairs, true);
  result.retained = dup_record(pairs, false);
  return result;
}
static qa_qc_machine fake_machine_table(fake_machine *machine) {
  qa_qc_machine table = {machine,
                         machine_snapshot,
                         machine_count,
                         machine_capacity,
                         machine_set_count,
                         machine_bytes,
                         machine_save_globals,
                         machine_save_entity,
                         machine_apply_globals,
                         machine_apply_entity};
  return table;
}

typedef struct fake_source {
  qa_identity_owner identity;
  int checkpoints, unlinks, releases, binds, initializes, clears, links;
  int emits;
  size_t entity_count;
  double finish_time;
} fake_source;

static void source_checkpoint(void *ctx) { ((fake_source *)ctx)->checkpoints++; }
static double source_time(void *ctx) {
  (void)ctx;
  return 42.5;
}
static bool source_loading(void *ctx) {
  (void)ctx;
  return false;
}
static const char *source_light_style(void *ctx, size_t index) {
  (void)ctx;
  return index == 0 ? "abc" : "";
}
static void source_emit(void *ctx, qa_content_id content, size_t style,
                        const char *pattern) {
  fake_source *source = ctx;
  (void)content;
  (void)style;
  (void)pattern;
  source->emits++;
}
static size_t source_entity_count(void *ctx) {
  return ((fake_source *)ctx)->entity_count;
}
static void source_set_entity_count(void *ctx, size_t count) {
  ((fake_source *)ctx)->entity_count = count;
}
static bool source_slot_actor(void *ctx, size_t slot, qa_owned_actor *out) {
  fake_source *source = ctx;
  if (slot >= 3) return false;
  qa_actor_id id = qa_identity_owner_actor(&source->identity, (int64_t)slot, 0);
  *out = qa_identity_owner_owned_actor(&source->identity, id, "quakec:game");
  return true;
}
static bool source_slot_is_free(void *ctx, size_t slot) {
  (void)ctx;
  return slot == 2;
}
static void source_clear_freed(void *ctx, size_t slot, qa_source_time now) {
  (void)slot;
  CHECK(now.kind == QA_SOURCE_TIME_SECONDS && now.value == 42.5);
  ((fake_source *)ctx)->clears++;
}
static qa_owned_actor source_bind_existing(void *ctx, size_t slot,
                                           const char *definition) {
  fake_source *source = ctx;
  CHECK(str_eq(definition, "quakec:edict"));
  source->binds++;
  qa_owned_actor actor;
  CHECK(source_slot_actor(ctx, slot < 3 ? slot : 0, &actor));
  return actor;
}
static void source_initialize(void *ctx, size_t slot, qa_owned_actor actor) {
  (void)slot;
  (void)actor;
  ((fake_source *)ctx)->initializes++;
}
static void source_unlink(void *ctx, qa_owned_actor actor) {
  (void)actor;
  ((fake_source *)ctx)->unlinks++;
}
static void source_release(void *ctx, qa_owned_actor actor) {
  (void)actor;
  ((fake_source *)ctx)->releases++;
}
static void source_link(void *ctx, size_t slot) {
  (void)slot;
  ((fake_source *)ctx)->links++;
}
static void source_finish(void *ctx, qa_q1_save_header header) {
  ((fake_source *)ctx)->finish_time = header.time;
}

static qa_quakec_source fake_source_table(fake_source *source,
                                          qa_qc_machine machine) {
  qa_quakec_source table;
  memset(&table, 0, sizeof table);
  table.ctx = source;
  table.machine = machine;
  table.kind = "netquake";
  table.max_clients = 1;
  table.mode = "singleplayer";
  table.skill = 2;
  table.map_requested_path = "maps/e1m1.bsp";
  table.reserved_client_slots = 1;
  table.owner_content = "q1:classic:id1:installed";
  table.checkpoint = source_checkpoint;
  table.time_seconds = source_time;
  table.loading = source_loading;
  table.light_style = source_light_style;
  table.emit_lightstyle = source_emit;
  table.entity_count = source_entity_count;
  table.set_entity_count = source_set_entity_count;
  table.slot_actor = source_slot_actor;
  table.slot_is_free = source_slot_is_free;
  table.clear_freed_slot = source_clear_freed;
  table.bind_existing_slot = source_bind_existing;
  table.initialize_slot = source_initialize;
  table.unlink_body = source_unlink;
  table.release_actor = source_release;
  table.link_slot = source_link;
  return table;
}

static void test_q1_source(void) {
  fake_machine machine;
  memset(&machine, 0, sizeof machine);
  machine.count = 3;
  machine.capacity = FAKE_SLOTS;
  fake_source source;
  memset(&source, 0, sizeof source);
  CHECK(qa_identity_owner_create(&source.identity, "q1-source-test"));
  qa_quakec_source table =
      fake_source_table(&source, fake_machine_table(&machine));
  double spawn[16];
  for (int i = 0; i < 16; i++) spawn[i] = i;
  qa_q1_save_data save;
  CHECK(qa_capture_q1_source_save(&table, 5, NULL, "my save\there", spawn,
                                  16, "// ext\n", &save));
  CHECK(source.checkpoints == 1);
  CHECK(machine.snapshots == 1);
  CHECK(save.format_version == 5 && save.game_directories == NULL);
  CHECK(str_eq(save.comment, "my_save_here"));
  CHECK(str_eq(save.map, "e1m1"));
  CHECK(save.skill == 2 && save.time == 42.5);
  CHECK(save.spawn_parameter_count == 16 && save.spawn_parameters[3] == 3);
  CHECK(save.light_style_count == 64);
  CHECK(str_eq(save.light_styles[0], "abc"));
  CHECK(str_eq(save.light_styles[1], "m"));
  CHECK(save.global_count == 1 && str_eq(save.globals[0].key, "serverflags"));
  CHECK(save.entity_count == 3);
  CHECK(save.entities[1].pair_count == 2);
  CHECK(str_eq(save.entities[1].pairs[0].key, "health"));
  CHECK(str_eq(save.entities[1].pairs[0].value, "73"));
  CHECK(save.entities[2].pair_count == 0); /* free slot */
  /* TS test: decodeQ1Save(encodeQ1Save(capture)) roundtrip. */
  size_t encoded_length = 0;
  uint8_t *encoded = qa_encode_q1_save(&save, &encoded_length);
  CHECK(encoded != NULL);
  qa_q1_save_data decoded;
  CHECK(qa_decode_q1_save(encoded, encoded_length, &decoded));
  free(encoded);
  CHECK(str_eq(decoded.map, "e1m1") && decoded.entity_count == 3);
  CHECK(str_eq(decoded.entities[1].pairs[1].value, "520 288 28"));
  qa_q1_save_data_free(&save);
  /* Add an unknown field to the player record before restoring. */
  decoded.entities[1].pairs =
      realloc(decoded.entities[1].pairs,
              (decoded.entities[1].pair_count + 1) *
                  sizeof *decoded.entities[1].pairs);
  size_t extra = decoded.entities[1].pair_count++;
  decoded.entities[1].pairs[extra].key = dup_text("mystery");
  decoded.entities[1].pairs[extra].value = dup_text("1");
  /* Restore into a fresh source, as the TS test does. */
  fake_machine machine2;
  memset(&machine2, 0xff, sizeof machine2);
  machine2.count = 3;
  machine2.capacity = FAKE_SLOTS;
  machine2.snapshots = 0;
  memset(machine2.applied, 0, sizeof machine2.applied);
  memset(machine2.saw_zeroed, 0, sizeof machine2.saw_zeroed);
  fake_source source2;
  memset(&source2, 0, sizeof source2);
  source2.identity = source.identity;
  source2.entity_count = 3;
  qa_quakec_source table2 =
      fake_source_table(&source2, fake_machine_table(&machine2));
  qa_q1_unknown_save_fields unknown;
  CHECK(qa_restore_q1_source_save(
      &table2, &decoded,
      (qa_q1_source_finish){&source2, source_finish}, &unknown));
  CHECK(unknown.globals.pair_count == 0);
  CHECK(unknown.entity_count == 1);
  CHECK(unknown.entities[0].slot == 1);
  CHECK(unknown.entities[0].fields.pair_count == 1);
  CHECK(str_eq(unknown.entities[0].fields.pairs[0].key, "mystery"));
  CHECK(source2.finish_time == decoded.time);
  CHECK(str_eq(machine2.applied[1], "health=73"));
  CHECK(machine2.saw_zeroed[0] && machine2.saw_zeroed[1] &&
        machine2.saw_zeroed[2]);
  CHECK(machine2.count == decoded.entity_count);
  CHECK(source2.unlinks == 3);
  CHECK(source2.releases == 1); /* only slot > reservedClientSlots */
  CHECK(source2.clears == 1);   /* the free slot 2 */
  CHECK(source2.binds == 2 && source2.initializes == 2);
  CHECK(source2.links == 2); /* free edicts are not linked */
  CHECK(source2.emits == 64);
  qa_q1_unknown_save_fields_free(&unknown);
  /* Mismatched candidates and saves abort. */
  qa_quakec_source quakeworld = table2;
  quakeworld.kind = "quakeworld";
  CHECK_ABORTS(qa_restore_q1_source_save(
      &quakeworld, &decoded, (qa_q1_source_finish){&source2, source_finish},
      &unknown));
  qa_quakec_source capture_bad = table;
  capture_bad.max_clients = 4;
  qa_q1_save_data ignored;
  CHECK_ABORTS(qa_capture_q1_source_save(&capture_bad, 5, NULL, "x", spawn,
                                         16, NULL, &ignored));
  char *good_map = decoded.map;
  decoded.map = "e1m2";
  CHECK_ABORTS(qa_restore_q1_source_save(
      &table2, &decoded, (qa_q1_source_finish){&source2, source_finish},
      &unknown));
  decoded.map = good_map;
  /* Saved edict count beyond guest capacity aborts in the machine
   * restore (TS SaveFormatError "q1.entities"). */
  machine2.capacity = 2;
  CHECK_ABORTS(qa_restore_q1_source_save(
      &table2, &decoded, (qa_q1_source_finish){&source2, source_finish},
      &unknown));
  machine2.capacity = FAKE_SLOTS;
  qa_q1_save_data_free(&decoded);
  qa_identity_owner_destroy(&source.identity);
}

/* --- q1-foundation.ts (synthetic roundtrip) --- */
static void test_q1_foundation(void) {
  static const char *models[] = {"", "progs/player.mdl"};
  static const char *sounds[] = {"misc/null.wav"};
  static const uint8_t extension_bytes[] = {1, 2, 3};
  qa_q1_foundation_checkpoint in;
  memset(&in, 0, sizeof in);
  in.provider = "q1:foundation";
  in.format = "q1-foundation";
  in.version = 5;
  in.precaches.phase = "frozen";
  in.precaches.models = models;
  in.precaches.model_count = 2;
  in.precaches.sounds = sounds;
  in.precaches.sound_count = 1;
  in.edition = "classic";
  in.time = 12.5;
  in.frame_seconds = 0.05;
  in.force_retouch = 1;
  in.sequence = 7;
  in.next_dynamic_slot = 9;
  in.total_secrets = 3;
  in.found_secrets = 1;
  in.total_monsters = 10;
  in.killed_monsters = 4;
  in.world_type = 2;
  in.map_name = "e1m1";
  in.basis.forward = (qa_vec3){1, 0, 0};
  in.basis.right = (qa_vec3){0, -1, 0};
  in.basis.up = (qa_vec3){0, 0, 1};
  in.has_world = true;
  in.world = (qa_saved_actor_id){0, 1};
  in.sight_time = 3;
  in.has_intermission = true;
  in.intermission.map = "e1m2";
  in.intermission.exit_after = 20;
  qa_q1_saved_entity entity;
  memset(&entity, 0, sizeof entity);
  entity.actor = (qa_saved_actor_id){3, 2};
  entity.actor_provider = "q1:foundation";
  entity.has_source_slot = true;
  entity.source_slot = 3;
  entity.classname = "monster_army";
  entity.has_source_ordinal = true;
  entity.source_ordinal = 11;
  entity.state.model = "progs/soldier.mdl";
  entity.state.frame = 5;
  entity.state.solid = "slidebox";
  entity.state.movement = "step";
  entity.state.target = "t1";
  entity.state.targetname = "";
  entity.state.killtarget = "";
  entity.state.message = "";
  entity.state.original_model = "progs/soldier.mdl";
  entity.state.state = "bottom";
  entity.state.has_trigger_bounds = true;
  entity.state.trigger_bounds =
      (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  entity.state.water_type = -3;
  entity.state.attack_state = "straight";
  qa_q1_saved_field fields[] = {{"targetname", "t1"}};
  entity.fields = fields;
  entity.field_count = 1;
  qa_q1_saved_reference references[] = {
      {"enemy", true, (qa_saved_actor_id){1, 0}}};
  entity.references = references;
  entity.reference_count = 1;
  entity.has_activator = true;
  entity.activator = (qa_saved_actor_id){1, 0};
  qa_saved_actor_id door_group[] = {(qa_saved_actor_id){4, 0}};
  entity.door_group = door_group;
  entity.door_group_count = 1;
  entity.has_monster = true;
  entity.monster.species = "army";
  entity.monster.mode = "run";
  entity.monster.frame_index = 2;
  double sequence[] = {1, 2, 3};
  entity.monster.sequence = sequence;
  entity.monster.sequence_count = 3;
  entity.monster.first_frame = 1;
  entity.monster.has_enemy = true;
  entity.monster.enemy = (qa_saved_actor_id){1, 0};
  entity.monster.path = "path_corner1";
  entity.has_move = true;
  entity.move.destination = (qa_vec3){1, 2, 3};
  entity.move.done = "army_run";
  entity.callbacks.think = "army_think";
  entity.callbacks.die = "army_die";
  in.entities = &entity;
  in.entity_count = 1;
  qa_q1_saved_player player;
  memset(&player, 0, sizeof player);
  player.actor = (qa_saved_actor_id){1, 0};
  player.actor_provider = "q1:foundation";
  player.state.alpha = 0.5;
  player.state.scale = 2;
  player.state.weapon = "rocketlauncher";
  player.state.auto_switch = "new";
  player.state.max_health = 100;
  player.state.view_angles = (qa_vec3){0, 90, 0};
  qa_q1_saved_powerup powerups[] = {{"quad", 99}};
  player.powerups = powerups;
  player.powerup_count = 1;
  in.players = &player;
  in.player_count = 1;
  qa_q1_checkpoint_extension extensions[] = {
      {"ext:test", (qa_byte_view){extension_bytes, 3}}};
  in.extensions = extensions;
  in.extension_count = 1;

  size_t length = 0;
  uint8_t *bytes = qa_encode_q1_foundation_checkpoint(&in, &length);
  CHECK(bytes != NULL);
  qa_q1_foundation_checkpoint out;
  CHECK(qa_decode_q1_foundation_checkpoint(bytes, length, &out));
  CHECK(str_eq(out.provider, "q1:foundation"));
  CHECK(str_eq(out.format, "q1-foundation") && out.version == 5);
  CHECK(str_eq(out.precaches.phase, "frozen"));
  CHECK(out.precaches.model_count == 2);
  CHECK(str_eq(out.precaches.models[1], "progs/player.mdl"));
  CHECK(out.time == 12.5 && out.sequence == 7 && out.next_dynamic_slot == 9);
  CHECK(out.basis.right.y == -1);
  CHECK(out.has_world && out.world.slot == 0 && out.world.generation == 1);
  CHECK(!out.has_sight_entity);
  CHECK(out.has_intermission && str_eq(out.intermission.map, "e1m2"));
  CHECK(out.entity_count == 1);
  const qa_q1_saved_entity *read_entity = &out.entities[0];
  CHECK(read_entity->actor.slot == 3 && read_entity->actor.generation == 2);
  CHECK(read_entity->has_source_slot && read_entity->source_slot == 3);
  CHECK(read_entity->has_source_ordinal && read_entity->source_ordinal == 11);
  CHECK(str_eq(read_entity->classname, "monster_army"));
  CHECK(str_eq(read_entity->state.model, "progs/soldier.mdl"));
  CHECK(str_eq(read_entity->state.solid, "slidebox"));
  CHECK(read_entity->state.water_type == -3);
  CHECK(read_entity->state.has_trigger_bounds);
  CHECK(read_entity->state.trigger_bounds.max.z == 32);
  CHECK(read_entity->state.projectile == NULL);
  CHECK(read_entity->field_count == 1);
  CHECK(str_eq(read_entity->fields[0].value, "t1"));
  CHECK(read_entity->reference_count == 1);
  CHECK(read_entity->references[0].has_actor);
  CHECK(!read_entity->has_owner && read_entity->has_activator);
  CHECK(read_entity->door_group_count == 1);
  CHECK(read_entity->has_monster);
  CHECK(str_eq(read_entity->monster.species, "army"));
  CHECK(read_entity->monster.sequence_count == 3);
  CHECK(read_entity->monster.sequence[2] == 3);
  CHECK(read_entity->has_move && str_eq(read_entity->move.done, "army_run"));
  CHECK(str_eq(read_entity->callbacks.think, "army_think"));
  CHECK(read_entity->callbacks.use == NULL);
  CHECK(out.player_count == 1);
  CHECK(str_eq(out.players[0].state.weapon, "rocketlauncher"));
  CHECK(out.players[0].state.alpha == 0.5 && out.players[0].state.scale == 2);
  CHECK(out.players[0].state.view_angles.y == 90);
  CHECK(out.players[0].powerup_count == 1);
  CHECK(str_eq(out.players[0].powerups[0].kind, "quad"));
  CHECK(out.extension_count == 1);
  CHECK(out.extensions[0].bytes.length == 3);
  CHECK(memcmp(out.extensions[0].bytes.bytes, extension_bytes, 3) == 0);
  qa_q1_foundation_checkpoint_free(&out);
  /* A wrong format version literal aborts the reader. */
  char *text = malloc(length + 1);
  memcpy(text, bytes, length);
  text[length] = '\0';
  char *version = strstr(text, "\"version\":5");
  CHECK(version != NULL);
  version[10] = '6';
  qa_q1_foundation_checkpoint bad;
  CHECK_ABORTS(qa_decode_q1_foundation_checkpoint((const uint8_t *)text,
                                                  length, &bad));
  free(text);
  free(bytes);
}

int main(void) {
  test_native_weapon();
  test_protection();
  test_q1_selection();
  test_q1_source();
  test_q1_foundation();
  if (failures != 0) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("all %d checks passed\n", checks);
  return 0;
}
