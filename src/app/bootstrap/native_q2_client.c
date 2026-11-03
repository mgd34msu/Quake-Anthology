/* Port of src/app/bootstrap/native-q2-client.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/native_q2_client.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/app/bootstrap/network/q2_service_presentation.h"
#include "qa/app/bootstrap/rerelease_presentation/fog.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/support.h"

struct qa_app_native_q2_client {
  qa_app_sim_q2_native_world world;
  double source_slot;
  qa_actor_id actor;
  char *content;
  qa_q2_server_message_reader reader;
  qa_app_q2_layout source_layout;
  /* configs: Map<number, string> in insertion order (values owned). */
  qa_app_sim_configstring *configs; size_t config_count, config_capacity;
  /* pending: EventRing<SimulationPresentationEvent>(8192) of owned copies. */
  qa_event_ring pending;
  qa_event_cursor pending_cursor;
  double sequence;
  double *counts; size_t count_count;
  char *layout_text;
  qa_q2_fog_state fog_target;
  /* The seconds the current presentationHost was built with. */
  double host_seconds;
};

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *dup_text(const char *text) {
  size_t length = strlen(text);
  return memcpy(checked(malloc(length + 1)), text, length + 1);
}
static bool world_actor(const qa_app_sim_q2_native_world *world, double slot, qa_actor_id *out) {
  return world->rerelease ? qa_app_sim_rerelease_guest_world_actor(world->rerelease_world, slot, out)
                          : qa_app_sim_classic_guest_world_actor(world->classic, slot, out);
}
static const char *config_get(const qa_app_native_q2_client *client, double index) {
  for (size_t i = 0; i < client->config_count; i++)
    if (client->configs[i].index == index) return client->configs[i].value;
  return NULL;
}
/* Map.set: an existing key keeps its position. */
static void config_set(qa_app_native_q2_client *client, double index, const char *value) {
  for (size_t i = 0; i < client->config_count; i++)
    if (client->configs[i].index == index) {
      char *copy = dup_text(value);
      free((char *)client->configs[i].value);
      client->configs[i].value = copy;
      return;
    }
  if (client->config_count == client->config_capacity) {
    client->config_capacity = client->config_capacity == 0 ? 64 : client->config_capacity * 2;
    client->configs = checked(realloc(client->configs, client->config_capacity * sizeof *client->configs));
  }
  client->configs[client->config_count].index = index;
  client->configs[client->config_count].value = dup_text(value);
  client->config_count++;
}

qa_app_native_q2_client *qa_app_native_q2_client_create(qa_app_sim_q2_native_world world,
    double source_slot, qa_actor_id actor, qa_content_id content) {
  qa_actor_id admitted;
  if (!world_actor(&world, source_slot, &admitted) || !qa_same_actor(admitted, actor))
    qa_raise("Error", 0, 0, "Native client presentation requires its admitted source player");
  qa_app_native_q2_client *client = checked(calloc(1, sizeof *client));
  client->world = world;
  client->source_slot = source_slot;
  client->actor = actor;
  client->content = dup_text(content);
  client->sequence = 1;
  client->fog_target = qa_q2_create_fog();
  /* this.configs = new Map(world.configstrings()) */
  size_t count = 0;
  const qa_app_sim_configstring *rows = world.rerelease
      ? qa_app_sim_rerelease_guest_world_configstrings(world.rerelease_world, &count)
      : qa_app_sim_classic_guest_world_configstrings(world.classic, &count);
  for (size_t i = 0; i < count; i++) config_set(client, rows[i].index, rows[i].value);
  /* Game imports produce multicast FLOAT records, independently of the selected client wire protocol. */
  qa_q2_protocol_identity protocol = world.rerelease ? (qa_q2_protocol_identity){"q2-rerelease", 1038, 0, 0}
                                                     : (qa_q2_protocol_identity){"q2-classic", 34, 0, 0};
  client->source_layout = qa_app_q2_application_layout(protocol);
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = client->source_layout.max_config_strings;
  options.inventory_slots = 256;
  if (!qa_q2_server_message_reader_init(&client->reader, protocol, options)) qa_range_error("Out of memory");
  if (!qa_event_ring_init(&client->pending, 8192, sizeof(qa_app_sim_presentation_event)) ||
      !qa_event_ring_open_cursor(&client->pending, &client->pending_cursor, false))
    qa_range_error("Out of memory");
  return client;
}
/* Releases an undrained event's copy (its storage is owned by the record). */
static void release_pending(const void *record, int64_t id, void *ctx) {
  (void)id;
  (void)ctx;
  qa_app_sim_presentation_event copy = *(const qa_app_sim_presentation_event *)record;
  qa_app_sim_presentation_event_release(&copy);
}
void qa_app_native_q2_client_destroy(qa_app_native_q2_client *client) {
  if (client == NULL) return;
  qa_event_ring_drain(&client->pending, &client->pending_cursor, release_pending, NULL);
  qa_event_ring_close_cursor(&client->pending, &client->pending_cursor);
  qa_event_ring_destroy(&client->pending);
  qa_q2_server_message_reader_destroy(&client->reader);
  for (size_t i = 0; i < client->config_count; i++) free((char *)client->configs[i].value);
  free(client->configs);
  free(client->counts);
  free(client->layout_text);
  free(client->content);
  free(client);
}
qa_app_sim_q2_native_world qa_app_native_q2_client_world(const qa_app_native_q2_client *client) { return client->world; }
double qa_app_native_q2_client_source_slot(const qa_app_native_q2_client *client) { return client->source_slot; }
qa_actor_id qa_app_native_q2_client_actor(const qa_app_native_q2_client *client) { return client->actor; }
qa_content_id qa_app_native_q2_client_content(const qa_app_native_q2_client *client) { return client->content; }
const qa_app_sim_configstring *qa_app_native_q2_client_configstrings(const qa_app_native_q2_client *client,
    size_t *out_count) {
  *out_count = client->config_count;
  return client->configs;
}
const char *qa_app_native_q2_client_config_string(const qa_app_native_q2_client *client, double index) {
  return config_get(client, index);
}
const double *qa_app_native_q2_client_inventory(const qa_app_native_q2_client *client, size_t *out_count) {
  *out_count = client->count_count;
  return client->counts;
}
const char *qa_app_native_q2_client_layout(const qa_app_native_q2_client *client) {
  return client->layout_text != NULL ? client->layout_text : "";
}
qa_app_native_q2_player_state qa_app_native_q2_client_player_state(const qa_app_native_q2_client *client) {
  qa_app_native_q2_player_state out;
  memset(&out, 0, sizeof out);
  out.rerelease = client->world.rerelease;
  if (out.rerelease)
    out.rerelease_state = qa_app_sim_rerelease_guest_world_player_state(client->world.rerelease_world, client->source_slot);
  else
    out.classic = qa_app_sim_classic_guest_world_player_state(client->world.classic, client->source_slot);
  return out;
}

/* ---- presentationHost(seconds) ---- */
static qa_content_id host_content(void *ctx) { return ((qa_app_native_q2_client *)ctx)->content; }
static double host_next_sequence(void *ctx) { return ((qa_app_native_q2_client *)ctx)->sequence++; }
static bool host_player(void *ctx, qa_app_q2_service_player *out) {
  qa_app_native_q2_client *client = ctx;
  out->actor = client->actor;
  out->source_entity = client->source_slot;
  return true;
}
static bool host_actor(void *ctx, double slot, qa_actor_id *out) {
  return world_actor(&((qa_app_native_q2_client *)ctx)->world, slot, out);
}
/* entity: slot => world.entityInfo(slot).active ? world.entityState(slot) : null */
static bool host_entity(void *ctx, double slot, qa_app_q2_service_entity *out) {
  qa_app_native_q2_client *client = ctx;
  if (client->world.rerelease) {
    if (!qa_app_sim_rerelease_guest_world_entity_info(client->world.rerelease_world, slot).active) return false;
    qa_q2_rerelease_entity_state state = qa_app_sim_rerelease_guest_world_entity_state(client->world.rerelease_world, slot);
    out->origin = state.base.origin;
    out->angles = state.base.angles;
    return true;
  }
  if (!qa_app_sim_classic_guest_world_entity_info(client->world.classic, slot).active) return false;
  qa_q2_entity_state state = qa_app_sim_classic_guest_world_entity_state(client->world.classic, slot);
  out->origin = state.origin;
  out->angles = state.angles;
  return true;
}
static qa_q2_fog_state host_fog(void *ctx, qa_q2_svc_fog_data value) {
  qa_app_native_q2_client *client = ctx;
  client->fog_target = qa_app_q2_fog_from_wire(client->fog_target, value);
  return client->fog_target;
}
static const char *host_config_string(void *ctx, double index) { return config_get(ctx, index); }
static void host_set_config_string(void *ctx, double index, const char *value) { config_set(ctx, index, value); }
static void host_set_inventory(void *ctx, const double *counts, size_t count) {
  qa_app_native_q2_client *client = ctx;
  double *copy = checked(malloc((count == 0 ? 1 : count) * sizeof *copy));
  if (count != 0) memcpy(copy, counts, count * sizeof *copy);
  free(client->counts);
  client->counts = copy;
  client->count_count = count;
}
static void host_set_layout(void *ctx, const char *value) {
  qa_app_native_q2_client *client = ctx;
  char *copy = dup_text(value);
  free(client->layout_text);
  client->layout_text = copy;
}
static void host_emit(void *ctx, const qa_app_sim_presentation_event *event) {
  qa_app_native_q2_client *client = ctx;
  qa_app_sim_presentation_event copy;
  if (!qa_app_sim_presentation_event_copy(&copy, event)) qa_range_error("Out of memory");
  if (qa_event_ring_append(&client->pending, &copy) == 0) {
    qa_app_sim_presentation_event_release(&copy);
    qa_range_error("Event ring capacity exhausted (8192 undrained records); drain before admitting more");
  }
}
static qa_app_q2_service_host presentation_host(qa_app_native_q2_client *client, double seconds) {
  qa_app_q2_service_host host;
  memset(&host, 0, sizeof host);
  host.ctx = client;
  host.has_edition = true;
  host.edition_rerelease = client->world.rerelease;
  host.seconds = seconds;
  host.image_config_offset = client->source_layout.images;
  host.sound_config_offset = client->source_layout.sounds;
  host.player_skin_config_offset = client->source_layout.player_skins;
  host.content = host_content;
  host.next_sequence = host_next_sequence;
  host.player = host_player;
  host.actor = host_actor;
  host.entity = host_entity;
  host.fog = host_fog;
  host.config_string = host_config_string;
  host.set_config_string = host_set_config_string;
  host.set_inventory = host_set_inventory;
  host.set_layout = host_set_layout;
  host.emit = host_emit;
  return host;
}

size_t qa_app_native_q2_client_receive(qa_app_native_q2_client *client, const qa_app_native_q2_message *messages,
    size_t message_count, double seconds, qa_q2_server_record **remaining_out) {
  qa_q2_server_record *remaining = NULL;
  size_t remaining_count = 0;
  for (size_t m = 0; m < message_count; m++) {
    qa_q2_server_record *records = NULL;
    size_t count = 0;
    if (!qa_q2_server_message_reader_read(&client->reader, messages[m].bytes, messages[m].length, &records, &count))
      qa_range_error("Out of memory");
    qa_app_q2_service_host host = presentation_host(client, seconds);
    /* translateQ2ServiceRecords compacts the records it leaves to the front of the array
     * (struct copies, in order); it runs on a copy so each original is either moved into
     * `remaining` or released. */
    qa_q2_server_record *translated = checked(malloc((count == 0 ? 1 : count) * sizeof *translated));
    if (count != 0) memcpy(translated, records, count * sizeof *records);
    size_t kept = qa_app_translate_q2_service_records(translated, count, &host);
    if (kept != 0) remaining = checked(realloc(remaining, (remaining_count + kept) * sizeof *remaining));
    size_t next = 0;
    for (size_t i = 0; i < count; i++) {
      if (next < kept && memcmp(&records[i], &translated[next], sizeof *records) == 0) {
        remaining[remaining_count++] = records[i];
        next++;
      } else {
        free(records[i].raw);
        qa_q2_server_event_clear(&records[i].event);
      }
    }
    free(translated);
    free(records);
  }
  *remaining_out = remaining;
  return remaining_count;
}
void qa_app_native_q2_client_print(qa_app_native_q2_client *client, double level, const char *text, double seconds) {
  qa_app_q2_service_host host = presentation_host(client, seconds);
  qa_app_q2_service_print(&host, level, text);
}
size_t qa_app_native_q2_client_take_events(qa_app_native_q2_client *client, qa_app_sim_presentation_event **out) {
  size_t count = 0;
  qa_app_sim_presentation_event *taken = qa_event_ring_take(&client->pending, &client->pending_cursor, &count);
  qa_event_ring_retire(&client->pending);
  *out = taken;
  return count;
}
