/* Port of src/app/bootstrap/component-media.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/component_media.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static bool eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
bool qa_app_presentation_audio_control(const qa_app_media_event *source) {
  return eq(source->kind, "presentation-owner") || eq(source->kind, "music") ||
      (eq(source->kind, "q2") && eq(source->event_kind, "music")) ||
      (eq(source->kind, "q1-level") && eq(source->event_kind, "finale"));
}
typedef struct { const qa_app_media_event *event; bool local; } ordered_row;
static int by_sequence(const void *a, const void *b) {
  double x = ((const ordered_row *)a)->event->sequence, y = ((const ordered_row *)b)->event->sequence;
  return x < y ? -1 : x > y ? 1 : 0;
}
const char *qa_app_prepare_presentation_audio(qa_app_component_media_host host,
                                              const qa_app_media_event *source, size_t source_count,
                                              const qa_app_media_seat_events *seats, size_t seat_count) {
  size_t pending_count = 0;
  const qa_app_media_event *pending_all = host.pending_local_media(host.ctx, &pending_count);
  ordered_row *ordered = NULL;
  size_t count = 0, cap = 0;
  for (size_t i = 0; i < source_count; i++)
    if (qa_app_presentation_audio_control(&source[i])) {
      if (count == cap) { cap = cap ? cap * 2 : 16; ordered = realloc(ordered, cap * sizeof *ordered); }
      ordered[count++] = (ordered_row){&source[i], false};
    }
  size_t local_from = count;
  for (size_t i = 0; i < pending_count; i++) {
    const qa_app_media_event *request = &pending_all[i];
    if (eq(request->kind, "local-media") && eq(request->event_kind, "shader-remap")) continue;
    if (count == cap) { cap = cap ? cap * 2 : 16; ordered = realloc(ordered, cap * sizeof *ordered); }
    ordered[count++] = (ordered_row){request, true};
  }
  (void)local_from;
  qsort(ordered, count, sizeof *ordered, by_sequence);
  const char *error = NULL;
  for (size_t i = 0; i < count && !error; i++) {
    const qa_app_media_event *request = ordered[i].event;
    bool local = ordered[i].local;
    if (!eq(request->kind, "presentation-owner") && request->sequence < host.applied_media_sequence(host.ctx)) {
      if (local) host.acknowledge_local_media_default(host.ctx, request);
      continue;
    }
    if (local) {
      if (!host.local_media_current(host.ctx, request)) continue;
      if (eq(request->kind, "local-media")) {
        if (eq(request->event_kind, "shader-remap")) continue;
        error = host.play_component_media(host.ctx, request);
      } else error = host.audio_receive(host.ctx, request, 1, false, 0, true);
      if (error) break;
      host.acknowledge_local_media_default(host.ctx, request);
    } else if (!eq(request->kind, "local-media")) {
      error = host.audio_receive(host.ctx, request, 1, false, 0, true);
      if (error) break;
    }
    if (!eq(request->kind, "presentation-owner")) host.applied_media(host.ctx, request->sequence);
  }
  free(ordered);
  for (size_t i = 0; i < seat_count && !error; i++) {
    /* Seat batches filter to audio controls, as in TS. */
    for (size_t j = 0; j < seats[i].event_count && !error; j++)
      if (qa_app_presentation_audio_control(&seats[i].events[j]))
        error = host.audio_receive(host.ctx, &seats[i].events[j], 1, true, seats[i].seat, seats[i].music);
  }
  return error;
}
const char *qa_app_prepare_presentation_shaders(qa_app_component_media_host host) {
  for (;;) {
    size_t pending_count = 0;
    const qa_app_media_event *pending_all = host.pending_local_media(host.ctx, &pending_count);
    const qa_app_media_event *pending = NULL;
    for (size_t i = 0; i < pending_count; i++)
      if (eq(pending_all[i].kind, "local-media") && eq(pending_all[i].event_kind, "shader-remap")) { pending = &pending_all[i]; break; }
    if (!pending) return NULL;
    if (!eq(pending->kind, "local-media")) qa_range_error("Shader delivery requires local media");
    const qa_app_media_event *request = host.resolve_shader_replay(host.ctx, pending);
    if (!request) continue;
    if (!eq(request->event_kind, "shader-remap")) qa_range_error("Shader delivery requires a material cue");
    if (!host.local_media_current(host.ctx, request)) { host.acknowledge_local_media(host.ctx, request, false); continue; }
    const char *result = host.remap_shader(host.ctx, request);
    if (eq(result, "stale") && host.local_media_current(host.ctx, request))
      qa_range_error("Shader destination changed before local media delivery");
    host.acknowledge_local_media(host.ctx, request, eq(result, "committed"));
    if (!eq(result, "stale")) host.applied_shader(host.ctx, request->original, request->sequence);
  }
}
const char *qa_app_primary_shader_control(qa_app_component_media_host host, const char *original, const char *replacement,
                                          const char *offset, bool initializing, bool current, const char *content) {
  host.enable_local_media(host.ctx);
  if (!current) return NULL;
  char *end = NULL;
  double parsed = strtod(offset, &end);
  double time_offset = end == offset ? 0 : parsed; /* Number.parseFloat; NaN -> 0 */
  qa_app_media_event request = {"local-media", "shader-remap", 0, original, replacement, time_offset, content};
  host.publish_local_media(host.ctx, &request, initializing);
  return qa_app_prepare_presentation_shaders(host);
}
const char *qa_app_component_media_control(qa_app_component_media_host host, const qa_app_media_event *request, bool initializing, bool current) {
  host.enable_local_media(host.ctx);
  if (!current) return NULL;
  host.publish_local_media(host.ctx, request, initializing);
  if (eq(request->event_kind, "shader-remap")) return qa_app_prepare_presentation_shaders(host);
  return qa_app_prepare_presentation_audio(host, NULL, 0, NULL, 0);
}
