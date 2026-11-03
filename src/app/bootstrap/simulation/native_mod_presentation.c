/* Port of src/app/bootstrap/simulation/native-mod-presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_mod_presentation.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/app/bootstrap/network/q2_service_presentation.h"
#include "qa/app/bootstrap/rerelease_presentation/fog.h"
#include "qa/app/bootstrap/simulation/native_mod_camera.h"
#include "qa/compat/q2/native_mod_host_api.h"
#include "qa/foundation/support.h"
#include "qa/network/q2/server_messages.h"
#include "qa/persistence/q2_rerelease_state.h"
#include "qa/persistence/save_image.h"
#include "qa/world/session/mod_engine.h"

typedef qa_save_reader R;
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static char *dup_text(const char *text) { size_t n = strlen(text); char *p = malloc(n + 1); if (p == NULL) qa_range_error("Out of memory"); memcpy(p, text, n + 1); return p; }
static void *xcalloc(size_t count, size_t size) { void *p = calloc(count == 0 ? 1 : count, size); if (p == NULL) qa_range_error("Out of memory"); return p; }
static const R *F(const R *reader, const char *name, R *out) { if (!qa_save_reader_field(reader, name, out)) qa_range_error("Out of memory"); return out; }
static bool same_actor(qa_actor_id a, qa_actor_id b) { return a.slot == b.slot && a.generation == b.generation; }

/* --- readNativeModPresentation --- */
static qa_saved_actor_id read_actor(const R *entry) {
  R actor, slot, generation; F(entry, "actor", &actor); qa_saved_actor_id out;
  out.slot = (int64_t)qa_save_reader_integer_min(F(&actor, "slot", &slot), 0); out.generation = (int64_t)qa_save_reader_integer_min(F(&actor, "generation", &generation), 0);
  qa_save_reader_destroy(&slot); qa_save_reader_destroy(&generation); qa_save_reader_destroy(&actor); return out;
}
void qa_app_sim_read_native_mod_presentation(const R *reader, qa_app_sim_native_mod_presentation_checkpoint *out) {
  memset(out, 0, sizeof *out); R list, v;
  F(reader, "fog", &list); out->fog_count = qa_save_reader_list_count(&list); out->fog = xcalloc(out->fog_count, sizeof *out->fog);
  for (size_t i = 0; i < out->fog_count; i++) { R entry; qa_save_reader_item(&list, i, &entry); out->fog[i].actor = read_actor(&entry);
    out->fog[i].value = qa_read_q2_fog_state(F(&entry, "value", &v)); qa_save_reader_destroy(&v); qa_save_reader_destroy(&entry); }
  qa_save_reader_destroy(&list);
  for (size_t i = 0; i < out->fog_count; i++) for (size_t j = 0; j < i; j++)
    if (out->fog[i].actor.slot == out->fog[j].actor.slot && out->fog[i].actor.generation == out->fog[j].actor.generation) fail("Duplicate native mod presentation recipient");
  F(reader, "clients", &list);
  if (!qa_save_reader_is_undefined(&list)) {
    out->client_count = qa_save_reader_list_count(&list); out->clients = xcalloc(out->client_count, sizeof *out->clients);
    for (size_t i = 0; i < out->client_count; i++) {
      R entry, inventory; qa_save_reader_item(&list, i, &entry); qa_app_sim_native_mod_client_entry *client = &out->clients[i];
      client->actor = read_actor(&entry); client->layout = qa_save_reader_string(F(&entry, "layout", &v)); qa_save_reader_destroy(&v);
      F(&entry, "inventory", &inventory); client->inventory_count = qa_save_reader_list_count(&inventory); client->inventory = xcalloc(client->inventory_count, sizeof *client->inventory);
      for (size_t k = 0; k < client->inventory_count; k++) { R item; qa_save_reader_item(&inventory, k, &item); client->inventory[k] = qa_save_reader_integer(&item); qa_save_reader_destroy(&item); }
      qa_save_reader_destroy(&inventory); qa_save_reader_destroy(&entry);
    }
    for (size_t i = 0; i < out->client_count; i++) for (size_t j = 0; j < i; j++)
      if (out->clients[i].actor.slot == out->clients[j].actor.slot && out->clients[i].actor.generation == out->clients[j].actor.generation) qa_save_reader_fail(reader, "Duplicate native client presentation");
  }
  qa_save_reader_destroy(&list);
}
void qa_app_sim_native_mod_presentation_checkpoint_free(qa_app_sim_native_mod_presentation_checkpoint *c) {
  for (size_t i = 0; i < c->client_count; i++) free(c->clients[i].inventory);
  free(c->fog); free(c->clients); memset(c, 0, sizeof *c);
}
static qa_save_value *checked(qa_save_value *value) { if (value == NULL) qa_range_error("Out of memory"); return value; }
static void put(qa_save_value *o, const char *key, qa_save_value *v) { if (!qa_save_value_set(o, key, checked(v))) qa_range_error("Out of memory"); }
static void push(qa_save_value *a, qa_save_value *v) { if (!qa_save_value_push(a, checked(v))) qa_range_error("Out of memory"); }
static qa_save_value *vector_value(qa_vec3 v) { qa_save_value *o = checked(qa_save_value_object()); put(o, "x", qa_save_value_number(v.x)); put(o, "y", qa_save_value_number(v.y)); put(o, "z", qa_save_value_number(v.z)); return o; }
static qa_save_value *actor_value(qa_saved_actor_id a) { qa_save_value *o = checked(qa_save_value_object()); put(o, "slot", qa_save_value_number((double)a.slot)); put(o, "generation", qa_save_value_number((double)a.generation)); return o; }
/* The tree readQ2FogState reads (persistence/q2-rerelease-state key order). */
static qa_save_value *fog_value(const qa_q2_fog_state *in) {
  qa_save_value *root = checked(qa_save_value_object()), *fog = checked(qa_save_value_object()), *height = checked(qa_save_value_object());
  put(fog, "density", qa_save_value_number(in->fog.density)); put(fog, "color", vector_value(in->fog.color)); put(fog, "skyFactor", qa_save_value_number(in->fog.sky_factor));
  put(height, "startColor", vector_value(in->height_fog.start_color)); put(height, "startDistance", qa_save_value_number(in->height_fog.start_distance));
  put(height, "endColor", vector_value(in->height_fog.end_color)); put(height, "endDistance", qa_save_value_number(in->height_fog.end_distance));
  if (in->height_fog.density_first) { put(height, "density", qa_save_value_number(in->height_fog.density)); put(height, "falloff", qa_save_value_number(in->height_fog.falloff)); }
  else { put(height, "falloff", qa_save_value_number(in->height_fog.falloff)); put(height, "density", qa_save_value_number(in->height_fog.density)); }
  put(root, "fog", fog); put(root, "heightFog", height); return root;
}
qa_save_value *qa_app_sim_native_mod_presentation_checkpoint_value(const qa_app_sim_native_mod_presentation_checkpoint *c) {
  qa_save_value *root = checked(qa_save_value_object()), *fog = checked(qa_save_value_array()), *clients = checked(qa_save_value_array());
  for (size_t i = 0; i < c->fog_count; i++) { qa_save_value *row = checked(qa_save_value_object()); put(row, "actor", actor_value(c->fog[i].actor)); put(row, "value", fog_value(&c->fog[i].value)); push(fog, row); }
  for (size_t i = 0; i < c->client_count; i++) {
    qa_save_value *row = checked(qa_save_value_object()), *inventory = checked(qa_save_value_array());
    put(row, "actor", actor_value(c->clients[i].actor)); put(row, "layout", qa_save_value_string(c->clients[i].layout));
    for (size_t k = 0; k < c->clients[i].inventory_count; k++) push(inventory, qa_save_value_number(c->clients[i].inventory[k]));
    put(row, "inventory", inventory); push(clients, row);
  }
  put(root, "fog", fog); put(root, "clients", clients); return root;
}

/* --- NativeModPresentation --- */
typedef struct fog_row { qa_actor_id actor; qa_q2_fog_state value; } fog_row;
typedef struct loop_row { qa_actor_id actor; char *path; double volume, attenuation; qa_vec3 origin; } loop_row;
typedef struct event_row { qa_actor_id actor; double event; } event_row;
typedef struct client_row { qa_actor_id actor; char *layout; double *inventory; size_t inventory_count; } client_row;
typedef struct frame_row { qa_actor_id actor; qa_mod_client_presentation_frame frame; double stats[32]; size_t stat_count; } frame_row;
typedef struct config_row { double index; char *value; } config_row;
/* TS ReadonlyMap<number, string> handed to the native HUD frame. */
typedef struct qa_app_sim_native_mod_configstrings { qa_native_q2_config_string *entries; size_t count; } configstring_snapshot;
struct qa_app_sim_native_mod_presentation {
  qa_app_sim_native_mod_presentation_source source; qa_content_id content; const qa_app_sim_native_mod_projection *projection;
  const qa_mod_host_services *services; qa_shared_scene_queries *scene; qa_provider_id owner;
  bool has_admission; qa_native_mod_client_presentation admission;
  qa_q2_protocol_identity protocol; qa_app_q2_layout layout; qa_q2_server_message_reader reader;
  fog_row *fog; size_t fog_count, fog_cap;
  config_row *configs; size_t config_count, config_cap;
  loop_row *loops; size_t loop_count, loop_cap;
  event_row *events; size_t event_count, event_cap;
  client_row *clients; size_t client_count, client_cap;
  frame_row *frames; size_t frame_count, frame_cap;
  configstring_snapshot snapshot;
  double revision;
  qa_app_simulation_presentation *payloads; size_t presentation_cap;
  char *attached_paths[3];
};
typedef qa_app_sim_native_mod_presentation presentation_t;
static size_t find_index(const void *array, size_t stride, size_t count, qa_actor_id actor) {
  for (size_t i = 0; i < count; i++) { const qa_actor_id *id = (const qa_actor_id *)((const char *)array + i * stride); if (same_actor(*id, actor)) return i; }
  return count;
}
static void *grow(void *items, size_t *cap, size_t count, size_t size) {
  if (count < *cap) return items;
  size_t next = *cap == 0 ? 8 : *cap * 2; void *g = realloc(items, next * size); if (g == NULL) qa_range_error("Out of memory"); *cap = next; return g;
}
static void remove_at(void *array, size_t stride, size_t *count, size_t index) { memmove((char *)array + index * stride, (char *)array + (index + 1) * stride, (*count - index - 1) * stride); (*count)--; }
static const qa_session_mod_engine_services *engine_of(presentation_t *p) { return p->services->engine; }
static qa_source_time now(presentation_t *p) { return p->services->time(p->services->ctx); }
static bool is_live(presentation_t *p, qa_actor_id actor) { return qa_session_actor_registry_is_live(p->services->actors, actor); }

presentation_t *qa_app_sim_native_mod_presentation_create(qa_app_sim_native_mod_presentation_source source, qa_content_id content, const qa_app_sim_native_mod_projection *projection,
                                                          const qa_mod_host_services *services, qa_shared_scene_queries *scene, qa_provider_id owner, const qa_native_mod_client_presentation *admission) {
  presentation_t *p = calloc(1, sizeof *p); if (p == NULL) return NULL;
  p->source = source; p->content = content; p->projection = projection; p->services = services; p->scene = scene; p->owner = owner;
  if (admission != NULL) { p->has_admission = true; p->admission = *admission; }
  bool classic = strcmp(source.edition, "classic") == 0;
  p->protocol.kind = classic ? "q2-classic" : "q2-rerelease"; p->protocol.version = classic ? 34 : 1038;
  p->layout = qa_app_q2_application_layout(p->protocol);
  qa_q2_server_message_options options; memset(&options, 0, sizeof options); options.max_config_strings = p->layout.max_config_strings; options.inventory_slots = 256;
  if (!qa_q2_server_message_reader_init(&p->reader, p->protocol, options)) { free(p); return NULL; }
  return p;
}
static void clear_frames(presentation_t *p) { p->frame_count = 0; }
static void free_snapshot(presentation_t *p) { for (size_t i = 0; i < p->snapshot.count; i++) free((char *)p->snapshot.entries[i].value); free(p->snapshot.entries); p->snapshot.entries = NULL; p->snapshot.count = 0; }
void qa_app_sim_native_mod_presentation_destroy(presentation_t *p) {
  if (p == NULL) return;
  qa_q2_server_message_reader_destroy(&p->reader);
  for (size_t i = 0; i < p->config_count; i++) free(p->configs[i].value);
  for (size_t i = 0; i < p->loop_count; i++) free(p->loops[i].path);
  for (size_t i = 0; i < p->client_count; i++) { free(p->clients[i].layout); free(p->clients[i].inventory); }
  free(p->fog); free(p->configs); free(p->loops); free(p->events); free(p->clients); free(p->frames); free_snapshot(p);
  free(p->payloads); for (int i = 0; i < 3; i++) free(p->attached_paths[i]);
  free(p);
}
double qa_app_sim_native_mod_presentation_generation(const presentation_t *p) { return p->revision; }
bool qa_app_sim_native_mod_presentation_client_frame(presentation_t *p, qa_actor_id actor, qa_mod_client_presentation_frame *out) {
  if (!is_live(p, actor)) return false;
  size_t i = find_index(p->frames, sizeof *p->frames, p->frame_count, actor); if (i == p->frame_count) return false;
  *out = p->frames[i].frame; out->native_hud_frame.stats = p->frames[i].stats; return true;
}
static void emit_q2(presentation_t *p, const qa_q2_presentation_event *payload, qa_source_time time, const qa_actor_id *recipient) {
  const qa_session_mod_engine_services *engine = engine_of(p); if (engine == NULL) fail("Native mod output requires presentation services");
  qa_session_mod_emit emit; memset(&emit, 0, sizeof emit);
  emit.event.kind = "q2"; emit.event.content = p->content; emit.event.q2 = payload;
  if (recipient != NULL) { emit.event.has_recipient = true; emit.event.recipient = *recipient; }
  emit.has_time = true; emit.time = time;
  engine->events.emit(engine->events.ctx, &emit);
}
static void sound(presentation_t *p, qa_actor_id actor, const loop_row *loop, const char *state) {
  qa_q2_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = "sound"; e.has_actor = true; e.actor = actor; e.path = loop->path; e.volume = loop->volume; e.attenuation = loop->attenuation; e.origin = loop->origin;
  e.channel = 0; e.reliable = false; e.loop = state; e.has_loop_owner = true; e.loop_owner = p->owner;
  emit_q2(p, &e, now(p), NULL);
}
static void stop(presentation_t *p, size_t index) {
  loop_row loop = p->loops[index]; sound(p, loop.actor, &loop, "stop");
  free(loop.path); remove_at(p->loops, sizeof *p->loops, &p->loop_count, index);
}
void qa_app_sim_native_mod_presentation_release(presentation_t *p, qa_actor_id actor) {
  size_t i = find_index(p->loops, sizeof *p->loops, p->loop_count, actor); if (i < p->loop_count) stop(p, i);
  i = find_index(p->events, sizeof *p->events, p->event_count, actor); if (i < p->event_count) remove_at(p->events, sizeof *p->events, &p->event_count, i);
  i = find_index(p->fog, sizeof *p->fog, p->fog_count, actor); if (i < p->fog_count) remove_at(p->fog, sizeof *p->fog, &p->fog_count, i);
  i = find_index(p->clients, sizeof *p->clients, p->client_count, actor); if (i < p->client_count) { free(p->clients[i].layout); free(p->clients[i].inventory); remove_at(p->clients, sizeof *p->clients, &p->client_count, i); }
  i = find_index(p->frames, sizeof *p->frames, p->frame_count, actor); if (i < p->frame_count) remove_at(p->frames, sizeof *p->frames, &p->frame_count, i);
}
static void clear_clients(presentation_t *p) { for (size_t i = 0; i < p->client_count; i++) { free(p->clients[i].layout); free(p->clients[i].inventory); } p->client_count = 0; }
void qa_app_sim_native_mod_presentation_close(presentation_t *p) {
  p->revision++;
  while (p->loop_count > 0) qa_app_sim_native_mod_presentation_release(p, p->loops[0].actor);
  p->event_count = 0; clear_clients(p); clear_frames(p);
}
void qa_app_sim_native_mod_presentation_begin_frame(presentation_t *p) { p->event_count = 0; }
static client_row *client_messages(presentation_t *p, const qa_actor_id *actor) {
  if (actor == NULL) fail("Native client presentation requires an admitted recipient");
  size_t i = find_index(p->clients, sizeof *p->clients, p->client_count, *actor);
  if (i == p->client_count) { p->clients = grow(p->clients, &p->client_cap, p->client_count, sizeof *p->clients); memset(&p->clients[i], 0, sizeof p->clients[i]); p->clients[i].actor = *actor; p->clients[i].layout = dup_text(""); p->client_count++; }
  return &p->clients[i];
}
static const char *config_get(presentation_t *p, double index) { for (size_t i = 0; i < p->config_count; i++) if (p->configs[i].index == index) return p->configs[i].value; return NULL; }
static void config_set(presentation_t *p, double index, const char *value) {
  for (size_t i = 0; i < p->config_count; i++) if (p->configs[i].index == index) { free(p->configs[i].value); p->configs[i].value = dup_text(value); return; }
  p->configs = grow(p->configs, &p->config_cap, p->config_count, sizeof *p->configs); p->configs[p->config_count].index = index; p->configs[p->config_count++].value = dup_text(value);
}

/* --- drain: translate each message for each recipient --- */
typedef struct translate_ctx { presentation_t *p; const qa_actor_id *recipient; bool has_slot; double slot; const qa_app_sim_classic_guest_message *message; qa_source_time time; } translate_ctx;
static qa_content_id t_content(void *ctx) { return ((translate_ctx *)ctx)->p->content; }
static double t_sequence(void *ctx) { (void)ctx; return 0; }
static bool t_player(void *ctx, qa_app_q2_service_player *out) { translate_ctx *t = ctx; if (t->recipient == NULL) return false; out->actor = *t->recipient; out->source_entity = t->has_slot ? t->slot : 0; return true; }
static bool t_actor(void *ctx, double slot, qa_actor_id *out) { translate_ctx *t = ctx; return t->p->projection->actor_at(t->p->projection->ctx, slot, out); }
static bool t_entity(void *ctx, double slot, qa_app_q2_service_entity *out) {
  translate_ctx *t = ctx; presentation_t *p = t->p; qa_actor_id actor;
  if (p->projection->actor_at(p->projection->ctx, slot, &actor)) { qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, actor); if (body.has_value) { out->origin = body.value.origin; out->angles = body.value.angles; return true; } }
  qa_app_sim_native_mod_appearance look = p->source.appearance(p->source.ctx, slot); out->origin = look.origin; out->angles = look.angles; return true;
}
static qa_q2_fog_state t_fog(void *ctx, qa_q2_svc_fog_data value) {
  translate_ctx *t = ctx; presentation_t *p = t->p; if (t->recipient == NULL) fail("Native fog requires a destination player");
  size_t i = find_index(p->fog, sizeof *p->fog, p->fog_count, *t->recipient);
  qa_q2_fog_state next = qa_app_q2_fog_from_wire(i < p->fog_count ? p->fog[i].value : qa_q2_create_fog(), value);
  if (i == p->fog_count) { p->fog = grow(p->fog, &p->fog_cap, p->fog_count, sizeof *p->fog); p->fog[i].actor = *t->recipient; p->fog_count++; }
  p->fog[i].value = next; return next;
}
static const char *t_config(void *ctx, double index) { return config_get(((translate_ctx *)ctx)->p, index); }
static void t_set_config(void *ctx, double index, const char *value) { config_set(((translate_ctx *)ctx)->p, index, value); }
static void t_inventory(void *ctx, const double *counts, size_t count) {
  translate_ctx *t = ctx; client_row *c = client_messages(t->p, t->recipient); free(c->inventory);
  c->inventory = xcalloc(count, sizeof *c->inventory); if (count != 0) memcpy(c->inventory, counts, count * sizeof *counts); c->inventory_count = count;
}
static void t_layout(void *ctx, const char *text) { translate_ctx *t = ctx; client_row *c = client_messages(t->p, t->recipient); free(c->layout); c->layout = dup_text(text); }
static void t_emit(void *ctx, const qa_app_sim_presentation_event *event) {
  translate_ctx *t = ctx; const qa_session_mod_engine_services *engine = engine_of(t->p);
  qa_session_mod_emit emit; memset(&emit, 0, sizeof emit);
  /* engine.events.emit(this.content, event, time, recipient): the engine restamps sequence,
   * content, seconds and sourceEntity, so only kind and payload travel. */
  qa_app_sim_source_presentation_event source = qa_app_sim_presentation_event_source(event);
  emit.event = qa_app_sim_presentation_event_view(&source);
  /* TS `{ ...event, event: { ...event.event, reliable: message.reliable } }` for q2 sound events. */
  qa_q2_presentation_event sound;
  if (strcmp(event->kind, "q2") == 0 && strcmp(event->q2->kind, "sound") == 0) {
    sound = *event->q2; sound.reliable = t->message->reliable; emit.event.q2 = &sound;
  }
  emit.event.content = t->p->content;
  if (t->recipient != NULL) { emit.event.has_recipient = true; emit.event.recipient = *t->recipient; }
  emit.has_time = true; emit.time = t->time;
  engine->events.emit(engine->events.ctx, &emit);
}
static size_t recipients(presentation_t *p, const qa_app_sim_classic_guest_message *message, qa_actor_id **out, bool *null_recipient) {
  *null_recipient = false;
  if (message->audience.unicast) {
    qa_actor_id actor;
    if (!p->projection->actor_at(p->projection->ctx, message->audience.slot, &actor) || !p->projection->accepts_client(p->projection->ctx, message->audience.slot))
      fail("Native mod message targets an unavailable destination player");
    *out = xcalloc(1, sizeof **out); (*out)[0] = actor; return 1;
  }
  const qa_session_mod_engine_services *engine = engine_of(p); size_t count = 0;
  const qa_actor_id *players = engine != NULL && engine->presentation_players != NULL ? engine->presentation_players(engine->ctx, &count) : NULL;
  if (count == 0) { *null_recipient = true; *out = NULL; return 1; }
  *out = xcalloc(count, sizeof **out); size_t kept = 0;
  if (strcmp(message->audience.scope, "all") == 0) { memcpy(*out, players, count * sizeof *players); return count; }
  double cluster = qa_shared_leaf_cluster(p->scene, qa_shared_point_leaf(p->scene, message->audience.origin));
  for (size_t i = 0; i < count; i++) {
    qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, players[i]);
    if (body.has_value && qa_shared_cluster_visible(p->scene, cluster, qa_shared_leaf_cluster(p->scene, qa_shared_point_leaf(p->scene, body.value.origin)), message->audience.scope)) (*out)[kept++] = players[i];
  }
  return kept;
}
void qa_app_sim_native_mod_presentation_drain(presentation_t *p) {
  size_t count = 0; const qa_app_sim_classic_guest_message *messages = p->source.drain_messages(p->source.ctx, &count); if (count == 0) return;
  const qa_session_mod_engine_services *engine = engine_of(p); if (engine == NULL) fail("Native mod output requires presentation services");
  size_t config_count = 0; const qa_app_sim_configstring *configs = p->source.configstrings(p->source.ctx, &config_count);
  for (size_t i = 0; i < config_count; i++) config_set(p, configs[i].index, configs[i].value);
  qa_source_time time = now(p); double seconds = qa_compat_q2_time_seconds(time);
  for (size_t m = 0; m < count; m++) {
    qa_q2_server_record *records = NULL; size_t record_count = 0;
    if (!qa_q2_server_message_reader_read(&p->reader, messages[m].bytes, messages[m].length, &records, &record_count)) qa_range_error("Out of memory");
    qa_actor_id *targets = NULL; bool null_recipient = false; size_t target_count = recipients(p, &messages[m], &targets, &null_recipient);
    qa_q2_server_record *copy = xcalloc(record_count, sizeof *copy);
    for (size_t r = 0; r < target_count; r++) {
      translate_ctx t; memset(&t, 0, sizeof t); t.p = p; t.recipient = null_recipient ? NULL : &targets[r]; t.message = &messages[m]; t.time = time;
      if (t.recipient != NULL) t.has_slot = p->projection->slot_of(p->projection->ctx, *t.recipient, &t.slot);
      qa_app_q2_service_host host; memset(&host, 0, sizeof host);
      host.ctx = &t; host.has_edition = true; host.edition_rerelease = strcmp(p->source.edition, "rerelease") == 0; host.seconds = seconds;
      host.sound_config_offset = p->layout.sounds; host.image_config_offset = p->layout.images; host.player_skin_config_offset = p->layout.player_skins;
      host.content = t_content; host.next_sequence = t_sequence; host.player = t_player; host.actor = t_actor; host.entity = t_entity; host.fog = t_fog;
      host.config_string = t_config; host.set_config_string = t_set_config; host.set_inventory = t_inventory; host.set_layout = t_layout; host.emit = t_emit;
      if (record_count != 0) memcpy(copy, records, record_count * sizeof *copy);
      size_t remaining = qa_app_translate_q2_service_records(copy, record_count, &host);
      for (size_t k = 0; k < remaining; k++) {
        qa_q2_server_event_kind kind = copy[k].event.kind;
        if (kind == QA_Q2_SERVER_NOP) continue;
        /* The original service also sends broadcast text to its server console. */
        if (kind == QA_Q2_SERVER_PRINT && t.recipient == NULL) continue;
        if (kind == QA_Q2_SERVER_COMMAND_TEXT && engine->message != NULL) {
          qa_network_event event; memset(&event, 0, sizeof event); event.kind = QA_NETWORK_EVENT_COMMAND_TEXT; event.text = copy[k].event.text;
          engine->message(engine->ctx, event, t.recipient);
        } else { free(copy); free(targets); qa_q2_server_records_free(records, record_count); qa_raise("Error", 0, 0, "Native mod message %d needs a shared destination binding", (int)kind); }
      }
    }
    free(copy); free(targets); qa_q2_server_records_free(records, record_count);
  }
}

void qa_app_sim_native_mod_presentation_publish(presentation_t *p, const qa_app_sim_native_mod_actor_slot *actors, size_t count, bool events) {
  qa_app_sim_native_mod_presentation_drain(p);
  clear_frames(p);
  bool admitted = p->has_admission; free_snapshot(p);
  if (admitted) {
    size_t n = 0; const qa_app_sim_configstring *table = p->source.configstrings(p->source.ctx, &n);
    p->snapshot.entries = xcalloc(n, sizeof *p->snapshot.entries); p->snapshot.count = n;
    for (size_t i = 0; i < n; i++) { p->snapshot.entries[i].index = table[i].index; p->snapshot.entries[i].value = dup_text(table[i].value); }
  }
  qa_source_time time = now(p);
  /* Union of entity-event and loop owners that left the published set (TS Set over both key lists). */
  for (bool again = true; again;) {
    again = false;
    for (size_t i = 0; i < p->event_count + p->loop_count && !again; i++) {
      qa_actor_id actor = i < p->event_count ? p->events[i].actor : p->loops[i - p->event_count].actor; bool active = false;
      for (size_t a = 0; a < count && !active; a++) active = same_actor(actors[a].actor, actor);
      if (!active) { qa_app_sim_native_mod_presentation_release(p, actor); again = true; }
    }
  }
  for (size_t a = 0; a < count; a++) {
    qa_actor_id actor = actors[a].actor; double slot = actors[a].slot;
    qa_app_sim_native_mod_entity_state state = p->source.state(p->source.ctx, slot);
    if (!state.active || !is_live(p, actor)) { qa_app_sim_native_mod_presentation_release(p, actor); continue; }
    if (admitted && p->projection->accepts_client(p->projection->ctx, slot)) {
      qa_app_sim_native_mod_player_state player = p->source.player_state(p->source.ctx, slot);
      size_t received = find_index(p->clients, sizeof *p->clients, p->client_count, actor);
      p->frames = grow(p->frames, &p->frame_cap, p->frame_count, sizeof *p->frames);
      frame_row *row = &p->frames[p->frame_count++]; memset(row, 0, sizeof *row); row->actor = actor;
      qa_mod_client_presentation_frame *frame = &row->frame; frame->kind = QA_MOD_CLIENT_PRESENTATION_NATIVE;
      if (strcmp(p->admission.hud, "none") != 0) {
        frame->has_native_hud = true; frame->native_hud_mode = p->admission.hud;
        qa_native_q2_hud_frame *hud = &frame->native_hud_frame;
        hud->protocol.kind = player.rerelease ? "q2-rerelease" : "q2-classic"; hud->protocol.version = player.rerelease ? 1038 : 34;
        qa_double_view stats = player.rerelease ? player.rerelease_state.view.stats : player.classic.view.stats;
        row->stat_count = stats.count < 32 ? stats.count : 32;
        if (row->stat_count != 0) memcpy(row->stats, stats.values, row->stat_count * sizeof *row->stats);
        hud->stats = row->stats; hud->stat_count = row->stat_count;
        hud->configstrings = p->snapshot.entries; hud->configstring_count = p->snapshot.count;
        hud->layout = received < p->client_count ? p->clients[received].layout : "";
        if (received < p->client_count) { hud->inventory = p->clients[received].inventory; hud->inventory_count = p->clients[received].inventory_count; }
        hud->player_number = slot - 1;
        qa_app_sim_native_mod_clock clock = p->source.clock(p->source.ctx);
        hud->server_frame = clock.server_frame; hud->time_milliseconds = clock.time_milliseconds;
        hud->has_frame_time_milliseconds = clock.has_frame_time_milliseconds; hud->frame_time_milliseconds = clock.frame_time_milliseconds;
      }
      if (strcmp(p->admission.view, "none") != 0) {
        qa_app_sim_native_mod_camera_view camera = player.rerelease ? qa_app_sim_native_mod_camera_rerelease(player.rerelease_state) : qa_app_sim_native_mod_camera_classic(player.classic);
        frame->has_native_view = true; frame->native_view.view = camera.view; frame->native_view.edition = camera.native.edition;
        frame->native_view.movement_origin = camera.native.movement_origin; frame->native_view.render_flags = camera.native.render_flags;
        frame->native_view.position_prediction = camera.native.position_prediction; frame->native_view.angular_prediction = camera.native.angular_prediction;
        frame->native_view.weapon_visible = camera.native.weapon_visible;
      }
    }
    if (!state.visible) {
      size_t l = find_index(p->loops, sizeof *p->loops, p->loop_count, actor); if (l < p->loop_count) stop(p, l);
      size_t e = find_index(p->events, sizeof *p->events, p->event_count, actor); if (e < p->event_count) remove_at(p->events, sizeof *p->events, &p->event_count, e);
      continue;
    }
    size_t e = find_index(p->events, sizeof *p->events, p->event_count, actor);
    if (events && state.event != 0 && (e == p->event_count || p->events[e].event != state.event)) {
      qa_q2_presentation_event payload; memset(&payload, 0, sizeof payload); payload.kind = "entity-event"; payload.has_actor = true; payload.actor = actor; payload.event = state.event;
      emit_q2(p, &payload, time, NULL);
    }
    if (e == p->event_count) { p->events = grow(p->events, &p->event_cap, p->event_count, sizeof *p->events); p->events[e].actor = actor; p->event_count++; }
    p->events[e].event = state.event;
    size_t l = find_index(p->loops, sizeof *p->loops, p->loop_count, actor);
    if (state.sound == 0) { if (l < p->loop_count) stop(p, l); continue; }
    size_t n = 0; const qa_app_sim_configstring *table = p->source.configstrings(p->source.ctx, &n); const char *path = NULL;
    for (size_t i = 0; i < n; i++) if (table[i].index == p->layout.sounds + state.sound) path = table[i].value;
    if (path == NULL || path[0] == '\0') qa_raise("Error", 0, 0, "Native mod loop sound %d has no configstring", (int)state.sound);
    bool rerelease = strcmp(p->source.edition, "rerelease") == 0;
    double volume = rerelease && state.volume == 0 ? 1 : state.volume;
    double attenuation = !rerelease ? state.attenuation : state.attenuation == -1 ? 0 : state.attenuation > 0 && state.attenuation != 3 ? state.attenuation / 5 : 1;
    qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, actor); qa_vec3 origin = body.has_value ? body.value.origin : state.origin;
    if (l < p->loop_count && strcmp(p->loops[l].path, path) == 0 && p->loops[l].volume == volume && p->loops[l].attenuation == attenuation) { p->loops[l].origin = origin; continue; }
    if (l < p->loop_count) stop(p, l);
    p->loops = grow(p->loops, &p->loop_cap, p->loop_count, sizeof *p->loops);
    loop_row *next = &p->loops[p->loop_count++]; next->actor = actor; next->path = dup_text(path); next->volume = volume; next->attenuation = attenuation; next->origin = origin;
    sound(p, actor, next, "start");
  }
}
const qa_app_simulation_presentation *qa_app_sim_native_mod_presentation_appearance(presentation_t *p, qa_actor_id actor, double slot, size_t *out_count) {
  qa_app_sim_native_mod_appearance source = p->source.appearance(p->source.ctx, slot);
  qa_body_state_result body = qa_shared_body_table_read(p->services->bodies, actor);
  size_t count = 1; for (size_t i = 0; i < source.attached_model_count && i < 3; i++) if (source.attached_models[i][0] != '\0') count++;
  if (count > p->presentation_cap) {
    free(p->payloads); p->payloads = xcalloc(count, sizeof *p->payloads); p->presentation_cap = count;
  }
  qa_app_simulation_presentation base; memset(&base, 0, sizeof base);
  base.actor = actor; base.content = p->content; base.family = QA_GAME_FAMILY_Q2; base.path = source.path; base.skin = source.skin; base.has_skin_path = source.skin_path != NULL; base.skin_path = source.skin_path;
  base.frame = source.frame; base.old_frame = source.old_frame; base.effects = source.effects; base.render_flags = source.render_flags; base.scale = source.scale; base.has_alpha = true; base.alpha = source.alpha;
  base.visible = source.visible; base.origin = body.has_value ? body.value.origin : source.origin; base.angles = body.has_value ? body.value.angles : source.angles; base.view_weapon = false;
  p->payloads[0] = base; size_t k = 1;
  for (size_t i = 0; i < source.attached_model_count && i < 3; i++) {
    if (source.attached_models[i][0] == '\0') continue;
    free(p->attached_paths[k - 1]); p->attached_paths[k - 1] = dup_text(source.attached_models[i]);
    p->payloads[k] = base; p->payloads[k].path = p->attached_paths[k - 1]; p->payloads[k].skin = 0; p->payloads[k].has_skin_path = false; p->payloads[k].skin_path = NULL; k++;
  }
  *out_count = count; return p->payloads;
}
const char *qa_app_sim_native_mod_presentation_signature(presentation_t *p, double slot) { return p->source.signature(p->source.ctx, slot); }
void qa_app_sim_native_mod_presentation_capture(presentation_t *p, qa_app_sim_native_mod_presentation_checkpoint *out) {
  memset(out, 0, sizeof *out); out->fog = xcalloc(p->fog_count, sizeof *out->fog); out->clients = xcalloc(p->client_count, sizeof *out->clients);
  for (size_t i = 0; i < p->fog_count; i++) if (is_live(p, p->fog[i].actor)) { out->fog[out->fog_count].actor = qa_saved_actor_id_of(p->fog[i].actor); out->fog[out->fog_count++].value = p->fog[i].value; }
  for (size_t i = 0; i < p->client_count; i++) if (is_live(p, p->clients[i].actor)) {
    qa_app_sim_native_mod_client_entry *c = &out->clients[out->client_count++]; c->actor = qa_saved_actor_id_of(p->clients[i].actor); c->layout = p->clients[i].layout;
    c->inventory = xcalloc(p->clients[i].inventory_count, sizeof *c->inventory); if (p->clients[i].inventory_count != 0) memcpy(c->inventory, p->clients[i].inventory, p->clients[i].inventory_count * sizeof *c->inventory); c->inventory_count = p->clients[i].inventory_count;
  }
}
static qa_actor_id reference(presentation_t *p, qa_saved_actor_id saved) {
  return p->services->reference_saved != NULL ? p->services->reference_saved(p->services->ctx, saved) : qa_session_actor_registry_reference_saved(p->services->actors, saved, "current");
}
void qa_app_sim_native_mod_presentation_restore(presentation_t *p, const qa_app_sim_native_mod_presentation_checkpoint *saved) {
  qa_actor_id *fog = xcalloc(saved->fog_count, sizeof *fog);
  for (size_t i = 0; i < saved->fog_count; i++) fog[i] = reference(p, saved->fog[i].actor);
  for (size_t i = 0; i < saved->fog_count; i++) if (!is_live(p, fog[i])) { free(fog); fail("Native mod fog recipient is unavailable"); }
  qa_app_sim_native_mod_presentation_close(p);
  size_t drained; (void)p->source.drain_messages(p->source.ctx, &drained);
  p->fog_count = 0; for (size_t i = 0; i < p->config_count; i++) free(p->configs[i].value);
  p->config_count = 0;
  for (size_t i = 0; i < saved->fog_count; i++) {
    size_t at = find_index(p->fog, sizeof *p->fog, p->fog_count, fog[i]);
    if (at == p->fog_count) { p->fog = grow(p->fog, &p->fog_cap, p->fog_count, sizeof *p->fog); p->fog[at].actor = fog[i]; p->fog_count++; }
    p->fog[at].value = saved->fog[i].value;
  }
  free(fog);
  for (size_t i = 0; i < saved->client_count; i++) {
    qa_actor_id actor = reference(p, saved->clients[i].actor);
    if (!is_live(p, actor)) fail("Native client presentation recipient is unavailable");
    client_row *c = client_messages(p, &actor); free(c->layout); free(c->inventory);
    c->layout = dup_text(saved->clients[i].layout); c->inventory = xcalloc(saved->clients[i].inventory_count, sizeof *c->inventory);
    if (saved->clients[i].inventory_count != 0) memcpy(c->inventory, saved->clients[i].inventory, saved->clients[i].inventory_count * sizeof *c->inventory);
    c->inventory_count = saved->clients[i].inventory_count;
  }
}
