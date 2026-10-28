/* Port of src/app/bootstrap/simulation/rerelease-guest-services.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_guest_services.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/resource_name_index.h"
#include "qa/foundation/support.h"
static const struct { const char *kind; double base, count; } ranges[] = {
  {"model", 62, 8192}, {"sound", 8254, 2048}, {"image", 10302, 512},
};
#define MAX_STRINGS 12448
#define MAX_MESSAGES 256
struct qa_app_sim_rerelease_guest_services {
  qa_app_sim_rerelease_guest_services_options options;
  qa_app_sim_rerelease_services_host host;
  const char *strings[MAX_STRINGS]; bool has_string[MAX_STRINGS];
  qa_resource_name_index names[3];
  /* weapon model names by configstring index (subset of strings) */
  const void *memory, *guest_host;
  bool loading;
  double frame;
  qa_app_sim_rerelease_guest_message messages[MAX_MESSAGES]; size_t message_count;
  /* core services port storage (compat opaque token) */
  int core_token;
  qa_app_sim_rerelease_configstring scratch[512]; size_t scratch_count;
  qa_app_sim_rerelease_guest_message drain[MAX_MESSAGES];
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static void validate_configstring(double index, const char *value) {
  if (index != trunc(index) || index < 0 || index >= 12448 || (value && strchr(value, '\0') != value + strlen(value)))
    qa_range_error("Invalid API2023 configstring");
}
static void store(qa_app_sim_rerelease_guest_services *s, double index, const char *value) {
  s->strings[(int)index] = value; s->has_string[(int)index] = true;
  for (int k = 0; k < 3; k++) {
    double slot = index - ranges[k].base;
    if (slot > 0 && slot < ranges[k].count) { qa_resource_name_index_set(&s->names[k], slot, value); break; }
  }
}
static void seed_map(qa_app_sim_rerelease_guest_services *s, size_t model_count) {
  for (int k = 0; k < 3; k++) qa_resource_name_index_clear(&s->names[k]);
  store(s, 63, s->host.map_path);
  static char models[8192][16];
  for (size_t i = 1; i < model_count; i++) {
    snprintf(models[i], sizeof models[i], "*%zu", i);
    store(s, 63 + (double)i + (i >= 254 ? 1 : 0), models[i]);
  }
}
qa_app_sim_rerelease_guest_services *qa_app_sim_rerelease_guest_services_create(
    qa_app_sim_rerelease_guest_services_options options, qa_app_sim_rerelease_services_host host) {
  qa_app_sim_rerelease_guest_services *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->options = options; s->host = host; s->loading = true;
  if (host.max_clients != trunc(host.max_clients) || host.max_clients < 1 || host.max_clients > 256)
    qa_range_error("Invalid API2023 maxclients");
  if (options.frame_milliseconds != trunc(options.frame_milliseconds) || options.frame_milliseconds <= 0
      || fmod(1000, options.frame_milliseconds) != 0)
    qa_range_error("Invalid API2023 frame duration");
  static const int64_t reserved[1] = {255};
  if (!qa_resource_name_index_init(&s->names[0], 8192, reserved, 1)
      || !qa_resource_name_index_init(&s->names[1], 2048, NULL, 0)
      || !qa_resource_name_index_init(&s->names[2], 512, NULL, 0)) qa_range_error("Out of memory");
  qa_app_sim_rerelease_guest_services_validate_map(s, host.model_count);
  seed_map(s, host.model_count);
  char text[16]; snprintf(text, sizeof text, "%d", (int)host.max_clients);
  if (host.set_cvar) host.set_cvar(host.ctx, "maxclients", text, true);
  return s;
}
void qa_app_sim_rerelease_guest_services_destroy(qa_app_sim_rerelease_guest_services *services) {
  if (!services) return;
  for (int k = 0; k < 3; k++) qa_resource_name_index_destroy(&services->names[k]);
  free(services);
}
qa_app_sim_rerelease_guest_services_options qa_app_sim_rerelease_guest_services_options_of(qa_app_sim_rerelease_guest_services *services) { return services->options; }
const void *qa_app_sim_rerelease_guest_services_memory(qa_app_sim_rerelease_guest_services *services) {
  if (!services->memory) qa_range_error("API2023 services have no guest memory");
  return services->memory;
}
const void *qa_app_sim_rerelease_guest_services_host_of(qa_app_sim_rerelease_guest_services *services) {
  if (!services->guest_host) qa_range_error("API2023 services have no guest host");
  return services->guest_host;
}
double qa_app_sim_rerelease_guest_services_server_frame(qa_app_sim_rerelease_guest_services *services) { return services->frame; }
const void *qa_app_sim_rerelease_guest_services_bind_memory(qa_app_sim_rerelease_guest_services *services, const void *memory) {
  if (services->memory) qa_range_error("API2023 memory already bound");
  services->memory = memory;
  return &services->core_token;
}
void qa_app_sim_rerelease_guest_services_bind_host(qa_app_sim_rerelease_guest_services *services, const void *host, const void *host_memory) {
  if (services->guest_host || host_memory != qa_app_sim_rerelease_guest_services_memory(services))
    qa_range_error("API2023 host/memory binding mismatch");
  services->guest_host = host;
}
void qa_app_sim_rerelease_guest_services_complete_spawn(qa_app_sim_rerelease_guest_services *services) { services->loading = false; }
void qa_app_sim_rerelease_guest_services_validate_map(qa_app_sim_rerelease_guest_services *services, size_t model_count) {
  (void)services;
  if (model_count >= 8191) qa_range_error("API2023 map exceeds model capacity");
}
void qa_app_sim_rerelease_guest_services_publish_entities(qa_app_sim_rerelease_guest_services *services) {
  services->host.publish_entities(services->host.ctx);
}
double qa_app_sim_rerelease_guest_services_player_ping(qa_app_sim_rerelease_guest_services *services, double slot) {
  return services->host.player_ping(services->host.ctx, slot);
}
void qa_app_sim_rerelease_guest_services_set_player_ping(qa_app_sim_rerelease_guest_services *services, double slot, double ping) {
  services->host.set_player_ping(services->host.ctx, slot, ping);
}
void qa_app_sim_rerelease_guest_services_begin_frame(qa_app_sim_rerelease_guest_services *services, double frame) {
  if (frame != trunc(frame) || frame < 0 || frame > 4294967295.0) qa_range_error("Invalid API2023 frame");
  services->frame = frame;
}
void qa_app_sim_rerelease_guest_services_rebind_world(qa_app_sim_rerelease_guest_services *services,
    qa_app_sim_rerelease_guest_map_services binding, size_t model_count) {
  (void)binding;
  qa_app_sim_rerelease_guest_services_validate_map(services, model_count);
  services->loading = true; services->frame = 0;
  memset(services->has_string, 0, sizeof services->has_string);
  services->message_count = 0;
  seed_map(services, model_count);
}
qa_q2_rerelease_entity_state qa_app_sim_rerelease_guest_services_entity_state(qa_app_sim_rerelease_guest_services *services, double slot) {
  return services->host.entity_state(services->host.ctx, slot);
}
qa_q2_rerelease_player_state qa_app_sim_rerelease_guest_services_player_state(qa_app_sim_rerelease_guest_services *services, double slot) {
  return services->host.player_state(services->host.ctx, slot);
}
qa_app_sim_rerelease_model_appearance qa_app_sim_rerelease_guest_services_model_appearance(qa_app_sim_rerelease_guest_services *services, double slot) {
  return services->host.model_appearance(services->host.ctx, slot);
}
qa_app_sim_rerelease_entity_info qa_app_sim_rerelease_guest_services_entity_info(qa_app_sim_rerelease_guest_services *services, double slot) {
  return services->host.entity_info(services->host.ctx, slot);
}
const qa_app_sim_rerelease_guest_message *qa_app_sim_rerelease_guest_services_drain_messages(qa_app_sim_rerelease_guest_services *services, size_t *out_count) {
  memcpy(services->drain, services->messages, services->message_count * sizeof services->messages[0]);
  if (out_count) *out_count = services->message_count;
  services->message_count = 0;
  return services->drain;
}
const qa_app_sim_rerelease_configstring *qa_app_sim_rerelease_guest_services_configstrings(qa_app_sim_rerelease_guest_services *services, size_t *out_count) {
  services->scratch_count = 0;
  for (int i = 0; i < MAX_STRINGS && services->scratch_count < 512; i++)
    if (services->has_string[i]) services->scratch[services->scratch_count++] = (qa_app_sim_rerelease_configstring){i, services->strings[i]};
  if (out_count) *out_count = services->scratch_count;
  return services->scratch;
}
void qa_app_sim_rerelease_guest_services_set_configstring(qa_app_sim_rerelease_guest_services *services, double index, const char *value) {
  validate_configstring(index, value);
  int at = (int)index;
  if (services->has_string[at] && is(services->strings[at], value)) return;
  store(services, index, value);
  if (index >= 10814 && index < 11070 && services->host.emit_lightstyle)
    services->host.emit_lightstyle(services->host.ctx, index - 10814, value);
  if (index == 1 && services->host.emit_music) services->host.emit_music(services->host.ctx, value);
}
void qa_app_sim_rerelease_guest_services_restore_configstrings(qa_app_sim_rerelease_guest_services *services,
    const qa_app_sim_rerelease_configstring *values, size_t count) {
  for (size_t i = 0; i < count; i++) validate_configstring(values[i].index, values[i].value);
  memset(services->has_string, 0, sizeof services->has_string);
  for (int k = 0; k < 3; k++) qa_resource_name_index_clear(&services->names[k]);
  for (size_t i = 0; i < count; i++) store(services, values[i].index, values[i].value);
}
static int range_of(const char *kind) {
  for (int k = 0; k < 3; k++) if (is(kind, ranges[k].kind)) return k;
  qa_range_error("Invalid API2023 resource kind");
}
const char *qa_app_sim_rerelease_guest_services_resource(qa_app_sim_rerelease_guest_services *services, const char *kind, double index) {
  int at = (int)(ranges[range_of(kind)].base + index);
  return services->has_string[at] ? services->strings[at] : "";
}
double qa_app_sim_rerelease_guest_services_resource_index(qa_app_sim_rerelease_guest_services *services, const char *kind, const char *name) {
  int k = range_of(kind);
  int64_t found = qa_resource_name_index_find(&services->names[k], name);
  if (found < 0) qa_range_error("API2023 %s index overflow", kind);
  double index = (double)found;
  if (index != 0 && !is(qa_app_sim_rerelease_guest_services_resource(services, kind, index), name))
    qa_app_sim_rerelease_guest_services_set_configstring(services, ranges[k].base + index, name);
  return index;
}
