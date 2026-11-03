/* unified_event_values.h against the TS codec corpus (unified_events_corpus.h: every switch branch of the TS
 * readers, decoded and re-encoded by bun). For every presentation row: the C codec decodes the wire into the TS event
 * object, the object becomes the typed C SimulationPresentationEvent and back, and the codec encodes that; the bytes
 * must equal the TS re-encoding. So the typed C payload structs carry every field the TS codec reads and writes.
 * The one C representation gap is SimulationPresentationEvent.sourceEntity: TS `?: number | null`, C has_source_entity
 * (false is null or undefined); emitOwned always sets it, so a typed event writes null, and a corpus row without it
 * compares with that member dropped. Three more C payload fields hold a narrower domain than the TS codec accepts, and
 * the expected tree is narrowed the same way before the comparison (so any other difference still fails):
 *  - Q1ClientSnapshot slot/shirt/pants and CtfStatus flags/runeItems are int64_t (TS numbers, integers from every
 *    producer; the corpus feeds fractions): truncated;
 *  - Q2PresentationEvent "model" attachedModels is `const char *[3]` (TS string[]; every producer sends
 *    [model2, model3, model4]): padded with "" / cut to three.
 * Also: a typed event built in C becomes the tree the server filters
 * (qa_app_sim_unified_presentation_for sees its actor identities), and simulation-event views of the corpus's sound
 * and message rows. GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/app/bootstrap/network/unified_event_codec.h"
#include "qa/app/bootstrap/network/unified_event_values.h"
#include "qa/app/bootstrap/simulation/network_unified.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "unified_events_corpus.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int ctx_sentinel;
static const char token_sentinel[] = "session";
static qa_actor_id map_actor(void *ctx, double slot, double generation) {
  CHECK(ctx == &ctx_sentinel);
  qa_actor_id id = {0};
  id.session.token = token_sentinel;
  id.slot = (int64_t)(slot + 10);
  id.generation = (int64_t)(generation + 1);
  id.token = token_sentinel;
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
  return "resource:unified:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
}
static const qa_app_unified_identity_decoder identity = {
  .ctx = &ctx_sentinel, .actor = map_actor, .client = map_client, .seat = map_seat, .resource_id = map_resource};

static qa_save_value *parse(const char *text, size_t length) {
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, length, false);
  if (!value) abort();
  return value;
}
/* Deep equality including object key order and exact numbers. */
static bool same(const qa_save_value *a, const qa_save_value *b) {
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case QA_SAVE_BOOLEAN: return a->boolean == b->boolean;
    case QA_SAVE_NUMBER: return a->number == b->number || (a->number != a->number && b->number != b->number);
    case QA_SAVE_STRING: return strcmp(a->text, b->text) == 0;
    case QA_SAVE_ARRAY:
      if (a->item_count != b->item_count) return false;
      for (size_t i = 0; i < a->item_count; i++) if (!same(a->items[i], b->items[i])) return false;
      return true;
    case QA_SAVE_OBJECT:
      if (a->entry_count != b->entry_count) return false;
      for (size_t i = 0; i < a->entry_count; i++)
        if (strcmp(a->entries[i].key, b->entries[i].key) != 0 || !same(a->entries[i].value, b->entries[i].value)) return false;
      return true;
    default: return true;
  }
}
static qa_save_value *member(qa_save_value *object, const char *key) { return (qa_save_value *)qa_save_value_get(object, key); }
static void truncate_number(qa_save_value *object, const char *key) {
  qa_save_value *value = member(object, key);
  if (value != NULL && value->kind == QA_SAVE_NUMBER) value->number = trunc(value->number);
}
/* The C domains of the file comment, applied to an expected event tree. */
static void narrow_expected(qa_save_value *event) {
  const qa_save_value *kind = qa_save_value_get(event, "kind");
  qa_save_value *payload = member(event, "event");
  const qa_save_value *payload_kind = payload != NULL ? qa_save_value_get(payload, "kind") : NULL;
  if (kind == NULL || payload_kind == NULL || payload_kind->kind != QA_SAVE_STRING) return;
  if (strcmp(kind->text, "q1-composition") == 0 && strcmp(payload_kind->text, "client") == 0) {
    qa_save_value *client = member(payload, "client");
    truncate_number(client, "slot"); truncate_number(client, "shirt"); truncate_number(client, "pants");
  } else if (strcmp(kind->text, "q1-composition") == 0 && strcmp(payload_kind->text, "ctf-status") == 0) {
    qa_save_value *status = member(payload, "status");
    truncate_number(status, "flags"); truncate_number(status, "runeItems");
  } else if (strcmp(kind->text, "q2") == 0 && strcmp(payload_kind->text, "model") == 0) {
    qa_save_value *models = member(payload, "attachedModels");
    while (models->item_count > 3) qa_save_value_free(models->items[--models->item_count]);
    while (models->item_count < 3) if (!qa_save_value_push(models, qa_save_value_string(""))) abort();
  }
}
/* Drops a top-level `sourceEntity: null` from event (see the file comment). */
static void drop_null_source_entity(qa_save_value *event) {
  for (size_t i = 0; i < event->entry_count; i++)
    if (strcmp(event->entries[i].key, "sourceEntity") == 0 && event->entries[i].value->kind == QA_SAVE_NULL) {
      qa_save_value_free(event->entries[i].value);
      free(event->entries[i].key);
      memmove(&event->entries[i], &event->entries[i + 1], (event->entry_count - i - 1) * sizeof *event->entries);
      event->entry_count--;
      return;
    }
}

static size_t typed_rows;
static void presentation_row(const corpus_row *row, size_t index) {
  size_t needed = strlen(row->wire) + 3;
  char *list = malloc(needed);
  snprintf(list, needed, "[%s]", row->wire);
  size_t count = 0;
  qa_save_value **events = qa_app_unified_decode_presentation_events((const uint8_t *)list, strlen(list), &identity, &count);
  free(list);
  CHECK(count == 1);
  qa_app_sim_presentation_event typed;
  qa_app_unified_presentation_event_from_tree(events[0], &typed);
  CHECK(typed.storage != NULL);
  /* A deep copy of the typed event (what PresentationState retains) gives the same tree. */
  qa_app_sim_presentation_event copy;
  CHECK(qa_app_sim_presentation_event_copy(&copy, &typed));
  qa_save_value *tree = qa_app_unified_presentation_event_tree(&copy);
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_presentation_events(&tree, 1, &length);
  qa_save_value *got = parse((const char *)bytes, length), *expected = parse(row->expected, strlen(row->expected));
  if (qa_save_value_get(expected->items[0], "sourceEntity") == NULL) drop_null_source_entity(got->items[0]);
  narrow_expected(expected->items[0]);
  bool equal = same(got, expected);
  CHECK(equal);
  if (!equal) fprintf(stderr, "row %zu\n got %.*s\n exp %s\n", index, (int)length, bytes, row->expected);
  /* Identities survive: every actor node of the rebuilt tree carries the decoder's identity. */
  qa_actor_id recipient;
  const qa_save_value *node = qa_save_value_get(tree, "recipient");
  if (node != NULL) CHECK(qa_app_unified_event_actor_id(node, &recipient) && recipient.token == token_sentinel);
  typed_rows++;
  free(bytes);
  qa_save_value_free(got);
  qa_save_value_free(expected);
  qa_save_value_free(tree);
  qa_app_sim_presentation_event_release(&copy);
  qa_app_sim_presentation_event_release(&typed);
  CHECK(typed.storage == NULL);
  qa_save_value_free(events[0]);
  free(events);
}
static size_t simulation_rows;
static void simulation_row(const corpus_row *row) {
  qa_save_value *wire = parse(row->wire, strlen(row->wire));
  qa_save_value *read = qa_app_unified_read_simulation_event(wire, &identity);
  const char *kind = qa_save_value_get(qa_save_value_get(read, "payload"), "kind")->text;
  if (strcmp(kind, "sound") == 0 || strcmp(kind, "message") == 0) {
    qa_simulation_event event;
    qa_app_unified_simulation_event_storage storage;
    qa_app_unified_simulation_event_view(read, &event, &storage);
    CHECK(event.sequence == qa_save_value_get(read, "sequence")->number);
    if (strcmp(kind, "sound") == 0) {
      CHECK(event.payload.kind == QA_SIM_EVENT_SOUND);
      CHECK(strcmp(event.payload.resource, qa_save_value_get(qa_save_value_get(read, "payload"), "resource")->text) == 0);
      const qa_save_value *actor = qa_save_value_get(qa_save_value_get(read, "payload"), "actor");
      CHECK((actor->kind == QA_SAVE_NULL) == (event.payload.actor == NULL));
      if (event.payload.actor != NULL) CHECK(event.payload.actor->token == token_sentinel);
    } else CHECK(event.payload.kind == QA_SIM_EVENT_MESSAGE);
    qa_app_unified_simulation_event_storage_release(&storage);
    simulation_rows++;
  }
  qa_save_value_free(read);
  qa_save_value_free(wire);
}

/* A typed event emitted in C (q1 weapon from a producer) becomes the tree the server filters and encodes. */
static void test_typed_event(void) {
  qa_actor_id player = map_actor(&ctx_sentinel, 2, 0), other = map_actor(&ctx_sentinel, 3, 0);
  qa_q1_event weapon;
  memset(&weapon, 0, sizeof weapon);
  weapon.kind = "weapon";
  weapon.has_player = true; weapon.player = player;
  weapon.weapon = "shotgun"; weapon.view_model = "progs/v_shot.mdl"; weapon.frame = 1; weapon.punch = -2;
  weapon.attack.has = true; weapon.attack.kind = "axe"; weapon.attack.variant = 2;
  qa_app_sim_presentation_event event;
  memset(&event, 0, sizeof event);
  event.kind = "q1"; event.q1 = &weapon;
  event.sequence = 5; event.content = "q1:classic:id1:base"; event.seconds = 0.25;
  event.has_source_entity = true; event.source_entity = 2;
  qa_save_value *tree = qa_app_unified_presentation_event_tree(&event);
  qa_client_id client = {0};
  CHECK(qa_app_sim_unified_presentation_for(player, client, tree));
  CHECK(!qa_app_sim_unified_presentation_for(other, client, tree));
  size_t length = 0;
  uint8_t *bytes = qa_app_unified_encode_presentation_events(&tree, 1, &length);
  static const char expected[] = "[{\"kind\":\"q1\",\"event\":{\"kind\":\"weapon\",\"player\":{\"slot\":12,\"generation\":1},"
      "\"weapon\":\"shotgun\",\"viewModel\":\"progs/v_shot.mdl\",\"frame\":1,\"punch\":-2,\"attack\":{\"kind\":\"axe\","
      "\"variant\":2}},\"sequence\":5,\"content\":\"q1:classic:id1:base\",\"seconds\":0.25,\"sourceEntity\":2}]";
  CHECK(length == strlen(expected) && memcmp(bytes, expected, length) == 0);
  if (!(length == strlen(expected) && memcmp(bytes, expected, length) == 0)) fprintf(stderr, " got %.*s\n", (int)length, bytes);
  free(bytes);
  qa_app_sim_presentation_event back;
  qa_app_unified_presentation_event_from_tree(tree, &back);
  CHECK(strcmp(back.kind, "q1") == 0 && strcmp(back.q1->kind, "weapon") == 0 && back.q1->has_player);
  CHECK(qa_same_actor(back.q1->player, player) && back.q1->attack.has && back.q1->attack.variant == 2);
  CHECK(back.has_source_entity && back.source_entity == 2 && !back.has_recipient && !back.has_owner);
  qa_app_sim_presentation_event_release(&back);
  qa_save_value_free(tree);
}

int main(void) {
  size_t presentation_count = 0;
  for (size_t i = 0; i < sizeof valid_rows / sizeof *valid_rows; i++) {
    if (valid_rows[i].presentation) { presentation_count++; presentation_row(&valid_rows[i], i); }
    else simulation_row(&valid_rows[i]);
  }
  test_typed_event();
  printf("typed presentation rows %zu, simulation views %zu\n", typed_rows, simulation_rows);
  CHECK(typed_rows == presentation_count && presentation_count > 200);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
