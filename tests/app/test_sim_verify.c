/* Side-by-side verification of SharedSimulation on real data: constructs
 * the C SharedSimulation the way application.ts does (parse the command,
 * load the application content, build SimulationOptions), admits one player,
 * steps it with the deterministic input script of
 * tests/app/oracles/sim_verify.ts and prints the same state dump. With an
 * expected dump (tests/app/oracles/sim_verify_<case>.txt, written by the TS
 * oracle) it compares line by line.
 *   test_sim_verify                      every case with an expected dump
 *   test_sim_verify <case> [frames...]   print one case's dump
 * The corpus is QTS_TEST_CORPUS (default ~/qfiles). The user content root is
 * SIM_VERIFY_USER_ROOT, else <cwd>/build/agents/sim-verify2/tmp/sim-verify-user-<case>
 * (recreated empty; TMPDIR is deliberately ignored). Its path names the user
 * mounts the checkpoints record, and the expected dumps were written from the
 * repository root with that same path, so run from the repository root. The
 * oracle script uses the same default. Checkpoint lines carry
 * a byte hash and a key-order-insensitive hash ("canonical") of each provider.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <openssl/sha.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/simulation/classic_guest_files.h"
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include "qa/app/bootstrap/simulation/q3/guest_artifact.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/platform/files/writable.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_services_contract.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"

/* ---- output buffer ---- */
typedef struct lines { char **items; size_t count, capacity; } lines;
static void lines_push(lines *list, const char *text) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 256;
    list->items = realloc(list->items, list->capacity * sizeof *list->items);
    if (!list->items) abort();
  }
  list->items[list->count] = strdup(text);
  if (!list->items[list->count]) abort();
  list->count++;
}
static void lines_free(lines *list) {
  for (size_t i = 0; i < list->count; i++) free(list->items[i]);
  free(list->items);
  memset(list, 0, sizeof *list);
}
static int lines_compare(const void *left, const void *right) {
  return strcmp(*(char *const *)left, *(char *const *)right);
}
/* Code-unit order equals byte order for the ASCII keys these lines carry. */
static void lines_sort(lines *list) { if (list->count > 1) qsort(list->items, list->count, sizeof *list->items, lines_compare); }

static lines out, console_lines;
static void emit(const char *format, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void emit(const char *format, ...) {
  char buffer[8192];
  va_list args; va_start(args, format); vsnprintf(buffer, sizeof buffer, format, args); va_end(args);
  lines_push(&out, buffer);
}

#define N(value) QA_JS_NUMBER(value)
static const char *vec(qa_vec3 value, char *buffer, size_t capacity) {
  char x[40], y[40], z[40];
  qa_net_js_number(value.x, x, sizeof x); qa_net_js_number(value.y, y, sizeof y); qa_net_js_number(value.z, z, sizeof z);
  snprintf(buffer, capacity, "%s %s %s", x, y, z);
  return buffer;
}
#define V(value) vec((value), (char[128]){0}, 128)

static const char *armor_text(qa_armor_state value, char *buffer, size_t capacity) {
  static const char *const kinds[] = {"none", "q1", "q2", "q3", "source"};
  char regular[160];
  if (value.regular.kind == QA_REGULAR_ARMOR_NONE) snprintf(regular, sizeof regular, "none");
  else snprintf(regular, sizeof regular, "%s:%s:%s", kinds[value.regular.kind], N(value.regular.points),
      value.regular.kind == QA_REGULAR_ARMOR_Q3 || value.regular.item == NULL ? "-" : value.regular.item);
  char powered[80];
  if (value.powered.kind == NULL || strcmp(value.powered.kind, "none") == 0) snprintf(powered, sizeof powered, "none");
  else snprintf(powered, sizeof powered, "%s:%s", value.powered.kind, N(value.powered.cells));
  snprintf(buffer, capacity, "%s %s", regular, powered);
  return buffer;
}

/* Key-order-insensitive form of a checkpoint value (sim_verify.ts canonical). */
typedef struct text_buffer { char *data; size_t length, capacity; } text_buffer;
static void text_append(text_buffer *b, const char *text, size_t length) {
  if (b->length + length + 1 > b->capacity) {
    b->capacity = (b->length + length + 1) * 2;
    b->data = realloc(b->data, b->capacity);
    if (!b->data) abort();
  }
  memcpy(b->data + b->length, text, length); b->length += length; b->data[b->length] = '\0';
}
static void text_puts(text_buffer *b, const char *text) { text_append(b, text, strlen(text)); }
static void json_string(const char *text, char *buffer, size_t capacity);
static void text_json(text_buffer *b, const char *text) {
  size_t capacity = strlen(text) * 6 + 8;
  char *json = malloc(capacity);
  if (!json) abort();
  json_string(text, json, capacity);
  text_puts(b, json); free(json);
}
static int entry_compare(const void *left, const void *right) {
  return strcmp((*(const qa_save_entry *const *)left)->key, (*(const qa_save_entry *const *)right)->key);
}
static void canonical(text_buffer *b, const qa_save_value *v) {
  switch (v->kind) {
    case QA_SAVE_UNDEFINED: case QA_SAVE_NULL: text_puts(b, "null"); break;
    case QA_SAVE_BOOLEAN: text_puts(b, v->boolean ? "true" : "false"); break;
    case QA_SAVE_NUMBER: text_puts(b, N(v->number)); break;
    case QA_SAVE_BIGINT: text_puts(b, "n"); text_puts(b, v->text); break;
    case QA_SAVE_STRING: text_json(b, v->text); break;
    case QA_SAVE_BYTES: {
      text_puts(b, "b");
      for (size_t i = 0; i < v->byte_length; i++) { char hex[3]; snprintf(hex, sizeof hex, "%02x", v->bytes[i]); text_append(b, hex, 2); }
      break;
    }
    case QA_SAVE_ARRAY:
      text_puts(b, "[");
      for (size_t i = 0; i < v->item_count; i++) { if (i) text_puts(b, ","); canonical(b, v->items[i]); }
      text_puts(b, "]");
      break;
    case QA_SAVE_OBJECT: {
      const qa_save_entry **sorted = malloc((v->entry_count ? v->entry_count : 1) * sizeof *sorted);
      if (!sorted) abort();
      for (size_t i = 0; i < v->entry_count; i++) sorted[i] = &v->entries[i];
      if (v->entry_count > 1) qsort(sorted, v->entry_count, sizeof *sorted, entry_compare);
      text_puts(b, "{");
      for (size_t i = 0; i < v->entry_count; i++) {
        if (i) text_puts(b, ",");
        text_json(b, sorted[i]->key); text_puts(b, ":"); canonical(b, sorted[i]->value);
      }
      text_puts(b, "}");
      free(sorted);
      break;
    }
  }
}
static void sha_hex(const uint8_t *bytes, size_t length, char out_hex[17]);
static void canonical_hex(const uint8_t *bytes, size_t length, char out_hex[17]) {
  qa_save_value *value = qa_decode_checkpoint_value(bytes, length);
  if (!value) abort();
  text_buffer b; memset(&b, 0, sizeof b);
  canonical(&b, value);
  sha_hex((const uint8_t *)b.data, b.length, out_hex);
  free(b.data); qa_save_value_free(value);
}
static void sha_hex(const uint8_t *bytes, size_t length, char out_hex[17]) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  SHA256(bytes, length, digest);
  for (int i = 0; i < 8; i++) snprintf(out_hex + i * 2, 3, "%02x", digest[i]);
}

/* ---- cases ---- */
typedef struct verify_case { const char *name; const char *argv[16]; size_t argc; } verify_case;
static const verify_case cases[] = {
  {"q1", {"--game", "q1-classic-id1", "--map", "e1m1", "--movement", "q1", "--character", "q1"}, 8},
  {"q2c", {"--game", "q2-classic-baseq2", "--map", "base1", "--movement", "q2", "--character", "q2"}, 8},
  {"q2r", {"--game", "q2-rerelease-baseq2", "--map", "base1", "--movement", "q2", "--character", "q2"}, 8},
  {"q3", {"--game", "q3-baseq3", "--map", "q3dm1", "--movement", "q3", "--character", "q3", "--mode", "deathmatch"}, 10},
  {"qc", {"--game", "q1-classic-id1", "--map", "e1m1", "--progs", "progs.dat", "--movement", "q1", "--character", "q1"}, 10},
  {"q2nc", {"--game", "q2-classic-baseq2", "--map", "base1", "--q2-game", "gamex86.dll", "--movement", "q2", "--character", "q2"}, 10},
  {"q2nr", {"--game", "q2-rerelease-baseq2", "--map", "base1", "--q2-game", "game_x64.dll", "--movement", "q2", "--character", "q2"}, 10},
  {"q3qvm", {"--game", "q3-baseq3", "--map", "q3dm1", "--movement", "q3", "--character", "q3", "--mode", "deathmatch"}, 10},
  {"mixed", {"--game", "q2-classic-baseq2", "--map", "base1"}, 4},
  {"preset", {"--preset", "q2-q1-q3"}, 2},
};

static const char *event_network_kind(qa_network_event_kind kind) {
  static const char *const names[] = {"print", "center-print", "command-text", "config-string", "sound", "q1-damage",
      "q1-particle", "q2-layout", "q2-inventory", "q2-muzzle-flash", "q3-server-command", "disconnect"};
  return (size_t)kind < sizeof names / sizeof names[0] ? names[kind] : "?";
}
typedef struct event_count { char *key; size_t count; } event_count;
static event_count *event_counts; static size_t event_count_size, event_count_capacity;
static void count_event(const char *key) {
  for (size_t i = 0; i < event_count_size; i++) if (strcmp(event_counts[i].key, key) == 0) { event_counts[i].count++; return; }
  if (event_count_size == event_count_capacity) {
    event_count_capacity = event_count_capacity ? event_count_capacity * 2 : 32;
    event_counts = realloc(event_counts, event_count_capacity * sizeof *event_counts);
    if (!event_counts) abort();
  }
  event_counts[event_count_size].key = strdup(key);
  event_counts[event_count_size].count = 1;
  event_count_size++;
}
static event_count *presentation_counts; static size_t presentation_count_size, presentation_count_capacity;
static void count_presentation(const char *key) {
  for (size_t i = 0; i < presentation_count_size; i++) if (strcmp(presentation_counts[i].key, key) == 0) { presentation_counts[i].count++; return; }
  if (presentation_count_size == presentation_count_capacity) {
    presentation_count_capacity = presentation_count_capacity ? presentation_count_capacity * 2 : 32;
    presentation_counts = realloc(presentation_counts, presentation_count_capacity * sizeof *presentation_counts);
    if (!presentation_counts) abort();
  }
  presentation_counts[presentation_count_size].key = strdup(key);
  presentation_counts[presentation_count_size].count = 1;
  presentation_count_size++;
}
/* SIM_VERIFY_EVENTS=<frame>: list that frame's presentation events on stderr (debugging aid). */
static double trace_frame = -1;
static void drain_presentation(qa_app_shared_simulation *sim) {
  size_t count = 0;
  const qa_app_sim_presentation_event *const *events = qa_app_shared_sim_drain_presentation_events(sim, &count);
  for (size_t i = 0; i < count; i++) {
    count_presentation(events[i]->kind);
    const char *trace = getenv("SIM_VERIFY_EVENTS");
    if (trace != NULL && atof(trace) == trace_frame) {
      const char *sub = strcmp(events[i]->kind, "q2") == 0 && events[i]->q2 != NULL ? events[i]->q2->kind : "";
      fprintf(stderr, "event %s %s %s\n", events[i]->kind, sub,
              strcmp(events[i]->kind, "q2") == 0 && events[i]->q2 != NULL && events[i]->q2->path != NULL ? events[i]->q2->path : "");
    }
  }
}
static int event_compare(const void *left, const void *right) {
  return strcmp(((const event_count *)left)->key, ((const event_count *)right)->key);
}
static void record(qa_simulation_output output) {
  for (size_t i = 0; i < output.event_count; i++) {
    const qa_simulation_event_payload *payload = &output.events[i].payload;
    char key[512];
    switch (payload->kind) {
      case QA_SIM_EVENT_MESSAGE:
        snprintf(key, sizeof key, "message:%s", event_network_kind(payload->event.kind));
        if (payload->event.text != NULL && (payload->event.kind == QA_NETWORK_EVENT_PRINT || payload->event.kind == QA_NETWORK_EVENT_CENTER_PRINT
            || payload->event.kind == QA_NETWORK_EVENT_COMMAND_TEXT || payload->event.kind == QA_NETWORK_EVENT_Q3_SERVER_COMMAND)) {
          char line[4096]; snprintf(line, sizeof line, "[message] %s", payload->event.text);
          lines_push(&console_lines, line);
        }
        break;
      case QA_SIM_EVENT_SOUND: snprintf(key, sizeof key, "sound:%s", payload->resource); break;
      case QA_SIM_EVENT_DAMAGE: snprintf(key, sizeof key, "damage"); break;
      default: snprintf(key, sizeof key, "transition"); break;
    }
    count_event(key);
  }
}

/* JSON.stringify of a string (the console text). */
static void json_string(const char *text, char *buffer, size_t capacity) {
  size_t at = 0;
  if (capacity < 8) return;
  buffer[at++] = '"';
  for (const unsigned char *c = (const unsigned char *)text; *c && at + 8 < capacity; c++) {
    switch (*c) {
      case '"': buffer[at++] = '\\'; buffer[at++] = '"'; break;
      case '\\': buffer[at++] = '\\'; buffer[at++] = '\\'; break;
      case '\n': buffer[at++] = '\\'; buffer[at++] = 'n'; break;
      case '\r': buffer[at++] = '\\'; buffer[at++] = 'r'; break;
      case '\t': buffer[at++] = '\\'; buffer[at++] = 't'; break;
      case '\b': buffer[at++] = '\\'; buffer[at++] = 'b'; break;
      case '\f': buffer[at++] = '\\'; buffer[at++] = 'f'; break;
      default:
        if (*c < 0x20) at += (size_t)snprintf(buffer + at, capacity - at, "\\u%04x", *c);
        else buffer[at++] = (char)*c;
    }
  }
  buffer[at++] = '"'; buffer[at] = '\0';
}

static const char *phase_name(qa_frame_phase phase) {
  static const char *const names[] = {"frame-entry", "client-command", "entity-prethink", "entity-physics", "entity-think", "client-end-frame", "frame-exit"};
  return names[phase];
}
static const char *time_kind(qa_source_time_kind kind) { return kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds"; }

static int body_compare(const void *left, const void *right) {
  int64_t a = ((const qa_body_snapshot *)left)->actor.slot, b = ((const qa_body_snapshot *)right)->actor.slot;
  return a < b ? -1 : a > b;
}
static int inventory_compare(const void *left, const void *right) {
  int64_t a = ((const qa_inventory_snapshot *)left)->actor.slot, b = ((const qa_inventory_snapshot *)right)->actor.slot;
  return a < b ? -1 : a > b;
}

typedef struct view_call { qa_app_shared_simulation *sim; qa_actor_id actor; qa_app_sim_player_view view; } view_call;
static void view_body(void *ctx) {
  view_call *call = ctx;
  call->view = qa_app_shared_sim_player_view(call->sim, call->actor);
}
typedef struct checkpoint_call { qa_app_shared_simulation *sim; qa_app_shared_sim_save save; } checkpoint_call;
static void checkpoint_body(void *ctx) {
  checkpoint_call *call = ctx;
  qa_app_shared_sim_checkpoint(call->sim, &call->save);
}
static void dump(qa_app_shared_simulation *sim, double frame, qa_simulation_output output, qa_actor_id player) {
  emit("== frame %s", N(frame));
  emit("source %s", qa_app_shared_sim_source_kind(sim));
  emit("time %s", N(qa_app_shared_sim_time_seconds(sim)));
  const qa_world_snapshot *snapshot = &output.snapshot;
  emit("frame-context %s %s:%s %s:%s %s", N(snapshot->frame.frame), time_kind(snapshot->frame.time.kind), N(snapshot->frame.time.value),
      time_kind(snapshot->frame.elapsed.kind), N(snapshot->frame.elapsed.value), phase_name(snapshot->frame.phase));
  emit("actors %zu", snapshot->actor_count);
  lines kinds = {0};
  for (size_t i = 0; i < snapshot->actor_count; i++) {
    char key[512]; snprintf(key, sizeof key, "%s %s", snapshot->actors[i].owner, snapshot->actors[i].definition);
    lines_push(&kinds, key);
  }
  lines_sort(&kinds);
  for (size_t i = 0; i < kinds.count;) {
    size_t j = i; while (j < kinds.count && strcmp(kinds.items[j], kinds.items[i]) == 0) j++;
    emit("actor-kind %s %zu", kinds.items[i], j - i);
    i = j;
  }
  lines_free(&kinds);
  lines actor_lines = {0};
  for (size_t i = 0; i < snapshot->actor_count; i++) {
    const qa_body_snapshot *body = NULL;
    for (size_t b = 0; b < snapshot->body_count; b++) if (snapshot->bodies[b].actor.slot == snapshot->actors[i].id.slot) body = &snapshot->bodies[b];
    char line[512]; snprintf(line, sizeof line, "actor %s %s", snapshot->actors[i].definition, body == NULL ? "-" : V(body->body.origin));
    lines_push(&actor_lines, line);
  }
  lines_sort(&actor_lines);
  for (size_t i = 0; i < actor_lines.count; i++) lines_push(&out, actor_lines.items[i]);
  lines_free(&actor_lines);
  emit("bodies %zu", snapshot->body_count);
  qa_body_snapshot *bodies = malloc((snapshot->body_count ? snapshot->body_count : 1) * sizeof *bodies);
  if (!bodies) abort();
  memcpy(bodies, snapshot->bodies, snapshot->body_count * sizeof *bodies);
  if (snapshot->body_count > 1) qsort(bodies, snapshot->body_count, sizeof *bodies, body_compare);
  for (size_t i = 0; i < snapshot->body_count; i++) {
    char ground[32];
    if (bodies[i].body.ground == NULL) snprintf(ground, sizeof ground, "-");
    else snprintf(ground, sizeof ground, "%lld", (long long)bodies[i].body.ground->slot);
    emit("body %lld:%lld o %s v %s a %s g %s", (long long)bodies[i].actor.slot, (long long)bodies[i].actor.generation,
        V(bodies[i].body.origin), V(bodies[i].body.velocity), V(bodies[i].body.angles), ground);
  }
  free(bodies);
  qa_inventory_snapshot *inventories = malloc((snapshot->inventory_count ? snapshot->inventory_count : 1) * sizeof *inventories);
  if (!inventories) abort();
  memcpy(inventories, snapshot->inventories, snapshot->inventory_count * sizeof *inventories);
  if (snapshot->inventory_count > 1) qsort(inventories, snapshot->inventory_count, sizeof *inventories, inventory_compare);
  for (size_t i = 0; i < snapshot->inventory_count; i++) {
    char line[8192]; size_t at = (size_t)snprintf(line, sizeof line, "inventory %lld ", (long long)inventories[i].actor.slot);
    for (size_t e = 0; e < inventories[i].entry_count && at < sizeof line; e++)
      at += (size_t)snprintf(line + at, sizeof line - at, "%s%s=%s/%s", e == 0 ? "" : " ", inventories[i].entries[e].item,
          N(inventories[i].entries[e].count), N(inventories[i].entries[e].capacity));
    lines_push(&out, line);
  }
  free(inventories);
  for (size_t i = 0; i < snapshot->actor_count; i++) {
    qa_combat_state_result state = qa_gameplay_authority_read(sim->combat, snapshot->actors[i].id);
    if (state.has_value) emit("combat %lld %s %s %s", (long long)snapshot->actors[i].id.slot, N(state.value.health),
        armor_text(state.value.armor, (char[256]){0}, 256), state.value.team ? state.value.team : "-");
  }
  qa_body_state_result body = qa_shared_body_table_read(qa_app_shared_sim_bodies(sim), player);
  if (!body.has_value) emit("player body -");
  else emit("player body o %s v %s a %s min %s max %s", V(body.value.origin), V(body.value.velocity), V(body.value.angles),
      V(body.value.bounds.min), V(body.value.bounds.max));
  view_call view_request; memset(&view_request, 0, sizeof view_request);
  view_request.sim = sim; view_request.actor = player;
  qa_caught_error view_error; memset(&view_error, 0, sizeof view_error);
  if (!qa_try(view_body, &view_request, &view_error)) emit("player view error %s", view_error.message);
  else emit("player view o %s a %s h %s", V(view_request.view.origin), V(view_request.view.angles), N(view_request.view.view_height));
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, player);
  if (!combat.has_value) emit("player combat -");
  else emit("player combat %s %s", N(combat.value.health), armor_text(combat.value.armor, (char[256]){0}, 256));
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, player, &entry_count);
  {
    char line[8192]; size_t at = (size_t)snprintf(line, sizeof line, "player inventory ");
    for (size_t e = 0; e < entry_count && at < sizeof line; e++)
      at += (size_t)snprintf(line + at, sizeof line - at, "%s%s=%s", e == 0 ? "" : " ", entries[e].item, N(entries[e].count));
    lines_push(&out, line);
  }
  if (event_count_size > 1) qsort(event_counts, event_count_size, sizeof *event_counts, event_compare);
  for (size_t i = 0; i < event_count_size; i++) emit("event %s %zu", event_counts[i].key, event_counts[i].count);
  if (presentation_count_size > 1) qsort(presentation_counts, presentation_count_size, sizeof *presentation_counts, event_compare);
  for (size_t i = 0; i < presentation_count_size; i++) emit("presentation %s %zu", presentation_counts[i].key, presentation_counts[i].count);
  for (size_t i = 0; i < console_lines.count; i++) {
    static char json[65536];
    json_string(console_lines.items[i], json, sizeof json);
    emit("console %s", json);
  }
  lines_free(&console_lines);
  checkpoint_call call; memset(&call, 0, sizeof call); call.sim = sim;
  qa_caught_error caught; memset(&caught, 0, sizeof caught);
  if (!qa_try(checkpoint_body, &call, &caught)) { emit("checkpoint-error %s", caught.message); return; }
  qa_app_shared_sim_save save = call.save;
  for (size_t i = 0; i < save.image.provider_count; i++) {
    const qa_provider_checkpoint *provider = &save.image.providers[i];
    char hex[17], canon[17]; sha_hex(provider->bytes.bytes, provider->bytes.length, hex);
    canonical_hex(provider->bytes.bytes, provider->bytes.length, canon);
    emit("checkpoint %s %s %s %s canonical %s", provider->provider, provider->schema, N(provider->version), hex, canon);
    const char *dump_directory = getenv("SIM_VERIFY_DUMP_DIR");
    if (dump_directory != NULL && dump_directory[0] != '\0') {
      char name[256]; size_t at = 0;
      for (const char *c = provider->schema; *c && at + 1 < sizeof name; c++)
        name[at++] = ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-') ? *c : '_';
      name[at] = '\0';
      char path[1024]; snprintf(path, sizeof path, "%s/%s-%zu-%s.json", dump_directory, N(frame), i, name);
      FILE *file = fopen(path, "wb");
      if (file != NULL) { fwrite(provider->bytes.bytes, 1, provider->bytes.length, file); fclose(file); }
    }
  }
  uint8_t *image_bytes = NULL; size_t image_length = 0;
  if (!qa_encode_save_image(&save.image, &image_bytes, &image_length)) abort();
  const char *image_directory = getenv("SIM_VERIFY_DUMP_DIR");
  if (image_directory != NULL && image_directory[0] != '\0') {
    char path[1024]; snprintf(path, sizeof path, "%s/%s-image.bin", image_directory, N(frame));
    FILE *file = fopen(path, "wb");
    if (file != NULL) { fwrite(image_bytes, 1, image_length, file); fclose(file); }
  }
  char hex[17]; sha_hex(image_bytes, image_length, hex);
  size_t start = 0; while (start < image_length && image_bytes[start] != '{') start++;
  uint32_t header_length = start >= 4 ? (uint32_t)image_bytes[start - 4] | (uint32_t)image_bytes[start - 3] << 8
      | (uint32_t)image_bytes[start - 2] << 16 | (uint32_t)image_bytes[start - 1] << 24 : 0;
  char header_canon[17], payload_canon[17];
  canonical_hex(image_bytes + start, header_length, header_canon);
  canonical_hex(image_bytes + start + header_length, image_length - start - header_length, payload_canon);
  emit("checkpoint-image %s canonical %s %s", hex, header_canon, payload_canon);
  free(image_bytes);
  qa_app_shared_sim_save_free(&save);
}

/* ---- input script (sim_verify.ts command_) ---- */
static qa_user_command script_command(qa_movement_state state, double frame, double time_seconds) {
  long f = (long)frame;
  double forward = f % 40 < 30 ? 200 : -100, side = f % 16 < 8 ? 50 : -50, yaw = (double)((f * 7) % 360);
  double buttons = f % 25 == 24 ? 1 : 0, up = f % 33 == 32 ? 200 : 0;
  double word = (double)(((long)(yaw * 65536 / 360)) & 65535);
  qa_user_command command; memset(&command, 0, sizeof command);
  switch (state.kind) {
    case QA_MOVEMENT_STATE_Q1:
      command.kind = QA_USER_COMMAND_Q1;
      command.as.q1 = (qa_q1_user_command){time_seconds, {0, yaw, 0}, forward, side, up, buttons, 0};
      break;
    case QA_MOVEMENT_STATE_QW:
      command.kind = QA_USER_COMMAND_QW;
      command.as.qw = (qa_qw_user_command){100, {0, yaw, 0}, forward, side, up, buttons, 0};
      break;
    case QA_MOVEMENT_STATE_Q2:
      command.kind = QA_USER_COMMAND_Q2;
      command.as.q2 = (qa_q2_user_command){100, {{0, word, 0}}, forward, side, up, buttons, 0, 0};
      break;
    case QA_MOVEMENT_STATE_Q2_RERELEASE:
      command.kind = QA_USER_COMMAND_Q2_RERELEASE;
      command.as.q2_rerelease = (qa_q2_rerelease_user_command){100, {0, yaw, 0}, forward, side, buttons, frame};
      break;
    case QA_MOVEMENT_STATE_Q3:
      command.kind = QA_USER_COMMAND_Q3;
      command.as.q3 = (qa_q3_user_command){state.as.q3.command_time_milliseconds + 100, {{0, word, 0}}, buttons, 2,
          forward > 0 ? 127 : -127, side > 0 ? 64 : -64, up > 0 ? 127 : 0};
      break;
  }
  return command;
}

/* Guest callbacks of Application.guestOptions on the deterministic clock (sim_verify.ts). */
typedef struct guest_state {
  const char *root;
  double clock_milliseconds;
  qa_app_sim_classic_guest_files *writable, *installed;
  qa_windows_capabilities capabilities;
  qa_app_sim_rerelease_localize localize;
  qa_app_sim_rerelease_clipboard clipboard;
} guest_state;
static guest_state *guest_of(void *ctx) { return ctx; }
static bool guest_now(void *ctx, double *out) { *out = guest_of(ctx)->clock_milliseconds; return true; }
static bool guest_counter(void *ctx, int64_t *out) { *out = (int64_t)(guest_of(ctx)->clock_milliseconds * 1000); return true; }
static qa_windows_file *guest_open(void *ctx, const char *path, bool read, bool write, int creation) {
  guest_state *guest = guest_of(ctx);
  qa_windows_file *file = qa_app_sim_classic_guest_files_open(guest->writable, path, read, write, creation);
  if (file != NULL || write || guest->installed == NULL) return file;
  return qa_app_sim_classic_guest_files_open(guest->installed, path, read, write, creation);
}
static void guest_stdout(void *ctx, qa_windows_stream stream, const uint8_t *bytes, size_t length) {
  (void)guest_of(ctx); (void)stream;
  char *text = malloc(length + 1);
  if (!text) abort();
  memcpy(text, bytes, length); text[length] = '\0';
  lines_push(&console_lines, text);
  free(text);
}
static void guest_print(void *ctx, const char *text) { (void)guest_of(ctx); lines_push(&console_lines, text); }
static void guest_add_command(void *ctx, const char *text) {
  (void)guest_of(ctx);
  char line[4096]; snprintf(line, sizeof line, "[command] %s", text);
  lines_push(&console_lines, line);
}
static void guest_debug_graph(void *ctx, double value, double color) { (void)guest_of(ctx); (void)value; (void)color; }
static char *guest_localize(void *ctx, const char *key, const char *const *arguments, size_t count) {
  (void)guest_of(ctx); (void)arguments; (void)count;
  return strdup(key);
}
static void next_frame(void *ctx) { (void)guest_of(ctx); }
static double guest_milliseconds(void *ctx) { return guest_of(ctx)->clock_milliseconds; }
/* The application's realTime fills the calendar; a fixed one keeps both sides deterministic. */
static double guest_real_time(void *ctx, bool has_output, qa_qvm_calendar *out) {
  (void)guest_of(ctx);
  if (has_output) *out = (qa_qvm_calendar){0, 0, 0, 1, 0, 124, 1, 0, 0};
  return 1704067200;
}
static void guest_command_line(const char *prefix, const char *text) {
  char line[4096]; snprintf(line, sizeof line, "[%s] %s", prefix, text != NULL ? text : "null");
  lines_push(&console_lines, line);
}
static void guest_execute_now(void *ctx, const char *text) { (void)guest_of(ctx); guest_command_line("now", text); }
static void guest_append(void *ctx, const char *text) { (void)guest_of(ctx); guest_command_line("append", text); }
static void guest_insert(void *ctx, const char *text) { (void)guest_of(ctx); guest_command_line("insert", text); }
static void guest_configstring(void *ctx, double index, const char *value) {
  (void)guest_of(ctx);
  char line[8192]; snprintf(line, sizeof line, "[configstring] %s %s", N(index), value);
  lines_push(&console_lines, line);
}
static void guest_server_command(void *ctx, double slot, const char *text) {
  (void)guest_of(ctx);
  char line[8192]; snprintf(line, sizeof line, "[server-command] %s %s", N(slot), text);
  lines_push(&console_lines, line);
}
static void guest_drop(void *ctx, double slot, const char *reason) {
  (void)guest_of(ctx);
  char line[4096]; snprintf(line, sizeof line, "[drop] %s %s", N(slot), reason);
  lines_push(&console_lines, line);
}
static qa_q3_base_user_command qvm_wire(double frame, double milliseconds) {
  long f = (long)frame;
  double yaw = (double)((f * 7) % 360);
  qa_q3_base_user_command command; memset(&command, 0, sizeof command);
  command.server_time = milliseconds;
  command.angles = (qa_vec3){0, (double)(((long)(yaw * 65536 / 360)) & 65535), 0};
  command.forwardmove = f % 40 < 30 ? 127 : -127; command.rightmove = f % 16 < 8 ? 64 : -64;
  command.upmove = f % 33 == 32 ? 127 : 0; command.buttons = f % 25 == 24 ? 1 : 0; command.weapon = 2;
  return command;
}
static bool player_identity(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id) {
  (void)client;
  if (ctx != &out) abort(); /* sentinel */
  *out_seat = 0; *out_social_id = "";
  return true;
}
static bool prompt_supported(void *ctx, qa_client_id client) {
  (void)client;
  if (ctx != &console_lines) abort(); /* sentinel */
  return false;
}

static const char *corpus_root(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root != NULL && root[0] != '\0') return root;
  static char home[512];
  snprintf(home, sizeof home, "%s/qfiles", getenv("HOME") ? getenv("HOME") : "");
  return home;
}

/* The Q3 guest admits through its awaited native lifecycle (Q3ApplicationServerHost
 * connect/begin/think, sim_verify.ts); no bot library is attached. */
static void run_qvm(qa_app_shared_simulation *sim, qa_app_q3_qvm_server_game *qvm, qa_client_id client, guest_state *guest,
    const double *checkpoints, size_t checkpoint_count) {
  qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(qvm));
  qa_cvar_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(cvars, "bot_enable", "0", true, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_app_q3_guest_output output = {guest, guest_drop, guest_server_command, guest_configstring};
  qa_app_q3_qvm_server_game_initialize(qvm, output, "new", NULL, 0);
  qa_app_q3_admission admission = qa_app_q3_qvm_server_game_connect(qvm, client,
      "\\name\\Player 1\\model\\sarge/default\\rate\\25000\\snaps\\20\\ip\\localhost");
  if (!admission.accepted) { fprintf(stderr, "q3qvm rejected: %s\n", admission.reason); abort(); }
  qa_app_q3_player player = admission.player;
  qa_app_q3_qvm_server_game_begin(qvm, player, qvm_wire(0, guest->clock_milliseconds));
  emit("admitted %lld:%lld entity %s", (long long)player.actor.slot, (long long)player.actor.generation, N(player.source_entity));
  qa_simulation_output initial = qa_app_shared_sim_current_output(sim);
  record(initial);
  for (size_t i = 0; i < checkpoint_count; i++) if (checkpoints[i] == 0) { dump(sim, 0, initial, player.actor); break; }
  double last = 0;
  for (size_t i = 0; i < checkpoint_count; i++) if (checkpoints[i] > last) last = checkpoints[i];
  for (double frame = 1; frame <= last; frame++) {
    guest->clock_milliseconds += 100;
    qa_app_q3_qvm_server_game_think(qvm, player, qvm_wire(frame, guest->clock_milliseconds));
    qa_input_batch batch = {100, NULL, 0};
    qa_simulation_output output_frame = qa_app_shared_sim_step_async(sim, batch, NULL, NULL);
    record(output_frame);
    trace_frame = frame;
    drain_presentation(sim);
    for (size_t i = 0; i < checkpoint_count; i++) if (checkpoints[i] == frame) { dump(sim, frame, output_frame, player.actor); break; }
  }
}
static bool run_case(const verify_case *spec, const double *checkpoints, size_t checkpoint_count) {
  char user_template[1024], cwd[600];
  if (getcwd(cwd, sizeof cwd) == NULL) { perror("getcwd"); return false; }
  /* One user content root for both sides (SIM_VERIFY_USER_ROOT): its path names the user mounts the checkpoint records. */
  const char *fixed_root = getenv("SIM_VERIFY_USER_ROOT");
  if (fixed_root != NULL && fixed_root[0] != '\0') snprintf(user_template, sizeof user_template, "%s", fixed_root);
  else snprintf(user_template, sizeof user_template, "%s/build/agents/sim-verify2/tmp/sim-verify-user-%s", cwd, spec->name);
  char clear[2400]; snprintf(clear, sizeof clear, "rm -rf '%s' && mkdir -p \"$(dirname '%s')\"", user_template, user_template);
  if (system(clear) != 0 || mkdir(user_template, 0777) != 0) { perror(user_template); return false; }
  const char *argv[32]; size_t argc = 0;
  argv[argc++] = "--content-root"; argv[argc++] = corpus_root();
  argv[argc++] = "--user-content-root"; argv[argc++] = user_template;
  for (size_t i = 0; i < spec->argc; i++) argv[argc++] = spec->argv[i];
  argv[argc++] = "--dedicated";
  qa_content_root_search search = {corpus_root(), NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, argc, search, NULL);
  if (strcmp(command.kind, "run") != 0) { fprintf(stderr, "expected run\n"); return false; }
  qa_app_content_host host; memset(&host, 0, sizeof host);
  /* The loaded content owns the options it is given (content.c). */
  qa_app_options launch = command.options;
  memset(&command.options, 0, sizeof command.options);
  qa_app_loaded_content *content = qa_app_load_application_content_native(host, launch, NULL, false, NULL, NULL, NULL);
  if (content == NULL) { fprintf(stderr, "%s: content did not load\n", spec->name); qa_app_application_command_destroy(&command); return false; }
  /* q3qvm: the same baseq3 launch with its execution replaced by the shipped
   * qagame.qvm (q3-baseq3 is a built-in product, whose launch selects the
   * TypeScript source), reloaded as a restored recipe. The first content
   * stays open: the restored recipe borrows its strings. */
  qa_app_loaded_content *base_content = NULL;
  qa_resolved_resource_reference qagame; memset(&qagame, 0, sizeof qagame);
  qa_resolved_execution_module qvm_execution; memset(&qvm_execution, 0, sizeof qvm_execution);
  qa_executable_recipe restored; memset(&restored, 0, sizeof restored);
  if (strcmp(spec->name, "q3qvm") == 0) {
    qa_content_opened_resource opened; memset(&opened, 0, sizeof opened);
    if (!qa_content_mounted_open((qa_content_mounted *)qa_app_loaded_content_mounts(content), "vm/qagame.qvm", NULL, NULL, &opened)
        || !qa_resolved_resource_copy(opened.reference, &qagame)) { fprintf(stderr, "vm/qagame.qvm is not mounted\n"); abort(); }
    free(opened.bytes);
    restored = *qa_app_loaded_content_recipe(content);
    qvm_execution.kind = QA_EXECUTION_MODULE_QVM; qvm_execution.owner = restored.map.entities; qvm_execution.role = "server-game";
    qvm_execution.artifact = &qagame; qvm_execution.api.kind = QA_GAME_API_Q3; qvm_execution.api.as.q3 = (qa_q3_api_identity){QA_Q3_API_QAGAME, 8};
    restored.execution = &qvm_execution; restored.execution_count = 1;
    base_content = content;
    qa_app_application_command again = qa_app_parse_application_command(argv, argc, search, NULL);
    qa_app_options relaunch = again.options;
    memset(&again.options, 0, sizeof again.options);
    qa_app_application_command_destroy(&again);
    content = qa_app_load_application_content_native(host, relaunch, &restored, true, NULL, NULL, NULL);
    if (content == NULL) { fprintf(stderr, "q3qvm: content did not load\n"); abort(); }
  }
  qa_identity_owner identity;
  char identity_name[64]; snprintf(identity_name, sizeof identity_name, "sim-verify-%s", spec->name);
  if (!qa_identity_owner_create(&identity, identity_name)) abort();
  qa_app_application_world world = qa_app_loaded_content_world(content);
  qa_app_simulation_options options; memset(&options, 0, sizeof options);
  options.dedicated = true;
  options.identity = identity;
  options.recipe = *qa_app_loaded_content_recipe(content);
  options.world = &world;
  options.mounts = qa_app_loaded_content_mounts(content);
  options.skill = launch.skill; options.mode = launch.mode; options.seed = launch.seed;
  options.max_clients = 1;
  options.has_player_colors = true; options.player_color_top = 0; options.player_color_bottom = 0;
  options.prompt_ctx = &console_lines; options.prompt_supported = prompt_supported;
  options.identity_ctx = &out; options.player_identity = player_identity;
  guest_state guest; memset(&guest, 0, sizeof guest);
  guest.root = user_template;
  options.prepared_quakec = qa_app_loaded_content_prepared_quakec(content);
  const char *q2_edition = NULL;
  const void *q2_prepared = qa_app_loaded_content_prepared_q2_game(content, &q2_edition);
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(content);
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, options.recipe.map.entities.content);
  char directory[2048]; snprintf(directory, sizeof directory, "%.1000s/%.1000s", user_template, product->expectation.content_directory);
  if (q2_prepared != NULL) {
    guest.writable = qa_app_sim_classic_guest_files_create(directory, NULL, 0);
    guest.installed = product->loose_root == NULL ? NULL : qa_app_sim_classic_guest_files_create(product->loose_root, NULL, 0);
    guest.capabilities.ctx = &guest;
    guest.capabilities.now_milliseconds = guest_now; guest.capabilities.performance_counter = guest_counter;
    guest.capabilities.has_performance_frequency = true; guest.capabilities.performance_frequency = 1000000;
    guest.capabilities.open_file = guest_open; guest.capabilities.standard_output = guest_stdout;
    guest.localize = (qa_app_sim_rerelease_localize){&guest, guest_localize};
    guest.clipboard = (qa_app_sim_rerelease_clipboard){false, NULL, NULL};
    options.has_q2_guest = true;
    options.q2_guest.capabilities = &guest.capabilities;
    options.q2_guest.print_ctx = &guest; options.q2_guest.print = guest_print;
    options.q2_guest.cmd_ctx = &guest; options.q2_guest.add_command = guest_add_command;
    options.q2_guest.graph_ctx = &guest; options.q2_guest.debug_graph = guest_debug_graph;
    options.q2_guest.edition = q2_edition; options.q2_guest.prepared = q2_prepared;
    if (strcmp(q2_edition, "rerelease") == 0) { options.q2_guest.localize = &guest.localize; options.q2_guest.clipboard = &guest.clipboard; }
  }
  const qa_app_prepared_q3_game *q3_prepared = qa_app_loaded_content_prepared_q3_game(content);
  qa_app_sim_q3_guest_options q3_guest; memset(&q3_guest, 0, sizeof q3_guest);
  qa_user_file_store q3_files; memset(&q3_files, 0, sizeof q3_files);
  qa_qvm_common_services q3_common; memset(&q3_common, 0, sizeof q3_common);
  if (q3_prepared != NULL) {
    if (!qa_user_file_store_init(&q3_files, directory)) abort();
    q3_common.ctx = &guest; q3_common.milliseconds = guest_milliseconds; q3_common.real_time = guest_real_time;
    q3_common.commands = (qa_qvm_console_commands){&guest, guest_execute_now, guest_insert, guest_append, NULL, NULL, NULL};
    q3_guest.prepared = q3_prepared; q3_guest.game_directory = "baseq3";
    q3_guest.has_game_type = true; q3_guest.game_type = 0;
    q3_guest.print_ctx = &guest; q3_guest.print = guest_print;
    q3_guest.writable = &q3_files; q3_guest.common = &q3_common;
    options.q3_guest = &q3_guest;
  }
  /* Application.open loads through loadSimulation; content.preparedMods is an empty list. */
  static const void *const no_prepared_mods[1] = {NULL};
  options.prepared_mods = no_prepared_mods; options.prepared_mod_count = 0;
  qa_app_shared_simulation *sim = qa_app_shared_simulation_load(options, next_frame, &guest);
  qa_client_id client = qa_identity_owner_client(&identity, 0, 0);
  qa_app_q3_qvm_server_game *qvm = sim->source.kind == QA_SS_SOURCE_Q3_QVM ? qa_app_shared_sim_q3_guest(sim) : NULL;
  if (qvm != NULL) run_qvm(sim, qvm, client, &guest, checkpoints, checkpoint_count);
  else {
  qa_app_sim_player_admission admission = qa_app_shared_sim_admit_player(sim, client);
  qa_actor_id actor = admission.actor;
  emit("admitted %lld:%lld view-height %s", (long long)actor.slot, (long long)actor.generation, N(admission.view_height));
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(sim, actor);
  qa_simulation_output initial = qa_app_shared_sim_current_output(sim);
  record(initial);
  size_t player_count = 0; (void)qa_app_shared_sim_players(sim, &player_count);
  emit("players %zu", player_count);
  for (size_t i = 0; i < checkpoint_count; i++) if (checkpoints[i] == 0) { dump(sim, 0, initial, actor); break; }
  double last = 0;
  for (size_t i = 0; i < checkpoint_count; i++) if (checkpoints[i] > last) last = checkpoints[i];
  for (double frame = 1; frame <= last; frame++) {
    guest.clock_milliseconds += 100;
    qa_actor_command commands[1]; size_t command_count = 0;
    /* The command dialect follows the source's own client movement (a native guest's player state), as observeClientCommand reads it. */
    bool has_state = player != NULL; qa_movement_state state; memset(&state, 0, sizeof state);
    if (sim->source.kind == QA_SS_SOURCE_Q2_NATIVE) {
      has_state = true;
      if (sim->source.classic_world != NULL) {
        state.kind = QA_MOVEMENT_STATE_Q2;
        state.as.q2 = qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, (double)client.slot + 1).movement;
      } else {
        state.kind = QA_MOVEMENT_STATE_Q2_RERELEASE;
        state.as.q2_rerelease = qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, (double)client.slot + 1).movement;
      }
    } else if (player != NULL) state = qa_app_sim_movement_player_read_state(player);
    if (has_state) {
      memset(&commands[0], 0, sizeof commands[0]);
      commands[0].actor = actor;
      /* Native guests take their remote clients through the wire endpoint; the verification seat is local. */
      if (sim->source.kind == QA_SS_SOURCE_Q2_NATIVE) {
        commands[0].source.kind = QA_COMMAND_SOURCE_LOCAL_SEAT; commands[0].source.seat = qa_identity_owner_seat(&identity, 0);
      } else commands[0].source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT;
      commands[0].source.client = client;
      commands[0].sequence = frame;
      commands[0].command = script_command(state, frame, qa_app_shared_sim_time_seconds(sim));
      command_count = 1;
    }
    qa_input_batch batch = {100, commands, command_count};
    qa_simulation_output output = qa_app_shared_sim_step(sim, batch);
    record(output);
    trace_frame = frame;
    drain_presentation(sim);
    if (getenv("SIM_VERIFY_DRAWS") != NULL && strcmp(getenv("SIM_VERIFY_DRAWS"), "1") == 0)
      emit("draws %s %lld", N(frame), (long long)qa_app_sim_source_random_checkpoint(sim->random).draws);
    for (size_t i = 0; i < checkpoint_count; i++) if (checkpoints[i] == frame) { dump(sim, frame, output, actor); break; }
  }
  }
  if (qvm != NULL) qa_app_q3_qvm_server_game_shutdown(qvm);
  qa_app_shared_sim_close(sim);
  qa_app_shared_simulation_destroy(sim);
  if (q3_prepared != NULL) qa_user_file_store_destroy(&q3_files);
  if (guest.writable) qa_app_sim_classic_guest_files_destroy(guest.writable);
  if (guest.installed) qa_app_sim_classic_guest_files_destroy(guest.installed);
  qa_identity_owner_destroy(&identity);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  if (base_content != NULL) {
    qa_app_loaded_content_close(base_content); qa_app_loaded_content_destroy(base_content);
    qa_resolved_resource_release(&qagame);
  }
  qa_app_application_command_destroy(&command);
  for (size_t i = 0; i < event_count_size; i++) free(event_counts[i].key);
  free(event_counts); event_counts = NULL; event_count_size = event_count_capacity = 0;
  for (size_t i = 0; i < presentation_count_size; i++) free(presentation_counts[i].key);
  free(presentation_counts); presentation_counts = NULL; presentation_count_size = presentation_count_capacity = 0;
  lines_free(&console_lines);
  return true;
}

static bool read_expected(const char *path, lines *expected) {
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  char *line = NULL; size_t capacity = 0; ssize_t length;
  while ((length = getline(&line, &capacity, file)) >= 0) {
    if (length > 0 && line[length - 1] == '\n') line[length - 1] = '\0';
    lines_push(expected, line);
  }
  free(line); fclose(file);
  return true;
}

/* Frames the expected dump checkpoints ("== frame N" lines). */
static size_t expected_frames(const lines *expected, double *frames, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < expected->count && count < capacity; i++)
    if (strncmp(expected->items[i], "== frame ", 9) == 0) frames[count++] = strtod(expected->items[i] + 9, NULL);
  return count;
}

/* The user content root is a fixed path (it is recorded in checkpoint
 * images), so concurrent runs of this test, or of the oracle, would share it.
 * Hold an exclusive lock for the whole run. */
static void lock_user_root(void) {
  if (system("mkdir -p build/agents/sim-verify2/tmp") != 0) return;
  int fd = open("build/agents/sim-verify2/tmp/.lock", O_CREAT | O_RDWR, 0666);
  if (fd >= 0 && flock(fd, LOCK_EX) != 0) perror("flock");
}
int main(int argc, char **argv) {
  lock_user_root();
  if (argc >= 2) {
    const verify_case *spec = NULL;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) if (strcmp(cases[i].name, argv[1]) == 0) spec = &cases[i];
    if (spec == NULL) { fprintf(stderr, "unknown case %s\n", argv[1]); return 2; }
    double frames[32]; size_t frame_count = 0;
    for (int i = 2; i < argc && frame_count < 32; i++) frames[frame_count++] = strtod(argv[i], NULL);
    if (frame_count == 0) { frames[0] = 1; frames[1] = 10; frames[2] = 100; frame_count = 3; }
    bool ok = run_case(spec, frames, frame_count);
    for (size_t i = 0; i < out.count; i++) printf("%s\n", out.items[i]);
    fflush(stdout); /* LeakSanitizer exits without flushing stdio */
    lines_free(&out);
    return ok ? 0 : 1;
  }
  int failures = 0, compared = 0;
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    char path[512]; snprintf(path, sizeof path, "tests/app/oracles/sim_verify_%s.txt", cases[c].name);
    lines expected = {0};
    if (!read_expected(path, &expected)) continue;
    double frames[32]; size_t frame_count = expected_frames(&expected, frames, 32);
    compared++;
    if (!run_case(&cases[c], frames, frame_count)) { failures++; lines_free(&expected); continue; }
    size_t mismatches = 0, checkpoint_mismatches = 0, total = expected.count > out.count ? expected.count : out.count;
    for (size_t i = 0; i < total; i++) {
      const char *want = i < expected.count ? expected.items[i] : "<end>", *got = i < out.count ? out.items[i] : "<end>";
      if (strcmp(want, got) == 0) continue;
      if (strncmp(want, "checkpoint", 10) == 0 && strncmp(got, "checkpoint", 10) == 0) checkpoint_mismatches++;
      if (mismatches < 10) fprintf(stderr, "%s line %zu\n  ts: %s\n  c:  %s\n", cases[c].name, i + 1, want, got);
      mismatches++;
    }
    printf("%s: %zu lines, %zu mismatches (%zu checkpoint, %zu state)\n", cases[c].name, total, mismatches,
        checkpoint_mismatches, mismatches - checkpoint_mismatches);
    if (mismatches != 0) failures++;
    lines_free(&expected); lines_free(&out);
  }
  printf("%d cases compared, %d failed\n", compared, failures);
  fflush(stdout);
  return failures == 0 ? 0 : 1;
}
