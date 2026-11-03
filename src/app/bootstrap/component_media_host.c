/* The arguments component-media.ts takes, as one qa_app_component_media_host:
 * preparePresentationAudio(simulation.events, audio, ...),
 * preparePresentationShaders(simulation.events, assets) and
 * componentMediaControl(events, audio, assets) read simulation.events (the
 * SharedSimulation's PresentationState), ApplicationAudio.playComponentMedia /
 * receive and ApplicationAssets.world / provider(content).shaders. The host's ctx
 * is one record per (simulation, audio, assets), kept until the simulation is
 * disposed. GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>

#include "application_internal.h"
#include "simulation/shared_simulation_internal.h"
#include "qa/app/bootstrap/component_media.h"
#include "qa/foundation/support.h"

typedef struct media_host_record {
  qa_app_shared_simulation *simulation;
  qa_app_application_audio *audio;
  qa_app_application_assets *assets;
  struct media_host_record *next;
} media_host_record;
static media_host_record *records;

static void record_dispose(void *ctx) {
  for (media_host_record **at = &records; *at != NULL; at = &(*at)->next)
    if (*at == ctx) {
      *at = ((media_host_record *)ctx)->next;
      break;
    }
  free(ctx);
}
static qa_app_presentation_state *state_of(void *ctx) { return ((media_host_record *)ctx)->simulation->presentation; }

static const qa_app_retained_presentation *const *host_pending_local_media(void *ctx, size_t *out_count) {
  return qa_app_presentation_state_pending_local_media(state_of(ctx), out_count);
}
static double host_applied_media_sequence(void *ctx) { return qa_app_presentation_state_applied_media_sequence(state_of(ctx)); }
static bool host_local_media_current(void *ctx, const qa_app_retained_presentation *request) {
  return qa_app_presentation_state_local_media_current(state_of(ctx), request);
}
static void host_acknowledge_local_media(void *ctx, const qa_app_retained_presentation *request, bool committed) {
  qa_app_presentation_state_acknowledge_local_media(state_of(ctx), request, committed);
}
static void host_applied_media(void *ctx, double sequence) { qa_app_presentation_state_applied_media(state_of(ctx), sequence); }
static void host_applied_shader(void *ctx, const char *original, double sequence) {
  qa_app_presentation_state_applied_shader(state_of(ctx), original, sequence);
}
static const qa_app_retained_presentation *host_resolve_shader_replay(void *ctx, const qa_app_retained_presentation *request) {
  return qa_app_presentation_state_resolve_shader_replay(state_of(ctx), request);
}
static void host_enable_local_media(void *ctx) { qa_app_presentation_state_enable_local_media(state_of(ctx)); }
static void host_publish_local_media(void *ctx, const qa_presentation_owner *owner, qa_content_id content,
                                     const qa_component_presentation_media_request *event, bool initializing,
                                     qa_app_media_current_fn current, void *current_ctx, void (*release_current)(void *current_ctx)) {
  qa_app_presentation_state_publish_local_media(state_of(ctx), owner, content, event, initializing, current, current_ctx,
                                                release_current);
}
/* audio.playComponentMedia({ ...request, event }, active): a TS rejection raises. */
static const char *host_play_component_media(void *ctx, const qa_app_local_presentation_media *source,
                                             qa_app_media_current_fn active, void *active_ctx) {
  qa_app_application_audio_play_component_media(((media_host_record *)ctx)->audio, source->has_owner ? &source->owner : NULL,
                                                source->content, &source->event, active, active_ctx);
  return NULL;
}
static const char *host_audio_receive(void *ctx, const qa_app_sim_presentation_event *events, size_t count,
                                      qa_audio_audience audience, bool music) {
  qa_app_application_audio_receive(((media_host_record *)ctx)->audio, events, count, audience, music);
  return NULL;
}
static qa_world_scene *host_world(void *ctx) { return qa_app_application_assets_world(((media_host_record *)ctx)->assets); }
/* (await assets.provider(content)).shaders: a provider failure raises. */
static qa_scene_shader_registry *host_provider_shaders(void *ctx, qa_content_id content, const char **out_error) {
  *out_error = NULL;
  return qa_app_application_assets_provider(((media_host_record *)ctx)->assets, content).shaders;
}

qa_app_component_media_host qa_app_component_media_host_of(qa_app_shared_simulation *simulation, qa_app_application_audio *audio,
                                                           qa_app_application_assets *assets) {
  media_host_record *record = records;
  while (record != NULL && !(record->simulation == simulation && record->audio == audio && record->assets == assets))
    record = record->next;
  if (record == NULL) {
    record = calloc(1, sizeof *record);
    if (record == NULL) qa_range_error("Out of memory");
    record->simulation = simulation;
    record->audio = audio;
    record->assets = assets;
    record->next = records;
    records = record;
    qa_app_shared_sim_own(simulation, record_dispose, record);
  }
  qa_app_component_media_host host;
  memset(&host, 0, sizeof host);
  host.ctx = record;
  host.pending_local_media = host_pending_local_media;
  host.applied_media_sequence = host_applied_media_sequence;
  host.local_media_current = host_local_media_current;
  host.acknowledge_local_media = host_acknowledge_local_media;
  host.applied_media = host_applied_media;
  host.applied_shader = host_applied_shader;
  host.resolve_shader_replay = host_resolve_shader_replay;
  host.enable_local_media = host_enable_local_media;
  host.publish_local_media = host_publish_local_media;
  host.play_component_media = host_play_component_media;
  host.audio_receive = host_audio_receive;
  host.world = host_world;
  host.provider_shaders = host_provider_shaders;
  return host;
}
