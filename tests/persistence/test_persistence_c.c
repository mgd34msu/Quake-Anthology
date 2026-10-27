/*
 * Port of the slice-c cases in tests/persistence/save-policy.test.ts
 * plus reader cases for recipe.ts/save-image.ts exports exercised
 * through tests/persistence/save.test.ts fixtures. TypeScript
 * toThrow(RangeError/SaveFormatError) cases run in a child process
 * and must abort. GPL-2.0-or-later.
 */
#define _DEFAULT_SOURCE
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/save_policy.h"

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

static qa_save_reader reader_of(qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) abort();
  return reader;
}
static void set_string(qa_save_value *object, const char *key,
                       const char *value) {
  if (!qa_save_value_set(object, key, qa_save_value_string(value))) abort();
}
static void set_number(qa_save_value *object, const char *key, double value) {
  if (!qa_save_value_set(object, key, qa_save_value_number(value))) abort();
}
static void set_value(qa_save_value *object, const char *key,
                      qa_save_value *value) {
  if (!qa_save_value_set(object, key, value)) abort();
}
static qa_save_value *provider_value(const char *provider,
                                     const char *content) {
  qa_save_value *object = qa_save_value_object();
  set_string(object, "provider", provider);
  set_string(object, "content", content);
  return object;
}

static void test_save_unavailable(void) {
  const double health[] = {100, 40};
  qa_save_eligibility state = {QA_GAME_FAMILY_Q2, QA_SAVE_AUTHORITY_OFFLINE,
      QA_SAVE_MODE_COOP, false, true, false, health, 2};
  CHECK(qa_save_unavailable(&state, QA_SAVE_PURPOSE_MANUAL) == NULL);
  /* q3 deathmatch/intermission/dead-player are not blocked. */
  const double zero[] = {0};
  qa_save_eligibility q3 = {QA_GAME_FAMILY_Q3, QA_SAVE_AUTHORITY_OFFLINE,
      QA_SAVE_MODE_DEATHMATCH, false, true, true, zero, 1};
  CHECK(qa_save_unavailable(&q3, QA_SAVE_PURPOSE_MANUAL) == NULL);
  qa_save_eligibility dead = state;
  const double dead_health[] = {100, 0};
  dead.player_health = dead_health;
  CHECK(qa_save_unavailable(&dead, QA_SAVE_PURPOSE_MANUAL) != NULL);
  qa_save_eligibility dm = state; dm.mode = QA_SAVE_MODE_DEATHMATCH;
  CHECK(qa_save_unavailable(&dm, QA_SAVE_PURPOSE_MANUAL) != NULL);
  qa_save_eligibility inter = state; inter.intermission = true;
  CHECK(qa_save_unavailable(&inter, QA_SAVE_PURPOSE_MANUAL) != NULL);
  qa_save_eligibility server = state; server.authority = QA_SAVE_AUTHORITY_SERVER;
  CHECK(qa_save_unavailable(&server, QA_SAVE_PURPOSE_MANUAL) != NULL);
  qa_save_eligibility remote = state; remote.authority = QA_SAVE_AUTHORITY_REMOTE;
  CHECK(qa_save_unavailable(&remote, QA_SAVE_PURPOSE_MANUAL) != NULL);
  server.player_health_count = 0;
  CHECK(qa_save_unavailable(&server, QA_SAVE_PURPOSE_TRANSITION) == NULL);
  CHECK(qa_save_unavailable(&remote, QA_SAVE_PURPOSE_TRANSITION) != NULL);
  qa_save_eligibility inactive = state; inactive.active = false;
  CHECK(strcmp(qa_save_unavailable(&inactive, QA_SAVE_PURPOSE_MANUAL),
               "No active world to save.") == 0);
  /* matchSave bypasses the player/deathmatch checks. */
  qa_save_eligibility match = dm; match.match_save = true;
  match.player_health = dead_health; match.intermission = true;
  CHECK(qa_save_unavailable(&match, QA_SAVE_PURPOSE_MANUAL) == NULL);
}

static void test_save_paths(void) {
  char *path = qa_save_command_path("/tmp/qa-saves", "quicksave");
  CHECK(path != NULL && strcmp(path, "/tmp/qa-saves/quicksave.sav") == 0);
  char *again = qa_save_command_path("/tmp/qa-saves", "quicksave.sav");
  CHECK(again != NULL && strcmp(again, path) == 0);
  char *nested = qa_save_command_path("/tmp/qa-saves", "campaign/manual");
  CHECK(nested != NULL &&
        strcmp(nested, "/tmp/qa-saves/campaign/manual.sav") == 0);
  char *absolute = qa_save_command_path("/tmp/qa-saves", "/tmp/original.sav");
  CHECK(absolute != NULL && strcmp(absolute, "/tmp/original.sav") == 0);
  free(path); free(again); free(nested); free(absolute);
  CHECK_ABORTS(qa_save_command_path("/tmp/qa-saves", ""));
  CHECK_ABORTS(qa_contained_save_name("/tmp/qa-saves", "/tmp/original.sav"));
  CHECK_ABORTS(qa_contained_save_name("/tmp/qa-saves", "/tmp/qa-saves/../outside.sav"));
  CHECK_ABORTS(qa_contained_save_name("/tmp/qa-saves", "/tmp/qa-saves/current.sav"));
  CHECK_ABORTS(qa_contained_save_name("/tmp/qa-saves", "/tmp/qa-saves/manual.txt"));
  char *name = qa_contained_save_name("/tmp/qa-saves", "/tmp/qa-saves/game/manual.sav");
  CHECK(name != NULL && strcmp(name, "game/manual.sav") == 0);
  free(name);
}

static void test_contained_write(void) {
  char directory[] = "/tmp/qa-save-policy-XXXXXX";
  CHECK(mkdtemp(directory) != NULL);
  char *path = qa_save_command_path(directory, "game/manual");
  const uint8_t first[] = {1, 2};
  const uint8_t second[] = {3, 4};
  CHECK(qa_write_contained_save(directory, path, first, sizeof first));
  CHECK(qa_write_contained_save(directory, path, second, sizeof second));
  FILE *file = fopen(path, "rb");
  CHECK(file != NULL);
  uint8_t bytes[2] = {0, 0};
  CHECK(file != NULL && fread(bytes, 1, sizeof bytes, file) == 2);
  if (file != NULL) fclose(file);
  CHECK(bytes[0] == 3 && bytes[1] == 4);
  free(path);
}

static void test_readers(void) {
  /* readProvider / readContentId */
  qa_save_value *provider = provider_value("q1:weapons", "q1:classic:id1:1");
  qa_save_reader provider_reader = reader_of(provider);
  qa_provider_reference reference = qa_read_provider(&provider_reader);
  CHECK(strcmp(reference.provider, "q1:weapons") == 0);
  CHECK(strcmp(reference.content, "q1:classic:id1:1") == 0);
  qa_save_reader_destroy(&provider_reader);
  qa_save_value_free(provider);
  CHECK_ABORTS({
    qa_save_value *bad = qa_save_value_string("not-an-id");
    qa_save_reader r = reader_of(bad);
    qa_read_content_id(&r);
  });

  /* readEquipment: both channels disabled. */
  qa_save_value *equipment = qa_save_value_object();
  qa_save_value *grapple = qa_save_value_object();
  set_string(grapple, "kind", "disabled");
  qa_save_value *grenades = qa_save_value_object();
  set_string(grenades, "kind", "disabled");
  set_value(equipment, "grapple", grapple);
  set_value(equipment, "handGrenades", grenades);
  qa_save_reader eq_reader = reader_of(equipment);
  qa_equipment_selection selection = qa_read_equipment(&eq_reader);
  CHECK(!selection.grapple.is_enabled && !selection.hand_grenades.is_enabled);
  qa_save_reader_destroy(&eq_reader);
  qa_save_value_free(equipment);

  /* Hand grenades over capacity fail with the TS message. */
  CHECK_ABORTS({
    qa_save_value *eq = qa_save_value_object();
    qa_save_value *g = qa_save_value_object();
    set_string(g, "kind", "disabled");
    qa_save_value *h = qa_save_value_object();
    set_string(h, "kind", "enabled");
    set_number(h, "initialAmmo", 5);
    set_number(h, "capacity", 2);
    set_value(eq, "grapple", g);
    set_value(eq, "handGrenades", h);
    qa_save_reader r = reader_of(eq);
    qa_read_equipment(&r);
  });

  /* readSavedActor / readInventoryEntry / armor readers. */
  qa_save_value *actor = qa_save_value_object();
  set_number(actor, "slot", 3);
  set_number(actor, "generation", 2);
  qa_save_reader actor_reader = reader_of(actor);
  qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
  CHECK(saved.slot == 3 && saved.generation == 2);
  qa_save_reader_destroy(&actor_reader);
  qa_save_value_free(actor);

  qa_save_value *entry = qa_save_value_object();
  set_string(entry, "item", "q1:shells");
  set_number(entry, "count", 25);
  set_number(entry, "capacity", 100);
  qa_save_reader entry_reader = reader_of(entry);
  qa_inventory_entry parsed = qa_read_inventory_entry(&entry_reader);
  CHECK(strcmp(parsed.item, "q1:shells") == 0 && parsed.count == 25 &&
        parsed.capacity == 100 && !parsed.has_count_policy);
  qa_save_reader_destroy(&entry_reader);
  qa_save_value_free(entry);

  /* New armor layout: regular + powered. */
  qa_save_value *armor = qa_save_value_object();
  qa_save_value *regular = qa_save_value_object();
  set_string(regular, "kind", "q1");
  set_number(regular, "points", 50);
  set_number(regular, "absorption", 0.5);
  set_string(regular, "item", "q1:armor");
  qa_save_value *powered = qa_save_value_object();
  set_string(powered, "kind", "none");
  set_value(armor, "regular", regular);
  set_value(armor, "powered", powered);
  qa_save_reader armor_reader = reader_of(armor);
  qa_armor_state state = qa_read_armor(&armor_reader);
  CHECK(state.regular.kind == QA_REGULAR_ARMOR_Q1 && state.regular.points == 50 &&
        state.regular.absorption == 0.5 &&
        strcmp(state.powered.kind, "none") == 0);
  qa_save_reader_destroy(&armor_reader);
  qa_save_value_free(armor);
}

int main(void) {
  test_save_unavailable();
  test_save_paths();
  test_contained_write();
  test_readers();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
