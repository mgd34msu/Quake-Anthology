/* Port of src/app/bootstrap/component-media.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_media.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/base/rules.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/world/session/mod_presentations.h"

static bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Out of memory"); return p; }
static char *xstrdup(const char *s) { size_t n = strlen(s) + 1; char *r = xmalloc(n); memcpy(r, s, n); return r; }

bool qa_app_presentation_audio_control(const qa_app_sim_presentation_event *source) {
  return eq(source->kind, "presentation-owner") || eq(source->kind, "music")
      || (eq(source->kind, "q2") && eq(source->q2->kind, "music"))
      || (eq(source->kind, "q1-level") && eq(source->q1_level->kind, "finale"));
}

static bool retained_kind(const qa_app_retained_presentation *request, const char *kind) {
  return request->local ? eq("local-media", kind) : eq(request->simulation.kind, kind);
}
static double retained_sequence(const qa_app_retained_presentation *request) {
  return request->local ? request->local_media.sequence : request->simulation.sequence;
}
static bool local_shader_remap(const qa_app_retained_presentation *request) {
  return request->local && request->local_media.event.kind == QA_MEDIA_REQUEST_SHADER_REMAP;
}

/* One row of TS `ordered`: a source control (event) or a pending request
 * (retained, the members of TS `local`). */
typedef struct ordered_row {
  const qa_app_sim_presentation_event *event;
  const qa_app_retained_presentation *retained;
  double sequence;
} ordered_row;
static int by_sequence(const void *a, const void *b) {
  double x = ((const ordered_row *)a)->sequence, y = ((const ordered_row *)b)->sequence;
  return x < y ? -1 : x > y ? 1 : 0;
}

typedef struct local_current { qa_app_component_media_host *host; const qa_app_retained_presentation *request; } local_current;
static bool local_media_current(void *ctx) {
  local_current *current = ctx;
  return current->host->local_media_current(current->host->ctx, current->request);
}

const char *qa_app_prepare_presentation_audio(qa_app_component_media_host host,
                                              const qa_app_sim_presentation_event *source, size_t source_count,
                                              const qa_app_audio_seat_events *seats, size_t seat_count) {
  size_t pending_count = 0;
  const qa_app_retained_presentation *const *pending = host.pending_local_media(host.ctx, &pending_count);
  ordered_row *ordered = NULL;
  size_t count = 0, cap = source_count + pending_count;
  if (cap > 0) ordered = xmalloc(cap * sizeof *ordered);
  for (size_t i = 0; i < source_count; i++)
    if (qa_app_presentation_audio_control(&source[i])) ordered[count++] = (ordered_row){&source[i], NULL, source[i].sequence};
  for (size_t i = 0; i < pending_count; i++) {
    if (local_shader_remap(pending[i])) continue;
    ordered[count++] = (ordered_row){NULL, pending[i], retained_sequence(pending[i])};
  }
  qa_js_sort(ordered, count, sizeof *ordered, by_sequence);
  const char *error = NULL;
  qa_audio_audience world = {0};
  world.kind = QA_AUDIO_AUDIENCE_WORLD;
  for (size_t i = 0; i < count && error == NULL; i++) {
    const qa_app_retained_presentation *retained = ordered[i].retained;
    double sequence = ordered[i].sequence;
    bool owner_control = retained != NULL ? retained_kind(retained, "presentation-owner") : eq(ordered[i].event->kind, "presentation-owner");
    if (!owner_control && sequence < host.applied_media_sequence(host.ctx)) {
      if (retained != NULL) host.acknowledge_local_media(host.ctx, retained, true);
      continue;
    }
    if (retained != NULL) {
      if (!host.local_media_current(host.ctx, retained)) continue;
      if (retained->local) {
        if (retained->local_media.event.kind == QA_MEDIA_REQUEST_SHADER_REMAP) continue;
        local_current current = {&host, retained};
        error = host.play_component_media(host.ctx, &retained->local_media, local_media_current, &current);
      } else error = host.audio_receive(host.ctx, &retained->simulation, 1, world, true);
      if (error != NULL) break;
      host.acknowledge_local_media(host.ctx, retained, true);
    } else {
      error = host.audio_receive(host.ctx, ordered[i].event, 1, world, true);
      if (error != NULL) break;
    }
    if (!owner_control) host.applied_media(host.ctx, sequence);
  }
  free(ordered);
  for (size_t i = 0; i < seat_count && error == NULL; i++) {
    const qa_app_audio_seat_events *batch = &seats[i];
    qa_app_sim_presentation_event *controls = NULL;
    size_t control_count = 0;
    if (batch->event_count > 0) controls = xmalloc(batch->event_count * sizeof *controls);
    for (size_t j = 0; j < batch->event_count; j++)
      if (qa_app_presentation_audio_control(&batch->events[j])) controls[control_count++] = batch->events[j];
    qa_audio_audience audience = {0};
    audience.kind = QA_AUDIO_AUDIENCE_SEAT; audience.seat = batch->seat;
    error = host.audio_receive(host.ctx, controls, control_count, audience, batch->music);
    free(controls);
  }
  return error;
}

const char *qa_app_prepare_presentation_shaders(qa_app_component_media_host host) {
  for (;;) {
    size_t pending_count = 0;
    const qa_app_retained_presentation *const *pending_all = host.pending_local_media(host.ctx, &pending_count);
    const qa_app_retained_presentation *pending = NULL;
    for (size_t i = 0; i < pending_count; i++)
      if (local_shader_remap(pending_all[i])) { pending = pending_all[i]; break; }
    if (pending == NULL) return NULL;
    if (!pending->local) qa_range_error("Shader delivery requires local media");
    const qa_app_retained_presentation *request = host.resolve_shader_replay(host.ctx, pending);
    if (request == NULL) continue;
    if (request->local_media.event.kind != QA_MEDIA_REQUEST_SHADER_REMAP) qa_range_error("Shader delivery requires a material cue");
    local_current current = {&host, request};
    if (!local_media_current(&current)) { host.acknowledge_local_media(host.ctx, request, false); continue; }
    const qa_component_presentation_media_request *cue = &request->local_media.event;
    qa_scene_shader_registry *shaders;
    if (eq(cue->original, cue->replacement)) shaders = host.world(host.ctx)->shaders;
    else {
      const char *error = NULL;
      shaders = host.provider_shaders(host.ctx, request->local_media.content, &error);
      if (shaders == NULL) return error;
    }
    if (!local_media_current(&current)) { host.acknowledge_local_media(host.ctx, request, false); continue; }
    qa_shader_remap_result result = qa_world_scene_remap_shader(host.world(host.ctx), cue->original, cue->replacement,
                                                                cue->time_offset, shaders, local_media_current, &current);
    if (result == QA_REMAP_STALE && local_media_current(&current))
      qa_range_error("Shader destination changed before local media delivery");
    /* Read before acknowledging: the acknowledgement may release request. */
    char *original = xstrdup(cue->original);
    double sequence = request->local_media.sequence;
    host.acknowledge_local_media(host.ctx, request, result == QA_REMAP_COMMITTED);
    if (result != QA_REMAP_STALE) host.applied_shader(host.ctx, original, sequence);
    free(original);
  }
}

typedef struct shader_current {
  qa_app_media_current_fn source; void *source_ctx;
  qa_app_media_current_fn consumer; void *consumer_ctx;
} shader_current;
static bool shader_control_current(void *ctx) {
  shader_current *current = ctx;
  return current->source(current->source_ctx) && current->consumer(current->consumer_ctx);
}

void qa_app_primary_shader_control_enable(qa_app_component_media_host host) { host.enable_local_media(host.ctx); }

const char *qa_app_primary_shader_control(qa_app_component_media_host host, qa_content_id content,
                                          qa_app_media_current_fn source_current, void *source_ctx,
                                          const char *original, const char *replacement, const char *offset,
                                          bool initializing, qa_app_media_current_fn consumer_current, void *consumer_ctx) {
  shader_current probe = {source_current, source_ctx, consumer_current, consumer_ctx};
  /* A consumer retired mid-dispatch (fast restart) drops the remap; its
   * replacement rebuilds shader state from the full configstrings at init. */
  if (!shader_control_current(&probe)) return NULL;
  /* The state keeps this closure with the pending request and frees it
   * through release_current when it drops the delivery. */
  shader_current *current = xmalloc(sizeof *current);
  *current = probe;
  double parsed = qa_js_parse_float(offset);
  qa_component_presentation_media_request request = {0};
  request.kind = QA_MEDIA_REQUEST_SHADER_REMAP;
  request.original = original; request.replacement = replacement;
  request.time_offset = isnan(parsed) ? 0 : parsed;
  host.publish_local_media(host.ctx, NULL, content, &request, initializing, shader_control_current, current, free);
  return qa_app_prepare_presentation_shaders(host);
}

void qa_app_component_media_control_enable(qa_app_component_media_host host) { host.enable_local_media(host.ctx); }

const char *qa_app_component_media_control(qa_app_component_media_host host, const struct qa_active_mod_presentation *source,
                                           const qa_component_presentation_media_request *request, bool initializing,
                                           qa_app_media_current_fn current, void *current_ctx) {
  if (!current(current_ctx)) return NULL;
  bool shader = request->kind == QA_MEDIA_REQUEST_SHADER_REMAP;
  host.publish_local_media(host.ctx, &source->owner, source->identity.source.content, request, initializing,
                           shader ? current : NULL, shader ? current_ctx : NULL, NULL);
  if (shader) return qa_app_prepare_presentation_shaders(host);
  return qa_app_prepare_presentation_audio(host, NULL, 0, NULL, 0);
}
