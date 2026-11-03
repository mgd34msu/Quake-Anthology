/*
 * Port of tests/persistence/q2-classic-guest.test.ts,
 * saved-game.test.ts (decodeSavedGame cases; corpus and
 * bootstrap-request cases are not ported), and the world-state /
 * source-items cases of save.test.ts (powered deferred restore,
 * attachments, prebound disagreement, mixed source items), against
 * in-memory fakes of the world tables world-state.ts drives.
 * TS toThrow cases run in a child process and must abort.
 * GPL-2.0-or-later.
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
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
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/saved_game.h"
#include "qa/persistence/source_items.h"
#include "qa/persistence/world_state.h"

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

static bool bytes_equal(qa_byte_view view, const uint8_t *bytes, size_t length) {
  return view.length == length && memcmp(view.bytes, bytes, length) == 0;
}

/* ---------------- q2-classic-guest ---------------- */
static uint8_t *cvars_bytes;
static size_t cvars_length;
static qa_q2_classic_configstring_entry server_configstrings[] = {{0, "base1"}};
static qa_q2_classic_portal_entry server_portals[] = {{1, true}};
static void server_state_fill(void *ctx, qa_q2_classic_original_server_state *out) {
  (void)ctx;
  memset(out, 0, sizeof *out);
  out->state.configstrings = server_configstrings;
  out->state.configstring_count = 1;
  out->state.portals = server_portals;
  out->state.portal_count = 1;
  out->cvars.bytes = cvars_bytes;
  out->cvars.length = cvars_length;
}
static qa_classic_original_save_files *guest_files;
static void write_file(const char *path, const uint8_t *bytes, size_t length) {
  qa_q2_classic_file *file = qa_classic_original_save_files_open_file(
      guest_files, path, false, true, 2);
  if (file == NULL) {
    fprintf(stderr, "missing callback file\n");
    abort();
  }
  if (file->write(file->ctx, 0, bytes, length) != (int64_t)length) abort();
  file->flush(file->ctx);
  file->close(file->ctx);
}
static bool saw_autosave;
static void capture_write(void *ctx, const char *game, const char *level,
                          bool autosave) {
  (void)ctx;
  saw_autosave = autosave;
  write_file(game, (const uint8_t *)"\x01\x02\x03", 3);
  write_file(level, (const uint8_t *)"\x04\x05", 2);
}
static uint8_t restored_game[3];
static uint8_t restored_level[2];
static void restore_read(void *ctx, const char *game, const char *level) {
  (void)ctx;
  const char *paths[2] = {game, level};
  uint8_t *targets[2] = {restored_game, restored_level};
  for (int i = 0; i < 2; i++) {
    qa_q2_classic_file *file = qa_classic_original_save_files_open_file(
        guest_files, paths[i], true, false, 3);
    if (file == NULL) abort();
    int64_t size = file->size(file->ctx);
    if (file->read(file->ctx, 0, targets[i], (size_t)size) != (size_t)size) abort();
    file->close(file->ctx);
  }
}
static void capture_game_only(void *ctx, const char *game, const char *level,
                              bool autosave) {
  (void)ctx;
  (void)level;
  (void)autosave;
  write_file(game, (const uint8_t *)"\x01", 1);
}
static void read_nothing(void *ctx, const char *game, const char *level) {
  (void)ctx;
  (void)game;
  (void)level;
}
static void restore_write_attempt(void *ctx, const char *game, const char *level) {
  (void)ctx;
  (void)level;
  (void)qa_classic_original_save_files_open_file(guest_files, game, true, true, 3);
}
static void travel_write(void *ctx, const char *level) {
  (void)ctx;
  write_file(level, (const uint8_t *)"\x08\x09\x0a", 3);
}
static void travel_write_nothing(void *ctx, const char *level) {
  (void)ctx;
  (void)level;
}
static void travel_open_game(void *ctx, const char *level) {
  (void)ctx;
  (void)level;
  (void)qa_classic_original_save_files_open_file(
      guest_files, "__qts_original_save/game.ssv", false, true, 2);
}
static qa_q2_classic_file *retained_file;
static int travel_owner;
static void *travel_read(void *ctx, const char *level) {
  uint8_t *source = (uint8_t *)ctx;
  qa_q2_classic_file *file = qa_classic_original_save_files_open_file(
      guest_files, level, true, false, 3);
  if (file == NULL) abort();
  source[0] = 99; /* the overlay holds its own copy */
  uint8_t seen[3] = {0, 0, 0};
  if (file->read(file->ctx, 0, seen, 3) != 3 || seen[0] != 8 || seen[1] != 9 ||
      seen[2] != 10) abort();
  retained_file = file;
  file->close(file->ctx);
  return &travel_owner;
}
static void *travel_read_nothing(void *ctx, const char *level) {
  (void)ctx;
  (void)level;
  return NULL;
}
static int fallback_calls;
static qa_q2_classic_file *fallback_open(void *ctx, const char *path, bool read,
                                         bool write, int creation) {
  (void)ctx;
  (void)read;
  (void)write;
  (void)creation;
  if (strcmp(path, "game.log") == 0) fallback_calls++;
  return NULL;
}
static qa_q2_classic_original_identity guest_identity(void) {
  qa_q2_classic_original_identity identity;
  identity.module.id = "q2:classic";
  identity.module.artifact_path = "gamex86.dll";
  identity.module.revision = "source-build";
  identity.module.digest =
      "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  identity.map = "maps/base1.bsp";
  return identity;
}
static bool saves_equal(const qa_q2_classic_original_save *left,
                        const qa_q2_classic_original_save *right) {
  if (strcmp(left->module.id, right->module.id) != 0 ||
      strcmp(left->module.artifact_path, right->module.artifact_path) != 0 ||
      strcmp(left->module.digest, right->module.digest) != 0 ||
      strcmp(left->module.revision, right->module.revision) != 0 ||
      strcmp(left->map, right->map) != 0 || left->autosave != right->autosave ||
      !bytes_equal(left->game, right->game.bytes, right->game.length) ||
      !bytes_equal(left->level, right->level.bytes, right->level.length) ||
      !bytes_equal(left->server.cvars, right->server.cvars.bytes,
                   right->server.cvars.length) ||
      left->visited_level_count != right->visited_level_count) return false;
  const qa_q2_classic_level_state *a = &left->server.state;
  const qa_q2_classic_level_state *b = &right->server.state;
  if (a->configstring_count != b->configstring_count ||
      a->portal_count != b->portal_count) return false;
  for (size_t i = 0; i < a->configstring_count; i++)
    if (a->configstrings[i].index != b->configstrings[i].index ||
        strcmp(a->configstrings[i].value, b->configstrings[i].value) != 0)
      return false;
  for (size_t i = 0; i < a->portal_count; i++)
    if (a->portals[i].portal != b->portals[i].portal ||
        a->portals[i].open != b->portals[i].open) return false;
  for (size_t i = 0; i < left->visited_level_count; i++) {
    const qa_q2_classic_visited_level *x = &left->visited_levels[i];
    const qa_q2_classic_visited_level *y = &right->visited_levels[i];
    if (x->version != y->version || strcmp(x->map, y->map) != 0 ||
        !bytes_equal(x->level, y->level.bytes, y->level.length) ||
        x->state.configstring_count != y->state.configstring_count ||
        x->state.portal_count != y->state.portal_count) return false;
    for (size_t j = 0; j < x->state.configstring_count; j++)
      if (x->state.configstrings[j].index != y->state.configstrings[j].index ||
          strcmp(x->state.configstrings[j].value,
                 y->state.configstrings[j].value) != 0) return false;
    for (size_t j = 0; j < x->state.portal_count; j++)
      if (x->state.portals[j].portal != y->state.portals[j].portal ||
          x->state.portals[j].open != y->state.portals[j].open) return false;
  }
  return true;
}
static void test_q2_classic_guest(void) {
  qa_save_value *cvars = qa_save_value_object();
  CHECK(cvars != NULL);
  CHECK(qa_save_value_set(cvars, "skill", qa_save_value_string("1")));
  cvars_bytes = qa_encode_checkpoint_value(cvars, &cvars_length);
  qa_save_value_free(cvars);
  CHECK(cvars_bytes != NULL);

  qa_q2_classic_original_identity identity = guest_identity();
  guest_files = qa_classic_original_save_files_create(fallback_open, NULL);
  CHECK(guest_files != NULL);
  CHECK(qa_classic_original_save_files_open_file(guest_files, "game.log",
                                                 false, true, 4) == NULL);
  CHECK(fallback_calls == 1);

  qa_q2_classic_original_save save;
  CHECK(qa_classic_original_save_files_capture(guest_files, identity,
      server_state_fill, NULL, capture_write, NULL, true, &save));
  CHECK(saw_autosave);
  CHECK(bytes_equal(save.game, (const uint8_t *)"\x01\x02\x03", 3));
  CHECK(bytes_equal(save.level, (const uint8_t *)"\x04\x05", 2));

  qa_provider_checkpoint record;
  CHECK(qa_encode_q2_classic_original_save(&save, &record));
  CHECK(strcmp(record.provider, "q2:classic") == 0);
  CHECK(strcmp(record.schema, "q2:classic-native-original") == 0);
  CHECK(record.version == 1);
  qa_q2_classic_original_save decoded;
  CHECK(qa_decode_q2_classic_original_save(&record, identity, &decoded));
  CHECK(saves_equal(&decoded, &save));
  qa_classic_original_save_files_restore(guest_files, &decoded, identity,
                                         restore_read, NULL);
  CHECK(memcmp(restored_game, "\x01\x02\x03", 3) == 0);
  CHECK(memcmp(restored_level, "\x04\x05", 2) == 0);
  CHECK_ABORTS((void)qa_classic_original_save_files_open_file(
      guest_files, "__qts_original_save/game.ssv", true, false, 3));

  qa_q2_classic_original_identity changed = identity;
  changed.module.revision = "changed";
  CHECK_ABORTS(qa_classic_original_save_files_restore(guest_files, &save,
                                                      changed, read_nothing, NULL));
  CHECK_ABORTS(qa_classic_original_save_files_restore(guest_files, &save,
                                                      identity, read_nothing, NULL));
  CHECK_ABORTS(qa_classic_original_save_files_restore(
      guest_files, &save, identity, restore_write_attempt, NULL));
  qa_q2_classic_original_save partial;
  CHECK_ABORTS((void)qa_classic_original_save_files_capture(
      guest_files, identity, server_state_fill, NULL, capture_game_only, NULL,
      false, &partial));

  uint8_t *travel = NULL;
  size_t travel_length = 0;
  CHECK(qa_classic_original_save_files_capture_travel_level(
      guest_files, travel_write, NULL, &travel, &travel_length));
  CHECK(travel_length == 3 && memcmp(travel, "\x08\x09\x0a", 3) == 0);
  CHECK_ABORTS((void)qa_classic_original_save_files_capture_travel_level(
      guest_files, travel_open_game, NULL, &travel, &travel_length));
  void *owner = qa_classic_original_save_files_with_travel_level(
      guest_files, travel, travel_length, travel_read, travel);
  CHECK(owner == &travel_owner);
  CHECK_ABORTS((void)retained_file->size(retained_file->ctx));
  uint8_t one = 1;
  CHECK_ABORTS((void)qa_classic_original_save_files_with_travel_level(
      guest_files, &one, 0, travel_read_nothing, NULL));
  CHECK_ABORTS((void)qa_classic_original_save_files_with_travel_level(
      guest_files, &one, 1, travel_read_nothing, NULL));
  uint8_t *again = NULL;
  size_t again_length = 0;
  CHECK_ABORTS((void)qa_classic_original_save_files_capture_travel_level(
      guest_files, travel_write_nothing, NULL, &again, &again_length));
  CHECK(qa_classic_original_save_files_capture_travel_level(
      guest_files, travel_write, NULL, &again, &again_length));
  CHECK(again_length == 3 && again[0] == 8);
  free(travel);
  free(again);

  /* Versioned visited levels roundtrip; invalid ones reject. */
  qa_q2_classic_configstring_entry visited_cs[] = {{1, "departed"}};
  qa_q2_classic_portal_entry visited_portals[] = {{2, false}};
  qa_q2_classic_visited_level visited;
  memset(&visited, 0, sizeof visited);
  visited.version = 1;
  visited.map = "maps/base2.bsp";
  visited.level.bytes = (const uint8_t *)"\x03\x04";
  visited.level.length = 2;
  visited.state.configstrings = visited_cs;
  visited.state.configstring_count = 1;
  visited.state.portals = visited_portals;
  visited.state.portal_count = 1;
  qa_q2_classic_original_save history = save;
  history.visited_levels = &visited;
  history.visited_level_count = 1;
  history.root = NULL;
  qa_provider_checkpoint history_record;
  CHECK(qa_encode_q2_classic_original_save(&history, &history_record));
  qa_q2_classic_original_save history_decoded;
  CHECK(qa_decode_q2_classic_original_save(&history_record, identity,
                                           &history_decoded));
  CHECK(saves_equal(&history_decoded, &history));
  qa_q2_classic_original_save_free(&history_decoded);
  free((void *)history_record.bytes.bytes);

  qa_q2_classic_visited_level bad = visited;
  qa_q2_classic_original_save bad_save = history;
  bad_save.visited_levels = &bad;
  qa_provider_checkpoint scratch;
  bad.map = identity.map;
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));
  bad.map = "maps/../escape.bsp";
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));
  bad.map = "base2.bsp";
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));
  bad.map = "maps/a:b.bsp";
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));
  bad.map = "maps/base2.bsp";
  bad.level.length = 0;
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));
  bad.level.length = 2;
  qa_q2_classic_configstring_entry dup_cs[] = {{1, "a"}, {1, "b"}};
  bad.state.configstrings = dup_cs;
  bad.state.configstring_count = 2;
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));
  qa_q2_classic_configstring_entry far_cs[] = {{2080, "invalid"}};
  bad.state.configstrings = far_cs;
  bad.state.configstring_count = 1;
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));
  bad.state.configstrings = visited_cs;
  qa_q2_classic_portal_entry far_portal[] = {{1024, true}};
  bad.state.portals = far_portal;
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));
  bad.state.portals = visited_portals;
  qa_q2_classic_visited_level pair[2] = {visited, visited};
  bad_save.visited_levels = pair;
  bad_save.visited_level_count = 2;
  CHECK_ABORTS((void)qa_encode_q2_classic_original_save(&bad_save, &scratch));

  qa_q2_classic_original_save_free(&decoded);
  qa_q2_classic_original_save_free(&save);
  free((void *)record.bytes.bytes);
  qa_classic_original_save_files_destroy(guest_files);
  guest_files = NULL;
  free(cvars_bytes);
  cvars_bytes = NULL;
}

/* ---------------- saved-game ---------------- */
static qa_q1_save_data q1_fixture(int version) {
  qa_q1_save_data data;
  memset(&data, 0, sizeof data);
  data.format_version = version;
  data.game_directories = version == 6 ? (char *)"id1;hipnotic" : NULL;
  data.comment = (char *)"native_save";
  static double spawn[16];
  for (int i = 0; i < 16; i++) spawn[i] = (double)i;
  data.spawn_parameters = spawn;
  data.spawn_parameter_count = 16;
  data.skill = 2;
  data.map = (char *)"e1m1";
  data.time = 42.5;
  static char *styles[64];
  for (int i = 0; i < 64; i++) styles[i] = (char *)"m";
  data.light_styles = styles;
  data.light_style_count = 64;
  static qa_q1_text_pair globals[] = {{(char *)"serverflags", (char *)"1"}};
  data.globals = globals;
  data.global_count = 1;
  static qa_q1_text_pair world[] = {{(char *)"classname", (char *)"worldspawn"}};
  static qa_q1_text_pair player[] = {{(char *)"health", (char *)"73"}};
  static qa_q1_text_record entities[2];
  entities[0].pairs = world;
  entities[0].pair_count = 1;
  entities[1].pairs = player;
  entities[1].pair_count = 1;
  data.entities = entities;
  data.entity_count = 2;
  data.extension_text = (char *)"// source extension\n";
  data.extension_length = strlen(data.extension_text);
  return data;
}
static void test_saved_game(void) {
  for (int version = 5; version <= 6; version++) {
    qa_q1_save_data data = q1_fixture(version);
    size_t length = 0;
    uint8_t *bytes = qa_encode_q1_save(&data, &length);
    CHECK(bytes != NULL);
    qa_saved_game decoded;
    CHECK(qa_decode_saved_game(bytes, length, &decoded));
    CHECK(decoded.kind == QA_SAVED_GAME_Q1_SOURCE);
    CHECK(decoded.data.format_version == version);
    CHECK(strcmp(decoded.data.map, "e1m1") == 0);
    CHECK(decoded.data.skill == 2);
    CHECK(decoded.data.time == 42.5);
    CHECK(decoded.data.entity_count == 2);
    CHECK(decoded.data.extension_length == strlen("// source extension\n"));
    CHECK(memcmp(decoded.data.extension_text, "// source extension\n",
                 decoded.data.extension_length) == 0);
    if (version == 6)
      CHECK(strcmp(decoded.data.game_directories, "id1;hipnotic") == 0);
    qa_saved_game_free(&decoded);
    free(bytes);
  }
  qa_saved_game scratch;
  CHECK_ABORTS((void)qa_decode_saved_game((const uint8_t *)"QTSAVE9\n", 8, &scratch));
  qa_q1_save_data bad_map = q1_fixture(5);
  bad_map.map = (char *)"../e1m1";
  size_t bad_length = 0;
  uint8_t *bad_bytes = qa_encode_q1_save(&bad_map, &bad_length);
  CHECK(bad_bytes != NULL);
  CHECK_ABORTS((void)qa_decode_saved_game(bad_bytes, bad_length, &scratch));
  free(bad_bytes);
  qa_q1_save_data no_entities = q1_fixture(5);
  no_entities.entities = NULL;
  no_entities.entity_count = 0;
  bad_bytes = qa_encode_q1_save(&no_entities, &bad_length);
  CHECK(bad_bytes != NULL);
  CHECK_ABORTS((void)qa_decode_saved_game(bad_bytes, bad_length, &scratch));
  free(bad_bytes);

  /* Contained write + read roundtrip. */
  char directory[] = "/tmp/qa-saved-game-XXXXXX";
  CHECK(mkdtemp(directory) != NULL);
  qa_saved_game written;
  memset(&written, 0, sizeof written);
  written.kind = QA_SAVED_GAME_Q1_SOURCE;
  written.data = q1_fixture(5);
  char path[512];
  (void)snprintf(path, sizeof path, "%s/manual.sav", directory);
  CHECK(qa_write_saved_game(directory, path, &written));
  memset(&written.data, 0, sizeof written.data); /* fixture is static; no free */
  qa_saved_game loaded;
  CHECK(qa_read_saved_game(path, &loaded));
  CHECK(loaded.kind == QA_SAVED_GAME_Q1_SOURCE);
  CHECK(strcmp(loaded.data.map, "e1m1") == 0);
  qa_saved_game_free(&loaded);
  (void)remove(path);
  (void)rmdir(directory);
}

/* ---------------- fake world tables ---------------- */
#define FAKE_MAX 8
typedef struct fake_actor {
  bool used;
  qa_owned_actor owned;
  qa_saved_actor_id saved;
  qa_world_storage_kind storage;
  bool has_body;
  qa_body_state body;
  qa_actor_id ground_storage;
  qa_persist_body_link_state links;
  qa_actor_id linked_ground_storage;
  bool has_attachment;
  qa_body_attachment attachment;
  int restore_link_calls;
  double restored_link_count;
  bool has_combat;
  qa_combat_state combat;
  bool has_inventory;
  qa_inventory_entry entries[4];
  size_t entry_count;
  const qa_source_item_snapshot *snapshot; /* NULL = no source items */
} fake_actor;
typedef struct fake_world {
  qa_identity_owner owner;
  fake_actor actors[FAKE_MAX];
  qa_actor_observation observations[FAKE_MAX];
  size_t observation_count;
  int assert_idle_calls;
  qa_session_actor_registry registry;
  qa_shared_body_table bodies;
  qa_gameplay_authority combat;
  qa_shared_inventory_table inventory;
} fake_world;
static fake_actor *fake_find(fake_world *world, qa_actor_id id) {
  for (size_t i = 0; i < FAKE_MAX; i++)
    if (world->actors[i].used && qa_same_actor(world->actors[i].owned.id, id))
      return &world->actors[i];
  return NULL;
}
static const qa_actor_observation *fake_observations(void *ctx, size_t *out_count) {
  fake_world *world = (fake_world *)ctx;
  *out_count = world->observation_count;
  return world->observations;
}
static bool fake_resolve_owned(void *ctx, qa_actor_id id, qa_owned_actor *out) {
  fake_actor *actor = fake_find((fake_world *)ctx, id);
  if (actor == NULL) return false;
  *out = actor->owned;
  return true;
}
static bool fake_resolve_saved(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out) {
  fake_world *world = (fake_world *)ctx;
  for (size_t i = 0; i < FAKE_MAX; i++) {
    if (!world->actors[i].used) continue;
    if (world->actors[i].saved.slot == saved.slot &&
        world->actors[i].saved.generation == saved.generation) {
      *out = world->actors[i].owned;
      return true;
    }
  }
  return false;
}
static qa_actor_id fake_reference_saved(void *ctx, qa_saved_actor_id saved) {
  fake_world *world = (fake_world *)ctx;
  for (size_t i = 0; i < FAKE_MAX; i++) {
    if (!world->actors[i].used) continue;
    if (world->actors[i].saved.slot == saved.slot &&
        world->actors[i].saved.generation == saved.generation)
      return world->actors[i].owned.id;
  }
  return qa_identity_owner_actor(&world->owner, (int64_t)saved.slot,
                                 (int64_t)saved.generation);
}
static bool fake_body_read(void *ctx, qa_actor_id id, qa_body_state *out) {
  fake_actor *actor = fake_find((fake_world *)ctx, id);
  if (actor == NULL || !actor->has_body) return false;
  *out = actor->body;
  return true;
}
static bool fake_link_state(void *ctx, qa_actor_id id, qa_persist_body_link_state *out) {
  fake_actor *actor = fake_find((fake_world *)ctx, id);
  if (actor == NULL || !actor->has_body) return false;
  *out = actor->links;
  return true;
}
static bool fake_attachment(void *ctx, qa_actor_id id, qa_body_attachment *out) {
  fake_actor *actor = fake_find((fake_world *)ctx, id);
  if (actor == NULL || !actor->has_attachment) return false;
  *out = actor->attachment;
  return true;
}
static void fake_restore_links(void *ctx, qa_owned_actor owned, qa_persist_body_link_state state) {
  fake_actor *actor = fake_find((fake_world *)ctx, owned.id);
  if (actor == NULL) abort();
  actor->links = state;
  if (state.has_linked && state.state.ground != NULL) {
    actor->linked_ground_storage = *state.state.ground;
    actor->links.state.ground = &actor->linked_ground_storage;
  }
  actor->restore_link_calls++;
  actor->restored_link_count = state.link_count;
}
static void fake_body_create(void *ctx, qa_owned_actor owned, qa_body_state state) {
  fake_actor *actor = fake_find((fake_world *)ctx, owned.id);
  if (actor == NULL) abort();
  actor->has_body = true;
  actor->body = state;
  if (state.ground != NULL) {
    actor->ground_storage = *state.ground;
    actor->body.ground = &actor->ground_storage;
  }
}
static void fake_attach(void *ctx, qa_owned_actor owned, qa_body_attachment attachment) {
  fake_actor *actor = fake_find((fake_world *)ctx, owned.id);
  if (actor == NULL) abort();
  actor->has_attachment = true;
  actor->attachment = attachment;
}
static void fake_assert_idle(void *ctx) { ((fake_world *)ctx)->assert_idle_calls++; }
static bool fake_combat_read(void *ctx, qa_actor_id id, qa_combat_state *out) {
  fake_actor *actor = fake_find((fake_world *)ctx, id);
  if (actor == NULL || !actor->has_combat) return false;
  *out = actor->combat;
  return true;
}
static void fake_combat_create(void *ctx, qa_owned_actor owned, qa_combat_state state) {
  fake_actor *actor = fake_find((fake_world *)ctx, owned.id);
  if (actor == NULL) abort();
  actor->has_combat = true;
  actor->combat = state;
}
static qa_armor_state fake_normalize(void *ctx, qa_owned_actor owned, qa_armor_state armor) {
  (void)ctx;
  (void)owned;
  return armor;
}
static qa_provider_id fake_protection_owner(void *ctx, qa_owned_actor owned,
                                            qa_protection_channel channel) {
  (void)ctx;
  (void)owned;
  (void)channel;
  return NULL;
}
static const qa_item_id *fake_protection_items(void *ctx, qa_owned_actor owned,
                                               qa_protection_channel channel,
                                               size_t *out_count) {
  (void)ctx;
  (void)owned;
  (void)channel;
  *out_count = 0;
  return NULL;
}
static bool fake_copied_armor(void *ctx, qa_owned_actor owned, qa_armor_state *out) {
  (void)ctx;
  (void)owned;
  (void)out;
  return false;
}
static void fake_set_powered(void *ctx, qa_owned_actor owned,
                             qa_powered_protection_state powered) {
  fake_actor *actor = fake_find((fake_world *)ctx, owned.id);
  if (actor == NULL) abort();
  actor->combat.armor.powered = powered;
}
static bool fake_source_items(void *ctx, qa_owned_actor owned,
                              qa_source_item_snapshot *out) {
  fake_actor *actor = fake_find((fake_world *)ctx, owned.id);
  if (actor == NULL || actor->snapshot == NULL) return false;
  *out = *actor->snapshot;
  return true;
}
static const qa_inventory_entry *fake_entries(void *ctx, qa_actor_id id,
                                              size_t *out_count) {
  fake_actor *actor = fake_find((fake_world *)ctx, id);
  *out_count = actor != NULL && actor->has_inventory ? actor->entry_count : 0;
  return actor != NULL ? actor->entries : NULL;
}
static bool fake_has(void *ctx, qa_actor_id id) {
  fake_actor *actor = fake_find((fake_world *)ctx, id);
  return actor != NULL && actor->has_inventory;
}
static void fake_inventory_create(void *ctx, qa_owned_actor owned,
                                  const qa_inventory_entry *entries, size_t count) {
  fake_actor *actor = fake_find((fake_world *)ctx, owned.id);
  if (actor == NULL || count > 4) abort();
  actor->has_inventory = true;
  actor->entry_count = count;
  for (size_t i = 0; i < count; i++) actor->entries[i] = entries[i];
}
static qa_world_storage_kind fake_storage(void *ctx, qa_owned_actor owned) {
  fake_actor *actor = fake_find((fake_world *)ctx, owned.id);
  return actor != NULL ? actor->storage : QA_WORLD_STORAGE_COPIED;
}
static void fake_world_init(fake_world *world, const char *name) {
  memset(world, 0, sizeof *world);
  if (!qa_identity_owner_create(&world->owner, name)) abort();
  world->registry.ctx = world;
  world->registry.observations = fake_observations;
  world->registry.resolve_owned = fake_resolve_owned;
  world->registry.resolve_saved = fake_resolve_saved;
  world->registry.reference_saved = fake_reference_saved;
  world->bodies.ctx = world;
  world->bodies.read = fake_body_read;
  world->bodies.link_state = fake_link_state;
  world->bodies.attachment = fake_attachment;
  world->bodies.restore_link_state = fake_restore_links;
  world->bodies.create = fake_body_create;
  world->bodies.attach = fake_attach;
  world->combat.ctx = world;
  world->combat.assert_idle = fake_assert_idle;
  world->combat.read = fake_combat_read;
  world->combat.create = fake_combat_create;
  world->combat.normalize_legacy_armor = fake_normalize;
  world->combat.protection_owner = fake_protection_owner;
  world->combat.protection_inventory_items = fake_protection_items;
  world->combat.copied_primary_armor = fake_copied_armor;
  world->combat.set_powered_protection = fake_set_powered;
  world->inventory.ctx = world;
  world->inventory.source_items = fake_source_items;
  world->inventory.entries = fake_entries;
  world->inventory.has = fake_has;
  world->inventory.create = fake_inventory_create;
}
/* Registers an actor whose saved identity is (slot, saved_gen) but
 * whose live identity is (slot, live_gen), mimicking restore remap. */
static fake_actor *fake_add(fake_world *world, int64_t slot, int64_t saved_gen,
                            int64_t live_gen, const char *owner) {
  for (size_t i = 0; i < FAKE_MAX; i++) {
    if (world->actors[i].used) continue;
    fake_actor *actor = &world->actors[i];
    actor->used = true;
    qa_actor_id id = qa_identity_owner_actor(&world->owner, slot, live_gen);
    actor->owned = qa_identity_owner_owned_actor(&world->owner, id, owner);
    actor->saved.slot = (double)slot;
    actor->saved.generation = (double)saved_gen;
    actor->storage = QA_WORLD_STORAGE_COPIED;
    world->observations[world->observation_count].id = id;
    world->observations[world->observation_count].owner = owner;
    world->observations[world->observation_count].definition = "test:actor";
    world->observation_count++;
    return actor;
  }
  abort();
}
static qa_shared_world_restore_host fake_host(fake_world *world) {
  qa_shared_world_restore_host host;
  host.actors = world->registry;
  host.bodies = world->bodies;
  host.combat = world->combat;
  host.inventory = world->inventory;
  host.storage_ctx = world;
  host.storage = fake_storage;
  return host;
}
static qa_body_state fake_body(double x) {
  qa_body_state body;
  memset(&body, 0, sizeof body);
  body.origin.x = x;
  return body;
}
static qa_combat_state fake_combat_state(double health) {
  qa_combat_state state;
  memset(&state, 0, sizeof state);
  state.health = health;
  state.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
  state.armor.powered.kind = "none";
  state.mass = 200;
  state.can_take_damage = true;
  return state;
}

/* ---------------- source-items ---------------- */
static void test_source_items(void) {
  fake_world world;
  fake_world_init(&world, "source-items");
  fake_actor *player = fake_add(&world, 0, 0, 0, "q1:game");
  fake_actor *gunner = fake_add(&world, 1, 0, 0, "q1:game");

  qa_inventory_entry nails_primary[] = {{
      .item = "q1:ammo/nails", .count = -3, .capacity = 200,
      .has_count_policy = true,
      .count_policy = {.kind = "source-counter", .arithmetic = "binary32"}}};
  qa_source_item_definition nails_def;
  memset(&nails_def, 0, sizeof nails_def);
  nails_def.item = "q1:ammo/nails";
  nails_def.label = "Component nails";
  nails_def.source.provider = "mod:ammo";
  nails_def.source.content = "q1:classic:id1:fixture";
  qa_source_item_admission nails_admission[] = {
      {.definition = nails_def, .admission = "replace-primary"}};
  qa_source_item_group nails_groups[] = {
      {.owner = "mod:ammo", .items = nails_admission, .item_count = 1}};
  qa_source_item_snapshot nails_snapshot = {
      .primary = nails_primary, .primary_count = 1,
      .groups = nails_groups, .group_count = 1};
  player->snapshot = &nails_snapshot;

  qa_inventory_entry shells_primary[] = {
      {.item = "q1:ammo/shells", .count = 10, .capacity = 100}};
  qa_weapon_hud_icon gun_icon;
  memset(&gun_icon, 0, sizeof gun_icon);
  gun_icon.kind = QA_WEAPON_HUD_ICON_SHADER;
  gun_icon.content = "q1:classic:id1:fixture";
  gun_icon.name = "gfx/gun";
  qa_source_item_action gun_actions[] = {QA_SOURCE_ITEM_ACTION_USE};
  qa_held_weapon_declaration no_held;
  memset(&no_held, 0, sizeof no_held);
  qa_source_item_definition gun_def;
  memset(&gun_def, 0, sizeof gun_def);
  gun_def.item = "mod:weapons/gun";
  gun_def.label = "Gun";
  gun_def.source.provider = "mod:weapons";
  gun_def.source.content = "q1:classic:id1:fixture";
  gun_def.has_icon = true;
  gun_def.icon = &gun_icon;
  gun_def.actions = gun_actions;
  gun_def.action_count = 1;
  gun_def.is_weapon = true;
  gun_def.weapon.ammo = "q1:ammo/shells";
  gun_def.weapon.held = &no_held;
  qa_source_item_admission gun_admission[] = {
      {.definition = gun_def, .admission = "add"}};
  qa_source_item_group gun_groups[] = {
      {.owner = "mod:weapons", .items = gun_admission, .item_count = 1}};
  qa_source_item_snapshot gun_snapshot = {
      .primary = shells_primary, .primary_count = 1,
      .groups = gun_groups, .group_count = 1};
  gunner->snapshot = &gun_snapshot;

  qa_provider_checkpoint checkpoint;
  CHECK(qa_capture_source_items(&world.registry, &world.inventory, &checkpoint));
  CHECK(strcmp(checkpoint.provider, "world:gameplay") == 0);
  CHECK(strcmp(checkpoint.schema, "world:source-items") == 0);
  CHECK(checkpoint.version == 1);

  qa_inventory_entry nails_effective[] = {
      {.item = "q1:ammo/nails", .count = 7, .capacity = 50}};
  qa_inventory_entry gun_effective[] = {
      {.item = "q1:ammo/shells", .count = 10, .capacity = 100},
      {.item = "mod:weapons/gun", .count = 1, .capacity = 1}};
  qa_inventory_checkpoint inventories[] = {
      {.actor = {0, 0}, .entries = nails_effective, .entry_count = 1},
      {.actor = {1, 0}, .entries = gun_effective, .entry_count = 2}};
  qa_save_image save;
  memset(&save, 0, sizeof save);
  save.providers = &checkpoint;
  save.provider_count = 1;
  save.inventories = inventories;
  save.inventory_count = 2;

  qa_source_items_result records;
  CHECK(qa_read_source_items(&save, &records));
  CHECK(records.count == 2);
  CHECK(records.records[0].owner != NULL &&
        strcmp(records.records[0].owner, "q1:game") == 0);
  CHECK(records.records[0].primary_count == 1);
  CHECK(records.records[0].primary[0].count == -3);
  CHECK(records.records[0].primary[0].has_count_policy);
  CHECK(strcmp(records.records[0].primary[0].count_policy.arithmetic,
               "binary32") == 0);
  CHECK(records.records[0].group_count == 1);
  CHECK(strcmp(records.records[0].groups[0].items[0].admission,
               "replace-primary") == 0);
  CHECK(strcmp(records.records[0].groups[0].items[0].definition.label,
               "Component nails") == 0);
  const qa_source_item_definition *decoded_gun =
      &records.records[1].groups[0].items[0].definition;
  CHECK(decoded_gun->is_weapon);
  CHECK(strcmp(decoded_gun->weapon.ammo, "q1:ammo/shells") == 0);
  CHECK(decoded_gun->weapon.held != NULL && !decoded_gun->weapon.held->is_model);
  CHECK(decoded_gun->has_icon && decoded_gun->icon != NULL);
  CHECK(decoded_gun->icon->kind == QA_WEAPON_HUD_ICON_SHADER);
  CHECK(strcmp(decoded_gun->icon->name, "gfx/gun") == 0);
  CHECK(decoded_gun->action_count == 1 &&
        decoded_gun->actions[0] == QA_SOURCE_ITEM_ACTION_USE);
  qa_source_items_result_free(&records);

  qa_provider_checkpoint duplicates[2] = {checkpoint, checkpoint};
  qa_save_image dup_save = save;
  dup_save.providers = duplicates;
  dup_save.provider_count = 2;
  qa_source_items_result scratch;
  CHECK_ABORTS((void)qa_read_source_items(&dup_save, &scratch));

  /* prepare: primary rows restored, effective rows kept, finish
   * validates the live tables against the committed state. */
  qa_source_item_restore restore;
  CHECK(qa_prepare_source_item_restore(&save, &restore));
  CHECK(restore.primary.inventories[0].entry_count == 1);
  CHECK(restore.primary.inventories[0].entries[0].count == -3);
  qa_inventory_checkpoint probe[] = {
      {.actor = {0, 0}, .entries = nails_primary, .entry_count = 1}};
  size_t effective_count = 0;
  const qa_inventory_checkpoint *effective =
      qa_source_item_restore_effective(&restore, probe, 1, &effective_count);
  CHECK(effective != NULL && effective_count == 1);
  CHECK(effective[0].entries[0].count == 7);
  player->has_inventory = true;
  player->entry_count = 1;
  player->entries[0] = nails_effective[0];
  gunner->has_inventory = true;
  gunner->entry_count = 2;
  gunner->entries[0] = gun_effective[0];
  gunner->entries[1] = gun_effective[1];
  qa_source_item_restore_finish(&restore, &world.registry, &world.inventory);
  player->entries[0].count = 8;
  CHECK_ABORTS(qa_source_item_restore_finish(&restore, &world.registry,
                                            &world.inventory));
  qa_source_item_restore_free(&restore);
  free((void *)checkpoint.bytes.bytes);
  qa_identity_owner_destroy(&world.owner);
}

/* ---------------- world-state ---------------- */
static void test_world_state(void) {
  /* Capture with an attachment and a ground actor. */
  fake_world before;
  fake_world_init(&before, "capture-bodies");
  fake_actor *anchor = fake_add(&before, 0, 0, 0, "q3:game");
  fake_actor *child = fake_add(&before, 1, 0, 0, "q2:equipment");
  anchor->has_body = true;
  anchor->body = fake_body(0);
  anchor->links.link_count = 3;
  child->has_body = true;
  child->body = fake_body(8);
  child->ground_storage = anchor->owned.id;
  child->body.ground = &child->ground_storage;
  child->links.link_count = 5;
  child->links.has_linked = true;
  child->links.state = fake_body(8);
  child->links.absolute_bounds.max.x = 16;
  child->has_attachment = true;
  child->attachment.anchor = anchor->owned.id;
  child->attachment.follow_kind = QA_BODY_FOLLOW_TRANSLATION;
  child->attachment.follow_offset.x = 8;
  qa_body_checkpoint *bodies = NULL;
  size_t body_count = 0;
  CHECK(qa_capture_shared_bodies(&before.registry, &before.bodies, &bodies,
                                 &body_count));
  CHECK(body_count == 2);
  CHECK(bodies[1].link_count == 5);
  CHECK(bodies[1].body.ground != NULL && bodies[1].body.ground->slot == 0);
  CHECK(bodies[1].attachment != NULL && bodies[1].attachment->anchor.slot == 0);
  CHECK(bodies[1].linked != NULL && bodies[1].linked->absolute_bounds.max.x == 16);

  /* Copied restore into a fresh world whose live generations differ. */
  fake_world after;
  fake_world_init(&after, "capture-bodies");
  fake_actor *restored_anchor = fake_add(&after, 0, 0, 7, "q3:game");
  fake_actor *restored_child = fake_add(&after, 1, 0, 7, "q2:equipment");
  qa_actor_slot_checkpoint slots[] = {
      {.id = {0, 0}, .lifetime_is_active = true, .owner = "q3:game"},
      {.id = {1, 0}, .lifetime_is_active = true, .owner = "q2:equipment"}};
  qa_combat_checkpoint combat[] = {{.actor = {1, 0}, .state = fake_combat_state(73)}};
  qa_inventory_entry child_entries[] = {
      {.item = "q1:ammo/nails", .count = -3, .capacity = 200}};
  qa_inventory_checkpoint inventories[] = {
      {.actor = {1, 0}, .entries = child_entries, .entry_count = 1}};
  qa_save_image save;
  memset(&save, 0, sizeof save);
  save.schema_version = 4;
  save.actors = slots;
  save.actor_count = 2;
  save.bodies = bodies;
  save.body_count = body_count;
  save.combat = combat;
  save.combat_count = 1;
  save.inventories = inventories;
  save.inventory_count = 1;
  qa_shared_world_restore_host host = fake_host(&after);
  qa_restore_shared_world_state(&save, &host);
  CHECK(restored_anchor->has_body && restored_anchor->body.origin.x == 0);
  CHECK(restored_child->has_body && restored_child->body.origin.x == 8);
  CHECK(restored_child->body.ground != NULL &&
        qa_same_actor(*restored_child->body.ground, restored_anchor->owned.id));
  CHECK(restored_child->has_attachment &&
        qa_same_actor(restored_child->attachment.anchor, restored_anchor->owned.id));
  CHECK(restored_child->has_combat && restored_child->combat.health == 73);
  CHECK(restored_child->has_inventory && restored_child->entries[0].count == -3);
  qa_shared_body_link_host link_host;
  link_host.actors = after.registry;
  link_host.bodies = after.bodies;
  link_host.storage_ctx = NULL;
  link_host.storage = NULL;
  qa_restore_shared_body_links(&save, &link_host);
  CHECK(restored_child->restore_link_calls == 1);
  CHECK(restored_child->restored_link_count == 5);
  CHECK(restored_child->links.has_linked);

  /* Missing saved actor aborts. */
  qa_body_checkpoint orphan = bodies[0];
  orphan.actor.slot = 99;
  qa_save_image orphan_save = save;
  orphan_save.bodies = &orphan;
  orphan_save.body_count = 1;
  orphan_save.combat_count = 0;
  orphan_save.inventory_count = 0;
  orphan_save.actor_count = 0;
  CHECK_ABORTS(qa_restore_shared_world_state(&orphan_save, &host));
  qa_body_checkpoints_free(bodies, body_count);
  qa_identity_owner_destroy(&before.owner);
  qa_identity_owner_destroy(&after.owner);

  /* Prebound source body disagreement aborts. */
  fake_world prebound;
  fake_world_init(&prebound, "prebound");
  fake_actor *bound = fake_add(&prebound, 0, 0, 0, "q1:game");
  bound->storage = QA_WORLD_STORAGE_PREBOUND;
  bound->has_body = true;
  bound->body = fake_body(1);
  qa_body_checkpoint saved_body;
  memset(&saved_body, 0, sizeof saved_body);
  saved_body.actor.slot = 0;
  saved_body.body.origin.x = 2;
  qa_save_image prebound_save;
  memset(&prebound_save, 0, sizeof prebound_save);
  prebound_save.bodies = &saved_body;
  prebound_save.body_count = 1;
  qa_shared_world_restore_host prebound_host = fake_host(&prebound);
  CHECK_ABORTS(qa_restore_shared_world_state(&prebound_save, &prebound_host));
  qa_identity_owner_destroy(&prebound.owner);

  /* Deferred powered restore: powered armor arrives at finish(). */
  fake_world deferred;
  fake_world_init(&deferred, "powered-save");
  fake_actor *player = fake_add(&deferred, 0, 0, 0, "q1:game");
  qa_combat_state state = fake_combat_state(73);
  state.armor.regular.kind = QA_REGULAR_ARMOR_Q1;
  state.armor.regular.points = 50;
  state.armor.regular.absorption = 0.6;
  state.armor.regular.item = "q1:armor/yellow";
  state.armor.powered.kind = "shield";
  state.armor.powered.cells = 20;
  qa_combat_checkpoint deferred_combat[] = {{.actor = {0, 0}, .state = state}};
  qa_inventory_entry cells[] = {{.item = "q2:cells", .count = 20, .capacity = 200}};
  qa_inventory_checkpoint deferred_inv[] = {
      {.actor = {0, 0}, .entries = cells, .entry_count = 1}};
  qa_save_image deferred_save;
  memset(&deferred_save, 0, sizeof deferred_save);
  deferred_save.combat = deferred_combat;
  deferred_save.combat_count = 1;
  deferred_save.inventories = deferred_inv;
  deferred_save.inventory_count = 1;
  qa_shared_world_restore_host deferred_host = fake_host(&deferred);
  qa_shared_world_restore_completion *completion =
      qa_restore_shared_world_state_deferred(&deferred_save, &deferred_host,
                                             NULL, NULL);
  CHECK(completion != NULL);
  CHECK(player->has_combat);
  CHECK(strcmp(player->combat.armor.powered.kind, "none") == 0);
  CHECK(player->combat.armor.regular.points == 50);
  CHECK_ABORTS(qa_shared_world_restore_assert_complete(completion));
  qa_shared_world_restore_finish(completion);
  qa_shared_world_restore_assert_complete(completion);
  CHECK(strcmp(player->combat.armor.powered.kind, "shield") == 0);
  CHECK(player->combat.armor.powered.cells == 20);
  CHECK_ABORTS(qa_shared_world_restore_finish(completion));
  qa_shared_world_restore_completion_free(completion);
  qa_identity_owner_destroy(&deferred.owner);
}

int main(void) {
  test_q2_classic_guest();
  test_saved_game();
  test_source_items();
  test_world_state();
  printf("persistence_left_b: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
