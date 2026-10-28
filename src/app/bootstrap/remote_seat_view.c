/* Port of src/app/bootstrap/remote-seat-view.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_seat_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/remote_components.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
struct qa_app_remote_seat_view {
  qa_app_remote_seat_view_host host;
  qa_app_remote_seat_source *source;
  qa_app_remote_component_view *components;
  char **reported; size_t reported_count, reported_cap;
  bool closed;
  const void **sound_scratch; size_t sound_cap;
};
static bool components_pending(void *ctx) { (void)ctx; return false; }
static void components_noop(void *ctx) { (void)ctx; }
static void components_dispatch(void *ctx, const void *request) { (void)ctx; (void)request; }
static void components_prepare(void *ctx, const void *presentation, const void *const *events, size_t count, double frame) {
  (void)ctx; (void)presentation; (void)events; (void)count; (void)frame;
}
static void components_audio(void *ctx, const void *const *events, size_t count) { (void)ctx; (void)events; (void)count; }
static bool components_owner(void *ctx, const void *event) { (void)ctx; (void)event; return false; }
qa_app_remote_seat_view *qa_app_remote_seat_view_prepare(qa_app_remote_seat_view_host host, qa_app_remote_seat_source *source) {
  qa_app_remote_seat_view *v = calloc(1, sizeof *v);
  if (!v) return NULL;
  v->host = host; v->source = source;
  const char *error = NULL;
  host.assert_current(host.ctx);
  if (host.owns_assets) error = host.load_assets_world(host.ctx);
  if (error == NULL && host.owns_assets) error = host.load_console_font(host.ctx);
  if (error == NULL) error = host.load_menu_typography(host.ctx);
  if (error == NULL) { host.assert_current(host.ctx); error = host.preload_effects(host.ctx); }
  if (error == NULL) { host.assert_current(host.ctx); error = host.create_ui(host.ctx); }
  if (error == NULL && host.remote_is_q3) {
    if (!host.has_q3_guest) {
      host.close_ui(host.ctx);
      host.close_effects(host.ctx);
      if (host.owns_assets) host.close_assets(host.ctx);
      free(v);
      qa_range_error("Q3 remote seat requires its admitted guest owners");
    }
    int32_t angles[3] = {0, 0, 0};
    bool has_command = host.q3_command_angles(host.ctx, angles);
    if (!has_command) host.set_view_angles(host.ctx, 0, 0, 0);
    else {
      double decoded[3];
      for (int i = 0; i < 3; i++) decoded[i] = (double)(int16_t)angles[i] * (360.0 / 65536.0);
      host.set_view_angles(host.ctx, decoded[0], decoded[1], decoded[2]);
    }
    error = host.create_q3_client(host.ctx);
    if (error == NULL) host.assert_current(host.ctx);
  }
  if (error == NULL && host.remote_is_unified && host.character_is_q3) error = host.load_q3_character(host.ctx);
  if (error == NULL) { host.assert_current(host.ctx); error = host.create_presentation(host.ctx); }
  if (error != NULL) {
    if (host.remote_is_q3 && host.has_q3_guest) (void)host.shutdown_q3_client(host.ctx);
    host.close_ui(host.ctx);
    host.close_effects(host.ctx);
    if (host.owns_assets) host.close_assets(host.ctx);
    free(v);
    qa_range_error("%s", error);
  }
  host.publish_layout(host.ctx, host.index(host.ctx), host.count(host.ctx));
  host.validate_presentation(host.ctx);
  if (host.remote_is_unified) {
    qa_app_remote_component_host component_host; memset(&component_host, 0, sizeof component_host);
    component_host.ctx = host.ctx;
    component_host.clients_pending_commands = components_pending;
    component_host.clients_dispatch_command = components_dispatch;
    component_host.clients_prepare = components_prepare;
    component_host.clients_close = components_noop;
    component_host.prepare_audio = components_audio;
    component_host.prepare_shaders = components_noop;
    component_host.event_is_presentation_owner = components_owner;
    v->components = qa_app_remote_component_view_create(component_host);
  }
  return v;
}
void qa_app_remote_seat_view_destroy(qa_app_remote_seat_view *v) {
  if (!v) return;
  qa_app_remote_seat_view_close(v);
  if (v->components) qa_app_remote_component_view_destroy(v->components);
  for (size_t i = 0; i < v->reported_count; i++) free(v->reported[i]);
  free(v->reported); free(v->sound_scratch); free(v);
}
void qa_app_remote_seat_view_publish(qa_app_remote_seat_view *v, double index, double count) {
  v->host.assert_current(v->host.ctx);
  v->host.publish_layout(v->host.ctx, index, count);
  v->host.attach_presentation(v->host.ctx);
}
qa_app_remote_seat_audio_events qa_app_remote_seat_view_prepare_frame(qa_app_remote_seat_view *v, qa_simulation_output output, bool music) {
  if (v->closed) qa_range_error("Remote seat view is closed");
  qa_app_remote_seat_view_host *host = &v->host;
  size_t event_count = 0;
  void *const *events = host->drain_presentation_events(host->ctx, &event_count);
  if (host->remote_is_unified) {
    size_t sim_count = 0;
    void *const *sim_events = host->drain_simulation_events(host->ctx, &sim_count);
    host->seat_receive(host->ctx, sim_events, sim_count);
  }
  size_t model_count = 0, character_count = 0;
  (void)host->remote_presentations(host->ctx, &model_count);
  (void)host->remote_character_views(host->ctx, &character_count);
  host->effects_receive(host->ctx, events, event_count);
  const char *error = NULL;
  if (!host->remote_is_q3) error = host->effects_prepare(host->ctx, output.snapshot);
  if (error != NULL) qa_range_error("%s", error);
  const char **keys = NULL, **kinds = NULL, **reasons = NULL;
  size_t unhandled = host->effects_drain_unhandled(host->ctx, &keys, &kinds, &reasons);
  for (size_t i = 0; i < unhandled; i++) {
    bool seen = false;
    for (size_t j = 0; j < v->reported_count; j++) if (strcmp(v->reported[j], keys[i]) == 0) seen = true;
    if (seen) continue;
    if (v->reported_count == v->reported_cap) { v->reported_cap = v->reported_cap ? v->reported_cap * 2 : 8; v->reported = realloc(v->reported, v->reported_cap * sizeof *v->reported); if (!v->reported) qa_range_error("Out of memory"); }
    v->reported[v->reported_count++] = qa_input_dup(keys[i]);
    char buf[2048];
    char *player = qa_input_js_number(host->seat_index(host->ctx) + 1);
    snprintf(buf, sizeof buf, "Player %s: unresolved %s effect: %s\n", player, kinds[i], reasons[i]);
    free(player);
    host->print(host->ctx, buf);
  }
  if (v->components != NULL)
    qa_app_remote_component_view_prepare_media(v->components, (const void *const *)events, event_count, music);
  host->presentation_source_events(host->ctx, events, event_count);
  error = host->presentation_prepare(host->ctx, output.snapshot);
  if (error != NULL) qa_range_error("%s", error);
  if (v->components != NULL)
    qa_app_remote_component_view_prepare(v->components, NULL, (const void *const *)events, event_count, output.snapshot.frame.frame);
  size_t sound_count = 0, recipient_count = 0;
  const void *const *sounds = host->effects_drain_sounds(host->ctx, &sound_count);
  const void **recipient_sounds = NULL;
  recipient_count = host->effects_drain_recipient_sounds(host->ctx, host->player_actor(host->ctx), &recipient_sounds);
  if (v->sound_cap < sound_count + recipient_count) {
    v->sound_cap = sound_count + recipient_count;
    v->sound_scratch = realloc(v->sound_scratch, v->sound_cap * sizeof *v->sound_scratch);
    if (!v->sound_scratch && v->sound_cap) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < sound_count; i++) v->sound_scratch[i] = sounds[i];
  for (size_t i = 0; i < recipient_count; i++) v->sound_scratch[sound_count + i] = recipient_sounds[i];
  qa_app_remote_seat_audio_events out; memset(&out, 0, sizeof out);
  out.seat = qa_session_seat_id(host->seat);
  out.snapshot = output.snapshot;
  out.events = events; out.event_count = event_count;
  out.scene = host->remote_scene(host->ctx);
  out.music = false;
  out.effect_sounds = v->sound_scratch;
  out.effect_sound_count = sound_count + recipient_count;
  return out;
}
void qa_app_remote_seat_view_close(qa_app_remote_seat_view *v) {
  if (v->closed) return;
  v->closed = true;
  bool failed = false;
  if (v->components) qa_app_remote_component_view_close(v->components);
  if (v->host.shutdown_q3_client && v->host.remote_is_q3 && v->host.shutdown_q3_client(v->host.ctx) != NULL) failed = true;
  if (v->host.seat_presentation_is_this(v->host.ctx)) v->host.clear_seat_presentation(v->host.ctx);
  else v->host.close_presentation(v->host.ctx);
  v->host.close_effects(v->host.ctx);
  if (v->host.owns_assets) v->host.close_assets(v->host.ctx);
  if (failed) qa_range_error("Remote seat view retirement failed");
}
