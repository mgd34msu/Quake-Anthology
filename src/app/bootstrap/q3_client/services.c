/* Port of src/app/bootstrap/q3-client/services.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q3_client/services.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/render/scene/particles/primitives.h"
struct qa_app_application_q3_services {
  qa_app_application_q3_service_options options;
  qa_q3_pres_scene_recorder *scene;
  qa_q3_pres_q3_renderer_resources *resources;
  qa_q3_pres_presentation_audio audio;
  qa_text_command_sink sink;
  qa_draw2d draw;
  qa_app_q3_client_collision collision_state;
  qa_q3_pres_collision_world collision;
  qa_app_application_q3_cinematics *cinematics;
  qa_q3_pres_scene_recorder_mirror recorder_mirror;
  void *remap_pair;
};
typedef struct remap_pair {
  qa_q3_pres_resource_host inner;
  void (*remap)(void *ctx, const char *original, const char *replacement, const char *offset);
  void *remap_ctx;
} remap_pair;
static bool pair_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  remap_pair *p = ctx; return p->inner.model(p->inner.ctx, path, out);
}
static bool pair_skin(void *ctx, const char *path, const qa_q3_pres_scene_skin **out) {
  remap_pair *p = ctx; return p->inner.skin(p->inner.ctx, path, out);
}
static bool pair_shader(void *ctx, const char *path, bool mip, const qa_material_picture **out) {
  remap_pair *p = ctx; return p->inner.shader(p->inner.ctx, path, mip, out);
}
static bool pair_world(void *ctx, const char *path, qa_q3_pres_world_scene *out) {
  remap_pair *p = ctx; return p->inner.world(p->inner.ctx, path, out);
}
static void pair_remap(void *ctx, const char *original, const char *replacement, const char *offset) {
  remap_pair *p = ctx; p->remap(p->remap_ctx, original, replacement, offset);
}
static const qa_q3_pres_fog_selection *target_fog(void *ctx, size_t *out_count) {
  qa_app_application_q3_services *s = ctx;
  return s->options.world.fog_selections(s->options.world.ctx, out_count);
}
static void target_print(void *ctx, const char *text) {
  qa_app_application_q3_services *s = ctx;
  qa_app_application_q3_assets_print(s->options.media, text);
}
static const qa_actor_id *target_actor(void *ctx, const qa_q3_pres_ref_model_entity *entity) {
  (void)ctx; (void)entity;
  return NULL;
}
static void target_publish(void *ctx, qa_q3_pres_presented_scene *scene) {
  qa_app_application_q3_services *s = ctx;
  s->options.output_scene(s->options.output_ctx, scene);
}
static void mirror_clear(void *ctx) { qa_q3_pres_scene_recorder_clear_scene(ctx); }
static void mirror_add_ref(void *ctx, const qa_q3_pres_admitted_ref_entity *entity) {
  qa_q3_pres_scene_recorder_add_ref_entity(ctx, entity);
}
static void mirror_add_poly(void *ctx, const qa_q3_pres_ref_poly *poly) {
  qa_q3_pres_scene_recorder_add_poly(ctx, poly);
}
static void mirror_add_light(void *ctx, qa_q3_pres_dynamic_light light) {
  qa_q3_pres_scene_recorder_add_light(ctx, light);
}
static void mirror_render(void *ctx, const qa_q3_pres_refdef *refdef) {
  qa_q3_pres_scene_recorder_render_scene(ctx, refdef);
}
static bool audio_actor(void *ctx, double source_number, qa_actor_id *out) {
  qa_app_application_q3_services *s = ctx;
  return s->options.actor_at(s->options.actor_ctx, source_number, out);
}
static double audio_frame_number(void *ctx) {
  qa_app_application_q3_services *s = ctx;
  return s->options.frame_number(s->options.frame_ctx);
}
static void audio_play(void *ctx, qa_play_sound sound) {
  qa_app_application_q3_services *s = ctx;
  qa_app_q3_seat_audio_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_APP_Q3_SEAT_AUDIO_PLAY;
  op.play = sound;
  if (s->options.has_owner) op.play.owner = s->options.owner;
  s->options.output_audio(s->options.output_ctx, op);
}
static void audio_loop(void *ctx, qa_loop_sound sound) {
  qa_app_application_q3_services *s = ctx;
  qa_app_q3_seat_audio_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_APP_Q3_SEAT_AUDIO_LOOP;
  op.loop = sound;
  if (s->options.has_owner) op.loop.owner = s->options.owner;
  s->options.output_audio(s->options.output_ctx, op);
}
static void audio_update_actor(void *ctx, qa_actor_id actor, qa_vec3 position) {
  qa_app_application_q3_services *s = ctx;
  qa_app_q3_seat_audio_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_APP_Q3_SEAT_AUDIO_POSITION;
  op.actor = actor;
  op.origin = position;
  s->options.output_audio(s->options.output_ctx, op);
}
static void audio_stop_loop(void *ctx, qa_seat_id seat, qa_actor_id actor) {
  qa_app_application_q3_services *s = ctx;
  (void)seat;
  qa_app_q3_seat_audio_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_APP_Q3_SEAT_AUDIO_STOP_LOOP;
  op.actor = actor;
  s->options.output_audio(s->options.output_ctx, op);
}
static void sound_start_sound(void *ctx, const qa_vec3 *origin, double entity, double channel, qa_pcm_sound *pcm) {
  qa_app_application_q3_services *s = ctx;
  qa_q3_pres_presentation_audio_start_sound(&s->audio, origin, entity, channel, pcm);
}
static void sound_start_source(void *ctx, qa_pcm_sound *pcm, qa_start_sound_options options) {
  qa_app_application_q3_services *s = ctx;
  qa_q3_pres_presentation_audio_start_source_sound(&s->audio, pcm, options);
}
static void sound_start_local(void *ctx, qa_pcm_sound *pcm, double channel) {
  qa_app_application_q3_services *s = ctx;
  qa_q3_pres_presentation_audio_start_local_sound(&s->audio, pcm, channel);
}
static void sound_add_loop(void *ctx, double entity, qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *pcm, bool real) {
  qa_app_application_q3_services *s = ctx;
  qa_q3_pres_presentation_audio_add_loop_sound(&s->audio, entity, origin, velocity, pcm, real);
}
static void sound_update_position(void *ctx, double entity, qa_vec3 position) {
  qa_app_application_q3_services *s = ctx;
  qa_q3_pres_presentation_audio_update_sound_position(&s->audio, entity, position);
}
static void sound_stop_looping(void *ctx, double entity) {
  qa_app_application_q3_services *s = ctx;
  qa_q3_pres_presentation_audio_stop_looping_sound(&s->audio, entity);
}
static void sound_clear_loops(void *ctx, bool kill_all) {
  qa_app_application_q3_services *s = ctx;
  qa_app_q3_seat_audio_operation op;
  memset(&op, 0, sizeof op);
  op.kind = QA_APP_Q3_SEAT_AUDIO_CLEAR_LOOPS;
  op.kill_all = kill_all;
  s->options.output_audio(s->options.output_ctx, op);
}
static void sound_set_listener(void *ctx, double client, qa_vec3 origin, qa_axis axis) {
  qa_app_application_q3_services *s = ctx;
  (void)client;
  s->options.output_listener(s->options.output_ctx, origin, axis);
}
static void sound_background(void *ctx, const char *intro, const char *loop) {
  qa_app_application_q3_services *s = ctx;
  char buffer[2048];
  snprintf(buffer, sizeof buffer, "%s %s", intro, loop);
  s->options.audio.play_music(s->options.audio.ctx,
                              qa_app_application_q3_assets_content(s->options.media), buffer);
}
static void sink_emit(void *ctx, const qa_render_command *command) {
  qa_app_application_q3_services *s = ctx;
  if (command->kind == QA_RENDER_COMMAND_SWAP_BUFFERS)
    qa_range_error("Cgame cannot present the shared framebuffer");
  s->options.output_command(s->options.output_ctx, command);
}
static void sink_text(void *ctx, const qa_material_text_draw *draw) {
  qa_app_application_q3_services *s = ctx;
  s->options.output_text(s->options.output_ctx, draw);
}
static qa_source_cluster_pvs cluster_pvs_bridge(void *ctx, double cluster) {
  qa_collision_world *world = ctx;
  return qa_collision_world_cluster_pvs(world, cluster);
}
static void cinematic_print(void *ctx, const char *text) {
  qa_app_application_q3_assets_print(ctx, text);
}
qa_app_application_q3_services *qa_app_create_application_q3_services(qa_app_application_q3_service_options options) {
  qa_app_application_q3_services *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->options = options;
  qa_q3_pres_scene_target target = {options.seat, options.viewport, 16384, 4,
                                    s, target_fog, target_print, target_actor, target_publish};
  s->scene = qa_q3_pres_scene_recorder_create(target);
  if (s->scene == NULL) qa_range_error("Out of memory");
  s->recorder_mirror.ctx = s->scene;
  s->recorder_mirror.clear_scene = mirror_clear;
  s->recorder_mirror.add_ref_entity = mirror_add_ref;
  s->recorder_mirror.add_poly = mirror_add_poly;
  s->recorder_mirror.add_light = mirror_add_light;
  s->recorder_mirror.render_scene = mirror_render;
  qa_q3_pres_resource_host host = qa_app_application_q3_assets_resource_host(options.media, s->recorder_mirror);
  if (options.remap_shader != NULL) {
    /* TS spreads the caller remapShader over the media host. */
    remap_pair *pair = malloc(sizeof *pair);
    if (pair == NULL) qa_range_error("Out of memory");
    pair->inner = host;
    pair->remap = options.remap_shader;
    pair->remap_ctx = options.remap_ctx;
    s->remap_pair = pair;
    host.ctx = pair;
    host.model = pair_model;
    host.skin = pair_skin;
    host.shader = pair_shader;
    host.world = pair_world;
    host.remap_shader = pair_remap;
  }
  qa_source_clip_models *clip = options.shared_queries != NULL
    ? qa_shared_native_q3_clip_models(options.shared_queries) : NULL;
  qa_q3_pres_resource_world world;
  const qa_q3_pres_resource_world *world_arg = NULL;
  if (options.world.map_kind != NULL && strcmp(options.world.map_kind, "q3-bsp") == 0 && clip != NULL) {
    world.map = options.world.map_geometry;
    world.ctx = clip->world;
    world.cluster_pvs = cluster_pvs_bridge;
    world_arg = &world;
  }
  qa_q3_pres_resource_handles handles = options.resource_handles != NULL
      && strcmp(options.resource_handles, "client") == 0
    ? QA_Q3_PRES_HANDLES_CLIENT : QA_Q3_PRES_HANDLES_RENDERER;
  if (!qa_q3_pres_q3_renderer_resources_init(&s->resources, host, world_arg, handles))
    qa_range_error("Out of memory");
  qa_q3_pres_audio_target audio_target = {options.seat,
      qa_app_application_q3_assets_bank(options.media), s, audio_actor, audio_frame_number,
      audio_play, audio_loop, audio_update_actor, audio_stop_loop};
  qa_q3_pres_presentation_audio_init(&s->audio, audio_target);
  qa_text_command_sink_init(&s->sink, options.seat, options.viewport, sink_emit, s, sink_text, s);
  s->draw.commands = qa_text_command_sink_sink(&s->sink);
  s->draw.space = QA_COORD_STRETCH_640;
  s->collision = qa_app_q3_client_collision_init(&s->collision_state, options.queries, options.collision_settings);
  const qa_app_q3_provider_scene_assets *provider = qa_app_application_q3_assets_provider(options.media);
  s->cinematics = qa_app_application_q3_cinematics_create(provider->mounts, options.cinematics_host,
      cinematic_print, options.media, options.audio.engine, options.seat,
      options.now, options.now_ctx, options.system_cinematics);
  return s;
}
void qa_app_application_q3_services_destroy(qa_app_application_q3_services *services) {
  if (services == NULL) return;
  qa_app_application_q3_cinematics_destroy(services->cinematics);
  qa_q3_pres_q3_renderer_resources_destroy(services->resources);
  free(services->remap_pair);
  qa_q3_pres_scene_recorder_destroy(services->scene);
  free(services);
}
qa_q3_pres_scene_recorder *qa_app_application_q3_services_scene(qa_app_application_q3_services *services) { return services->scene; }
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_services_resources(qa_app_application_q3_services *services) { return services->resources; }
qa_app_q3_client_sound qa_app_application_q3_services_sound(qa_app_application_q3_services *services) {
  qa_app_q3_client_sound sound = {services, qa_app_application_q3_assets_bank(services->options.media),
      sound_start_sound, sound_start_source, sound_start_local, sound_add_loop, sound_update_position,
      sound_stop_looping, sound_clear_loops, sound_set_listener, sound_background};
  return sound;
}
qa_draw2d *qa_app_application_q3_services_draw(qa_app_application_q3_services *services) { return &services->draw; }
qa_q3_pres_collision_world qa_app_application_q3_services_collision(qa_app_application_q3_services *services) { return services->collision; }
qa_app_application_q3_cinematics *qa_app_application_q3_services_cinematics(qa_app_application_q3_services *services) { return services->cinematics; }
