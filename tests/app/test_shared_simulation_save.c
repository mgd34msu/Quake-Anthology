/* Checkpoint and restore support for the SharedSimulation port: the save
 * image contract (schema 4, QTSAVE5 envelope with its header, consumption,
 * the two SaveImage key orders) and the conversion between the two C shapes
 * of the TS Q1FoundationCheckpoint.
 *
 * Expected encodings come from the TS codec under bun on the same fixture
 * (tests/app/oracles/shared_simulation_save_image.json):
 *   bun tests/app/oracles/shared_simulation_save.ts
 * prints the length and FNV-1a 64 of each encoding checked below.
 * Run from the repository root. GPL-2.0-or-later. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/checkpoint.h"
#include "qa/persistence/q1_foundation.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/saved_game.h"
#include "qa/persistence/value.h"
#include "qa/app/bootstrap/simulation/arsenal/q3.h"
#include "qa/app/bootstrap/simulation/checkpoint_values.h"
#include "qa/app/bootstrap/simulation/dropped_pickups.h"
#include "qa/app/bootstrap/simulation/equipment_checkpoint.h"
#include "qa/app/bootstrap/simulation/grapple_checkpoint.h"
#include "qa/app/bootstrap/simulation/player_checkpoint.h"
#include "qa/app/bootstrap/simulation/weapon_slot_checkpoint.h"
#include "qa/persistence/q2_hand_grenades.h"

static int failures, checks;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                       \
    }                                                                   \
  } while (0)

static uint64_t fnv(const uint8_t *bytes, size_t length) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < length; i++) hash = (hash ^ bytes[i]) * 0x100000001b3ULL;
  return hash;
}
static uint8_t *read_file(const char *path, size_t *length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  uint8_t *bytes = malloc((size_t)size);
  *length = fread(bytes, 1, (size_t)size, file);
  fclose(file);
  return bytes;
}
static void check_encoding(const qa_save_image *image, size_t length, uint64_t hash) {
  uint8_t *bytes = NULL; size_t got = 0;
  CHECK(qa_encode_save_image(image, &bytes, &got));
  CHECK(got == length);
  CHECK(fnv(bytes, got) == hash);
  if (got != length || fnv(bytes, got) != hash)
    fprintf(stderr, "  got %zu %016llx\n", got, (unsigned long long)fnv(bytes, got));
  free(bytes);
}

static void test_save_image(void) {
  size_t length = 0;
  uint8_t *text = read_file("tests/app/oracles/shared_simulation_save_image.json", &length);
  CHECK(text != NULL);
  if (text == NULL) return;
  qa_save_value *tree = qa_decode_checkpoint_value(text, length);
  qa_save_image_result parsed;
  CHECK(qa_parse_save_image(tree, &parsed));
  CHECK(parsed.image.schema_version == 4);
  CHECK(parsed.image.consumption_count == 2);
  CHECK(strcmp(parsed.image.consumption[0].outcome, "partial") == 0);
  CHECK(strcmp(parsed.image.consumption[1].detail, "q3 interposed at the guest Pmove") == 0);
  /* bun: parsed 14312 c7b32739ca537019 */
  check_encoding(&parsed.image, 14312, 0xc7b32739ca537019ULL);
  /* bun: checkpoint 14312 be8f712c1db7e96d (checkpointFromSource key order,
   * bodies in captureSharedBodies order) */
  qa_save_image checkpoint = parsed.image;
  checkpoint.key_order = QA_SAVE_IMAGE_ORDER_CHECKPOINT;
  check_encoding(&checkpoint, 14312, 0xbe8f712c1db7e96dULL);

  uint8_t *bytes = NULL; size_t size = 0;
  CHECK(qa_encode_save_image(&parsed.image, &bytes, &size));
  CHECK(size > 12 && memcmp(bytes, "QTSAVE5\n", 8) == 0);
  qa_save_image_result decoded;
  CHECK(qa_decode_save_image(bytes, size, &decoded));
  /* bun: redecoded 14312 c7b32739ca537019 */
  check_encoding(&decoded.image, 14312, 0xc7b32739ca537019ULL);
  qa_save_image_result_free(&decoded);

  /* The shared branch of decodeSavedGame and writeSavedGame. */
  qa_saved_game game;
  CHECK(qa_decode_saved_game(bytes, size, &game));
  CHECK(game.kind == QA_SAVED_GAME_SHARED && game.image.consumption_count == 2);
  const char *path = "build/agents/runtime-restore/tmp/shared_simulation_save.qts";
  CHECK(qa_write_save_image(path, &game.image));
  qa_saved_game_free(&game);
  qa_save_header_result header;
  CHECK(qa_read_save_header(path, &header));
  CHECK(header.present && strcmp(header.header.map, "maps/start.bsp") == 0);
  CHECK(strcmp(header.header.content, parsed.image.recipe.map.entities.content) == 0);
  qa_save_header_result_free(&header);
  remove(path);

  /* A header that disagrees with its recipe is a SaveFormatError; a
   * corrupt header length too. Both abort as the TS throw does, which the
   * fork checks in test_persistence_recipe cover for the envelope; here the
   * header bytes are checked to be exactly the TS {map, content} record. */
  uint32_t header_size = (uint32_t)bytes[8] | (uint32_t)bytes[9] << 8 | (uint32_t)bytes[10] << 16 | (uint32_t)bytes[11] << 24;
  const char *expected_prefix = "{\"map\":\"maps/start.bsp\",\"content\":\"";
  CHECK(header_size > strlen(expected_prefix) && memcmp(bytes + 12, expected_prefix, strlen(expected_prefix)) == 0);
  free(bytes);
  qa_save_image_result_free(&parsed);
  qa_save_value_free(tree);
  free(text);
}

/* A version 3 envelope: no header, schemaVersion 3, consumption migrates
 * to an empty record (bun: v3 14093 5b3ea8c92c3d0b65 after re-encoding). */
static void test_v3_envelope(void) {
  size_t length = 0;
  uint8_t *text = read_file("tests/app/oracles/shared_simulation_save_image.json", &length);
  qa_save_value *tree = qa_decode_checkpoint_value(text, length);
  free(text);
  qa_save_value *legacy = qa_save_value_object();
  for (size_t i = 0; i < tree->entry_count; i++) {
    const char *key = tree->entries[i].key;
    if (strcmp(key, "consumption") == 0) continue;
    qa_save_value *value = strcmp(key, "schemaVersion") == 0 ? qa_save_value_number(3) : tree->entries[i].value;
    if (strcmp(key, "schemaVersion") != 0) tree->entries[i].value = NULL;
    qa_save_value_set(legacy, key, value);
  }
  size_t body_length = 0;
  uint8_t *body = qa_encode_checkpoint_value(legacy, &body_length);
  uint8_t *bytes = malloc(8 + body_length);
  memcpy(bytes, "QTSAVE3\n", 8);
  memcpy(bytes + 8, body, body_length);
  qa_save_image_result decoded;
  CHECK(qa_decode_save_image(bytes, 8 + body_length, &decoded));
  CHECK(decoded.image.schema_version == 4 && decoded.image.consumption_count == 0);
  check_encoding(&decoded.image, 14093, 0x5b3ea8c92c3d0b65ULL);
  qa_save_image_result_free(&decoded);
  free(bytes); free(body);
  qa_save_value_free(legacy);
  qa_save_value_free(tree);
}

/* TS has one Q1FoundationCheckpoint; converting the reader shape to the
 * content shape and back must re-encode to the same bytes. */
static void test_q1_checkpoint_conversion(void) {
  const char *json =
      "{\"format\":\"q1-foundation\",\"provider\":\"q1:official\",\"version\":5,"
      "\"precaches\":{\"phase\":\"frozen\",\"models\":[\"progs/player.mdl\",\"*1\"],\"sounds\":[\"misc/null.wav\"]},"
      "\"edition\":\"classic\",\"time\":12.5,\"frameSeconds\":0.1,\"forceRetouch\":0,"
      "\"basis\":{\"forward\":{\"x\":1,\"y\":0,\"z\":0},\"right\":{\"x\":0,\"y\":-1,\"z\":0},\"up\":{\"x\":0,\"y\":0,\"z\":1}},"
      "\"sequence\":9,\"nextDynamicSlot\":33,\"totalSecrets\":2,\"foundSecrets\":1,\"totalMonsters\":4,\"killedMonsters\":3,"
      "\"worldType\":0,\"mapName\":\"e1m1\",\"world\":{\"slot\":0,\"generation\":1},\"sightEntity\":null,\"sightTime\":0,"
      "\"intermission\":{\"map\":\"e1m2\",\"cause\":{\"slot\":5,\"generation\":2},\"exitAfter\":15},"
      "\"entities\":[{\"actor\":{\"slot\":7,\"generation\":1},\"actorProvider\":\"q1:official\",\"sourceSlot\":7,\"classname\":\"monster_army\",\"sourceOrdinal\":3,"
      "\"state\":{\"model\":\"progs/soldier.mdl\",\"frame\":4,\"skin\":0,\"effects\":0,\"solid\":\"slidebox\",\"movement\":\"step\",\"target\":\"t1\",\"targetname\":\"\","
      "\"killtarget\":\"\",\"message\":\"\",\"delay\":0,\"spawnflags\":0,\"sounds\":0,\"wait\":0,\"speed\":0,\"damage\":0,\"maxHealth\":30,\"aimedDamage\":true,"
      "\"nextThink\":12.6,\"originalModel\":\"\",\"pos1\":{\"x\":0,\"y\":0,\"z\":0},\"pos2\":{\"x\":0,\"y\":0,\"z\":0},\"dest1\":{\"x\":0,\"y\":0,\"z\":0},"
      "\"dest2\":{\"x\":0,\"y\":0,\"z\":0},\"movedir\":{\"x\":0,\"y\":0,\"z\":0},\"mangle\":{\"x\":0,\"y\":0,\"z\":0},\"state\":\"bottom\","
      "\"triggerBounds\":{\"min\":{\"x\":-1,\"y\":-2,\"z\":-3},\"max\":{\"x\":1,\"y\":2,\"z\":3}},\"attackFinished\":0,\"count\":0,\"activated\":false,"
      "\"projectile\":null,\"projectileWeapon\":null,\"angularVelocity\":{\"x\":0,\"y\":0,\"z\":0},\"waterLevel\":0,\"waterType\":-1,"
      "\"movementFlags\":0,\"idealYaw\":90,\"yawSpeed\":20,\"attackState\":\"straight\"},"
      "\"fields\":[{\"key\":\"angle\",\"value\":\"90\"}],\"references\":[{\"key\":\"goalentity\",\"actor\":{\"slot\":8,\"generation\":1}},{\"key\":\"enemy\",\"actor\":null}],"
      "\"owner\":null,\"activator\":{\"slot\":5,\"generation\":2},\"doorGroup\":[{\"slot\":9,\"generation\":1}],"
      "\"monster\":{\"species\":\"army\",\"mode\":\"run\",\"frameIndex\":2,\"sequence\":[1,2,3],\"firstFrame\":6,\"enemy\":{\"slot\":5,\"generation\":2},\"oldEnemy\":null,"
      "\"path\":\"t1\",\"pauseUntil\":0,\"attackFinished\":13,\"painFinished\":0,\"searchUntil\":0,\"deathDrop\":false,\"refired\":true},"
      "\"move\":{\"destination\":{\"x\":4,\"y\":5,\"z\":6},\"done\":\"door_hit_top\"},"
      "\"callbacks\":{\"think\":\"army_run\",\"use\":null,\"touch\":null,\"pain\":\"army_pain\",\"die\":\"army_die\",\"blocked\":null,\"pathEnd\":null}}],"
      "\"players\":[{\"actor\":{\"slot\":5,\"generation\":2},\"actorProvider\":\"q1:official\",\"state\":{\"alpha\":0.5,\"scale\":0,\"weapon\":\"shotgun\","
      "\"primaryHolstered\":false,\"attackHeld\":true,\"jumpHeld\":false,\"teleportUntil\":0,\"attackFinished\":12.9,\"weaponFrame\":1,"
      "\"weaponAnimationAt\":12.4,\"weaponAnimationBase\":0,\"continuousFiring\":false,\"nextWeaponFrame\":0,\"lightningSoundAt\":0,"
      "\"punchAngles\":{\"x\":-2,\"y\":0,\"z\":0},\"nailSide\":0,\"maxHealth\":100,\"megaRotAt\":0,\"hostileUntil\":0,\"viewAngles\":{\"x\":10,\"y\":20,\"z\":0},"
      "\"waterLevel\":0,\"airFinished\":24.5,\"drownDamage\":2,\"drownAt\":0,\"hazardAt\":0,\"autoSwitch\":\"new\"},"
      "\"powerups\":[{\"kind\":\"quad\",\"expires\":40}]}],"
      "\"extensions\":[{\"id\":\"q1:fog\",\"bytes\":{\"$qts\":\"bytes\",\"value\":\"AQID\"}}]}";
  qa_persist_q1_foundation_checkpoint saved;
  CHECK(qa_decode_q1_foundation_checkpoint((const uint8_t *)json, strlen(json), &saved));
  size_t first_length = 0;
  uint8_t *first = qa_encode_q1_foundation_checkpoint(&saved, &first_length);
  qa_q1_foundation_checkpoint content;
  qa_persist_q1_foundation_to_content(&saved, &content);
  CHECK(content.entity_count == 1 && content.player_count == 1 && content.extension_count == 1);
  /* A version 5 grunt keeps its sequence cursor as the legacy animation. */
  CHECK(content.entities[0].monster != NULL && content.entities[0].monster->has_legacy_animation &&
        strcmp(content.entities[0].monster->legacy_animation.mode, "run") == 0 &&
        content.entities[0].monster->legacy_animation.first_frame == 6 &&
        content.entities[0].monster->legacy_animation.frame_index == 2);
  CHECK(content.has_intermission && strcmp(content.intermission_map, "e1m2") == 0 && content.has_intermission_cause);
  CHECK(content.players[0].state.has_alpha && content.players[0].state.alpha == 0.5);
  CHECK(content.extensions[0].len == 3 && content.extensions[0].bytes[2] == 3);
  qa_persist_q1_foundation_checkpoint back;
  qa_persist_q1_foundation_from_content(&content, &back);
  size_t second_length = 0;
  uint8_t *second = qa_encode_q1_foundation_checkpoint(&back, &second_length);
  CHECK(first != NULL && second != NULL && first_length == second_length && memcmp(first, second, first_length) == 0);
  qa_save_value *value = qa_q1_foundation_checkpoint_value(&back);
  size_t third_length = 0;
  uint8_t *third = qa_encode_checkpoint_value(value, &third_length);
  CHECK(third_length == first_length && memcmp(third, first, first_length) == 0);
  free(first); free(second); free(third);
  qa_save_value_free(value);
  qa_q1_foundation_checkpoint_free(&back);
  qa_persist_q1_foundation_content_free(&content);
  qa_q1_foundation_checkpoint_free(&saved);
}

/* ---- checkpoint writers against the TS records (oracles/checkpoint_writers.ts) ---- */
static char *writer_text;
static const char *oracle_line(const char *name) {
  size_t length = strlen(name);
  for (const char *line = writer_text; line != NULL && *line != 0;) {
    const char *end = strchr(line, '\n');
    if (strncmp(line, name, length) == 0 && line[length] == '\t') {
      static char buffer[16384];
      size_t size = (size_t)((end ? end : line + strlen(line)) - (line + length + 1));
      if (size >= sizeof buffer) return NULL;
      memcpy(buffer, line + length + 1, size); buffer[size] = 0;
      return buffer;
    }
    line = end ? end + 1 : NULL;
  }
  return NULL;
}
/* The writer's tree must encode to exactly the TS bytes; frees value. */
static void check_writer(const char *name, qa_save_value *value) {
  const char *expected = oracle_line(name);
  CHECK(expected != NULL && value != NULL);
  if (expected == NULL || value == NULL) { qa_save_value_free(value); return; }
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  bool same = bytes != NULL && length == strlen(expected) && memcmp(bytes, expected, length) == 0;
  CHECK(same);
  if (!same) fprintf(stderr, "  %s\n  want %s\n  got  %.*s\n", name, expected, (int)length, bytes ? (const char *)bytes : "");
  free(bytes);
  qa_save_value_free(value);
}
/* A reader over the TS record (the tree outlives the returned reader). */
static qa_save_value *oracle_tree(const char *name) {
  const char *text = oracle_line(name);
  return text == NULL ? NULL : qa_decode_checkpoint_value((const uint8_t *)text, strlen(text));
}
static qa_save_reader reader_of(qa_save_value *tree) {
  qa_save_reader reader;
  qa_save_reader_init(&reader, tree, "oracle");
  return reader;
}

static int registry_sentinel;
static int registry_calls;
static bool cargo_is_live(void *ctx, qa_actor_id actor) { (void)actor; CHECK(ctx == &registry_sentinel); registry_calls++; return true; }
static bool cargo_source_of(void *ctx, qa_actor_id actor, qa_provider_id *out_provider, double *out_slot) {
  CHECK(ctx == &registry_sentinel); registry_calls++; *out_provider = "q2:game"; *out_slot = actor.slot; return true;
}
static bool cargo_at_source(void *ctx, qa_provider_id provider, double slot, qa_actor_id *out) {
  CHECK(ctx == &registry_sentinel && strcmp(provider, "q2:game") == 0); registry_calls++;
  memset(out, 0, sizeof *out); out->slot = (uint32_t)slot; out->generation = 1; return true;
}
static bool cargo_resolve_saved(void *ctx, qa_saved_actor_id saved, qa_actor_id *out) {
  CHECK(ctx == &registry_sentinel); registry_calls++;
  memset(out, 0, sizeof *out); out->slot = (uint32_t)saved.slot; out->generation = (uint32_t)saved.generation; return true;
}
static qa_actor_id actor_at(uint32_t slot, uint32_t generation) { qa_actor_id id; memset(&id, 0, sizeof id); id.slot = slot; id.generation = generation; return id; }

static void test_checkpoint_writers(void) {
  size_t length = 0;
  writer_text = (char *)read_file("tests/app/oracles/checkpoint_writers.txt", &length);
  CHECK(writer_text != NULL);
  if (writer_text == NULL) return;
  writer_text = realloc(writer_text, length + 1); writer_text[length] = 0;

  /* Q3ArsenalRuntimeState: q3RequestWeaponHolster(q3RequestWeapon(q3SpawnArsenalRuntime("missionpack", 125, 7), 3)). */
  qa_q3_arsenal_runtime_state arsenal; memset(&arsenal, 0, sizeof arsenal);
  arsenal.product = "missionpack"; arsenal.max_health = 125; arsenal.respawned = true; arsenal.event_sequence = 7;
  arsenal.external_slot = QA_Q3_WEAPON_SLOT_HOLSTER_REQUESTED; arsenal.has_requested_weapon = true; arsenal.requested_weapon = 3;
  check_writer("q3-arsenal", qa_app_q3_arsenal_runtime_state_value(&arsenal));

  /* player.sourceMovement in its three literal orders. */
  qa_app_sim_movement_player *player = calloc(1, sizeof *player);
  player->has_source_movement = true;
  qa_app_sim_client_movement_options *m = &player->source_movement;
  m->has_trace_mask = true; m->trace_mask = 0x02010001; m->has_fixed_msec = true; m->fixed_msec = 8; m->gauntlet_hit = true;
  m->debug_level = 1; m->ladders = true; m->order = QA_APP_SIM_MOVEMENT_ORDER_THINK;
  check_writer("movement-think", qa_app_sim_movement_player_source_movement_value(player));
  memset(m, 0, sizeof *m);
  m->trace_mask = 0x10001; m->order = QA_APP_SIM_MOVEMENT_ORDER_SPECTATOR;
  check_writer("movement-spectator", qa_app_sim_movement_player_source_movement_value(player));
  memset(m, 0, sizeof *m);
  m->trace_mask = 0x02010001; m->no_footsteps = true; m->debug_level = 2; m->order = QA_APP_SIM_MOVEMENT_ORDER_SAVED;
  check_writer("movement-saved", qa_app_sim_movement_player_source_movement_value(player));
  player->has_source_movement = false;
  qa_save_value *null_value = qa_app_sim_movement_player_source_movement_value(player);
  CHECK(null_value != NULL && null_value->kind == QA_SAVE_NULL);
  qa_save_value_free(null_value);
  free(player);

  /* saveQ2Attack of a Q3 combat-bridge provenance. */
  qa_actor_id attacker = actor_at(1, 2), inflictor = actor_at(5, 1);
  qa_attack_provenance attack; memset(&attack, 0, sizeof attack);
  attack.sequence = 9; attack.time.kind = QA_SOURCE_TIME_MILLISECONDS; attack.time.value = 1500;
  attack.attacker = &attacker; attack.inflictor = &inflictor;
  attack.has_originating_projectile = true; attack.originating_projectile = actor_at(6, 3);
  attack.weapon = "q3:weapon/rocketlauncher"; attack.weapon_provider = "q3:weapons"; attack.combat_provider = "q3:combat";
  attack.inventory_provider = "q3:inventory"; attack.movement_provider = "q3:movement";
  attack.has_damage_powerup_owner = true; attack.damage_powerup_owner = "q3:weapons";
  attack.cause.kind = QA_ATTACK_CAUSE_Q3; attack.cause.means_of_death = 7; attack.cause.damage_flags = 1;
  check_writer("q2-attack", qa_app_sim_save_q2_attack_value(&attack));

  /* Records the C readers decode: read, then write back. */
  qa_save_value *tree = oracle_tree("hand-grenades");
  qa_save_reader reader = reader_of(tree);
  qa_app_sim_hand_grenade_runtime_checkpoint grenades = qa_app_sim_read_hand_grenade_runtime_checkpoint(&reader);
  check_writer("hand-grenades", qa_app_sim_hand_grenade_runtime_checkpoint_value(&grenades));
  qa_app_sim_hand_grenade_runtime_checkpoint_destroy(&grenades);
  qa_save_reader_destroy(&reader); qa_save_value_free(tree);
  static const char *const grapples[] = {"grapple-ctf", "grapple-lmctf"};
  static const char *const restored_grapples[] = {"grapple-ctf-restored", "grapple-lmctf-restored"};
  for (size_t i = 0; i < 2; i++) {
    tree = oracle_tree(grapples[i]); reader = reader_of(tree);
    qa_app_sim_grapple_runtime_checkpoint grapple = qa_app_sim_read_grapple_runtime_checkpoint(&reader);
    /* The read records keep the reader's key order, as a restored runtime captures them. */
    check_writer(restored_grapples[i], qa_app_sim_grapple_runtime_checkpoint_value(&grapple));
    /* The same records in the admit()/createGrappleWeaponState() order. */
    for (size_t k = 0; k < grapple.control_count; k++) grapple.controls[k].control.read_order = false;
    for (size_t k = 0; k < grapple.weapon_animation_count; k++) grapple.weapon_animations[k].animation.state_read_order = false;
    check_writer(grapples[i], qa_app_sim_grapple_runtime_checkpoint_value(&grapple));
    qa_app_sim_grapple_runtime_checkpoint_destroy(&grapple);
    qa_save_reader_destroy(&reader); qa_save_value_free(tree);
  }
  static const char *const slots[] = {"slot-active", "slot-activating"};
  for (size_t i = 0; i < 2; i++) {
    tree = oracle_tree(slots[i]); reader = reader_of(tree);
    check_writer(slots[i], qa_app_sim_weapon_slot_state_value(qa_app_sim_read_weapon_slot_state(&reader)));
    qa_save_reader_destroy(&reader); qa_save_value_free(tree);
  }
  /* The WeaponSlot.snapshot() literals built directly, so the writer is compared with the TS bytes and not only
   * with its own reader's round trip. */
  qa_app_sim_weapon_slot_state active; memset(&active, 0, sizeof active);
  active.kind = "active"; active.provider = "q2:weapons";
  check_writer("slot-active", qa_app_sim_weapon_slot_state_value(active));
  qa_app_sim_weapon_slot_state activating; memset(&activating, 0, sizeof activating);
  activating.kind = "activating"; activating.from = "q2:weapons";
  activating.next.provider = "q2:grapple"; activating.next.item = "q2:weapon_grapple"; activating.request = 3;
  check_writer("slot-activating", qa_app_sim_weapon_slot_state_value(activating));
  tree = oracle_tree("q3-selected-arsenal"); reader = reader_of(tree);
  qa_app_q3_selected_arsenal_checkpoint selected = qa_app_read_q3_selected_arsenal_checkpoint(&reader);
  CHECK(selected.arsenal_read_order);
  check_writer("q3-selected-arsenal-restored", qa_app_q3_selected_arsenal_checkpoint_value(&selected));
  selected.arsenal_read_order = false;
  check_writer("q3-selected-arsenal", qa_app_q3_selected_arsenal_checkpoint_value(&selected));
  free((void *)selected.arsenal.ammo);
  qa_save_reader_destroy(&reader); qa_save_value_free(tree);
  tree = oracle_tree("q3-character"); reader = reader_of(tree);
  qa_q3_character_checkpoint character = qa_app_sim_read_q3_character(&reader);
  check_writer("q3-character", qa_q3_character_checkpoint_value(&character));
  qa_save_reader_destroy(&reader); qa_save_value_free(tree);
  tree = oracle_tree("q1-travel"); reader = reader_of(tree);
  qa_q1_travel_state travel = qa_app_sim_read_q1_travel(&reader);
  check_writer("q1-travel", qa_q1_travel_state_value(&travel));
  qa_q1_travel_state_destroy(&travel);
  qa_save_reader_destroy(&reader); qa_save_value_free(tree);
  tree = oracle_tree("q2-view"); reader = reader_of(tree);
  qa_q2_base_player_view view = qa_app_sim_read_q2_view(&reader);
  check_writer("q2-view", qa_q2_player_view_value(&view));
  view.help_icon = true;
  check_writer("q2-view-help", qa_q2_player_view_value(&view));
  qa_save_reader_destroy(&reader); qa_save_value_free(tree);

  /* The Q2 "model" event the sourceModels map keeps. */
  qa_q2_presentation_event model; memset(&model, 0, sizeof model);
  model.kind = "model"; model.has_actor = true; model.actor = actor_at(4, 1); model.path = "models/monsters/soldier/tris.md2";
  model.attached_models[0] = ""; model.attached_models[1] = "models/weapons/g_shotg/tris.md2"; model.attached_models[2] = "";
  model.frame = 12; model.old_frame = 11; model.scale = 1; model.alpha = 0.5; model.skin = 2; model.render_flags = 4;
  check_writer("q2-model", qa_q2_presentation_model_value(&model));
  check_writer("q2-model-restored", qa_q2_presentation_model_value_ordered(&model, true));

  /* SourcePickupCargo: capture, then restore into a fresh owner and capture again. */
  qa_app_sim_pickup_registry registry = {&registry_sentinel, cargo_is_live, cargo_source_of, cargo_at_source, cargo_resolve_saved};
  qa_app_sim_source_pickup_cargo *cargo = qa_app_sim_source_pickup_cargo_create(registry, "q2:game");
  qa_app_sim_source_pickup_cargo_set(cargo, actor_at(11, 1), (qa_app_sim_dropped_pickup_cargo){"q2:ammo_shells", 10});
  qa_app_sim_dropped_pickup_slot retained = {"q2:ammo_cells", 50, 12};
  qa_app_sim_dropped_pickup_level level = {"base1", &retained, 1};
  qa_app_sim_source_pickup_cargo_revisit(cargo, &level, 1, "base2");
  qa_app_sim_source_pickup_cargo_set(cargo, actor_at(13, 1), (qa_app_sim_dropped_pickup_cargo){"q2:item_quad", 1});
  qa_save_value *captured = qa_app_sim_source_pickup_cargo_capture(cargo);
  qa_app_sim_source_pickup_cargo *restored = qa_app_sim_source_pickup_cargo_create(registry, "q2:game");
  reader = reader_of(captured);
  qa_app_sim_source_pickup_cargo_restore(restored, &reader);
  qa_save_reader_destroy(&reader);
  check_writer("dropped-pickups", captured);
  check_writer("dropped-pickups-restored", qa_app_sim_source_pickup_cargo_capture(restored));
  qa_app_sim_dropped_pickup_cargo got;
  CHECK(qa_app_sim_source_pickup_cargo_get(restored, actor_at(13, 1), &got) && strcmp(got.item, "q2:item_quad") == 0 && got.count == 1);
  /* An undefined reader clears both stores. */
  qa_save_reader undefined_reader = reader_of(NULL);
  qa_app_sim_source_pickup_cargo_restore(restored, &undefined_reader);
  qa_save_value *empty = qa_app_sim_source_pickup_cargo_capture(restored);
  CHECK(empty != NULL && empty->entry_count == 2 && empty->entries[0].value->item_count == 0 && empty->entries[1].value->item_count == 0);
  qa_save_value_free(empty);
  CHECK(registry_calls > 0);
  qa_app_sim_source_pickup_cargo_destroy(restored);
  qa_app_sim_source_pickup_cargo_destroy(cargo);
  free(writer_text); writer_text = NULL;
}

/* The decoded hand-grenades checkpoint releases its actors and tree. */
static void test_hand_grenades_free(void) {
  const char *json = "{\"version\":1,\"edition\":\"rerelease\",\"actors\":[{\"actor\":{\"slot\":1,\"generation\":2},\"state\":"
      "{\"config\":{\"enabled\":true,\"initialAmmo\":5,\"capacity\":50,\"infiniteAmmo\":false},\"action\":{\"kind\":\"idle\"}}}]}";
  qa_persist_hand_grenades_checkpoint decoded;
  CHECK(qa_decode_hand_grenades_checkpoint((const uint8_t *)json, strlen(json), &decoded));
  CHECK(decoded.actors_count == 1 && decoded.root != NULL && strcmp(decoded.edition, "rerelease") == 0);
  qa_persist_hand_grenades_checkpoint_free(&decoded);
  CHECK(decoded.actors == NULL && decoded.root == NULL && decoded.actors_count == 0);
  qa_persist_hand_grenades_checkpoint_free(NULL);
}

int main(void) {
  test_checkpoint_writers();
  test_hand_grenades_free();
  test_save_image();
  test_v3_envelope();
  test_q1_checkpoint_conversion();
  printf("test_shared_simulation_save: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
