/* Port of tests/network/unified-events.test.ts and unified-player-events.test.ts, with
 * rows captured from the TS codec in unified_events_corpus.h. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/contracts/identity.h"
#include "unified_events_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int ctx_sentinel;
typedef struct map_rule { double slot_add, generation_add; const char *resource; double expect_slot, expect_generation; } map_rule;
static map_rule rule = {10, 1, "resource:unified:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", -1, -1};

static qa_actor_id map_actor(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  if (rule.expect_slot >= 0) CHECK(slot == rule.expect_slot && generation == rule.expect_generation);
  qa_actor_id id = {0};
  id.slot = (int64_t)(slot + rule.slot_add);
  id.generation = (int64_t)(generation + rule.generation_add);
  return id;
}
static qa_client_id map_client(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  qa_client_id id = {0};
  id.slot = (int64_t)slot + 20;
  id.generation = (int64_t)generation + 2;
  return id;
}
static qa_seat_id map_seat(void *ctx, double index) {
  CHECK(ctx == &ctx_sentinel);
  qa_seat_id id = {0};
  id.index = (int64_t)index + 1;
  return id;
}
static qa_resource_id map_resource(void *ctx, qa_resource_id id) {
  CHECK(ctx == &ctx_sentinel);
  (void)id;
  return rule.resource;
}
static const qa_app_unified_identity_decoder identity = {
  .ctx = &ctx_sentinel, .actor = map_actor, .client = map_client, .seat = map_seat, .resource_id = map_resource};

static qa_save_value *parse(const char *text) {
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, strlen(text), false);
  if (!value) abort();
  return value;
}
/* Deep equality including object key order and exact numbers. */
static bool same(const qa_save_value *a, const qa_save_value *b) {
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case QA_SAVE_BOOLEAN: return a->boolean == b->boolean;
    case QA_SAVE_NUMBER: return a->number == b->number;
    case QA_SAVE_STRING: return strcmp(a->text, b->text) == 0;
    case QA_SAVE_ARRAY:
      if (a->item_count != b->item_count) return false;
      for (size_t i = 0; i < a->item_count; i++) if (!same(a->items[i], b->items[i])) return false;
      return true;
    case QA_SAVE_OBJECT: {
      size_t ai = 0, bi = 0;
      for (;;) {
        while (ai < a->entry_count && a->entries[ai].value->kind == QA_SAVE_UNDEFINED) ai++;
        while (bi < b->entry_count && b->entries[bi].value->kind == QA_SAVE_UNDEFINED) bi++;
        if (ai == a->entry_count || bi == b->entry_count) return ai == a->entry_count && bi == b->entry_count;
        if (strcmp(a->entries[ai].key, b->entries[bi].key) != 0) return false;
        if (!same(a->entries[ai].value, b->entries[bi].value)) return false;
        ai++; bi++;
      }
    }
    default: return true;
  }
}
static const qa_save_value *path(const qa_save_value *value, const char *a, const char *b, const char *c) {
  const char *keys[] = {a, b, c};
  for (size_t i = 0; i < 3 && keys[i] && value; i++) value = qa_save_value_get(value, keys[i]);
  return value;
}
static void free_events(qa_save_value **events, size_t count) {
  for (size_t i = 0; i < count; i++) qa_save_value_free(events[i]);
  free(events);
}
/* Wire: decode then encode; compares bytes with `expected` when set. */
static qa_save_value **roundtrip(const char *wire, const char *expected, size_t *count) {
  qa_save_value **events = qa_app_unified_decode_presentation_events(
      (const uint8_t *)wire, strlen(wire), &identity, count);
  if (expected) {
    size_t length = 0;
    uint8_t *bytes = qa_app_unified_encode_presentation_events(events, *count, &length);
    bool equal = length == strlen(expected) && memcmp(bytes, expected, length) == 0;
    if (!equal) fprintf(stderr, "bytes differ\n got %.*s\n exp %s\n", (int)length, bytes, expected);
    CHECK(equal);
    free(bytes);
  }
  return events;
}

/* Runs fn in a child; returns the first stderr line, or NULL when it did not abort. */
typedef void (*child_fn)(const void *arg);
static char *abort_message(child_fn fn, const void *arg) {
  int pipes[2];
  if (pipe(pipes) != 0) abort();
  pid_t pid = fork();
  if (pid == 0) {
    prctl(PR_SET_DUMPABLE, 0);
    dup2(pipes[1], 2);
    close(pipes[0]);
    fn(arg);
    _exit(0);
  }
  close(pipes[1]);
  static char text[8192];
  size_t used = 0;
  ssize_t got;
  while ((got = read(pipes[0], text + used, sizeof text - 1 - used)) > 0) used += (size_t)got;
  text[used] = '\0';
  close(pipes[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return NULL;
  text[strcspn(text, "\n")] = '\0';
  return text;
}
static void decode_child(const void *arg) {
  const char *text = arg;
  size_t count;
  qa_app_unified_decode_presentation_events((const uint8_t *)text, strlen(text), &identity, &count);
}
static void read_sim_child(const void *arg) {
  qa_save_value *value = parse(arg);
  qa_app_unified_read_simulation_event(value, &identity);
}
static void write_sim_child(const void *arg) {
  qa_save_value *value = parse(arg);
  qa_app_unified_write_simulation_event(value);
}

static void test_ts_unified_events(void) {
  /* Identity (3,7) -> (12,9), resource -> local id: the TS test's decoder. */
  rule = (map_rule){9, 2, "resource:local-sound", 3, 7};
  size_t count = 0;
  /* decode alone: re-encoding would differ only by the remapped actors. */
  qa_save_value **decoded = roundtrip(ts_test_events, NULL, &count);
  CHECK(count == 7);
  const qa_save_value *q1 = decoded[0], *poi = decoded[1], *inventory = decoded[2], *character = decoded[4],
      *entity = decoded[5], *pause = decoded[6];
  CHECK(path(q1, "recipient", "slot", NULL)->number == 12 && path(q1, "recipient", "generation", NULL)->number == 9);
  CHECK(path(pause, "recipient", "slot", NULL)->number == 12);
  qa_save_value *pause_event = parse("{\"kind\":\"pause\",\"paused\":true}");
  CHECK(same(qa_save_value_get(pause, "event"), pause_event));
  qa_save_value_free(pause_event);
  CHECK(path(q1, "event", "player", "slot")->number == 12);
  qa_save_value *parts = parse("[{\"text\":\"part\",\"args\":[2]}]");
  CHECK(same(path(q1, "event", "parts", NULL), parts));
  qa_save_value_free(parts);
  CHECK(strcmp(path(poi, "event", "fields", "image")->text, "i_help") == 0);
  CHECK(path(inventory, "event", "selected", NULL)->kind == QA_SAVE_NULL);
  CHECK(qa_save_value_get(path(inventory, "event", "entries", NULL)->items[0], "count")->number == -2);
  CHECK(path(character, "event", "actor", "slot")->number == 12);
  qa_save_value *delta = parse("{\"x\":4,\"y\":5,\"z\":6}");
  CHECK(same(qa_save_value_get(path(entity, "event", "state", "pos"), "delta"), delta));
  qa_save_value_free(delta);
  free_events(decoded, count);

  /* Encoding is byte-exact, with no owner text: re-encode the wire under a pass-through identity. */
  rule = (map_rule){0, 0, "resource:local-sound", 3, 7};
  decoded = roundtrip(ts_test_events, ts_test_events, &count);
  size_t length = 0;
  uint8_t *encoded = qa_app_unified_encode_presentation_events(decoded, count, &length);
  CHECK(memmem(encoded, length, "event-server", 12) == NULL);
  free(encoded);
  free_events(decoded, count);

  /* POI fields: a typed tree holds an ordered object; the wire holds {key, value} rows. */
  qa_save_value *poi_typed = parse("{\"kind\":\"q2\",\"sequence\":17,\"content\":\"q3:classic:baseq3:test\",\"seconds\":1.5,"
      "\"sourceEntity\":null,\"event\":{\"kind\":\"poi\",\"origin\":{\"x\":1,\"y\":2,\"z\":3},\"message\":\"target\","
      "\"fields\":{\"image\":\"i_help\"}}}");
  encoded = qa_app_unified_encode_presentation_events(&poi_typed, 1, &length);
  CHECK(memmem(encoded, length, "\"fields\":[{\"key\":\"image\",\"value\":\"i_help\"}]", 43) != NULL);
  free(encoded);
  qa_save_value_free(poi_typed);

  /* Simulation events: resource rebinding, client remap. */
  rule = (map_rule){9, 2, "resource:local-sound", 3, 7};
  qa_save_value *wire = parse(ts_test_sound);
  qa_save_value *sound = qa_app_unified_read_simulation_event(wire, &identity);
  CHECK(path(sound, "audience", "client", "slot")->number == 21);
  CHECK(strcmp(path(sound, "payload", "resource", NULL)->text, "resource:local-sound") == 0);
  CHECK(path(sound, "payload", "actor", "slot")->number == 12);
  qa_save_value_free(sound);
  qa_save_value_free(wire);

  /* Damage: armor mutations survive unchanged and the target is remapped. */
  wire = parse(ts_test_damage);
  qa_save_value *damage = qa_app_unified_read_simulation_event(wire, &identity);
  const qa_save_value *decision = path(damage, "payload", "outcome", "decision");
  qa_save_value *mutations = parse("[{\"kind\":\"armor\",\"before\":{\"regular\":{\"kind\":\"q1\",\"points\":75,\"absorption\":0.6,"
      "\"item\":\"q1:armor/yellow\"},\"powered\":{\"kind\":\"screen\",\"cells\":20}},\"after\":{\"regular\":{\"kind\":\"q1\","
      "\"points\":75,\"absorption\":0.6,\"item\":\"q1:armor/yellow\"},\"powered\":{\"kind\":\"screen\",\"cells\":15}}}]");
  CHECK(same(qa_save_value_get(decision, "mutations"), mutations));
  qa_save_value_free(mutations);
  CHECK(path(decision, "request", "target", "slot")->number == 12);
  /* Writing the remapped tree back with a pass-through identity reproduces the TS bytes. */
  rule = (map_rule){0, 0, "resource:local-sound", 3, 7};
  qa_save_value *passed = qa_app_unified_read_simulation_event(wire, &identity);
  qa_save_value *written = qa_app_unified_write_simulation_event(passed);
  CHECK(same(written, wire));
  qa_save_value_free(written);
  qa_save_value_free(passed);
  qa_save_value_free(damage);
  qa_save_value_free(wire);

  /* toThrow cases. */
  rule = (map_rule){10, 1, "resource:local-sound", -1, -1};
  char *message = abort_message(decode_child, "[{\"kind\":\"unknown\"}]");
  CHECK(message && strstr(message, "unified.presentation-events[0]: unknown event variant"));
  message = abort_message(read_sim_child, "{\"sequence\":1,\"time\":{\"kind\":\"seconds\",\"value\":0},\"audience\":{\"kind\":\"world\"},"
      "\"payload\":{\"kind\":\"sound\",\"resource\":\"resource:/private/server/pak0\"}}");
  CHECK(message && strstr(message, "expected negotiated unified resource identity"));
  message = abort_message(write_sim_child, "{\"sequence\":1,\"time\":{\"kind\":\"seconds\",\"value\":0},\"audience\":{\"kind\":\"world\"},"
      "\"payload\":{\"kind\":\"sound\",\"resource\":\"resource:/private/server/pak0\"}}");
  CHECK(message && strstr(message, "Unified events require negotiated resource identities"));
}

static void test_ts_player_event(void) {
  rule = (map_rule){37, -5, "resource:local", 3, 7}; /* TS maps (3,7) to (40,2). */
  size_t count = 0;
  qa_save_value **events = roundtrip(ts_test_player_event, NULL, &count);
  CHECK(count == 1);
  const qa_save_value *event = qa_save_value_get(events[0], "event");
  CHECK(path(event, "actor", "slot", NULL)->number == 40 && path(event, "actor", "generation", NULL)->number == 2);
  CHECK(strcmp(path(event, "source", "abiProfile", NULL)->text, "q3-1.16n-base") == 0);
  CHECK(path(event, "sequence", "time", NULL)->number == -1);
  CHECK(qa_save_value_get(event, "event")->number == 834);
  const qa_save_value *state = qa_save_value_get(event, "playerState");
  CHECK(state && state->kind == QA_SAVE_OBJECT);
  CHECK(qa_save_value_get(state, "persistent")->item_count == 16);
  /* The record travels whole: source slots and legacy enums are untranslated. */
  size_t length = 0;
  uint8_t *encoded = qa_app_unified_encode_presentation_events(events, count, &length);
  CHECK(memmem(encoded, length, "event-source", 12) == NULL);
  free(encoded);
  free_events(events, count);
  /* Pass-through identity: bytes equal TS encode(decode(bytes)); module keys take readModule's order. */
  rule = (map_rule){0, 0, "resource:local", 3, 7};
  events = roundtrip(ts_test_player_event, ts_test_player_event_reencoded, &count);
  free_events(events, count);
}

static void test_corpus(void) {
  rule = (map_rule){10, 1, "resource:unified:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", -1, -1};
  for (size_t i = 0; i < sizeof valid_rows / sizeof *valid_rows; i++) {
    const corpus_row *row = &valid_rows[i];
    if (row->presentation) {
      size_t needed = strlen(row->wire) + 3;
      char *list = malloc(needed);
      snprintf(list, needed, "[%s]", row->wire);
      size_t count = 0;
      qa_save_value **events = roundtrip(list, row->expected, &count);
      free_events(events, count);
      free(list);
    } else {
      qa_save_value *wire = parse(row->wire), *expected = parse(row->expected);
      qa_save_value *read = qa_app_unified_read_simulation_event(wire, &identity);
      qa_save_value *written = qa_app_unified_write_simulation_event(read);
      CHECK(same(written, expected));
      qa_save_value_free(written);
      qa_save_value_free(read);
      qa_save_value_free(expected);
      qa_save_value_free(wire);
    }
  }
  for (size_t i = 0; i < sizeof error_rows / sizeof *error_rows; i++) {
    const corpus_row *row = &error_rows[i];
    char *message;
    if (row->presentation) {
      size_t needed = strlen(row->wire) + 3;
      char *list = malloc(needed);
      snprintf(list, needed, "[%s]", row->wire);
      message = abort_message(decode_child, list);
      free(list);
    } else {
      message = abort_message(read_sim_child, row->wire);
    }
    CHECK(message && strstr(message, row->expected));
    if (!message || !strstr(message, row->expected))
      fprintf(stderr, "row %zu: expected [%s] got [%s]\n", i, row->expected, message ? message : "(no abort)");
  }
}

static void test_bounds_and_text(void) {
  rule = (map_rule){0, 0, "resource:local", -1, -1};
  /* Fatal UTF-8 and JSON.parse failures throw. */
  CHECK(abort_message(decode_child, "[\"\xC0\x80\"]") != NULL);
  CHECK(abort_message(decode_child, "[\"\xED\xA0\x80\"]") != NULL);
  CHECK(abort_message(decode_child, "[\"\xE2\x82\"]") != NULL);
  CHECK(abort_message(decode_child, "{not json") != NULL);
  /* A leading byte order mark is dropped by TextDecoder. */
  size_t count = 99;
  qa_save_value **events = qa_app_unified_decode_presentation_events(
      (const uint8_t *)"\xEF\xBB\xBF[]", 5, &identity, &count);
  CHECK(count == 0);
  free(events);
  /* JSON.parse: the last duplicate key decides. */
  const char *duplicate = "[{\"kind\":\"unknown\",\"kind\":\"music\",\"event\":{\"kind\":\"pause\",\"paused\":false},"
      "\"sequence\":1,\"content\":\"q1:a:b:c\",\"seconds\":0}]";
  events = qa_app_unified_decode_presentation_events((const uint8_t *)duplicate, strlen(duplicate), &identity, &count);
  CHECK(count == 1 && strcmp(qa_save_value_get(events[0], "kind")->text, "music") == 0);
  free_events(events, count);
  /* Number::toString layout. */
  qa_save_value *numbers = parse("{\"kind\":\"music\",\"event\":{\"kind\":\"cd-track\",\"track\":0.7},\"sequence\":1e21,"
      "\"content\":\"q1:a:b:c\",\"seconds\":1.5e-7,\"sourceEntity\":123456789012345680000}");
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_presentation_events(&numbers, 1, &length);
  const char *expect = "[{\"kind\":\"music\",\"event\":{\"kind\":\"cd-track\",\"track\":0.7},\"sequence\":1e+21,"
      "\"content\":\"q1:a:b:c\",\"seconds\":1.5e-7,\"sourceEntity\":123456789012345680000}]";
  CHECK(length == strlen(expect) && memcmp(bytes, expect, length) == 0);
  free(bytes);
  qa_save_value_free(numbers);
  /* Oversized input is rejected before decoding. */
  size_t big = 16 * 1024 * 1024 + 1;
  uint8_t *huge = calloc(big + 1, 1);
  memset(huge, ' ', big);
  CHECK(abort_message(decode_child, huge) && 1);
  free(huge);
}

int main(void) {
  test_ts_unified_events();
  test_ts_player_event();
  test_corpus();
  test_bounds_and_text();
  printf("unified_events_depth: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
