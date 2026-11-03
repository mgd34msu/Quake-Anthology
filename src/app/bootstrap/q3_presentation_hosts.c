/* The ApplicationAssets and ApplicationAudio members the Q3 presentation services read (TS ApplicationQ3Services reads
 * assets.world.fogSelections, assets.content.world and audio.engine / audio.playMusic directly; the C services take
 * them as qa_app_q3_presentation_hosts, mod_presentation.h). GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/mod_presentation.h"
#include "qa/foundation/support.h"

/* The assets' world fog selections as the Q3 presentation reads them ({ index, volume.bounds }), read live on every
 * call. Kept by the loaded content (released with it), as the assets are. */
typedef struct q3_hosts_world {
  qa_app_application_assets *assets;
  qa_q3_pres_fog_selection *selections; size_t capacity;
} q3_hosts_world;
static void world_release(void *record) {
  q3_hosts_world *world = record;
  free(world->selections);
  free(world);
}
static const qa_q3_pres_fog_selection *world_fog_selections(void *ctx, size_t *out_count) {
  q3_hosts_world *world = ctx;
  qa_world_scene *scene = qa_app_application_assets_world(world->assets);
  size_t count = scene->fog_selection_count;
  if (count > world->capacity) {
    qa_q3_pres_fog_selection *grown = realloc(world->selections, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    world->selections = grown;
    world->capacity = count;
  }
  for (size_t i = 0; i < count; i++) {
    world->selections[i].index = scene->fog_selections[i].index;
    world->selections[i].bounds = scene->fog_selections[i].volume.bounds;
  }
  *out_count = count;
  return world->selections;
}
static void mixer_queue(void *ctx, const qa_audio_stream_target *target, const qa_stream_pcm *pcm) {
  qa_unified_audio_queue_stream(ctx, *target, pcm);
}
static void mixer_stop(void *ctx, const char *id) { qa_unified_audio_stop_stream(ctx, id); }
static void mixer_pause(void *ctx, const char *id, bool paused) { qa_unified_audio_pause_stream(ctx, id, paused); }
static qa_save_value *mixer_capture(void *ctx, const char *id) { return qa_unified_audio_capture_stream_checkpoint(ctx, id); }
static void mixer_restore(void *ctx, const qa_audio_stream_target *target, const qa_save_value *value) {
  qa_unified_audio_restore_stream_checkpoint(ctx, *target, value);
}
static void audio_play_music(void *ctx, qa_content_id content, const char *track) {
  qa_app_application_audio_play_music(ctx, content, track, NULL);
}

qa_app_q3_presentation_hosts qa_app_application_assets_q3_presentation_hosts(qa_app_application_assets *assets,
    qa_app_application_audio *audio) {
  qa_app_q3_presentation_hosts hosts;
  memset(&hosts, 0, sizeof hosts);
  q3_hosts_world *world = calloc(1, sizeof *world);
  if (world == NULL) qa_range_error("Out of memory");
  world->assets = assets;
  qa_app_loaded_content *content = qa_app_application_assets_content(assets);
  qa_app_loaded_content_keep(content, world, world_release);
  qa_app_application_world map = qa_app_loaded_content_world(content);
  hosts.world.ctx = world;
  hosts.world.fog_selections = world_fog_selections;
  hosts.world.map_kind = map.kind;
  hosts.world.map_geometry = map.kind != NULL && strcmp(map.kind, "q3-bsp") == 0 ? map.value : NULL;
  hosts.audio.ctx = audio;
  hosts.audio.engine.ctx = qa_app_application_audio_engine(audio);
  hosts.audio.engine.queue_stream = mixer_queue;
  hosts.audio.engine.stop_stream = mixer_stop;
  hosts.audio.engine.pause_stream = mixer_pause;
  hosts.audio.engine.capture_stream_checkpoint = mixer_capture;
  hosts.audio.engine.restore_stream_checkpoint = mixer_restore;
  hosts.audio.play_music = audio_play_music;
  return hosts;
}
