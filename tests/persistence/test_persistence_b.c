/*
 * Port of tests/persistence/q2-classic-guest.test.ts plus synthetic
 * encode/decode roundtrips for the q2 checkpoint readers of slice B
 * (movers, items, base entities, hand grenades, foundation, monsters,
 * weapons, players, missionpacks, rerelease state), derived from the
 * TS reader code. TS expect(...).toThrow becomes a forked child that
 * must die by abort, so post-failure reuse of the same overlay (which
 * TS catches) is exercised with a fresh overlay instead.
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

#include "qa/contracts/content.h"
#include "qa/persistence/q2_base_entities.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/q2_foundation.h"
#include "qa/persistence/q2_hand_grenades.h"
#include "qa/persistence/q2_items.h"
#include "qa/persistence/q2_missionpacks.h"
#include "qa/persistence/q2_monsters.h"
#include "qa/persistence/q2_movers.h"
#include "qa/persistence/q2_players.h"
#include "qa/persistence/q2_rerelease_state.h"
#include "qa/persistence/q2_weapons.h"
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

static bool bytes_eq(qa_byte_view view, const uint8_t *want, size_t length) {
  return view.length == length &&
         (length == 0 || memcmp(view.bytes, want, length) == 0);
}
static qa_saved_actor_id actor(double slot, double generation) {
  qa_saved_actor_id id = {slot, generation};
  return id;
}

/* --- q2-classic-guest.test.ts --- */
static char *digest_text(void) {
  char hex[65];
  memset(hex, 'a', 64);
  hex[64] = '\0';
  return qa_create_content_digest(hex);
}
static qa_q2_classic_original_identity test_identity(void) {
  qa_q2_classic_original_identity identity;
  identity.module.id = "q2:classic";
  identity.module.artifact_path = "gamex86.dll";
  identity.module.revision = "source-build";
  identity.module.digest = digest_text();
  identity.map = "maps/base1.bsp";
  return identity;
}
static uint8_t *cvar_bytes(size_t *out_length) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value_set(object, "skill", qa_save_value_string("1"));
  uint8_t *bytes = qa_encode_checkpoint_value(object, out_length);
  qa_save_value_free(object);
  return bytes;
}
typedef struct server_ctx {
  qa_q2_classic_configstring_entry configstrings[1];
  qa_q2_classic_portal_entry portals[1];
  uint8_t *cvars;
  size_t cvar_length;
} server_ctx;
static void fill_server(void *ctx, qa_q2_classic_original_server_state *out) {
  server_ctx *server = (server_ctx *)ctx;
  server->configstrings[0].index = 0;
  server->configstrings[0].value = "base1";
  server->portals[0].portal = 1;
  server->portals[0].open = true;
  memset(out, 0, sizeof *out);
  out->state.configstrings = server->configstrings;
  out->state.configstring_count = 1;
  out->state.portals = server->portals;
  out->state.portal_count = 1;
  out->cvars.bytes = server->cvars;
  out->cvars.length = server->cvar_length;
}
typedef struct write_ctx {
  qa_classic_original_save_files *files;
  bool saw_autosave;
  qa_q2_classic_file *retained;
} write_ctx;
static void write_file(write_ctx *ctx, const char *path, const uint8_t *bytes,
                       size_t length) {
  qa_q2_classic_file *file =
      qa_classic_original_save_files_open_file(ctx->files, path, false, true, 2);
  CHECK(file != NULL);
  if (file == NULL) return;
  CHECK(file->write(file->ctx, 0, bytes, length) == (int64_t)length);
  file->flush(file->ctx);
  file->close(file->ctx);
}
static void capture_write(void *ctx, const char *game, const char *level,
                          bool autosave) {
  write_ctx *write = (write_ctx *)ctx;
  write->saw_autosave = autosave;
  static const uint8_t game_bytes[] = {1, 2, 3};
  static const uint8_t level_bytes[] = {4, 5};
  write_file(write, game, game_bytes, sizeof game_bytes);
  write_file(write, level, level_bytes, sizeof level_bytes);
}
typedef struct restore_ctx {
  qa_classic_original_save_files *files;
  const qa_q2_classic_original_save *save;
} restore_ctx;
static void restore_read(void *ctx, const char *game, const char *level) {
  restore_ctx *restore = (restore_ctx *)ctx;
  const char *paths[] = {game, level};
  qa_byte_view wants[] = {restore->save->game, restore->save->level};
  for (int i = 0; i < 2; i++) {
    qa_q2_classic_file *file = qa_classic_original_save_files_open_file(
        restore->files, paths[i], true, false, 3);
    CHECK(file != NULL);
    if (file == NULL) continue;
    uint8_t buffer[8];
    int64_t size = file->size(file->ctx);
    size_t got = file->read(file->ctx, 0, buffer, (size_t)size);
    CHECK(got == wants[i].length && memcmp(buffer, wants[i].bytes, got) == 0);
    file->close(file->ctx);
  }
}
static void test_classic_guest(void) {
  qa_q2_classic_original_identity identity = test_identity();
  server_ctx server = {0};
  server.cvars = cvar_bytes(&server.cvar_length);
  qa_classic_original_save_files *files =
      qa_classic_original_save_files_create(NULL, NULL);
  CHECK(files != NULL);
  write_ctx write = {files, false, NULL};
  qa_q2_classic_original_save save;
  CHECK(qa_classic_original_save_files_capture(files, identity, fill_server,
                                               &server, capture_write, &write,
                                               true, &save));
  CHECK(write.saw_autosave);
  static const uint8_t want_game[] = {1, 2, 3};
  static const uint8_t want_level[] = {4, 5};
  CHECK(bytes_eq(save.game, want_game, sizeof want_game));
  CHECK(bytes_eq(save.level, want_level, sizeof want_level));
  qa_provider_checkpoint record;
  CHECK(qa_encode_q2_classic_original_save(&save, &record));
  CHECK(record.provider != NULL && strcmp(record.provider, "q2:classic") == 0);
  CHECK(record.schema != NULL &&
        strcmp(record.schema, "q2:classic-native-original") == 0);
  CHECK(record.version == 1);
  qa_q2_classic_original_save decoded;
  CHECK(qa_decode_q2_classic_original_save(&record, identity, &decoded));
  CHECK(decoded.autosave == true);
  CHECK(decoded.map != NULL && strcmp(decoded.map, "maps/base1.bsp") == 0);
  CHECK(bytes_eq(decoded.game, want_game, sizeof want_game));
  CHECK(bytes_eq(decoded.level, want_level, sizeof want_level));
  CHECK(decoded.server.state.configstring_count == 1);
  CHECK(decoded.server.state.configstrings[0].index == 0);
  CHECK(strcmp(decoded.server.state.configstrings[0].value, "base1") == 0);
  CHECK(decoded.server.state.portals[0].portal == 1);
  CHECK(decoded.server.state.portals[0].open == true);
  CHECK(decoded.visited_level_count == 0);
  restore_ctx restore = {files, &decoded};
  qa_classic_original_save_files_restore(files, &decoded, identity, restore_read,
                                         &restore);
  /* Outside an operation the reserved root refuses to open. */
  CHECK_ABORTS(qa_classic_original_save_files_open_file(
      files, "__qts_original_save/game.ssv", true, false, 3));
  /* Mismatched module rejects before callbacks. */
  qa_q2_classic_original_identity changed = identity;
  changed.module.revision = "changed";
  restore_ctx restore2 = {files, &decoded};
  CHECK_ABORTS(qa_classic_original_save_files_restore(files, &decoded, changed,
                                                      restore_read, &restore2));
  free((void *)record.bytes.bytes);
  qa_q2_classic_original_save_free(&decoded);
  qa_q2_classic_original_save_free(&save);
  qa_classic_original_save_files_destroy(files);
  free(server.cvars);
  free((void *)identity.module.digest);
}
static void partial_write(void *ctx, const char *game, const char *level,
                          bool autosave) {
  (void)level;
  (void)autosave;
  write_ctx *write = (write_ctx *)ctx;
  static const uint8_t bytes[] = {1};
  write_file(write, game, bytes, sizeof bytes);
}
static void retained_write(void *ctx, const char *game, const char *level,
                           bool autosave) {
  (void)autosave;
  write_ctx *write = (write_ctx *)ctx;
  qa_q2_classic_file *file =
      qa_classic_original_save_files_open_file(write->files, game, false, true, 2);
  write->retained = file;
  if (file != NULL) {
    static const uint8_t bytes[] = {1};
    (void)file->write(file->ctx, 0, bytes, sizeof bytes);
  }
  static const uint8_t level_bytes[] = {2};
  write_file(write, level, level_bytes, sizeof level_bytes);
}
static void noop_read(void *ctx, const char *game, const char *level) {
  (void)ctx;
  (void)game;
  (void)level;
}
static void test_classic_guest_failures(void) {
  qa_q2_classic_original_identity identity = test_identity();
  server_ctx server = {0};
  server.cvars = cvar_bytes(&server.cvar_length);
  qa_classic_original_save_files *files =
      qa_classic_original_save_files_create(NULL, NULL);
  write_ctx write = {files, false, NULL};
  qa_q2_classic_original_save save;
  CHECK_ABORTS({
    qa_q2_classic_original_save partial;
    qa_classic_original_save_files_capture(files, identity, fill_server, &server,
                                           partial_write, &write, false, &partial);
  });
  CHECK_ABORTS({
    qa_q2_classic_original_save leaked;
    qa_classic_original_save_files_capture(files, identity, fill_server, &server,
                                           retained_write, &write, false, &leaked);
  });
  /* Restore that consumes nothing aborts. */
  CHECK(qa_classic_original_save_files_capture(files, identity, fill_server,
                                               &server, capture_write, &write,
                                               false, &save));
  CHECK_ABORTS(qa_classic_original_save_files_restore(files, &save, identity,
                                                      noop_read, NULL));
  qa_q2_classic_original_save_free(&save);
  qa_classic_original_save_files_destroy(files);
  free(server.cvars);
  free((void *)identity.module.digest);
}
typedef struct fallback_ctx {
  char last[64];
  int calls;
} fallback_ctx;
static qa_q2_classic_file *fallback_open(void *ctx, const char *path, bool read,
                                         bool write, int creation) {
  (void)read;
  (void)write;
  (void)creation;
  fallback_ctx *fallback = (fallback_ctx *)ctx;
  fallback->calls++;
  snprintf(fallback->last, sizeof fallback->last, "%s", path);
  return NULL;
}
typedef struct travel_ctx {
  qa_classic_original_save_files *files;
  uint8_t *mutated;
  int owner;
} travel_ctx;
static void travel_write(void *ctx, const char *level_path) {
  travel_ctx *travel = (travel_ctx *)ctx;
  write_ctx write = {travel->files, false, NULL};
  static const uint8_t bytes[] = {8, 9, 10};
  write_file(&write, level_path, bytes, sizeof bytes);
}
static void *travel_read(void *ctx, const char *level_path) {
  travel_ctx *travel = (travel_ctx *)ctx;
  qa_q2_classic_file *file = qa_classic_original_save_files_open_file(
      travel->files, level_path, true, false, 3);
  CHECK(file != NULL);
  if (file == NULL) return NULL;
  travel->mutated[0] = 99;
  uint8_t buffer[3];
  size_t got = file->read(file->ctx, 0, buffer, (size_t)file->size(file->ctx));
  static const uint8_t want[] = {8, 9, 10};
  CHECK(got == sizeof want && memcmp(buffer, want, got) == 0);
  file->close(file->ctx);
  return &travel->owner;
}
static void game_open_write(void *ctx, const char *level_path) {
  (void)level_path;
  travel_ctx *travel = (travel_ctx *)ctx;
  (void)qa_classic_original_save_files_open_file(
      travel->files, "__qts_original_save/game.ssv", false, true, 2);
}
static void test_classic_travel_and_fallback(void) {
  fallback_ctx fallback = {0};
  qa_classic_original_save_files *files =
      qa_classic_original_save_files_create(fallback_open, &fallback);
  CHECK(qa_classic_original_save_files_open_file(files, "game.log", false, true,
                                                 4) == NULL);
  CHECK(fallback.calls == 1 && strcmp(fallback.last, "game.log") == 0);
  travel_ctx travel = {files, NULL, 2};
  uint8_t *bytes = NULL;
  size_t length = 0;
  CHECK(qa_classic_original_save_files_capture_travel_level(files, travel_write,
                                                            &travel, &bytes,
                                                            &length));
  static const uint8_t want[] = {8, 9, 10};
  CHECK(length == sizeof want && memcmp(bytes, want, length) == 0);
  travel.mutated = bytes;
  void *result = qa_classic_original_save_files_with_travel_level(
      files, bytes, length, travel_read, &travel);
  CHECK(result == &travel.owner);
  CHECK_ABORTS(qa_classic_original_save_files_with_travel_level(
      files, bytes, 0, travel_read, &travel));
  free(bytes);
  /* Opening the game path during a level-only capture is outside. */
  CHECK_ABORTS({
    uint8_t *leaked = NULL;
    size_t leaked_length = 0;
    qa_classic_original_save_files_capture_travel_level(files, game_open_write,
                                                        &travel, &leaked,
                                                        &leaked_length);
  });
  qa_classic_original_save_files_destroy(files);
}
static void test_classic_visited(void) {
  qa_q2_classic_original_identity identity = test_identity();
  server_ctx server = {0};
  server.cvars = cvar_bytes(&server.cvar_length);
  qa_classic_original_save_files *files =
      qa_classic_original_save_files_create(NULL, NULL);
  write_ctx write = {files, false, NULL};
  qa_q2_classic_original_save save;
  CHECK(qa_classic_original_save_files_capture(files, identity, fill_server,
                                               &server, capture_write, &write,
                                               false, &save));
  static const uint8_t visited_level[] = {3, 4};
  qa_q2_classic_configstring_entry visited_cs[] = {{1, "departed"}};
  qa_q2_classic_portal_entry visited_portals[] = {{2, false}};
  qa_q2_classic_visited_level visited;
  memset(&visited, 0, sizeof visited);
  visited.version = 1;
  visited.map = "maps/base2.bsp";
  visited.level.bytes = visited_level;
  visited.level.length = sizeof visited_level;
  visited.state.configstrings = visited_cs;
  visited.state.configstring_count = 1;
  visited.state.portals = visited_portals;
  visited.state.portal_count = 1;
  save.visited_levels = &visited;
  save.visited_level_count = 1;
  qa_provider_checkpoint record;
  CHECK(qa_encode_q2_classic_original_save(&save, &record));
  qa_q2_classic_original_save decoded;
  CHECK(qa_decode_q2_classic_original_save(&record, identity, &decoded));
  CHECK(decoded.visited_level_count == 1);
  CHECK(strcmp(decoded.visited_levels[0].map, "maps/base2.bsp") == 0);
  CHECK(bytes_eq(decoded.visited_levels[0].level, visited_level,
                 sizeof visited_level));
  CHECK(decoded.visited_levels[0].state.configstrings[0].index == 1);
  free((void *)record.bytes.bytes);
  qa_q2_classic_original_save_free(&decoded);
  /* Invalid visited maps and duplicates abort encoding. */
  const char *bad_maps[] = {"maps/base1.bsp", "maps/../escape.bsp", "base2.bsp",
                            "maps/a:b.bsp"};
  for (size_t i = 0; i < sizeof bad_maps / sizeof bad_maps[0]; i++) {
    visited.map = bad_maps[i];
    CHECK_ABORTS({
      qa_provider_checkpoint bad;
      qa_encode_q2_classic_original_save(&save, &bad);
    });
  }
  visited.map = "maps/base2.bsp";
  qa_q2_classic_visited_level duplicates[2] = {visited, visited};
  save.visited_levels = duplicates;
  save.visited_level_count = 2;
  CHECK_ABORTS({
    qa_provider_checkpoint bad;
    qa_encode_q2_classic_original_save(&save, &bad);
  });
  save.visited_levels = NULL;
  save.visited_level_count = 0;
  qa_q2_classic_original_save_free(&save);
  qa_classic_original_save_files_destroy(files);
  free(server.cvars);
  free((void *)identity.module.digest);
}

/* --- checkpoint roundtrips for the generated readers --- */
static void test_movers_items(void) {
  qa_q2_door_entry door;
  memset(&door, 0, sizeof door);
  door.actor = actor(1, 0);
  door.master = actor(1, 0);
  door.state.phase = "bottom";
  door.state.distance = 64;
  qa_q2_movers_checkpoint movers;
  memset(&movers, 0, sizeof movers);
  movers.doors = &door;
  movers.doors_count = 1;
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_movers_checkpoint(&movers, &length);
  CHECK(bytes != NULL);
  qa_q2_movers_checkpoint decoded;
  CHECK(qa_decode_q2_movers_checkpoint(bytes, length, &decoded));
  CHECK(decoded.doors_count == 1);
  CHECK(decoded.doors[0].actor.slot == 1);
  CHECK(strcmp(decoded.doors[0].state.phase, "bottom") == 0);
  CHECK(decoded.doors[0].state.distance == 64);
  qa_save_value_free(decoded.root);
  free(bytes);

  qa_q2_pickup_entry pickup;
  memset(&pickup, 0, sizeof pickup);
  pickup.actor = actor(2, 1);
  pickup.classname = "item_health";
  pickup.retained = true;
  qa_q2_items_checkpoint items;
  memset(&items, 0, sizeof items);
  items.power_cube_count = 3;
  items.pickups = &pickup;
  items.pickups_count = 1;
  bytes = qa_encode_q2_items_checkpoint(&items, &length);
  CHECK(bytes != NULL);
  qa_q2_items_checkpoint items_decoded;
  CHECK(qa_decode_q2_items_checkpoint(bytes, length, &items_decoded));
  CHECK(items_decoded.power_cube_count == 3);
  CHECK(strcmp(items_decoded.pickups[0].classname, "item_health") == 0);
  CHECK(items_decoded.pickups[0].has_expires_at == false);
  qa_save_value_free(items_decoded.root);
  free(bytes);
}
static void test_hand_grenades_and_weapons(void) {
  qa_hand_grenades_actor entry;
  memset(&entry, 0, sizeof entry);
  entry.actor = actor(0, 0);
  entry.state.config.enabled = true;
  entry.state.config.initial_ammo = 5;
  entry.state.config.capacity = 50;
  entry.state.action.kind = QA_HAND_ACTION_COOKING;
  entry.state.action.expires_at = 12.5;
  qa_hand_grenades_checkpoint checkpoint;
  memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.version = 1;
  checkpoint.edition = "classic";
  checkpoint.actors = &entry;
  checkpoint.actors_count = 1;
  size_t length = 0;
  uint8_t *bytes = qa_encode_hand_grenades_checkpoint(&checkpoint, &length);
  CHECK(bytes != NULL);
  qa_hand_grenades_checkpoint decoded;
  CHECK(qa_decode_hand_grenades_checkpoint(bytes, length, &decoded));
  CHECK(decoded.actors[0].state.action.kind == QA_HAND_ACTION_COOKING);
  CHECK(decoded.actors[0].state.action.expires_at == 12.5);
  CHECK(decoded.actors[0].state.config.capacity == 50);
  qa_save_value_free(decoded.root);
  free(bytes);
  /* initialAmmo above capacity aborts decoding. */
  entry.state.config.initial_ammo = 51;
  bytes = qa_encode_hand_grenades_checkpoint(&checkpoint, &length);
  CHECK_ABORTS({
    qa_hand_grenades_checkpoint bad;
    qa_decode_hand_grenades_checkpoint(bytes, length, &bad);
  });
  free(bytes);

  qa_q2_weapon_state_entry state;
  memset(&state, 0, sizeof state);
  state.actor = actor(0, 0);
  state.state.primary_handoff = "active";
  state.state.hand_reservation.kind = "none";
  state.state.phase = "ready";
  state.state.weapon = "blaster";
  qa_q2_weapon_input_entry input;
  memset(&input, 0, sizeof input);
  input.actor = actor(0, 0);
  input.input.hand = "right";
  qa_q2_weapons_checkpoint weapons;
  memset(&weapons, 0, sizeof weapons);
  weapons.format_version = 2;
  weapons.source_rules = "base";
  weapons.states = &state;
  weapons.states_count = 1;
  weapons.inputs = &input;
  weapons.inputs_count = 1;
  bytes = qa_encode_q2_weapons_checkpoint(&weapons, &length);
  CHECK(bytes != NULL);
  qa_q2_weapons_checkpoint weapons_decoded;
  CHECK(qa_decode_q2_weapons_checkpoint(bytes, length, &weapons_decoded));
  CHECK(weapons_decoded.format_version == 2);
  CHECK(strcmp(weapons_decoded.states[0].state.weapon, "blaster") == 0);
  CHECK(strcmp(weapons_decoded.inputs[0].input.hand, "right") == 0);
  qa_save_value_free(weapons_decoded.root);
  free(bytes);
  /* Format versions other than 2 abort decoding. */
  weapons.format_version = 3;
  bytes = qa_encode_q2_weapons_checkpoint(&weapons, &length);
  CHECK_ABORTS({
    qa_q2_weapons_checkpoint bad;
    qa_decode_q2_weapons_checkpoint(bytes, length, &bad);
  });
  free(bytes);
}
static void test_foundation_monsters(void) {
  qa_q2_entity_checkpoint entity;
  memset(&entity, 0, sizeof entity);
  entity.actor = actor(3, 0);
  entity.spawn.classname = "monster_soldier";
  entity.values.classname = "monster_soldier";
  entity.values.health_target = "health";
  entity.values.item_target = "";
  entity.values.solid = "box";
  entity.values.motion = "step";
  entity.values.alpha = 1;
  qa_q2_foundation_checkpoint foundation;
  memset(&foundation, 0, sizeof foundation);
  foundation.version = 1;
  foundation.counters.total_monsters = 4;
  foundation.entities = &entity;
  foundation.entities_count = 1;
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_foundation_checkpoint(&foundation, &length);
  CHECK(bytes != NULL);
  qa_q2_foundation_checkpoint decoded;
  CHECK(qa_decode_q2_foundation_checkpoint(bytes, length, &decoded));
  CHECK(decoded.counters.total_monsters == 4);
  CHECK(strcmp(decoded.entities[0].values.solid, "box") == 0);
  CHECK(strcmp(decoded.entities[0].values.health_target, "health") == 0);
  qa_save_value_free(decoded.root);
  free(bytes);

  qa_q2_monster_actor monster;
  memset(&monster, 0, sizeof monster);
  monster.actor = actor(4, 0);
  monster.definition = "soldier";
  monster.state.initial_power_armor_type = "none";
  monster.state.kind = "soldier";
  monster.state.weapon = "blaster";
  monster.state.locomotion = "walk";
  monster.state.spawned_by = "none";
  monster.state.attack_state = "straight";
  monster.state.health_scaling = 1;
  qa_q2_monsters_checkpoint monsters;
  memset(&monsters, 0, sizeof monsters);
  monsters.version = 1;
  monsters.actors = &monster;
  monsters.actors_count = 1;
  bytes = qa_encode_q2_monsters_checkpoint(&monsters, &length);
  CHECK(bytes != NULL);
  qa_q2_monsters_checkpoint monsters_decoded;
  CHECK(qa_decode_q2_monsters_checkpoint(bytes, length, &monsters_decoded));
  CHECK(strcmp(monsters_decoded.actors[0].state.weapon, "blaster") == 0);
  CHECK(monsters_decoded.actors[0].state.water_level == 0);
  qa_save_value_free(monsters_decoded.root);
  free(bytes);
}
static void test_players_missionpacks_rerelease(void) {
  qa_q2_player_entry player;
  memset(&player, 0, sizeof player);
  player.actor = actor(0, 0);
  player.state.gender = "male";
  player.state.hand = "right";
  player.state.name = "Player";
  qa_q2_players_checkpoint players;
  memset(&players, 0, sizeof players);
  players.version = 1;
  players.intermission.kind = "playing";
  players.players = &player;
  players.players_count = 1;
  size_t length = 0;
  uint8_t *bytes = qa_encode_q2_players_checkpoint(&players, &length);
  CHECK(bytes != NULL);
  qa_q2_players_checkpoint decoded;
  CHECK(qa_decode_q2_players_checkpoint(bytes, length, &decoded));
  CHECK(strcmp(decoded.players[0].state.name, "Player") == 0);
  CHECK(strcmp(decoded.intermission.kind, "playing") == 0);
  qa_save_value_free(decoded.root);
  free(bytes);

  qa_q2_tag_checkpoint tag;
  memset(&tag, 0, sizeof tag);
  tag.count = 2;
  bytes = qa_encode_q2_tag_checkpoint(&tag, &length);
  CHECK(bytes != NULL);
  qa_q2_tag_checkpoint tag_decoded;
  CHECK(qa_decode_q2_tag_checkpoint(bytes, length, &tag_decoded));
  CHECK(tag_decoded.count == 2 && tag_decoded.has_token == false);
  qa_save_value_free(tag_decoded.root);
  free(bytes);

  qa_q2_mission_pack_monsters_checkpoint pack;
  memset(&pack, 0, sizeof pack);
  pack.version = 1;
  pack.flyer_next_move = "none";
  pack.widow_damage_multiplier = 2;
  pack.has_hints = true;
  pack.hints.version = 1;
  pack.hints.present = true;
  bytes = qa_encode_q2_mission_pack_monsters_checkpoint(&pack, &length);
  CHECK(bytes != NULL);
  qa_q2_mission_pack_monsters_checkpoint pack_decoded;
  CHECK(qa_decode_q2_mission_pack_monsters_checkpoint(bytes, length,
                                                      &pack_decoded));
  CHECK(pack_decoded.widow_damage_multiplier == 2);
  CHECK(pack_decoded.has_hints && pack_decoded.hints.present);
  qa_save_value_free(pack_decoded.root);
  free(bytes);

  qa_q2_rerelease_module_checkpoint module;
  memset(&module, 0, sizeof module);
  module.version = 1;
  module.story = "story";
  module.sky.name = "space1";
  qa_q2_health_bar_opt bars[2];
  memset(bars, 0, sizeof bars);
  bars[1].has = true;
  bars[1].value.controller = actor(1, 0);
  bars[1].value.target = actor(2, 0);
  module.health_bars = bars;
  module.health_bars_count = 2;
  bytes = qa_encode_q2_rerelease_module_checkpoint(&module, &length);
  CHECK(bytes != NULL);
  qa_q2_rerelease_module_checkpoint module_decoded;
  CHECK(qa_decode_q2_rerelease_module_checkpoint(bytes, length,
                                                 &module_decoded));
  CHECK(strcmp(module_decoded.sky.name, "space1") == 0);
  CHECK(module_decoded.health_bars_count == 2);
  CHECK(module_decoded.health_bars[0].has == false);
  CHECK(module_decoded.health_bars[1].has == true);
  CHECK(module_decoded.health_bars[1].value.target.slot == 2);
  qa_save_value_free(module_decoded.root);
  free(bytes);

  qa_q2_base_entities_checkpoint base;
  memset(&base, 0, sizeof base);
  base.version = 1;
  qa_q2_platform_entry platform;
  memset(&platform, 0, sizeof platform);
  platform.actor = actor(5, 0);
  platform.state.phase = "top";
  base.movers.platforms = &platform;
  base.movers.platforms_count = 1;
  bytes = qa_encode_q2_base_entities_checkpoint(&base, &length);
  CHECK(bytes != NULL);
  qa_q2_base_entities_checkpoint base_decoded;
  CHECK(qa_decode_q2_base_entities_checkpoint(bytes, length, &base_decoded));
  CHECK(strcmp(base_decoded.movers.platforms[0].state.phase, "top") == 0);
  qa_save_value_free(base_decoded.root);
  free(bytes);
}

int main(void) {
  test_classic_guest();
  test_classic_guest_failures();
  test_classic_travel_and_fallback();
  test_classic_visited();
  test_movers_items();
  test_hand_grenades_and_weapons();
  test_foundation_monsters();
  test_players_missionpacks_rerelease();
  if (failures == 0) {
    printf("persistence-b: %d checks passed\n", checks);
    return 0;
  }
  fprintf(stderr, "persistence-b: %d/%d checks FAILED\n", failures, checks);
  return 1;
}
