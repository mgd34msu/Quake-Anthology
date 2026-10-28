/*
 * Port of the sourceActorsCheckpoint / readSourceActorsCheckpoint
 * cases in tests/persistence/save.test.ts (src/persistence/
 * save-image.ts): the "world:source-slots" provider checkpoint
 * round-trips an empty and a populated source actor list, and an
 * unsupported checkpoint identity is a SaveFormatError (abort).
 * Also covers the QTSAVE image codec itself (parseSaveImage /
 * encodeSaveImage / decodeSaveImage / file read+write) against the
 * recipe fixture shape from tests/first-playable/campaign-unit
 * .test.ts: field-level parse checks, an encode->decode->encode
 * byte-stable round trip, the legacy QTSAVE2 flat-armor layout,
 * a bad signature abort, and an atomic file write/read.
 * TS toThrow cases run in a child process and must abort.
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

#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"

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

static void test_source_actors_round_trip(void) {
  const qa_source_actor_checkpoint sources[2] = {
      {"q1:game", 3, {5, 2}},
      {"q2:game", 0, {0, 7}},
  };
  qa_provider_checkpoint checkpoint;
  CHECK(qa_source_actors_checkpoint(sources, 2, &checkpoint));
  CHECK(strcmp(checkpoint.provider, "world:actors") == 0);
  CHECK(strcmp(checkpoint.schema, "world:source-slots") == 0);
  CHECK(checkpoint.version == 1);
  CHECK(checkpoint.bytes.length > 0);

  qa_source_actors_result read;
  CHECK(qa_read_source_actors_checkpoint(&checkpoint, &read));
  CHECK(read.count == 2);
  if (read.count == 2) {
    CHECK(strcmp(read.entries[0].provider, "q1:game") == 0);
    CHECK(read.entries[0].source_slot == 3);
    CHECK(read.entries[0].actor.slot == 5 && read.entries[0].actor.generation == 2);
    CHECK(strcmp(read.entries[1].provider, "q2:game") == 0);
    CHECK(read.entries[1].source_slot == 0);
    CHECK(read.entries[1].actor.slot == 0 && read.entries[1].actor.generation == 7);
  }
  qa_source_actors_result_free(&read);
  free((uint8_t *)checkpoint.bytes.bytes);
}

static void test_source_actors_empty(void) {
  qa_provider_checkpoint checkpoint;
  CHECK(qa_source_actors_checkpoint(NULL, 0, &checkpoint));
  qa_source_actors_result read;
  CHECK(qa_read_source_actors_checkpoint(&checkpoint, &read));
  CHECK(read.count == 0);
  qa_source_actors_result_free(&read);
  free((uint8_t *)checkpoint.bytes.bytes);
}

static void test_source_actors_unsupported_aborts(void) {
  qa_provider_checkpoint checkpoint;
  CHECK(qa_source_actors_checkpoint(NULL, 0, &checkpoint));
  qa_provider_checkpoint wrong_schema = checkpoint;
  wrong_schema.schema = "world:other";
  qa_provider_checkpoint wrong_version = checkpoint;
  wrong_version.version = 2;
  qa_source_actors_result read;
  CHECK_ABORTS((void)qa_read_source_actors_checkpoint(&wrong_schema, &read));
  CHECK_ABORTS((void)qa_read_source_actors_checkpoint(&wrong_version, &read));
  free((uint8_t *)checkpoint.bytes.bytes);
}

/* --- save image codec fixture (campaign-unit.test.ts recipe) --- */
#define CONTENT "q1:classic:id1:fixture"
#define DIGEST \
  "sha256:0000000000000000000000000000000000000000000000000000000000000000"
#define PROVIDER(role) "{\"provider\":\"q1:" role "\",\"content\":\"" CONTENT "\"}"
#define MOUNT \
  "{\"kind\":\"loose\",\"identity\":{\"id\":\"mount:q1:fixture\",\"content\":\"" CONTENT \
  "\",\"generation\":2},\"rootPath\":\"/fixture\"}"
#define CLOCK \
  "{\"kind\":\"q1-netquake\",\"minimumFrameSeconds\":0.001," \
  "\"maximumFrameSeconds\":0.1,\"fixedFrameSeconds\":null}"
#define BOUNDS \
  "{\"min\":{\"x\":-16,\"y\":-16,\"z\":-24},\"max\":{\"x\":16,\"y\":16,\"z\":32}}"
#define BODY \
  "{\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"angles\":{\"x\":0,\"y\":90,\"z\":0}," \
  "\"velocity\":{\"x\":0,\"y\":0,\"z\":0},\"bounds\":" BOUNDS \
  ",\"ground\":{\"slot\":4,\"generation\":1}}"
#define MODULE \
  "{\"id\":\"q3:game\",\"artifactPath\":\"qagame.qvm\",\"digest\":\"" DIGEST \
  "\",\"revision\":\"r1\"}"

static void patch_bytes(qa_save_value *object, const char *key,
                        const uint8_t *bytes, size_t length) {
  for (size_t i = 0; i < object->entry_count; i++) {
    if (strcmp(object->entries[i].key, key) != 0) continue;
    qa_save_value_free(object->entries[i].value);
    object->entries[i].value = qa_save_value_bytes(bytes, length);
    return;
  }
  CHECK(false && "patch target exists");
}
static char *fixture_resource_id(void) {
  static const qa_loose_mount loose = {{"mount:q1:fixture", CONTENT, 2},
                                       "/fixture"};
  qa_resolved_resource_reference resource;
  memset(&resource, 0, sizeof resource);
  resource.requested_path = "maps/start.bsp";
  resource.provenance.kind = QA_CONTENT_MOUNT_LOOSE;
  resource.provenance.loose_mount = &loose;
  resource.provenance.member_path = "maps/start.bsp";
  resource.digest = DIGEST;
  resource.byte_length = 123;
  resource.resolution.kind = QA_RESOURCE_RESOLUTION_DEFAULT_ORDER;
  resource.resolution.plan = "mount-plan:fixture:1";
  resource.resolution.rank = 0;
  return qa_create_resource_id(&resource);
}
static qa_save_value *fixture_tree(int version, bool legacy_armor) {
  char *resource_id = fixture_resource_id();
  CHECK(resource_id != NULL);
  char geometry[1024];
  snprintf(geometry, sizeof geometry,
           "{\"id\":\"%s\",\"requestedPath\":\"maps/start.bsp\","
           "\"provenance\":{\"kind\":\"loose\",\"memberPath\":\"maps/start.bsp\","
           "\"mount\":" MOUNT "},\"digest\":\"" DIGEST "\",\"byteLength\":123,"
           "\"resolution\":{\"kind\":\"default-order\","
           "\"plan\":\"mount-plan:fixture:1\",\"rank\":0}}",
           resource_id);
  const char *armor =
      legacy_armor
          ? "{\"kind\":\"q2\",\"points\":10,\"normalProtection\":0.6,"
            "\"energyProtection\":0.3,\"item\":\"q2:armor/body\","
            "\"powerArmor\":{\"kind\":\"screen\",\"cells\":30}}"
          : "{\"regular\":{\"kind\":\"q1\",\"points\":50,\"absorption\":0.8,"
            "\"item\":\"q1:armor/red\"},\"powered\":{\"kind\":\"shield\","
            "\"cells\":77}}";
  char text[8192];
  int written = snprintf(text, sizeof text,
           "{\"schemaVersion\":%d,\"recipe\":{\"schemaVersion\":3,"
           "\"id\":\"recipe:fixture:1\",\"preset\":\"recipe:fixture:1\","
           "\"map\":{\"geometryContent\":\"" CONTENT "\",\"geometry\":%s,"
           "\"entities\":" PROVIDER("game") "},"
           "\"campaign\":{\"kind\":\"campaign\",\"mission\":" PROVIDER("mission")
           ",\"gamecode\":" PROVIDER("game") "},"
           "\"movement\":" PROVIDER("movement") ","
           "\"character\":{\"definition\":" PROVIDER("character")
           ",\"appearance\":" PROVIDER("appearance") "},"
           "\"weapons\":[" PROVIDER("weapons") "],"
           "\"equipment\":{\"grapple\":{\"kind\":\"disabled\"},"
           "\"handGrenades\":{\"kind\":\"enabled\",\"source\":" PROVIDER("game")
           ",\"binding\":\"offhand\",\"edition\":\"classic\","
           "\"initialAmmo\":2,\"capacity\":5}},"
           "\"enemies\":{\"kind\":\"replace\","
           "\"default\":{\"kind\":\"map-defined\"},\"byClassname\":{"
           "\"monster_army\":{\"source\":" PROVIDER("game")
           ",\"classname\":\"monster_dog\"}}},"
           "\"presentation\":{\"doppler\":{\"kind\":\"source\"},"
           "\"environment\":{\"kind\":\"audio-content\"},\"assets\":\"" CONTENT
           "\",\"hud\":" PROVIDER("hud") ",\"effects\":" PROVIDER("effects")
           ",\"audio\":" PROVIDER("audio") "},"
           "\"engineBehavior\":" PROVIDER("engine") ",\"combat\":"
           PROVIDER("combat") ",\"inventory\":" PROVIDER("inventory")
           ",\"match\":" PROVIDER("match") ",\"transition\":"
           PROVIDER("transition") ","
           "\"execution\":[{\"kind\":\"typescript\",\"owner\":" PROVIDER("game")
           ",\"implementation\":\"q1:official\",\"role\":\"server-game\","
           "\"api\":{\"kind\":\"q1-netquake\",\"programVersion\":6,"
           "\"systemCrc\":5927}}],"
           "\"mounts\":{\"id\":\"mount-plan:fixture:1\",\"mounts\":[" MOUNT
           "],\"defaultOrder\":[\"mount:q1:fixture\"],\"prefixOrders\":[{"
           "\"prefix\":\"maps/\",\"mounts\":[\"mount:q1:fixture\"]}]},"
           "\"resources\":[%s],",
           version, geometry, geometry);
  CHECK(written > 0 && (size_t)written < sizeof text);
  snprintf(text + written, sizeof text - (size_t)written,
           "\"timing\":[{\"provider\":\"q1:official\",\"clock\":" CLOCK
           ",\"numeric\":{\"id\":\"q1:binary32\","
           "\"arithmetic\":{\"kind\":\"binary32\",\"round\":\"each-operation\"},"
           "\"scalarStorage\":\"binary32\","
           "\"floatToInt\":\"checked-c-truncation\","
           "\"integerOverflow\":\"wrap32\"}}],"
           "\"ordering\":{\"kind\":\"native\","
           "\"traversal\":\"source-slot-order\",\"clock\":" CLOCK "}},"
           "\"frame\":{\"frame\":3,\"time\":{\"kind\":\"seconds\",\"value\":2.5},"
           "\"elapsed\":{\"kind\":\"seconds\",\"value\":0.1},"
           "\"phase\":\"frame-exit\"},"
           "\"nextEventSequence\":19,"
           "\"clocks\":[{\"provider\":\"q1:official\","
           "\"time\":{\"kind\":\"seconds\",\"value\":2.5}}],"
           "\"random\":[{\"provider\":\"q1:official\",\"state\":{"
           "\"kind\":\"q3-lcg\",\"seed\":42,\"draws\":7}}],"
           "\"actors\":[{\"slot\":1,\"generation\":2,\"lifetime\":{"
           "\"kind\":\"active\",\"owner\":\"q1:game\","
           "\"definition\":\"q1:player\"}},"
           "{\"slot\":4,\"generation\":1,\"lifetime\":{\"kind\":\"free\","
           "\"freedAt\":{\"kind\":\"milliseconds\",\"value\":900}}}],"
           "\"bodies\":[{\"actor\":{\"slot\":1,\"generation\":2},\"body\":" BODY
           ",\"attachment\":{\"anchor\":{\"slot\":4,\"generation\":1},"
           "\"follow\":{\"kind\":\"center\"}},\"linkCount\":7,"
           "\"linked\":{\"state\":" BODY ",\"absoluteBounds\":" BOUNDS "}}],"
           "\"combat\":[{\"actor\":{\"slot\":1,\"generation\":2},\"state\":{"
           "\"health\":73,\"armor\":%s,\"mass\":200,\"canTakeDamage\":true,"
           "\"invulnerable\":false,\"team\":null,\"noKnockback\":true}}],"
           "\"inventories\":[{\"actor\":{\"slot\":1,\"generation\":2},"
           "\"entries\":[{\"item\":\"q1:ammo/nails\",\"count\":-3,"
           "\"capacity\":200,\"countPolicy\":{\"kind\":\"source-counter\","
           "\"arithmetic\":\"binary32\"}}]}],"
           "\"configurations\":[{\"actor\":{\"slot\":1,\"generation\":2},"
           "\"movement\":" PROVIDER("movement") ",\"character\":{"
           "\"definition\":" PROVIDER("character") ",\"appearance\":"
           PROVIDER("appearance") "},\"weapons\":[" PROVIDER("weapons")
           "],\"inventory\":" PROVIDER("inventory") "}],"
           "\"thinks\":[{\"actor\":{\"slot\":1,\"generation\":2},"
           "\"callback\":\"q1:think\",\"due\":{\"kind\":\"seconds\","
           "\"value\":3},\"boundary\":\"after-physics\","
           "\"provider\":\"q1:game\",\"sequence\":5,"
           "\"executionProvider\":\"q1:exec\"}],"
           "\"providers\":[{\"provider\":\"q1:game\",\"schema\":\"q1:world\","
           "\"version\":1,\"bytes\":0}],"
           "\"guests\":[{\"kind\":\"typescript\",\"module\":" MODULE ","
           "\"random\":[],\"callbacks\":[],"
           "\"api\":{\"kind\":\"q3-qagame\",\"version\":7},"
           "\"state\":{\"module\":" MODULE ",\"format\":\"q3:state\","
           "\"bytes\":0}}]}",
           armor);
  free(resource_id);
  qa_save_value *tree =
      qa_parse_json_value((const uint8_t *)text, strlen(text), false);
  CHECK(tree != NULL);
  if (tree == NULL) return NULL;
  qa_save_value *providers =
      (qa_save_value *)qa_save_value_get(tree, "providers");
  patch_bytes(providers->items[0], "bytes", (const uint8_t *)"abc", 3);
  qa_save_value *guests = (qa_save_value *)qa_save_value_get(tree, "guests");
  qa_save_value *state =
      (qa_save_value *)qa_save_value_get(guests->items[0], "state");
  patch_bytes(state, "bytes", (const uint8_t *)"xy", 2);
  return tree;
}
static void check_image(const qa_save_image *image) {
  CHECK(image->schema_version == 3);
  CHECK(strcmp(image->recipe.id, "recipe:fixture:1") == 0);
  CHECK(strcmp(image->recipe.map.geometry.requested_path,
               "maps/start.bsp") == 0);
  CHECK(image->recipe.campaign.is_campaign);
  CHECK(image->recipe.execution_count == 1);
  CHECK(image->recipe.execution[0].kind == QA_EXECUTION_MODULE_TYPESCRIPT);
  CHECK(image->recipe.enemies.kind == QA_ENEMY_SELECTION_REPLACE);
  CHECK(image->recipe.enemies.by_classname_count == 1);
  CHECK(image->recipe.equipment.hand_grenades.is_enabled);
  CHECK(image->frame.frame == 3 && image->next_event_sequence == 19);
  CHECK(image->clock_count == 1 && image->random_count == 1);
  CHECK(image->random[0].state.seed == 42);
  CHECK(image->actor_count == 2);
  CHECK(image->actors[0].lifetime_is_active);
  CHECK(!image->actors[1].lifetime_is_active &&
        image->actors[1].freed_at != NULL &&
        image->actors[1].freed_at->value == 900);
  CHECK(image->body_count == 1 && image->bodies[0].link_count == 7);
  CHECK(image->bodies[0].body.ground != NULL &&
        image->bodies[0].body.ground->slot == 4);
  CHECK(image->bodies[0].attachment != NULL &&
        image->bodies[0].attachment->follow_kind == QA_BODY_FOLLOW_CENTER);
  CHECK(image->bodies[0].linked != NULL);
  CHECK(image->combat_count == 1 && image->combat[0].state.health == 73);
  CHECK(image->combat[0].state.has_no_knockback &&
        image->combat[0].state.no_knockback);
  CHECK(image->inventory_count == 1 &&
        image->inventories[0].entries[0].has_count_policy &&
        strcmp(image->inventories[0].entries[0].count_policy.arithmetic,
               "binary32") == 0);
  CHECK(image->configuration_count == 1 &&
        image->configurations[0].weapons.count == 1);
  CHECK(image->think_count == 1 &&
        image->thinks[0].boundary == QA_THINK_AFTER_PHYSICS &&
        image->thinks[0].has_execution_provider);
  CHECK(image->provider_count == 1 &&
        image->providers[0].bytes.length == 3 &&
        memcmp(image->providers[0].bytes.bytes, "abc", 3) == 0);
  CHECK(image->guest_count == 1 &&
        image->guests[0].kind == QA_GUEST_CHECKPOINT_TYPESCRIPT &&
        image->guests[0].state.bytes.length == 2);
  CHECK(image->mods == NULL);
}
static void test_save_image_round_trip(void) {
  qa_save_value *tree = fixture_tree(3, false);
  qa_save_image_result parsed;
  CHECK(qa_parse_save_image(tree, &parsed));
  CHECK(parsed.root == NULL);
  CHECK(!parsed.image.legacy_armor_layout);
  check_image(&parsed.image);
  CHECK(parsed.image.combat[0].state.armor.regular.kind == QA_REGULAR_ARMOR_Q1);
  CHECK(strcmp(parsed.image.combat[0].state.armor.powered.kind, "shield") == 0);
  CHECK(parsed.image.combat[0].state.armor.powered.cells == 77);

  uint8_t *bytes = NULL;
  size_t length = 0;
  CHECK(qa_encode_save_image(&parsed.image, &bytes, &length));
  CHECK(length > 8 && memcmp(bytes, "QTSAVE3\n", 8) == 0);
  qa_save_image_result_free(&parsed);
  qa_save_value_free(tree);

  qa_save_image_result decoded;
  CHECK(qa_decode_save_image(bytes, length, &decoded));
  check_image(&decoded.image);
  uint8_t *again = NULL;
  size_t again_length = 0;
  CHECK(qa_encode_save_image(&decoded.image, &again, &again_length));
  CHECK(again_length == length && memcmp(again, bytes, length) == 0);
  free(again);

  const char *path = "/tmp/qa_test_save_image.qts";
  CHECK(qa_write_save_image(path, &decoded.image));
  qa_save_image_result_free(&decoded);
  qa_save_image_result loaded;
  CHECK(qa_read_save_image(path, &loaded));
  CHECK(loaded.image.combat_count == 1 && loaded.image.combat[0].state.health == 73);
  qa_save_image_result_free(&loaded);
  remove(path);
  free(bytes);
}
static void test_save_image_legacy(void) {
  qa_save_value *tree = fixture_tree(2, true);
  size_t payload_length = 0;
  uint8_t *payload = qa_encode_checkpoint_value(tree, &payload_length);
  CHECK(payload != NULL);
  qa_save_value_free(tree);
  uint8_t *bytes = malloc(8 + payload_length);
  memcpy(bytes, "QTSAVE2\n", 8);
  memcpy(bytes + 8, payload, payload_length);
  free(payload);
  qa_save_image_result decoded;
  CHECK(qa_decode_save_image(bytes, 8 + payload_length, &decoded));
  CHECK(decoded.image.schema_version == 3);
  CHECK(decoded.image.legacy_armor_layout);
  CHECK(decoded.image.combat[0].state.armor.regular.kind == QA_REGULAR_ARMOR_Q2);
  CHECK(strcmp(decoded.image.combat[0].state.armor.powered.kind, "screen") == 0);
  CHECK(decoded.image.combat[0].state.armor.powered.cells == 30);
  qa_save_image_result_free(&decoded);
  bytes[6] = '1'; /* TS: oldSignature[6] = 49 -> signature/version */
  qa_save_image_result rejected;
  CHECK_ABORTS((void)qa_decode_save_image(bytes, 8 + payload_length, &rejected));
  free(bytes);
}

int main(void) {
  test_source_actors_round_trip();
  test_source_actors_empty();
  test_source_actors_unsupported_aborts();
  test_save_image_round_trip();
  test_save_image_legacy();
  printf("test_persistence_recipe: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
