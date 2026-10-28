/* Port of src/app/bootstrap/native-q2-client.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/native_q2_client.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/event_ring.h"
#include "qa/foundation/support.h"
typedef struct config_entry { double index; char *value; } config_entry;
struct qa_app_native_q2_client {
  qa_app_native_q2_world world;
  double source_slot;
  qa_actor_id actor;
  qa_content_id content;
  qa_q2_server_message_reader reader;
  qa_app_native_q2_layout source_layout;
  config_entry *configs; size_t config_count, config_capacity;
  qa_event_ring pending;
  qa_event_cursor cursor;
  double sequence;
  double *counts; size_t count_count;
  char *layout_text;
  const void *fog_target;
};
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
static const char *config_get(qa_app_native_q2_client *client, double index) {
  for (size_t i = 0; i < client->config_count; i++)
    if (client->configs[i].index == index) return client->configs[i].value;
  return NULL;
}
static void config_set(qa_app_native_q2_client *client, double index, const char *value) {
  for (size_t i = 0; i < client->config_count; i++) {
    if (client->configs[i].index == index) {
      free(client->configs[i].value);
      client->configs[i].value = dup_text(value);
      return;
    }
  }
  if (client->config_count == client->config_capacity) {
    size_t cap = client->config_capacity == 0 ? 16 : client->config_capacity * 2;
    config_entry *next = realloc(client->configs, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    client->configs = next; client->config_capacity = cap;
  }
  client->configs[client->config_count].index = index;
  client->configs[client->config_count].value = dup_text(value);
  client->config_count++;
}
qa_app_native_q2_client *qa_app_native_q2_client_create(qa_app_native_q2_world world,
    double source_slot, qa_actor_id actor, qa_content_id content) {
  qa_actor_id admitted;
  if (!world.actor(world.ctx, source_slot, &admitted) || !qa_same_actor(admitted, actor))
    qa_range_error("Native client presentation requires its admitted source player");
  qa_app_native_q2_client *client = calloc(1, sizeof *client);
  if (client == NULL) qa_range_error("Out of memory");
  client->world = world;
  client->source_slot = source_slot;
  client->actor = actor;
  client->content = content;
  client->sequence = 1;
  client->layout_text = dup_text("");
  client->fog_target = world.empty_fog;
  const double *indices = NULL;
  const char *const *values = NULL;
  size_t rows = world.configstrings(world.ctx, &indices, &values);
  for (size_t i = 0; i < rows; i++) config_set(client, indices[i], values[i]);
  qa_q2_protocol_identity protocol = {0};
  if (strcmp(world.edition, "rerelease") == 0) { protocol.kind = "q2-rerelease"; protocol.version = 1038; }
  else { protocol.kind = "q2-classic"; protocol.version = 34; }
  client->source_layout = world.layout(world.ctx, protocol);
  qa_q2_server_message_options options = {0};
  options.max_config_strings = client->source_layout.max_config_strings;
  options.inventory_slots = 256;
  if (!qa_q2_server_message_reader_init(&client->reader, protocol, options))
    qa_range_error("Out of memory");
  if (!qa_event_ring_init(&client->pending, 8192, sizeof(qa_app_native_q2_event)))
    qa_range_error("Out of memory");
  if (!qa_event_ring_open_cursor(&client->pending, &client->cursor, false))
    qa_range_error("Out of memory");
  return client;
}
void qa_app_native_q2_client_destroy(qa_app_native_q2_client *client) {
  if (client == NULL) return;
  qa_q2_server_message_reader_destroy(&client->reader);
  qa_event_ring_close_cursor(&client->pending, &client->cursor);
  qa_event_ring_destroy(&client->pending);
  for (size_t i = 0; i < client->config_count; i++) free(client->configs[i].value);
  free(client->configs); free(client->counts); free(client->layout_text);
  free(client);
}
const char *qa_app_native_q2_client_config_string(qa_app_native_q2_client *client, double index) {
  return config_get(client, index);
}
const double *qa_app_native_q2_client_inventory(qa_app_native_q2_client *client, size_t *out_count) {
  *out_count = client->count_count;
  return client->counts;
}
const char *qa_app_native_q2_client_layout(qa_app_native_q2_client *client) { return client->layout_text; }
const void *qa_app_native_q2_client_player_state(qa_app_native_q2_client *client) {
  return client->world.player_state(client->world.ctx, client->source_slot);
}
static qa_content_id host_content(void *ctx) { return ((qa_app_native_q2_client *)ctx)->content; }
static double host_next_sequence(void *ctx) { return ((qa_app_native_q2_client *)ctx)->sequence++; }
static bool host_player(void *ctx, qa_actor_id *actor_out, double *source_entity_out) {
  qa_app_native_q2_client *client = ctx;
  *actor_out = client->actor;
  *source_entity_out = client->source_slot;
  return true;
}
static bool host_actor(void *ctx, double slot, qa_actor_id *out) {
  qa_app_native_q2_client *client = ctx;
  return client->world.actor(client->world.ctx, slot, out);
}
static const void *host_entity(void *ctx, double slot) {
  qa_app_native_q2_client *client = ctx;
  return client->world.entity_active(client->world.ctx, slot)
      ? client->world.entity_state(client->world.ctx, slot) : NULL;
}
static const void *host_fog(void *ctx, const void *value) {
  qa_app_native_q2_client *client = ctx;
  client->fog_target = client->world.fog_from_wire(client->world.ctx, client->fog_target, value);
  return client->fog_target;
}
static const char *host_config_string(void *ctx, double index) { return config_get(ctx, index); }
static void host_set_config_string(void *ctx, double index, const char *value) { config_set(ctx, index, value); }
static void host_set_inventory(void *ctx, const double *counts, size_t count) {
  qa_app_native_q2_client *client = ctx;
  free(client->counts);
  client->counts = calloc(count == 0 ? 1 : count, sizeof *client->counts);
  if (client->counts == NULL) qa_range_error("Out of memory");
  memcpy(client->counts, counts, count * sizeof *counts);
  client->count_count = count;
}
static void host_set_layout(void *ctx, const char *value) {
  qa_app_native_q2_client *client = ctx;
  free(client->layout_text);
  client->layout_text = dup_text(value);
}
static void host_emit(void *ctx, qa_app_native_q2_event event) {
  qa_app_native_q2_client *client = ctx;
  qa_event_ring_admit(&client->pending, &event);
}
static qa_app_native_q2_presentation_host presentation_host(qa_app_native_q2_client *client, double seconds) {
  qa_app_native_q2_presentation_host host = {0};
  host.ctx = client;
  host.content = host_content;
  host.seconds = seconds;
  host.next_sequence = host_next_sequence;
  host.edition = client->world.edition;
  host.player = host_player;
  host.actor = host_actor;
  host.entity = host_entity;
  host.fog = host_fog;
  host.image_config_offset = client->source_layout.images;
  host.sound_config_offset = client->source_layout.sounds;
  host.player_skin_config_offset = client->source_layout.player_skins;
  host.config_string = host_config_string;
  host.set_config_string = host_set_config_string;
  host.set_inventory = host_set_inventory;
  host.set_layout = host_set_layout;
  host.emit = host_emit;
  return host;
}
size_t qa_app_native_q2_client_receive(qa_app_native_q2_client *client,
    const uint8_t *const *messages, const size_t *lengths, size_t message_count,
    double seconds, qa_q2_server_record **remaining_out) {
  qa_q2_server_record *remaining = NULL;
  size_t total = 0, capacity = 0;
  for (size_t m = 0; m < message_count; m++) {
    qa_q2_server_record *records = NULL;
    size_t count = 0;
    if (!qa_q2_server_message_reader_read(&client->reader, messages[m], lengths[m], &records, &count))
      qa_range_error("Out of memory");
    qa_app_native_q2_presentation_host host = presentation_host(client, seconds);
    qa_q2_server_record *left = NULL;
    size_t left_count = client->world.translate(client->world.ctx, records, count, &host, &left);
    if (total + left_count > capacity) {
      capacity = (total + left_count) * 2 + 8;
      qa_q2_server_record *next = realloc(remaining, capacity * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      remaining = next;
    }
    if (left_count > 0) memcpy(remaining + total, left, left_count * sizeof *left);
    total += left_count;
    qa_q2_server_records_free(records, count);
  }
  *remaining_out = remaining;
  return total;
}
void qa_app_native_q2_client_print(qa_app_native_q2_client *client, double level,
    const char *text, double seconds) {
  qa_app_native_q2_presentation_host host = presentation_host(client, seconds);
  client->world.service_print(client->world.ctx, &host, level, text);
}
size_t qa_app_native_q2_client_take_events(qa_app_native_q2_client *client,
    qa_app_native_q2_event **out) {
  size_t count = 0;
  *out = qa_event_ring_take(&client->pending, &client->cursor, &count);
  qa_event_ring_retire(&client->pending);
  return count;
}
