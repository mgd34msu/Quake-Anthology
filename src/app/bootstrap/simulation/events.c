/* Port of src/app/bootstrap/simulation/events.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_sim_simulation_events {
  qa_app_sim_simulation_events_options options;
  double sequence;
  qa_simulation_event emitted[QA_APP_SIM_SIMULATION_EVENT_CAPACITY];
  qa_actor_id payload_actors[QA_APP_SIM_SIMULATION_EVENT_CAPACITY];
  /* TS event strings are values: each slot owns copies of its message's text,
   * value, program and reason (and counts), released when the slot is reused. */
  char *payload_strings[QA_APP_SIM_SIMULATION_EVENT_CAPACITY][4];
  double *payload_counts[QA_APP_SIM_SIMULATION_EVENT_CAPACITY];
  size_t emitted_count;
};
static const char *own_string(char **slot, const char *text) {
  free(*slot); *slot = NULL;
  if (text == NULL) return NULL;
  *slot = strdup(text);
  if (*slot == NULL) qa_range_error("Out of memory");
  return *slot;
}
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static qa_source_time now_of(qa_app_sim_simulation_events *e) { return e->options.now(e->options.ctx); }
qa_app_sim_simulation_events *qa_app_sim_simulation_events_create(qa_app_sim_simulation_events_options options) {
  qa_app_sim_simulation_events *e = calloc(1, sizeof *e);
  if (!e) qa_range_error("Out of memory");
  e->options = options;
  return e;
}
void qa_app_sim_simulation_events_destroy(qa_app_sim_simulation_events *events) {
  if (!events) return;
  for (size_t i = 0; i < QA_APP_SIM_SIMULATION_EVENT_CAPACITY; i++) {
    for (size_t j = 0; j < 4; j++) free(events->payload_strings[i][j]);
    free(events->payload_counts[i]);
  }
  free(events);
}
double qa_app_sim_simulation_events_next_sequence(const qa_app_sim_simulation_events *events) { return events->sequence; }
void qa_app_sim_simulation_events_append(qa_app_sim_simulation_events *events,
    qa_simulation_event_payload payload, bool has_actor, qa_actor_id actor) {
  qa_client_id client; bool has_client = false;
  if (has_actor) {
    has_client = events->options.client_for(events->options.ctx, actor, &client);
    if (!has_client) return;
  }
  if (events->emitted_count >= QA_APP_SIM_SIMULATION_EVENT_CAPACITY) {
    /* Sounds and transient effects are unreliable output; a full queue drops
     * them as the original servers drop overflowing datagrams. */
    if (payload.kind == QA_SIM_EVENT_SOUND || (payload.kind == QA_SIM_EVENT_MESSAGE
        && (payload.event.kind == QA_NETWORK_EVENT_SOUND || payload.event.kind == QA_NETWORK_EVENT_Q1_PARTICLE
            || payload.event.kind == QA_NETWORK_EVENT_Q2_MUZZLE_FLASH))) return;
    qa_range_error("Simulation event capacity exhausted (%d undrained events); publish output before admitting more",
        QA_APP_SIM_SIMULATION_EVENT_CAPACITY);
  }
  qa_simulation_event event; memset(&event, 0, sizeof event);
  event.sequence = events->sequence;
  event.time = now_of(events);
  event.payload = payload;
  if (event.payload.actor != NULL) {
    events->payload_actors[events->emitted_count] = *event.payload.actor;
    event.payload.actor = &events->payload_actors[events->emitted_count];
  }
  if (event.payload.kind == QA_SIM_EVENT_MESSAGE) {
    char **strings = events->payload_strings[events->emitted_count];
    qa_network_event *message = &event.payload.event;
    message->text = own_string(&strings[0], message->text);
    message->value = own_string(&strings[1], message->value);
    message->program = own_string(&strings[2], message->program);
    message->reason = own_string(&strings[3], message->reason);
    double **counts = &events->payload_counts[events->emitted_count];
    free(*counts); *counts = NULL;
    if (message->counts.count != 0) {
      *counts = malloc(message->counts.count * sizeof **counts);
      if (*counts == NULL) qa_range_error("Out of memory");
      memcpy(*counts, message->counts.values, message->counts.count * sizeof **counts);
      message->counts.values = *counts;
    }
  }
  event.audience.kind = has_client ? "client" : "world";
  if (has_client) event.audience.client = client;
  qa_simulation_event *stored = &events->emitted[events->emitted_count++];
  *stored = event;
  /* A sound's origin is a value too: keep it in the slot's own event. */
  if (stored->payload.kind == QA_SIM_EVENT_MESSAGE && stored->payload.event.origin != NULL) {
    stored->payload.event.origin_value = *stored->payload.event.origin;
    stored->payload.event.origin = &stored->payload.event.origin_value;
  }
  events->sequence++;
}
void qa_app_sim_simulation_events_message(qa_app_sim_simulation_events *events, qa_network_event event,
    bool has_actor, qa_actor_id actor, bool has_sequence, double source_presentation_sequence) {
  qa_simulation_event_payload payload; memset(&payload, 0, sizeof payload);
  payload.kind = QA_SIM_EVENT_MESSAGE;
  payload.event = event;
  payload.has_source_presentation_sequence = has_sequence;
  payload.source_presentation_sequence = source_presentation_sequence;
  qa_app_sim_simulation_events_append(events, payload, has_actor, actor);
}
const qa_simulation_event *qa_app_sim_simulation_events_take(qa_app_sim_simulation_events *events, size_t *out_count) {
  *out_count = events->emitted_count;
  events->emitted_count = 0;
  return events->emitted;
}
size_t qa_app_sim_simulation_events_drain(qa_app_sim_simulation_events *events,
    void (*visit)(void *ctx, qa_simulation_event event), void *visit_ctx) {
  size_t count = events->emitted_count;
  for (size_t i = 0; i < count; i++) visit(visit_ctx, events->emitted[i]);
  events->emitted_count = 0;
  return count;
}
void qa_app_sim_simulation_events_assert_output_consumed(qa_app_sim_simulation_events *events) {
  if (events->options.assert_super_output_consumed)
    events->options.assert_super_output_consumed(events->options.ctx);
  if (events->emitted_count != 0) qa_range_error("Save requires consumed source output");
}
double qa_app_sim_simulation_events_capture_sequence(const qa_app_sim_simulation_events *events) { return events->sequence; }
void qa_app_sim_simulation_events_restore(qa_app_sim_simulation_events *events, const qa_save_reader *reader) {
  qa_save_reader sequence;
  if (!qa_save_reader_field(reader, "sequence", &sequence)) qa_save_reader_fail(reader, "out of memory");
  events->sequence = qa_save_reader_integer_min(&sequence, 0);
  qa_save_reader_destroy(&sequence);
  events->emitted_count = 0;
}
static void sound_event(qa_app_sim_simulation_events *e, qa_content_id content, const char *path,
    bool has_actor, qa_actor_id actor, qa_vec3 origin, double channel, double volume, double attenuation,
    bool has_recipient, qa_actor_id recipient) {
  qa_resource_id resource = NULL;
  bool found = e->options.resource_for(e->options.ctx, content, path, &resource);
  if (!found) {
    char prefixed[1024];
    int written = snprintf(prefixed, sizeof prefixed, "sound/%s", path);
    if (written < 0 || (size_t)written >= sizeof prefixed) return;
    found = e->options.resource_for(e->options.ctx, content, prefixed, &resource);
  }
  if (!found) return;
  qa_simulation_event_payload payload; memset(&payload, 0, sizeof payload);
  payload.kind = QA_SIM_EVENT_SOUND;
  payload.resource = resource;
  if (has_actor) payload.actor = &actor; /* append copies into per-event storage */
  payload.origin = origin;
  payload.channel = channel;
  payload.volume = volume;
  payload.attenuation = attenuation;
  qa_app_sim_simulation_events_append(e, payload, has_recipient, recipient);
}
static void q1_event(qa_app_sim_simulation_events *e, qa_content_id content, const qa_q1_event *event,
    double sequence, bool has_recipient, qa_actor_id recipient) {
  if (is(event->kind, "sound")) {
    /* TS: typeof channel === "number" passes through (Q1SoundChannel
     * -1|5|6|7, encoded by C producers as decimal strings); named
     * channels map auto/weapon/voice/item, anything else is body (4). */
    double channel = 4;
    if (is(event->sound_channel, "auto")) channel = 0;
    else if (is(event->sound_channel, "weapon")) channel = 1;
    else if (is(event->sound_channel, "voice")) channel = 2;
    else if (is(event->sound_channel, "item")) channel = 3;
    else if (event->sound_channel != NULL) {
      char *end = NULL;
      long numeric = strtol(event->sound_channel, &end, 10);
      if (end != event->sound_channel && end != NULL && *end == '\0') channel = (double)numeric;
    }
    qa_body_state_result body = qa_shared_body_table_read(e->options.bodies, event->actor);
    qa_vec3 center = {0, 0, 0};
    if (body.has_value) {
      /* SV_StartSound origin capture with TS Math.fround staging. */
      center.x = (float)(body.value.origin.x + (float)(body.value.bounds.min.x + body.value.bounds.max.x) * 0.5);
      center.y = (float)(body.value.origin.y + (float)(body.value.bounds.min.y + body.value.bounds.max.y) * 0.5);
      center.z = (float)(body.value.origin.z + (float)(body.value.bounds.min.z + body.value.bounds.max.z) * 0.5);
    }
    sound_event(e, content, event->path, event->has_actor, event->actor,
        event->has_origin ? event->origin : center, channel, event->volume, event->attenuation,
        has_recipient, recipient);
  } else if (is(event->kind, "ambient")) {
    sound_event(e, content, event->path, false, event->actor, event->origin, 0, event->volume,
        event->attenuation, has_recipient, recipient);
  } else if (is(event->kind, "message")) {
    qa_network_event message; memset(&message, 0, sizeof message);
    message.kind = event->center ? QA_NETWORK_EVENT_CENTER_PRINT : QA_NETWORK_EVENT_PRINT;
    message.level = 2;
    message.text = event->text;
    qa_app_sim_simulation_events_message(e, message, event->has_player, event->player, true, sequence);
  }
}
static void q2_event(qa_app_sim_simulation_events *e, qa_content_id content,
    const qa_q2_presentation_event *event, double sequence) {
  if (is(event->kind, "sound")) {
    sound_event(e, content, event->path, event->has_actor, event->actor, event->origin,
        event->channel, event->volume, event->attenuation, false, event->actor);
  } else if (is(event->kind, "centerprint")) {
    qa_network_event message; memset(&message, 0, sizeof message);
    message.kind = QA_NETWORK_EVENT_CENTER_PRINT;
    message.text = event->text;
    qa_app_sim_simulation_events_message(e, message, event->has_actor, event->actor, true, sequence);
  } else if (is(event->kind, "help")) {
    qa_network_event message; memset(&message, 0, sizeof message);
    message.kind = QA_NETWORK_EVENT_PRINT;
    message.level = 2;
    message.text = event->text;
    qa_app_sim_simulation_events_message(e, message, false, event->actor, true, sequence);
  }
}
qa_app_sim_presentation_event qa_app_sim_simulation_events_emit_owned(qa_app_sim_simulation_events *events,
    bool has_owner, qa_presentation_owner owner, qa_content_id content, qa_app_sim_source_event_input source,
    bool has_time, qa_source_time time, bool has_recipient, qa_actor_id recipient) {
  qa_app_sim_presentation_event presentation; memset(&presentation, 0, sizeof presentation);
  events->options.emit_super(events->options.ctx, has_owner, owner, content, source,
      has_time ? time : now_of(events), has_recipient, recipient, &presentation);
  if (is(source.kind, "q2-composition") && source.q2_composition != NULL
      && (source.q2_composition->kind == QA_Q2_COMPOSITION_EVENT_CTF
          || source.q2_composition->kind == QA_Q2_COMPOSITION_EVENT_LMCTF)) {
    bool lmctf = source.q2_composition->kind == QA_Q2_COMPOSITION_EVENT_LMCTF;
    const char *kind = lmctf ? source.q2_composition->lmctf->kind : source.q2_composition->ctf->kind;
    const char *layout = lmctf ? source.q2_composition->lmctf->layout : source.q2_composition->ctf->layout;
    qa_actor_id actor = lmctf ? source.q2_composition->lmctf->actor : source.q2_composition->ctf->actor;
    /* TS `"layout" in event`: the lmctf hud event carries a layout. */
    if (is(kind, "scoreboard") || (lmctf && is(kind, "hud") && layout != NULL)) {
      qa_network_event message; memset(&message, 0, sizeof message);
      message.kind = QA_NETWORK_EVENT_Q2_LAYOUT;
      message.program = layout;
      qa_app_sim_simulation_events_message(events, message, true, actor, false, 0);
    } else if (!lmctf && is(kind, "match-status")) {
      qa_network_event message; memset(&message, 0, sizeof message);
      message.kind = QA_NETWORK_EVENT_PRINT;
      message.level = 2;
      message.text = source.q2_composition->ctf->text;
      qa_app_sim_simulation_events_message(events, message, false, actor, false, 0);
    }
  }
  if (is(source.kind, "q1") && source.q1 != NULL) {
    q1_event(events, content, source.q1, presentation.sequence, has_recipient, recipient);
  } else if (is(source.kind, "q2") && source.q2 != NULL) {
    q2_event(events, content, source.q2, presentation.sequence);
  } else if (is(source.kind, "q2-weapon") && source.q2_weapon != NULL
             && source.q2_weapon->kind == QA_Q2W_EVENT_MUZZLEFLASH && source.q2_weapon->actor != NULL) {
    double entity_number = 0;
    if (events->options.source_slot(events->options.ctx, *source.q2_weapon->actor, &entity_number)) {
      qa_network_event message; memset(&message, 0, sizeof message);
      message.kind = QA_NETWORK_EVENT_Q2_MUZZLE_FLASH;
      message.entity_number = entity_number;
      message.flash = source.q2_weapon->flash;
      message.monster = false;
      qa_app_sim_simulation_events_message(events, message, false, *source.q2_weapon->actor, false, 0);
    }
  }
  return presentation;
}
void qa_app_sim_simulation_events_emit(qa_app_sim_simulation_events *events, qa_content_id content,
    qa_app_sim_source_event_input source, bool has_time, qa_source_time time, bool has_recipient, qa_actor_id recipient) {
  qa_presentation_owner owner; memset(&owner, 0, sizeof owner);
  (void)qa_app_sim_simulation_events_emit_owned(events, false, owner, content, source, has_time, time, has_recipient, recipient);
}
void qa_app_sim_simulation_events_register_resource(qa_app_sim_simulation_events *events, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource) {
  if (resource.requested_path == NULL || path == NULL || strcmp(resource.requested_path, path) != 0)
    qa_raise("Error", 0, 0, "Registered resource path does not match its source request");
  if (events->options.register_resource_super)
    events->options.register_resource_super(events->options.ctx, content, path, resource);
}
const char *qa_app_sim_simulation_events_light_style(qa_app_sim_simulation_events *events, double style) {
  const char *pattern = events->options.light_style_super ? events->options.light_style_super(events->options.ctx, style) : NULL;
  return pattern ? pattern : "";
}
