/* Port of src/app/bootstrap/presentation.ts and seat-factory.ts (createSeatPresentation). GPL-2.0-or-later. */
#include "qa/app/bootstrap/presentation.h"
#include <math.h>
#include <stdlib.h>
#include "qa/core/math.h"
#include "qa/foundation/support.h"
qa_rect qa_app_seat_viewport(double index, double count, double width, double height) {
  if (index != trunc(index) || index < 0 || index >= count || count < 1 || count > 4)
    qa_range_error("Invalid local seat layout");
  if (count == 1) { qa_rect out = {0, 0, width, height}; return out; }
  double columns = count == 2 ? 1 : 2, rows = 2;
  double column = fmod(index, columns), row = trunc(index / columns);
  double x = trunc(column * width / columns), y = trunc(row * height / rows);
  qa_rect out = {x, y, trunc((column + 1) * width / columns) - x,
      trunc((row + 1) * height / rows) - y};
  return out;
}
static qa_vec3 rotate(const qa_axis *axis, qa_vec3 v) {
  qa_vec3 out = {
      axis->vectors[0].x * v.x + axis->vectors[1].x * v.y + axis->vectors[2].x * v.z,
      axis->vectors[0].y * v.x + axis->vectors[1].y * v.y + axis->vectors[2].y * v.z,
      axis->vectors[0].z * v.x + axis->vectors[1].z * v.y + axis->vectors[2].z * v.z};
  return out;
}
qa_scene_camera qa_app_camera_with_kick(qa_scene_camera camera, qa_vec3 kick) {
  if (kick.x == 0 && kick.y == 0 && kick.z == 0) return camera;
  qa_axis local = qa_angles_to_axis(kick);
  qa_scene_camera out = camera;
  for (size_t i = 0; i < 3; i++) out.axis.vectors[i] = rotate(&camera.axis, local.vectors[i]);
  return out;
}
qa_scene_camera qa_app_camera_with_client_offset(qa_scene_camera camera, const qa_app_sim_player_view *player) {
  if (!player->has_client_view_offset_delta || camera.clip_is_portal) return camera;
  qa_scene_camera out = camera;
  out.origin.x = camera.origin.x + player->client_view_offset_delta.x;
  out.origin.y = camera.origin.y + player->client_view_offset_delta.y;
  out.origin.z = camera.origin.z + player->client_view_offset_delta.z;
  return out;
}
qa_scene_camera qa_app_camera_with_character_death(qa_scene_camera camera, const qa_app_sim_player_view *player) {
  if (!player->foreign_character_death || camera.clip_is_portal) return camera;
  qa_scene_camera out = camera;
  out.origin = player->origin;
  out.origin.z = player->origin.z + player->view_height;
  out.axis = qa_angles_to_axis(player->angles);
  return out;
}

/* ---- WorldSeatPresentation ---------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include "qa/app/bootstrap/finale.h"
#include "qa/app/bootstrap/q1_fog.h"
#include "qa/app/bootstrap/q1_localization.h"
#include "qa/app/bootstrap/q1_service_presentation.h"
#include "qa/app/bootstrap/q2_damage_blend.h"
#include "qa/app/bootstrap/q2_native_hud.h"
#include "qa/app/bootstrap/simulation/native_mod_camera.h"
#include "qa/app/bootstrap/weapon_view.h"
#include "qa/console/draw.h"
#include "qa/console/metrics.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/core/commands/text.h"
#include "qa/formats/images/indexed.h"
#include "qa/formats/images/palette.h"
#include "qa/formats/q1_map/types.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/platform/sdl.h"
#include "qa/render/commands/frame.h"
#include "qa/render/commands/material2d.h"
#include "qa/render/q3_hardware.h"
#include "qa/render/scene/debug_shapes.h"
#include "qa/render/scene/view.h"
#include "qa/render/scene/world_text.h"
#include "qa/text/layout.h"
#include "qa/world/session/mod_client_presentation.h"
#include "qa/world/session/session.h"

typedef struct held_value { void (*release)(void *value); void *value; } held_value;
typedef struct value_store { held_value *items; size_t count, capacity; } value_store;
static void store_add(value_store *store, void (*release)(void *value), void *value) {
  if (store->count == store->capacity) {
    size_t capacity = store->capacity == 0 ? 32 : store->capacity * 2;
    held_value *next = realloc(store->items, capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    store->items = next; store->capacity = capacity;
  }
  store->items[store->count++] = (held_value){ release, value };
}
static void *store_alloc(value_store *store, size_t size) {
  void *value = calloc(1, size == 0 ? 1 : size);
  if (value == NULL) qa_range_error("Out of memory");
  store_add(store, free, value);
  return value;
}
static void store_release(value_store *store) {
  while (store->count > 0) { held_value item = store->items[--store->count]; item.release(item.value); }
}
static char *heap_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *out = malloc(length);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, length);
  return out;
}
#define GROW(array, count, capacity, initial) do { \
  if ((count) == (capacity)) { \
    size_t next_capacity_ = (capacity) == 0 ? (initial) : (capacity) * 2; \
    void *next_ = realloc((array), next_capacity_ * sizeof *(array)); \
    if (next_ == NULL) qa_range_error("Out of memory"); \
    (array) = next_; (capacity) = next_capacity_; \
  } } while (0)

typedef struct component_effect {
  int64_t id;
  qa_app_component_effect_frame frame;
  bool has_overlay; qa_app_component_overlay overlay;
} component_effect;
/* TS ComponentClientFrame. */
typedef struct component_client {
  qa_active_mod_client_presentation source;
  double generation;
  qa_mod_client_presentation_frame frame;
  qa_app_q2_native_hud *hud;
} component_client;
typedef struct pending_message { char *text; bool has_sequence; double sequence; } pending_message;
typedef struct world_font { char *content; qa_app_world_font *font; } world_font;
/* TS FinaleAssets per content (the banner the finale host draws). */
typedef struct finale_banner { char *content; qa_image_picture banner; double width, height; } finale_banner;

struct qa_app_world_seat_presentation {
  qa_app_world_seat_presentation_options options;
  qa_app_local_input *local;
  qa_app_application_assets *assets;
  bool has_q3_client; qa_app_seat_q3_client q3_client;
  component_effect *component_effects; size_t component_effect_count, component_effect_capacity; int64_t next_binding;
  qa_app_q1_message_localization *q1_messages;
  qa_event_ring pending_messages, pending_q1, pending_q2;
  qa_event_cursor pending_messages_cursor, pending_q1_cursor, pending_q2_cursor;
  qa_scene_frame_builder frames;
  qa_seat_text_presentation text;
  qa_app_source_finale *finale;
  finale_banner *banners; size_t banner_count, banner_capacity;
  const finale_banner *finale_current;
  double prepared_time;
  qa_world_text *world_text; size_t world_text_count;
  qa_app_component_drawings *component_drawings;
  double component_drawing_frame;
  const qa_debug_line *component_lines; size_t component_line_count;
  world_font *world_fonts; size_t world_font_count, world_font_capacity;
  qa_app_application_world_scene *scene;
  qa_app_q1_map_fog *q1_fog;
  qa_app_q1_service_presentation *q1_services;
  double layout_index, layout_count;
  bool has_native_q2_frame; qa_native_q2_hud_frame native_q2_frame;
  component_client *component_clients; size_t component_client_count, component_client_capacity;
  bool has_movement_origin; qa_vec3 movement_origin;
  bool closed;
  qa_seat_hud_active hud_active; bool has_hud_active;
  qa_draw2d *current_draw; /* finale drawing during frame() */
  value_store frame_store;  /* frame() storage until the next frame */
  value_store effect_store; /* q3 effect hooks until the next prepare */
};

/* ---- small readers ---- */
static qa_seat_id seat_id(const qa_app_world_seat_presentation *p) { return qa_session_seat_id(p->local->player.seat); }
static qa_client_id client_id(const qa_app_world_seat_presentation *p) {
  return qa_session_client_id(qa_session_seat_client(p->local->player.seat));
}
static qa_actor_id local_actor(const qa_app_world_seat_presentation *p) { return p->local->player.actor; }
static qa_app_sim_player_view player_view(const qa_app_world_seat_presentation *p) {
  return p->options.simulation->player_view(p->options.simulation->ctx, local_actor(p));
}
static double field_of_view(const qa_app_world_seat_presentation *p) {
  return p->options.field_of_view != NULL ? p->options.field_of_view(p->options.field_of_view_ctx) : 90;
}
static bool view_size(const qa_app_world_seat_presentation *p, qa_app_q1_view_settings *out) {
  return p->options.view_size != NULL && p->options.view_size(p->options.view_size_ctx, out);
}
static qa_scene_camera camera_override(const qa_app_world_seat_presentation *p, qa_scene_camera camera) {
  return p->options.camera_override != NULL ? p->options.camera_override(p->options.camera_override_ctx, camera) : camera;
}
static double cvar_integer(qa_cvar_registry *cvars, const char *name, double absent) {
  qa_cvar_snapshot value;
  if (cvars == NULL || !qa_cvar_registry_get(cvars, name, &value)) return absent;
  double result = value.integer_value;
  qa_cvar_read_free(&value);
  return result;
}
static qa_vec3 kick_of(const qa_app_sim_player_view *player) {
  return player->has_kick_angles ? player->kick_angles : (qa_vec3){0, 0, 0};
}
static bool finale_active(const qa_app_world_seat_presentation *p) { return qa_app_source_finale_active(p->finale); }

/* ---- the frame builder's image registry (TS assets.images) ---- */
static const qa_image_resource_operation *registry_drain(void *ctx, size_t *out_count) {
  qa_app_world_seat_presentation *p = ctx;
  /* Drained operations stay with the frame until the next frame (the builder borrows them). */
  qa_image_resource_operation *drained = qa_scene_image_registry_drain_operations(qa_app_application_assets_images(p->assets), out_count);
  if (drained != NULL) store_add(&p->frame_store, free, drained);
  return drained;
}
static void registry_require(void *ctx, qa_renderer_image image) {
  qa_app_world_seat_presentation *p = ctx;
  qa_scene_image_registry_require(qa_app_application_assets_images(p->assets), image);
}

/* ---- Q1MessageLocalization(seat, assets, language) ---- */
static qa_content_mounted *localization_mounts(void *ctx, qa_content_id content) {
  qa_app_world_seat_presentation *p = ctx;
  return qa_app_application_assets_provider(p->assets, content).mounts;
}
static const char *localization_language(void *ctx) {
  qa_app_world_seat_presentation *p = ctx;
  const char *language = p->options.rerelease != NULL ? qa_app_rerelease_selected_language(p->options.rerelease, seat_id(p)) : NULL;
  return language != NULL ? language : "english";
}

/* ---- SourceFinale(assets, text, q1Messages) ---- */
typedef struct banner_job { qa_app_world_seat_presentation *p; qa_content_id content; finale_banner *banner; } banner_job;
static void banner_run(void *ctx) {
  banner_job *job = ctx;
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(job->p->assets, job->content);
  if (provider.family != QA_GAME_FAMILY_Q1 || provider.palette == NULL) qa_raise("Error", 0, 0, "Quake finale requires its source palette");
  qa_content_opened_resource picture;
  if (!qa_content_mounted_open(provider.mounts, "gfx/finale.lmp", NULL, NULL, &picture))
    qa_raise("Error", 0, 0, "Quake finale picture is absent from selected content");
  qa_cleanup bytes_guard;
  qa_cleanup_push(&bytes_guard, free, picture.bytes);
  qa_indexed_image decoded;
  memset(&decoded, 0, sizeof decoded);
  (void)qa_decode_qpic(picture.bytes, picture.byte_length, "gfx/finale.lmp", &decoded);
  qa_image_level level = { (double)decoded.width, (double)decoded.height, { decoded.indices, decoded.indices_length } };
  qa_palette_transparency transparency = { "index", 255 };
  qa_indexed_render_image image = qa_indexed_render_image_make(&level, 1, *provider.palette, transparency, NULL, NULL);
  qa_texture_sampling sampling = { "clamp", "nearest" };
  qa_renderer_image source;
  memset(&source, 0, sizeof source);
  source.source_is_resource = true; source.source_resource = *picture.reference;
  const qa_scene_image_handle *handle = qa_scene_image_registry_register(qa_app_application_assets_images(job->p->assets),
      "gfx/finale.lmp", image, sampling, source);
  qa_cleanup_pop(&bytes_guard);
  free(picture.bytes);
  job->banner->banner.name = "gfx/finale.lmp";
  job->banner->banner.image = handle->image;
  job->banner->width = (double)decoded.width; job->banner->height = (double)decoded.height;
  qa_indexed_image_destroy(&decoded);
}
static const char *finale_load_banner(void *ctx, qa_content_id content, double *width, double *height) {
  qa_app_world_seat_presentation *p = ctx;
  for (size_t i = 0; i < p->banner_count; i++)
    if (strcmp(p->banners[i].content, content) == 0) { *width = p->banners[i].width; *height = p->banners[i].height; return NULL; }
  finale_banner banner;
  memset(&banner, 0, sizeof banner);
  banner_job job = { p, content, &banner };
  qa_caught_error caught;
  if (!qa_try(banner_run, &job, &caught)) {
    char key[512], text[2300];
    snprintf(key, sizeof key, "finale:%s", content);
    snprintf(text, sizeof text, "Finale unavailable: %s\n", caught.message);
    qa_app_application_assets_warn_once(p->assets, key, text);
    static __thread char message[QA_CAUGHT_MESSAGE_CAPACITY];
    snprintf(message, sizeof message, "%s", caught.message);
    return message;
  }
  banner.content = heap_text(content);
  GROW(p->banners, p->banner_count, p->banner_capacity, 4);
  p->banners[p->banner_count++] = banner;
  *width = banner.width; *height = banner.height;
  return NULL;
}
static const char *finale_resolve(void *ctx, qa_content_id content, const char *text, char **out) {
  qa_app_world_seat_presentation *p = ctx;
  *out = qa_app_q1_message_localization_resolve(p->q1_messages, content, text, NULL, 0, NULL, 0);
  return NULL;
}
static void finale_draw_pic(void *ctx, double x, double y, double width, double height) {
  qa_app_world_seat_presentation *p = ctx;
  const finale_banner *banner = p->finale_current;
  if (banner == NULL || p->current_draw == NULL) return;
  qa_picture_asset picture;
  memset(&picture, 0, sizeof picture);
  picture.kind = QA_PICTURE_IMAGE; picture.image = banner->banner;
  qa_draw2d_draw_pic(p->current_draw, (qa_rect){x, y, width, height}, &picture);
}
static qa_text_layout_options finale_text(const qa_app_world_seat_presentation *p, const char *line, double scale) {
  qa_text_layout_options options;
  memset(&options, 0, sizeof options);
  options.text = line; options.text_length = strlen(line); options.font = p->text.font;
  options.scale = scale; options.color = (qa_vec4){1, 1, 1, 1};
  return options;
}
static double finale_text_width(void *ctx, const char *line, double scale) {
  qa_app_world_seat_presentation *p = ctx;
  qa_text_layout layout;
  qa_text_layout_options options = finale_text(p, line, scale);
  if (!qa_seat_text_presentation_layout(&p->text, &options, &layout)) qa_range_error("Out of memory");
  double width = layout.width;
  qa_text_layout_destroy(&layout);
  return width;
}
static void finale_draw_text(void *ctx, const char *line, double scale, long max_glyphs, double x, double y) {
  qa_app_world_seat_presentation *p = ctx;
  if (p->current_draw == NULL) return;
  qa_text_layout layout;
  qa_text_layout_options options = finale_text(p, line, scale);
  options.has_max_glyphs = true; options.max_glyphs = (double)max_glyphs;
  if (!qa_seat_text_presentation_draw(&p->text, p->current_draw, &options, (qa_vec2){x, y}, 0, &layout)) qa_range_error("Out of memory");
  qa_text_layout_destroy(&layout);
}

/* ---- Q1ServicePresentation assets (provider.textures) ---- */
static bool q1_service_texture(void *ctx, qa_content_id content, const char *path, qa_renderer_image *out) {
  qa_app_world_seat_presentation *p = ctx;
  qa_scene_texture_load_options options = { true, false, "clamp", "q1", "sky" };
  const qa_scene_texture *texture = qa_scene_texture_loader_load(qa_app_application_assets_provider(p->assets, content).textures, path, &options);
  if (texture == NULL) return false;
  *out = texture->image;
  return true;
}
static qa_renderer_image q1_service_missing(void *ctx, qa_content_id content) {
  qa_app_world_seat_presentation *p = ctx;
  return qa_app_application_assets_provider(p->assets, content).textures->missing->image;
}
static qa_app_q1_service_assets q1_service_assets(qa_app_world_seat_presentation *p) {
  qa_app_q1_service_assets assets = { p, q1_service_texture, q1_service_missing };
  return assets;
}

static qa_q3_hardware read_hardware(void *ctx) {
  qa_app_world_seat_presentation *p = ctx;
  const qa_app_gl_driver *driver = qa_app_native_renderer_driver(p->options.renderer);
  return qa_q3_hardware_classify(driver != NULL && driver->renderer != NULL ? driver->renderer : "");
}

static bool planar_shadows(void *ctx) {
  qa_app_world_seat_presentation *p = ctx;
  return p->options.planar_shadows != NULL && p->options.planar_shadows(p->options.planar_shadows_ctx);
}

qa_app_world_seat_presentation *qa_app_world_seat_presentation_create(const qa_app_world_seat_presentation_options *options) {
  qa_app_world_seat_presentation *p = calloc(1, sizeof *p);
  if (p == NULL) return NULL;
  p->options = *options;
  p->local = options->local;
  p->assets = options->assets;
  if (options->q3_client != NULL) { p->has_q3_client = true; p->q3_client = *options->q3_client; }
  p->layout_index = (double)qa_session_seat_id(options->local->player.seat).index;
  qa_app_application_effects_bind_renderer_hardware(options->effects, read_hardware, p);
  p->layout_count = options->seat_count;
  qa_app_loaded_content *content = qa_app_application_assets_content(options->assets);
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  qa_app_application_world world = qa_app_loaded_content_world(content);
  bool q1 = world.kind != NULL && strcmp(world.kind, "q1-bsp") == 0;
  const char *entities = q1 ? ((const qa_q1_map *)world.value)->world.entities : "";
  qa_content_id *contents = calloc(recipe->mounts.mount_count == 0 ? 1 : recipe->mounts.mount_count, sizeof *contents);
  if (contents == NULL) { free(p); return NULL; }
  for (size_t i = 0; i < recipe->mounts.mount_count; i++) contents[i] = recipe->mounts.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE ? recipe->mounts.mounts[i].as.archive.identity.content
                                                                                   : recipe->mounts.mounts[i].as.loose.identity.content;
  p->q1_fog = qa_app_q1_map_fog_create(entities, contents, recipe->mounts.mount_count, options->local->player.actor, q1);
  free(contents);
  qa_app_q1_localization_assets localization = { p, qa_app_application_assets_catalog(options->assets), localization_mounts };
  p->q1_messages = qa_app_q1_message_localization_create(seat_id(p), localization, localization_language, p);
  p->scene = qa_app_application_world_scene_create(options->assets, options->characters, planar_shadows, p);
  qa_render_frame_image_registry registry = { p, qa_app_application_assets_images(options->assets)->owner, registry_drain, registry_require };
  if (!qa_scene_frame_builder_init(&p->frames, registry)) qa_range_error("Out of memory");
  qa_seat_text_presentation_init(&p->text, seat_id(p), options->font);
  qa_app_finale_host finale = { p, finale_load_banner, finale_resolve, 0, 0, finale_draw_pic, finale_text_width, finale_draw_text };
  p->finale = qa_app_source_finale_create(finale);
  p->q1_services = qa_app_q1_service_presentation_create();
  p->component_drawings = qa_app_component_drawings_create();
  if (!qa_event_ring_init(&p->pending_messages, 4096, sizeof(pending_message))
      || !qa_event_ring_init(&p->pending_q1, 4096, sizeof(qa_app_sim_presentation_event))
      || !qa_event_ring_init(&p->pending_q2, 4096, sizeof(qa_app_sim_presentation_event)))
    qa_range_error("Out of memory");
  if (!qa_event_ring_open_cursor(&p->pending_messages, &p->pending_messages_cursor, false)
      || !qa_event_ring_open_cursor(&p->pending_q1, &p->pending_q1_cursor, false)
      || !qa_event_ring_open_cursor(&p->pending_q2, &p->pending_q2_cursor, false))
    qa_range_error("Out of memory");
  p->next_binding = 1;
  return p;
}

qa_app_local_input *qa_app_world_seat_presentation_local(const qa_app_world_seat_presentation *p) { return p->local; }
qa_app_application_assets *qa_app_world_seat_presentation_assets(const qa_app_world_seat_presentation *p) { return p->assets; }
qa_app_seat_ui *qa_app_world_seat_presentation_ui(const qa_app_world_seat_presentation *p) { return p->options.ui; }
const qa_app_seat_q3_client *qa_app_world_seat_presentation_q3_client(const qa_app_world_seat_presentation *p) {
  return p->has_q3_client ? &p->q3_client : NULL;
}
bool qa_app_world_seat_presentation_replace_q3_client(qa_app_world_seat_presentation *p,
    const qa_app_seat_q3_client *candidate, qa_app_seat_q3_client *previous) {
  if (!qa_same_seat(candidate->seat, seat_id(p)) || !qa_same_actor(candidate->actor, local_actor(p)))
    qa_raise("Error", 0, 0, "Replacement presentation belongs to another local player");
  bool had = p->has_q3_client;
  if (had && previous != NULL) *previous = p->q3_client;
  p->q3_client = *candidate; p->has_q3_client = true;
  return had;
}

qa_rect qa_app_world_seat_presentation_viewport(const qa_app_world_seat_presentation *p) {
  qa_sdl_size size = qa_sdl_window_drawable_size(qa_app_native_renderer_window(p->options.renderer));
  return qa_app_seat_viewport(p->layout_index, p->layout_count, size.width, size.height);
}
bool qa_app_world_seat_presentation_split_screen(const qa_app_world_seat_presentation *p) { return p->layout_count > 1; }
void qa_app_world_seat_presentation_publish_layout(qa_app_world_seat_presentation *p, double index, double count) {
  bool safe = index == trunc(index) && fabs(index) <= QA_MAX_SAFE_INTEGER && count == trunc(count) && fabs(count) <= QA_MAX_SAFE_INTEGER;
  if (!safe || index < 0 || index >= count || count < 1 || count > 4) qa_range_error("Invalid local seat layout");
  p->layout_index = index;
  p->layout_count = count;
}

qa_seat_client_state qa_app_world_seat_presentation_state(qa_app_world_seat_presentation *p) {
  qa_seat_id seat = seat_id(p);
  qa_client_id client = client_id(p);
  qa_rect viewport = qa_app_world_seat_presentation_viewport(p);
  qa_seat_client_state state;
  memset(&state, 0, sizeof state);
  state.seat = seat; state.client = client;
  state.actor.is_player = true; state.actor.actor = local_actor(p);
  state.ui = qa_native_ui_controller_state(qa_app_seat_ui_controller(p->options.ui));
  state.ui.focus = qa_seat_input_get_focus(p->local->input);
  if (p->has_hud_active) { qa_seat_hud_active_free(&p->hud_active); p->has_hud_active = false; }
  if (!qa_seat_hud_messages_active(qa_app_seat_ui_messages(p->options.ui), p->prepared_time * 1000, &p->hud_active))
    qa_range_error("Out of memory");
  p->has_hud_active = true;
  state.ui.notifications = p->hud_active.notifications; state.ui.notification_count = p->hud_active.notification_count;
  if (p->hud_active.has_center_print) state.ui.center_print = &p->hud_active.center_print;
  state.ui.show_scores = qa_input_button_active(qa_seat_input_button(p->local->input, "scores"));
  state.presentation.seat = seat; state.presentation.client = client;
  state.presentation.viewport = viewport; state.presentation.safe_area = viewport; state.presentation.hud_scale = 1;
  state.presentation.presentation = qa_app_loaded_content_recipe(qa_app_application_assets_content(p->assets))->presentation;
  return state;
}
static qa_ui_draw_context draw_context(qa_app_world_seat_presentation *p) {
  qa_ui_draw_context context;
  context.binding = qa_app_world_seat_presentation_state(p).presentation;
  context.time_milliseconds = p->prepared_time * 1000;
  return context;
}

/* ---- component clients ---- */
static const qa_mod_client_presentation_frame *viewing_frame(const qa_app_world_seat_presentation *p) {
  for (size_t i = 0; i < p->component_client_count; i++) {
    const qa_mod_client_presentation_frame *frame = &p->component_clients[i].frame;
    if ((frame->kind == QA_MOD_CLIENT_PRESENTATION_QUAKEC && frame->has_quakec_view)
        || (frame->kind == QA_MOD_CLIENT_PRESENTATION_NATIVE && frame->has_native_view)) return frame;
  }
  return NULL;
}
static bool component_view(const qa_app_world_seat_presentation *p, qa_app_sim_player_view *out) {
  const qa_mod_client_presentation_frame *frame = viewing_frame(p);
  if (frame == NULL) return false;
  if (frame->kind == QA_MOD_CLIENT_PRESENTATION_NATIVE) {
    qa_app_sim_native_mod_camera_view view;
    view.view = frame->native_view.view;
    view.native.edition = frame->native_view.edition; view.native.movement_origin = frame->native_view.movement_origin;
    view.native.render_flags = frame->native_view.render_flags;
    view.native.position_prediction = frame->native_view.position_prediction;
    view.native.angular_prediction = frame->native_view.angular_prediction;
    view.native.weapon_visible = frame->native_view.weapon_visible;
    *out = qa_app_sim_resolve_native_mod_camera(view, p->has_movement_origin ? &p->movement_origin : NULL, player_view(p).angles);
  } else *out = frame->quakec_view;
  return true;
}
static const qa_native_mod_camera_view *native_view(const qa_app_world_seat_presentation *p) {
  const qa_mod_client_presentation_frame *frame = viewing_frame(p);
  return frame != NULL && frame->kind == QA_MOD_CLIENT_PRESENTATION_NATIVE ? &frame->native_view : NULL;
}
static bool component_weapon_visible(const qa_app_world_seat_presentation *p) {
  const qa_mod_client_presentation_frame *frame = viewing_frame(p);
  return frame == NULL || frame->kind != QA_MOD_CLIENT_PRESENTATION_NATIVE || frame->native_view.weapon_visible;
}

/* ---- cameras ---- */
static qa_scene_camera client_camera(qa_app_world_seat_presentation *p, const qa_app_sim_player_view *view) {
  qa_rect viewport = qa_app_world_seat_presentation_viewport(p);
  double x = view->has_field_of_view ? view->field_of_view : field_of_view(p);
  double y = atan(viewport.height / viewport.width * tan(x * 3.141592653589793 / 360)) * 360 / 3.141592653589793;
  qa_scene_camera camera;
  memset(&camera, 0, sizeof camera);
  camera.origin = view->origin; camera.origin.z = view->origin.z + view->view_height;
  camera.axis = qa_angles_to_axis(view->angles); camera.viewport = viewport;
  camera.projection = qa_scene_perspective_projection(x, y, 16384, 4);
  return qa_app_camera_with_kick(camera, kick_of(view));
}
static bool chase_settings(qa_app_world_seat_presentation *p, qa_app_q1_chase_settings *out) {
  if (p->has_q3_client || finale_active(p)) return false;
  qa_app_q1_view_settings settings;
  if (!view_size(p, &settings) || !settings.has_chase) return false;
  *out = settings.chase;
  return true;
}
static qa_scene_camera apply_view_size(qa_app_world_seat_presentation *p, qa_scene_camera camera) {
  qa_app_q1_view_settings settings;
  if (!view_size(p, &settings)) return camera;
  return qa_app_q1_view_camera(camera, qa_app_world_seat_presentation_viewport(p), settings, finale_active(p));
}
static qa_scene_camera q3_seat_camera(qa_app_world_seat_presentation *p, qa_scene_camera q3_camera, const qa_app_sim_player_view *player) {
  bool third_person = cvar_integer(p->q3_client.cvars(p->q3_client.ctx), "cg_thirdPerson", 0) != 0;
  qa_scene_camera offset = qa_app_camera_with_client_offset(q3_camera, player);
  return apply_view_size(p, qa_app_camera_with_kick(third_person ? offset : qa_app_camera_with_character_death(offset, player), kick_of(player)));
}
static qa_scene_camera source_camera(qa_app_world_seat_presentation *p) {
  qa_app_sim_player_view controlled;
  if (component_view(p, &controlled)) return client_camera(p, &controlled);
  if (p->has_q3_client && strcmp(p->q3_client.kind, "qvm") == 0) {
    qa_app_sim_player_view player = player_view(p);
    return qa_app_camera_with_kick(qa_app_camera_with_client_offset(p->q3_client.camera(p->q3_client.ctx), &player), kick_of(&player));
  }
  qa_app_sim_player_view player = player_view(p);
  qa_app_q1_view_settings size;
  qa_rect viewport = qa_app_world_seat_presentation_viewport(p);
  if (view_size(p, &size)) viewport = qa_app_q1_view_rectangle(viewport, size.size, finale_active(p), size.overlay_status);
  if (p->has_q3_client) return q3_seat_camera(p, p->q3_client.camera(p->q3_client.ctx), &player);
  double fov_x = player.has_field_of_view ? player.field_of_view : field_of_view(p);
  double fov_y = atan(viewport.height / viewport.width * tan(fov_x * 3.141592653589793 / 360)) * 360 / 3.141592653589793;
  qa_scene_camera camera;
  memset(&camera, 0, sizeof camera);
  camera.origin = player.origin; camera.origin.z = player.origin.z + player.view_height;
  camera.axis = qa_angles_to_axis(player.angles); camera.viewport = viewport;
  camera.projection = qa_scene_perspective_projection(fov_x, fov_y, 16384, 4);
  qa_scene_camera first_person = qa_app_application_effects_player_view(p->options.effects, local_actor(p),
                                                                        qa_app_camera_with_kick(camera, kick_of(&player))).camera;
  qa_app_q1_chase_settings chase;
  if (!chase_settings(p, &chase)) return first_person;
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(qa_app_application_assets_content(p->assets));
  const qa_provider_timing *timing = NULL;
  for (size_t i = 0; i < recipe->timing_count; i++)
    if (strcmp(recipe->timing[i].provider, recipe->engine_behavior.provider) == 0) { timing = &recipe->timing[i]; break; }
  if (timing == NULL) qa_raise("Error", 0, 0, "Chase camera requires the selected numeric profile");
  qa_scene_queries queries = qa_app_application_effects_queries(p->options.effects);
  return qa_app_q1_chase_camera(first_person, player.angles, chase, &queries, timing->numeric, local_actor(p));
}
qa_scene_camera qa_app_world_seat_presentation_camera(qa_app_world_seat_presentation *p) {
  return camera_override(p, source_camera(p));
}

/* ---- events ---- */
void qa_app_world_seat_presentation_receive(qa_app_world_seat_presentation *p, const qa_simulation_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_simulation_event *event = &events[i];
    if (event->payload.kind != QA_SIM_EVENT_MESSAGE || event->payload.event.text == NULL) continue;
    pending_message message = { heap_text(event->payload.event.text), event->payload.has_source_presentation_sequence,
                                event->payload.source_presentation_sequence };
    if (qa_event_ring_admit(&p->pending_messages, &message) == 0) { free(message.text); qa_range_error("Event ring capacity exhausted"); }
  }
}
static bool owns(const qa_app_world_seat_presentation *p, qa_actor_id actor) { return qa_same_actor(actor, local_actor(p)); }
static bool q2_message_kind(const char *kind) {
  return strcmp(kind, "help") == 0 || strcmp(kind, "pickup") == 0 || strcmp(kind, "print") == 0 || strcmp(kind, "centerprint") == 0;
}
static void admit_copy(qa_event_ring *ring, const qa_app_sim_presentation_event *event) {
  qa_app_sim_presentation_event copy;
  if (!qa_app_sim_presentation_event_copy(&copy, event)) qa_range_error("Out of memory");
  if (qa_event_ring_admit(ring, &copy) == 0) { qa_app_sim_presentation_event_release(&copy); qa_range_error("Event ring capacity exhausted"); }
}
static void clear_component_client(component_client *client) {
  qa_app_q2_native_hud_clear(client->hud);
}
void qa_app_world_seat_presentation_source_events(qa_app_world_seat_presentation *p, const qa_app_sim_presentation_event *incoming, size_t count) {
  qa_app_sim_presentation_event *events = calloc(count == 0 ? 1 : count, sizeof *events);
  if (events == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, events);
  size_t event_count = 0;
  for (size_t i = 0; i < count; i++) if (!incoming[i].has_recipient || owns(p, incoming[i].recipient)) events[event_count++] = incoming[i];
  for (size_t i = 0; i < event_count; i++) {
    if (strcmp(events[i].kind, "presentation-owner") != 0) continue;
    qa_presentation_owner owner = events[i].presentation_owner.owner;
    for (size_t k = 0; k < p->component_client_count;) {
      component_client *client = &p->component_clients[k];
      if (qa_same_presentation_owner(&client->source.owner, owner)) {
        clear_component_client(client);
        qa_app_q2_native_hud_destroy(client->hud);
        memmove(client, client + 1, (p->component_client_count - k - 1) * sizeof *client);
        p->component_client_count--;
      } else k++;
    }
  }
  qa_app_component_drawings_receive(p->component_drawings, events, event_count);
  /* Q1MapFog takes its event view. */
  qa_app_q1_fog_event *fog = calloc(event_count == 0 ? 1 : event_count, sizeof *fog);
  if (fog == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_sim_presentation_event *e = &events[i];
    fog[i].kind = e->kind; fog[i].has_owner = e->has_owner; if (e->has_owner) fog[i].owner = e->owner;
    fog[i].content = e->content;
    if (strcmp(e->kind, "presentation-owner") == 0) { fog[i].event_kind = e->presentation_owner.kind; fog[i].has_owner = true; fog[i].owner = e->presentation_owner.owner; }
    else if (strcmp(e->kind, "q1-fog") == 0) {
      fog[i].event_kind = e->q1_fog.kind; fog[i].has_player = e->q1_fog.has_player; fog[i].player = e->q1_fog.player;
      fog[i].transition = e->q1_fog.transition; fog[i].sky_factor = e->q1_fog.sky_factor;
    }
  }
  qa_app_q1_map_fog_receive(p->q1_fog, fog, event_count);
  free(fog);
  qa_app_q1_service_presentation_receive(p->q1_services, events, event_count);
  for (size_t i = 0; i < event_count; i++)
    if (strcmp(events[i].kind, "q1") == 0 && strcmp(events[i].q1->kind, "message") == 0 && owns(p, events[i].q1->player))
      admit_copy(&p->pending_q1, &events[i]);
  if (p->has_q3_client) {
    if (qa_input_command_builder_dialect(p->local->builder) != QA_COMMAND_DIALECT_Q3)
      for (size_t i = 0; i < event_count; i++)
        if (strcmp(events[i].kind, "view-reset") == 0 && owns(p, events[i].view_reset.actor))
          qa_input_command_builder_set_view_angles(p->local->builder, events[i].view_reset.angles);
    qa_cleanup_pop(&guard);
    free(events);
    return;
  }
  qa_app_application_world_scene_receive(p->scene, events, event_count);
  qa_app_sim_presentation_event *ui_events = calloc(event_count == 0 ? 1 : event_count, sizeof *ui_events);
  if (ui_events == NULL) qa_range_error("Out of memory");
  size_t ui_count = 0;
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_sim_presentation_event *e = &events[i];
    if (strcmp(e->kind, "q1") == 0 && strcmp(e->q1->kind, "message") == 0) continue;
    if (strcmp(e->kind, "q2") == 0 && q2_message_kind(e->q2->kind)) continue;
    if (strcmp(e->kind, "q2-player") == 0 && strcmp(e->q2_player->kind, "print") == 0) continue;
    ui_events[ui_count++] = *e;
  }
  qa_app_seat_ui_receive(p->options.ui, ui_events, ui_count);
  free(ui_events);
  qa_app_source_finale_receive(p->finale, events, event_count);
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_sim_presentation_event *e = &events[i];
    if (strcmp(e->kind, "view-reset") == 0) {
      if (owns(p, e->view_reset.actor)) qa_input_command_builder_set_view_angles(p->local->builder, e->view_reset.angles);
    } else if (strcmp(e->kind, "q1") == 0) {
      if (strcmp(e->q1->kind, "teleport-player") == 0 && owns(p, e->q1->player))
        qa_input_command_builder_set_view_angles(p->local->builder, e->q1->angles);
    } else if (strcmp(e->kind, "q2") == 0) {
      if (q2_message_kind(e->q2->kind)) admit_copy(&p->pending_q2, e);
    } else if (strcmp(e->kind, "q2-player") == 0) {
      if (strcmp(e->q2_player->kind, "print") == 0) admit_copy(&p->pending_q2, e);
    } else if (strcmp(e->kind, "q3-source") == 0 && e->q3_source->kind == QA_APP_Q3_EVENT_SERVER_COMMAND
               && (e->q3_source->client < 0 || e->q3_source->client == (double)client_id(p).slot)) {
      qa_command_tokens tokens;
      if (!qa_tokenize_command(e->q3_source->text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
      if (tokens.argc >= 2 && (strcmp(tokens.argv[0], "print") == 0 || strcmp(tokens.argv[0], "chat") == 0 || strcmp(tokens.argv[0], "tchat") == 0))
        qa_seat_console_print(p->local->console, tokens.argv[1]);
      qa_command_tokens_destroy(&tokens);
    }
  }
  qa_cleanup_pop(&guard);
  free(events);
}

/* ---- prepare ---- */
typedef struct palette_state { qa_app_world_seat_presentation *p; } palette_state;
static bool load_palette(void *ctx, qa_content_id content, qa_palette *out, const char **out_error) {
  qa_app_world_seat_presentation *p = ctx;
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(p->assets, content);
  if (provider.palette == NULL) { *out_error = "Native component debug graph requires its source palette"; return false; }
  *out = *provider.palette;
  return true;
}
typedef struct hud_assert {
  qa_app_world_seat_presentation *p;
  qa_actor_id actor;
  qa_app_q2_native_hud *hud; /* the client's identity */
} hud_assert;
static void component_assert_current(void *ctx) {
  hud_assert *check = ctx;
  qa_app_world_seat_presentation *p = check->p;
  component_client *client = NULL;
  for (size_t i = 0; i < p->component_client_count; i++) if (p->component_clients[i].hud == check->hud) client = &p->component_clients[i];
  if (client != NULL) client->source.source.assert_current(client->source.source.ctx);
  qa_mod_client_presentation_frame frame;
  if (p->closed || !qa_same_actor(local_actor(p), check->actor) || client == NULL
      || client->generation != client->source.source.generation
      || !client->source.source.frame(client->source.source.ctx, check->actor, &frame))
    qa_raise("Error", 0, 0, "Component client presentation is retired");
}
static void prepare_component_clients(qa_app_world_seat_presentation *p) {
  size_t source_count = 0;
  const qa_active_mod_client_presentation *sources = p->options.simulation->mod_client_presentation_sources != NULL
      ? p->options.simulation->mod_client_presentation_sources(p->options.simulation->ctx, &source_count) : NULL;
  qa_actor_id actor = local_actor(p);
  component_client *next = calloc(source_count == 0 ? 1 : source_count, sizeof *next);
  if (next == NULL) qa_range_error("Out of memory");
  size_t next_count = 0;
  for (size_t i = 0; i < source_count; i++) {
    const qa_active_mod_client_presentation *source = &sources[i];
    source->source.assert_current(source->source.ctx);
    qa_mod_client_presentation_frame frame;
    if (!source->source.frame(source->source.ctx, actor, &frame)) continue;
    qa_app_q2_native_hud *hud = NULL;
    for (size_t k = 0; k < p->component_client_count; k++) {
      const component_client *previous = &p->component_clients[k];
      if (qa_same_presentation_owner(&previous->source.owner, source->owner) && previous->source.source.ctx == source->source.ctx
          && previous->generation == source->source.generation) { hud = previous->hud; break; }
    }
    if (hud == NULL) hud = qa_app_q2_native_hud_create();
    next[next_count++] = (component_client){ *source, source->source.generation, frame, hud };
  }
  for (size_t k = 0; k < p->component_client_count; k++) {
    bool kept = false;
    for (size_t i = 0; i < next_count; i++) if (next[i].hud == p->component_clients[k].hud) kept = true;
    if (!kept) { qa_app_q2_native_hud_clear(p->component_clients[k].hud); qa_app_q2_native_hud_destroy(p->component_clients[k].hud); }
  }
  free(p->component_clients);
  p->component_clients = next; p->component_client_count = next_count; p->component_client_capacity = source_count == 0 ? 1 : source_count;
  for (size_t i = 0; i < p->component_client_count; i++) {
    component_client *client = &p->component_clients[i];
    if (client->frame.kind != QA_MOD_CLIENT_PRESENTATION_NATIVE || !client->frame.has_native_hud) continue;
    hud_assert *check = calloc(1, sizeof *check);
    if (check == NULL) qa_range_error("Out of memory");
    store_add(&p->effect_store, free, check);
    *check = (hud_assert){ p, actor, client->hud };
    qa_app_seat_ui_q2_component component = { client->hud, client->frame.native_hud_mode, component_assert_current, check };
    qa_app_seat_ui_prepare_native_q2_hud(p->options.ui, &client->frame.native_hud_frame, client->source.identity.source.content,
                                         p->assets, draw_context(p), &component);
    component_assert_current(check);
  }
}

typedef struct body_list { qa_app_component_body *items; size_t count, capacity; } body_list;
static body_list effect_bodies(qa_app_world_seat_presentation *p) {
  body_list list = {0};
  for (size_t i = 0; i < p->component_effect_count; i++) {
    const component_effect *effect = &p->component_effects[i];
    if (!effect->has_overlay || effect->overlay.bodies == NULL) continue;
    size_t count = 0;
    const qa_app_component_body *bodies = effect->overlay.bodies(effect->overlay.ctx, &count);
    for (size_t k = 0; k < count; k++) { GROW(list.items, list.count, list.capacity, 4); list.items[list.count++] = bodies[k]; }
  }
  return list;
}

static char *localize_q2(qa_app_world_seat_presentation *p, qa_content_id content, const char *text) {
  if (p->options.rerelease == NULL) return heap_text(text);
  return qa_app_rerelease_localize_message(p->options.rerelease, seat_id(p), content, text, NULL, 0);
}
static void console_print(qa_app_world_seat_presentation *p, const char *text, bool newline) {
  if (!newline) { qa_seat_console_print(p->local->console, text); return; }
  size_t length = strlen(text);
  char *line = malloc(length + 2);
  if (line == NULL) qa_range_error("Out of memory");
  memcpy(line, text, length); line[length] = '\n'; line[length + 1] = '\0';
  qa_seat_console_print(p->local->console, line);
  free(line);
}
static void release_take(void *value) { free(value); }

void qa_app_world_seat_presentation_prepare(qa_app_world_seat_presentation *p, const qa_world_snapshot *snapshot,
    const qa_app_simulation_presentation *presentations, size_t presentation_count,
    const qa_q3_character_view *characters, size_t character_count) {
  store_release(&p->effect_store);
  p->prepared_time = snapshot->frame.time.kind == QA_SOURCE_TIME_SECONDS ? snapshot->frame.time.value : snapshot->frame.time.value / 1000;
  p->has_movement_origin = false;
  for (size_t i = 0; i < snapshot->body_count; i++)
    if (qa_same_actor(snapshot->bodies[i].actor, local_actor(p))) { p->has_movement_origin = true; p->movement_origin = snapshot->bodies[i].body.origin; break; }
  prepare_component_clients(p);
  const char *graph_error = qa_app_component_drawings_prepare_graphs(p->component_drawings, load_palette, p);
  if (graph_error != NULL) qa_raise("Error", 0, 0, "%s", graph_error);
  body_list bodies = effect_bodies(p);
  if (bodies.items != NULL) store_add(&p->effect_store, free, bodies.items);
  qa_app_simulation_presentation *visible = store_alloc(&p->effect_store, (presentation_count == 0 ? 1 : presentation_count) * sizeof *visible);
  size_t visible_count = 0;
  for (size_t i = 0; i < presentation_count; i++) {
    const qa_app_simulation_presentation *presentation = &presentations[i];
    if (presentation->render_owner != NULL && strcmp(presentation->render_owner, "source-client") == 0) continue;
    if (p->options.rerelease != NULL && !qa_app_rerelease_item_visible(p->options.rerelease, local_actor(p), presentation->actor)) continue;
    visible[visible_count++] = *presentation;
  }
  size_t q1_count = 0, q2_count = 0, message_count = 0;
  qa_app_sim_presentation_event *q1_messages = qa_event_ring_take(&p->pending_q1, &p->pending_q1_cursor, &q1_count);
  qa_event_ring_retire(&p->pending_q1);
  qa_app_sim_presentation_event *q2_messages = qa_event_ring_take(&p->pending_q2, &p->pending_q2_cursor, &q2_count);
  qa_event_ring_retire(&p->pending_q2);
  qa_cleanup q1_guard, q2_guard;
  qa_cleanup_push(&q1_guard, release_take, q1_messages);
  qa_cleanup_push(&q2_guard, release_take, q2_messages);
  pending_message *messages = qa_event_ring_take(&p->pending_messages, &p->pending_messages_cursor, &message_count);
  qa_event_ring_retire(&p->pending_messages);
  for (size_t i = 0; i < message_count; i++) {
    bool mirrored = false;
    if (messages[i].has_sequence) {
      for (size_t k = 0; k < q1_count && !mirrored; k++) mirrored = q1_messages[k].sequence == messages[i].sequence;
      for (size_t k = 0; k < q2_count && !mirrored; k++) {
        const qa_app_sim_presentation_event *source = &q2_messages[k];
        if (strcmp(source->kind, "q2") == 0 && (strcmp(source->q2->kind, "help") == 0
            || (strcmp(source->q2->kind, "centerprint") == 0 && owns(p, source->q2->actor))))
          mirrored = source->sequence == messages[i].sequence;
      }
    }
    if (!mirrored) console_print(p, messages[i].text, true);
    free(messages[i].text);
  }
  free(messages);
  for (size_t i = 0; i < q1_count; i++) {
    const qa_app_sim_presentation_event *source = &q1_messages[i];
    const qa_q1_event *event = source->q1;
    if (strcmp(event->kind, "message") != 0) continue;
    char *text = qa_app_q1_message_localization_resolve(p->q1_messages, source->content, event->text, event->args, event->arg_count,
                                                        event->parts, event->part_count);
    if (!event->center) console_print(p, text, true);
    qa_q1_event resolved = *event;
    resolved.text = text;
    qa_app_sim_presentation_event copy = *source;
    copy.q1 = &resolved;
    qa_app_seat_ui_receive(p->options.ui, &copy, 1);
    free(text);
  }
  for (size_t i = 0; i < q2_count; i++) {
    const qa_app_sim_presentation_event *source = &q2_messages[i];
    if (strcmp(source->kind, "q2") == 0) {
      const qa_q2_presentation_event *event = source->q2;
      if (strcmp(event->kind, "pickup") == 0 && owns(p, event->player)) {
        char *text = localize_q2(p, source->content, event->name);
        console_print(p, text, true);
        free(text);
      } else if (strcmp(event->kind, "help") == 0 || (strcmp(event->kind, "print") == 0 && (!event->has_actor || owns(p, event->actor)))
                 || (strcmp(event->kind, "centerprint") == 0 && owns(p, event->actor))) {
        char *text = localize_q2(p, source->content, event->text);
        if (strcmp(event->kind, "centerprint") != 0) console_print(p, text, strcmp(event->kind, "help") == 0);
        qa_q2_presentation_event resolved = *event;
        resolved.text = text;
        qa_app_sim_presentation_event copy = *source;
        copy.q2 = &resolved;
        qa_app_seat_ui_receive(p->options.ui, &copy, 1);
        free(text);
      }
    } else if (strcmp(source->q2_player->kind, "print") == 0 && (!source->q2_player->has_target || owns(p, source->q2_player->target))) {
      char *text = localize_q2(p, source->content, source->q2_player->text);
      console_print(p, text, false);
      qa_q2_player_event resolved = *source->q2_player;
      resolved.text = text;
      qa_app_sim_presentation_event copy = *source;
      copy.q2_player = &resolved;
      qa_app_seat_ui_receive(p->options.ui, &copy, 1);
      free(text);
    }
  }
  qa_cleanup_pop(&q2_guard);
  qa_cleanup_pop(&q1_guard);
  for (size_t i = 0; i < q1_count; i++) qa_app_sim_presentation_event_release(&q1_messages[i]);
  for (size_t i = 0; i < q2_count; i++) qa_app_sim_presentation_event_release(&q2_messages[i]);
  free(q1_messages); free(q2_messages);
  qa_app_q1_service_assets services = q1_service_assets(p);
  qa_app_q1_service_presentation_prepare(p->q1_services, &services);
  qa_app_seat_ui_prepare(p->options.ui, p->assets);
  qa_app_component_drawings_snapshot drawings = qa_app_component_drawings_snapshot_at(p->component_drawings, p->prepared_time,
                                                                                       ++p->component_drawing_frame);
  p->component_lines = drawings.lines; p->component_line_count = drawings.line_count;
  size_t simulation_count = 0;
  const qa_world_text *simulation_text = p->options.simulation->world_text(p->options.simulation->ctx, &simulation_count);
  free(p->world_text);
  p->world_text_count = simulation_count + drawings.text_count;
  p->world_text = calloc(p->world_text_count == 0 ? 1 : p->world_text_count, sizeof *p->world_text);
  if (p->world_text == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < simulation_count; i++) p->world_text[i] = simulation_text[i];
  for (size_t i = 0; i < drawings.text_count; i++) p->world_text[simulation_count + i] = *drawings.text[i];
  for (size_t i = 0; i < p->world_text_count; i++) {
    bool known = false;
    for (size_t k = 0; k < p->world_font_count; k++) if (strcmp(p->world_fonts[k].content, p->world_text[i].content) == 0) known = true;
    if (known) continue;
    qa_app_world_font *font = qa_app_application_assets_load_world_font(p->assets, p->world_text[i].content,
                                                                        qa_app_application_assets_image_policy(p->assets));
    GROW(p->world_fonts, p->world_font_count, p->world_font_capacity, 4);
    p->world_fonts[p->world_font_count++] = (world_font){ heap_text(p->world_text[i].content), font };
  }
  p->prepared_time = snapshot->frame.time.kind == QA_SOURCE_TIME_SECONDS ? snapshot->frame.time.value : snapshot->frame.time.value / 1000;
  p->has_native_q2_frame = p->options.has_native_q2;
  if (p->has_native_q2_frame) {
    p->native_q2_frame = p->options.native_q2_frame(p->options.native_q2_ctx);
    qa_app_seat_ui_prepare_native_q2_hud(p->options.ui, &p->native_q2_frame,
        qa_app_loaded_content_recipe(qa_app_application_assets_content(p->assets))->presentation.assets, p->assets, draw_context(p), NULL);
  }
  if (p->has_q3_client) {
    qa_app_seat_q3_client *q3 = &p->q3_client;
    qa_app_q1_view_settings size;
    qa_rect viewport = qa_app_world_seat_presentation_viewport(p);
    if (view_size(p, &size)) viewport = qa_app_q1_view_rectangle(viewport, size.size, finale_active(p), size.overlay_status);
    bool draw_status = true;
    for (size_t i = 0; i < p->component_client_count; i++) {
      const qa_mod_client_presentation_frame *frame = &p->component_clients[i].frame;
      bool has_hud = frame->kind == QA_MOD_CLIENT_PRESENTATION_QUAKEC ? frame->has_quakec_hud
          : frame->kind == QA_MOD_CLIENT_PRESENTATION_NATIVE ? frame->has_native_hud : frame->qvm_hud_mode != NULL;
      const char *mode = frame->kind == QA_MOD_CLIENT_PRESENTATION_NATIVE ? frame->native_hud_mode
          : frame->kind == QA_MOD_CLIENT_PRESENTATION_QVM ? frame->qvm_hud_mode : NULL;
      if (has_hud && (frame->kind == QA_MOD_CLIENT_PRESENTATION_QUAKEC || (mode != NULL && strcmp(mode, "replace-status") == 0))) draw_status = false;
    }
    q3->prepare(q3->ctx, snapshot->frame.frame, viewport, visible, visible_count, draw_status, bodies.items, bodies.count,
                component_weapon_visible(p), native_view(p) != NULL);
    qa_cvar_registry *cvars = q3->cvars(q3->ctx);
    bool third_person = native_view(p) == NULL && cvar_integer(cvars, "cg_thirdPerson", 0) != 0;
    bool shared = false, shared_visible = false;
    if (q3->shared_equipment_view_visible != NULL) shared = q3->shared_equipment_view_visible(q3->ctx, &shared_visible);
    bool draw_weapon = component_weapon_visible(p) && (shared ? shared_visible : cvar_integer(cvars, "cg_drawGun", 1) != 0);
    qa_app_simulation_presentation *supplemental = store_alloc(&p->effect_store, (visible_count == 0 ? 1 : visible_count) * sizeof *supplemental);
    size_t supplemental_count = 0;
    for (size_t i = 0; i < visible_count; i++) {
      if (visible[i].render_owner != NULL && strcmp(visible[i].render_owner, "source-client") == 0) continue;
      qa_app_simulation_presentation source = visible[i];
      qa_vec3 origin, angles;
      if (!source.view_weapon && q3->body_pose(q3->ctx, source.actor, &origin, &angles)) { source.origin = origin; source.angles = angles; }
      supplemental[supplemental_count++] = source;
    }
    qa_app_world_scene_prepare_options options = qa_app_world_scene_prepare_defaults();
    options.field_of_view = cvar_integer(cvars, "cg_fov", 90);
    options.held_weapons = q3->shared_held_weapons(q3->ctx, &options.held_weapon_count);
    options.view_weapon_visible = draw_weapon;
    options.bodies = bodies.items; options.body_count = bodies.count;
    options.primary_bodies = q3->shared_bodies(q3->ctx, &options.primary_body_count);
    qa_actor_id actor = local_actor(p);
    qa_app_application_world_scene_prepare(p->scene, third_person ? NULL : &actor, snapshot, supplemental, supplemental_count, NULL, 0, &options);
    return;
  }
  const char *finale_error = qa_app_source_finale_prepare(p->finale);
  if (finale_error != NULL && qa_app_source_finale_active(p->finale)) qa_raise("Error", 0, 0, "%s", finale_error);
  qa_app_q1_chase_settings chase;
  qa_actor_id actor = local_actor(p);
  bool viewer = native_view(p) != NULL || !chase_settings(p, &chase);
  qa_app_world_scene_prepare_options options = qa_app_world_scene_prepare_defaults();
  options.field_of_view = atan(1 / qa_app_world_seat_presentation_camera(p).projection.values[0]) * 360 / 3.141592653589793;
  options.view_weapon_visible = component_weapon_visible(p);
  options.bodies = bodies.items; options.body_count = bodies.count;
  qa_app_application_world_scene_prepare(p->scene, viewer ? &actor : NULL, snapshot, visible, visible_count, characters, character_count, &options);
}

int64_t qa_app_world_seat_presentation_bind_component_effects(qa_app_world_seat_presentation *p,
    qa_app_component_effect_frame frame, const qa_app_component_overlay *overlay) {
  for (size_t i = 0; i < p->component_effect_count; i++)
    if (p->component_effects[i].frame.ctx == frame.ctx && p->component_effects[i].frame.frame == frame.frame) {
      p->component_effects[i].has_overlay = overlay != NULL;
      if (overlay != NULL) p->component_effects[i].overlay = *overlay;
      return p->component_effects[i].id;
    }
  GROW(p->component_effects, p->component_effect_count, p->component_effect_capacity, 4);
  component_effect *effect = &p->component_effects[p->component_effect_count++];
  memset(effect, 0, sizeof *effect);
  effect->id = p->next_binding++; effect->frame = frame;
  effect->has_overlay = overlay != NULL;
  if (overlay != NULL) effect->overlay = *overlay;
  return effect->id;
}
void qa_app_world_seat_presentation_unbind_component_effects(qa_app_world_seat_presentation *p, int64_t binding) {
  for (size_t i = 0; i < p->component_effect_count; i++)
    if (p->component_effects[i].id == binding) {
      memmove(&p->component_effects[i], &p->component_effects[i + 1], (p->component_effect_count - i - 1) * sizeof *p->component_effects);
      p->component_effect_count--;
      return;
    }
}

/* ---- frame ---- */
typedef struct seat_effect_frame {
  const qa_q3_pres_scene_admission **admissions; size_t admission_count;
  qa_scene_operation *operations; size_t operation_count;
  qa_surface_dynamic_light *lights; size_t light_count;
  qa_q3_dynamic_light *q3_lights; size_t q3_light_count;
} seat_effect_frame;
static void release_effect_frame(void *value) { qa_app_application_effect_frame_destroy(value); free(value); }
static bool polygon_operation(const qa_scene_operation *operation) {
  return operation->is_group && operation->group.order.kind == QA_GROUP_SOURCE && !operation->group.order.source.entity.is_refentity;
}
/* TS private effectFrame(camera, source, viewer, fog). Values live in store. */
static seat_effect_frame effect_frame(qa_app_world_seat_presentation *p, value_store *store, qa_scene_camera camera,
                                      qa_source_scene_order *source, const qa_actor_id *viewer, const qa_scene_fog *fog) {
  size_t frame_count = 1 + p->component_effect_count;
  qa_app_application_effect_frame **frames = store_alloc(store, frame_count * sizeof *frames);
  for (size_t i = 0; i < frame_count; i++) {
    qa_app_application_effect_frame *frame = calloc(1, sizeof *frame);
    if (frame == NULL) qa_range_error("Out of memory");
    store_add(store, release_effect_frame, frame);
    if (i == 0) qa_app_application_effects_frame(p->options.effects, camera, source, viewer, fog, frame);
    else p->component_effects[i - 1].frame.frame(p->component_effects[i - 1].frame.ctx, camera, source, fog, frame);
    frames[i] = frame;
  }
  seat_effect_frame out;
  memset(&out, 0, sizeof out);
  size_t admissions = 0, operations = 0, lights = 0, q3_lights = 0;
  const qa_scene_operation **converted = store_alloc(store, frame_count * sizeof *converted);
  size_t *converted_count = store_alloc(store, frame_count * sizeof *converted_count);
  for (size_t i = 0; i < frame_count; i++) {
    admissions += frames[i]->q3_admission_count; lights += frames[i]->light_count; q3_lights += frames[i]->q3_light_count;
    converted[i] = qa_app_application_world_scene_effect_operations(p->scene, &frames[i]->operations, &converted_count[i]);
    operations += converted_count[i];
  }
  out.admissions = store_alloc(store, (admissions == 0 ? 1 : admissions) * sizeof *out.admissions);
  out.operations = store_alloc(store, (operations == 0 ? 1 : operations) * sizeof *out.operations);
  out.lights = store_alloc(store, (lights == 0 ? 1 : lights) * sizeof *out.lights);
  out.q3_lights = store_alloc(store, (q3_lights == 0 ? 1 : q3_lights) * sizeof *out.q3_lights);
  for (size_t i = 0; i < frame_count; i++) {
    for (size_t k = 0; k < frames[i]->q3_admission_count; k++) out.admissions[out.admission_count++] = frames[i]->q3_admissions[k];
    for (size_t k = 0; k < frames[i]->light_count; k++) out.lights[out.light_count++] = frames[i]->lights[k];
    for (size_t k = 0; k < frames[i]->q3_light_count; k++) out.q3_lights[out.q3_light_count++] = frames[i]->q3_lights[k];
  }
  if (p->component_effect_count == 0) {
    /* TS returns the base frame as is. */
    for (size_t k = 0; k < converted_count[0]; k++) out.operations[out.operation_count++] = converted[0][k];
    return out;
  }
  for (size_t pass = 0; pass < 2; pass++)
    for (size_t i = 0; i < frame_count; i++)
      for (size_t k = 0; k < converted_count[i]; k++)
        if (polygon_operation(&converted[i][k]) == (pass == 0)) out.operations[out.operation_count++] = converted[i][k];
  if (out.q3_light_count > 32) out.q3_light_count = 32;
  return out;
}

typedef struct frame_state {
  qa_app_world_seat_presentation *p;
  qa_world_view_input input;
  qa_rect viewport;
} frame_state;
static void emit_command(void *ctx, const qa_render_command *command) {
  frame_state *state = ctx;
  if (command->kind == QA_RENDER_COMMAND_SWAP_BUFFERS) qa_raise("Error", 0, 0, "Text cannot present a frame");
  qa_scene_frame_builder_command(&state->p->frames, *command);
}
static void ui_emit(void *ctx, const qa_render_command *command) {
  frame_state *state = ctx;
  qa_scene_frame_builder_command(&state->p->frames, *command);
}
static qa_render_view overlay_view(const qa_app_world_seat_presentation *p, qa_source_time time, qa_rect viewport,
                                   const qa_render_operation *operations, size_t count) {
  qa_render_view view;
  memset(&view, 0, sizeof view);
  view.state.viewport = viewport;
  view.target_is_seat = true; view.target_seat = seat_id(p);
  view.time = time;
  view.operations = operations; view.operation_count = count;
  return view;
}
static void material_text(void *ctx, const qa_material_text_draw *draw) {
  frame_state *state = ctx;
  qa_app_world_seat_presentation *p = state->p;
  qa_world_material_context *context = store_alloc(&p->frame_store, sizeof *context);
  qa_world_scene_material_context(qa_app_application_assets_world(p->assets), &state->input, NULL, NULL, context);
  qa_draw_batch *batches = NULL; size_t count = 0;
  (void)qa_prepare_material_text(*draw, state->viewport, context->context, &batches, &count);
  if (batches != NULL) store_add(&p->frame_store, free, batches);
  qa_render_operation *operation = store_alloc(&p->frame_store, sizeof *operation);
  operation->kind = QA_RENDER_OP_DRAW; operation->batches = batches; operation->batch_count = count;
  qa_scene_frame_builder_view(&p->frames, overlay_view(p, state->input.time, state->viewport, operation, 1));
}
static void ui_material(void *ctx, const qa_material_text_draw *draw) { material_text(ctx, draw); }
static qa_text_font_selection world_font_for(void *ctx, const qa_world_text *text) {
  qa_app_world_seat_presentation *p = ctx;
  for (size_t i = 0; i < p->world_font_count; i++)
    if (strcmp(p->world_fonts[i].content, text->content) == 0) return qa_app_world_font_selection(p->world_fonts[i].font);
  qa_raise("Error", 0, 0, "World text font was not prepared");
}
static double style_of(void *ctx, double index) {
  qa_app_world_seat_presentation *p = ctx;
  return qa_app_application_world_scene_style(p->scene, index, 12) / 12;
}
static qa_picture_asset white_picture(const qa_app_world_seat_presentation *p) {
  qa_picture_asset picture;
  memset(&picture, 0, sizeof picture);
  picture.kind = QA_PICTURE_IMAGE;
  picture.image.name = "white";
  picture.image.image = qa_app_application_assets_world(p->assets)->shaders->textures->white->image;
  return picture;
}
/* The q3Client.frame hooks. */
typedef struct q3_hooks_state { qa_app_world_seat_presentation *p; bool no_world_model, infrared; qa_source_time time;
                                bool has_fog; qa_scene_fog fog; } q3_hooks_state;
static qa_app_seat_q3_effects q3_effects(void *ctx, qa_scene_camera camera, qa_source_scene_order *source) {
  q3_hooks_state *state = ctx;
  qa_app_world_seat_presentation *p = state->p;
  qa_actor_id actor = local_actor(p);
  seat_effect_frame effects = effect_frame(p, &p->effect_store, camera, source, &actor, state->has_fog ? &state->fog : NULL);
  qa_world_view_input *input = store_alloc(&p->effect_store, sizeof *input);
  input->no_world_model = state->no_world_model; input->camera = camera; input->time = state->time;
  input->target_is_seat = true; input->target_seat = seat_id(p);
  input->world_space_batches = qa_sdl_window_backend(qa_app_native_renderer_window(p->options.renderer)) == QA_SDL_BACKEND_GL
      && (getenv("QTS_WORLD_SPACE") == NULL || strcmp(getenv("QTS_WORLD_SPACE"), "0") != 0);
  qa_world_surface_admission *admission = store_alloc(&p->effect_store, sizeof *admission);
  qa_create_world_surface_admission(source, admission);
  store_add(&p->effect_store, (void (*)(void *))qa_world_surface_admission_destroy, admission);
  input->source = admission;
  input->lights = effects.lights; input->light_count = effects.light_count; input->has_lights = true;
  input->visibility.q3_lights = effects.q3_lights; input->visibility.q3_light_count = effects.q3_light_count;
  if (state->has_fog) { input->has_q1_fog = true; input->q1_fog = state->fog; }
  qa_scene_camera weapon_camera = p->q3_client.supplemental_weapon_camera(p->q3_client.ctx, camera);
  size_t count = 0;
  const qa_scene_operation *supplemental = qa_app_application_world_scene_supplemental(p->scene, input, &weapon_camera, state->infrared, &count);
  qa_scene_operation *operations = store_alloc(&p->effect_store, (effects.operation_count + count + 1) * sizeof *operations);
  memcpy(operations, effects.operations, effects.operation_count * sizeof *operations);
  if (count != 0) memcpy(operations + effects.operation_count, supplemental, count * sizeof *operations);
  qa_app_seat_q3_effects out = { effects.admissions, effects.admission_count, operations, effects.operation_count + count,
                                 effects.lights, effects.light_count, effects.q3_lights, effects.q3_light_count };
  return out;
}
static qa_scene_camera q3_camera_hook(void *ctx, qa_scene_camera camera) {
  q3_hooks_state *state = ctx;
  qa_app_world_seat_presentation *p = state->p;
  qa_app_sim_player_view controlled;
  if (component_view(p, &controlled)) return camera_override(p, client_camera(p, &controlled));
  qa_app_sim_player_view player = player_view(p);
  if (strcmp(p->q3_client.kind, "qvm") == 0)
    return camera_override(p, qa_app_camera_with_kick(qa_app_camera_with_client_offset(camera, &player), kick_of(&player)));
  return camera_override(p, q3_seat_camera(p, camera, &player));
}

/* The console's Draw2D and SeatTextPresentation views (console/draw.h). */
typedef struct console_draw_state { qa_app_world_seat_presentation *p; const qa_draw2d *draw; } console_draw_state;
/* TS drawConsole's clipped Draw2D: every stretch is clipped to bounds. */
typedef struct clip_sink { const qa_text_draw_sink *sink; qa_rect bounds; } clip_sink;
static void clip_set_color(void *ctx, const qa_vec4 *color) {
  const clip_sink *clip = ctx;
  clip->sink->set_color(clip->sink->ctx, color);
}
static void clip_stretch(void *ctx, qa_rect rect, qa_text_texture_rect uv, const qa_picture_asset *picture) {
  const clip_sink *clip = ctx;
  qa_clipped_picture clipped;
  qa_texture_rect source = { uv.s, uv.t, uv.s2, uv.t2 };
  if (!qa_clip_picture(rect, source, clip->bounds, &clipped)) return;
  qa_text_texture_rect out = { clipped.uv.s1, clipped.uv.t1, clipped.uv.s2, clipped.uv.t2 };
  clip->sink->stretch_pixels(clip->sink->ctx, clipped.rect, out, picture);
}
static qa_draw2d clipped_draw(const qa_draw2d *draw, clip_sink *clip, qa_rect bounds) {
  clip->sink = &draw->commands; clip->bounds = bounds;
  qa_draw2d out = *draw;
  out.commands.ctx = clip; out.commands.set_color = clip_set_color; out.commands.stretch_pixels = clip_stretch;
  return out;
}
static void console_draw_pic(void *ctx, qa_rect rect, const void *picture, qa_rect clip) {
  console_draw_state *state = ctx;
  clip_sink sink;
  qa_draw2d draw = clipped_draw(state->draw, &sink, clip);
  qa_draw2d_draw_pic(&draw, rect, picture);
}
static void console_glyph_metrics(void *ctx, const void *font, uint32_t character, double line_height, double cell_width,
                                  double *out_line_height, double *out_top) {
  (void)ctx;
  char text[8] = {0};
  if (character < 0x80) text[0] = (char)character;
  else if (character < 0x800) { text[0] = (char)(0xc0 | (character >> 6)); text[1] = (char)(0x80 | (character & 63)); }
  else if (character < 0x10000) { text[0] = (char)(0xe0 | (character >> 12)); text[1] = (char)(0x80 | ((character >> 6) & 63)); text[2] = (char)(0x80 | (character & 63)); }
  else { text[0] = (char)(0xf0 | (character >> 18)); text[1] = (char)(0x80 | ((character >> 12) & 63)); text[2] = (char)(0x80 | ((character >> 6) & 63)); text[3] = (char)(0x80 | (character & 63)); }
  qa_console_glyph_metrics metrics = qa_console_glyph_metrics_for(font, text, line_height, cell_width);
  *out_line_height = metrics.line_height; *out_top = metrics.top;
}
static qa_text_layout_options console_options(const qa_app_world_seat_presentation *p, char *text, uint32_t character, double scale,
                                              qa_vec4 color, bool alternate, double line_height) {
  size_t length = 0;
  if (character < 0x80) text[length++] = (char)character;
  else if (character < 0x800) { text[length++] = (char)(0xc0 | (character >> 6)); text[length++] = (char)(0x80 | (character & 63)); }
  else if (character < 0x10000) { text[length++] = (char)(0xe0 | (character >> 12)); text[length++] = (char)(0x80 | ((character >> 6) & 63)); text[length++] = (char)(0x80 | (character & 63)); }
  else { text[length++] = (char)(0xf0 | (character >> 18)); text[length++] = (char)(0x80 | ((character >> 12) & 63)); text[length++] = (char)(0x80 | ((character >> 6) & 63)); text[length++] = (char)(0x80 | (character & 63)); }
  text[length] = '\0';
  qa_text_layout_options options;
  memset(&options, 0, sizeof options);
  options.text = text; options.text_length = length; options.font = p->text.font; options.scale = scale; options.color = color;
  options.alternate = alternate; options.has_line_height = true; options.line_height = line_height;
  return options;
}
static double console_layout_width(void *ctx, uint32_t character, double scale, qa_vec4 color, bool alternate, double line_height) {
  console_draw_state *state = ctx;
  char text[8];
  qa_text_layout_options options = console_options(state->p, text, character, scale, color, alternate, line_height);
  qa_text_layout layout;
  if (!qa_seat_text_presentation_layout(&state->p->text, &options, &layout)) qa_range_error("Out of memory");
  double width = layout.width;
  qa_text_layout_destroy(&layout);
  return width;
}
static void console_draw_character(void *ctx, uint32_t character, double scale, qa_vec4 color, bool alternate, double line_height,
                                   double x, double y, qa_rect clip) {
  console_draw_state *state = ctx;
  char text[8];
  qa_text_layout_options options = console_options(state->p, text, character, scale, color, alternate, line_height);
  qa_text_layout layout;
  clip_sink sink;
  qa_draw2d draw = clipped_draw(state->draw, &sink, clip);
  if (!qa_seat_text_presentation_draw(&state->p->text, &draw, &options, (qa_vec2){x, y}, 0, &layout)) qa_range_error("Out of memory");
  qa_text_layout_destroy(&layout);
}

/* rerelease drawStory over this seat's Draw2D and SeatTextPresentation. */
static qa_text_layout_options story_options(const qa_app_world_seat_presentation *p, const char *text, double scale) {
  qa_text_layout_options options;
  memset(&options, 0, sizeof options);
  options.text = text; options.text_length = strlen(text); options.font = p->text.font;
  options.scale = scale; options.color = (qa_vec4){1, 1, 1, 1}; options.align = QA_TEXT_ALIGN_CENTER;
  return options;
}
static void story_layout(void *ctx, const char *text, double scale, double *out_width, double *out_height) {
  qa_app_world_seat_presentation *p = ctx;
  qa_text_layout_options options = story_options(p, text, scale);
  qa_text_layout layout;
  if (!qa_seat_text_presentation_layout(&p->text, &options, &layout)) qa_range_error("Out of memory");
  *out_width = layout.width; *out_height = layout.height;
  qa_text_layout_destroy(&layout);
}
static void story_draw(void *ctx, const char *text, double scale, double max_width, double x, double y) {
  qa_app_world_seat_presentation *p = ctx;
  qa_text_layout_options options = story_options(p, text, scale);
  options.has_max_width = true; options.max_width = max_width;
  qa_text_layout layout;
  if (!qa_seat_text_presentation_draw(&p->text, p->current_draw, &options, (qa_vec2){x, y}, scale, &layout)) qa_range_error("Out of memory");
  qa_text_layout_destroy(&layout);
}

qa_render_frame qa_app_world_seat_presentation_frame(qa_app_world_seat_presentation *p, const qa_world_snapshot *snapshot) {
  store_release(&p->frame_store);
  qa_app_q1_chase_settings chase;
  qa_actor_id actor = local_actor(p);
  bool has_viewer = native_view(p) != NULL || !chase_settings(p, &chase);
  qa_source_time time = snapshot->frame.time;
  qa_scene_camera camera = qa_app_world_seat_presentation_camera(p);
  qa_source_scene_order *source = NULL;
  if (!p->has_q3_client) {
    source = store_alloc(&p->frame_store, sizeof *source);
    qa_source_scene_order_init(source, qa_app_application_assets_material_registrations(p->assets));
  }
  bool has_fog = qa_app_q1_map_fog_active(p->q1_fog);
  qa_scene_fog fog;
  memset(&fog, 0, sizeof fog);
  if (has_fog) fog = qa_app_q1_map_fog_current(p->q1_fog, p->prepared_time);
  seat_effect_frame effects;
  memset(&effects, 0, sizeof effects);
  if (source != NULL) effects = effect_frame(p, &p->frame_store, camera, source, has_viewer ? &actor : NULL, has_fog ? &fog : NULL);
  qa_app_q2_effect_player_view player = qa_app_application_effects_player_view(p->options.effects, actor, camera);
  const qa_native_mod_camera_view *native = native_view(p);
  bool no_world_model = native != NULL && (qa_js_to_int32(native->render_flags) & 2) != 0;
  bool infrared = native == NULL ? player.infrared : (qa_js_to_int32(native->render_flags) & 4) != 0;
  frame_state *state = store_alloc(&p->frame_store, sizeof *state);
  state->p = p;
  qa_world_view_input *input = &state->input;
  input->no_world_model = no_world_model;
  if (source != NULL) {
    qa_world_surface_admission *admission = store_alloc(&p->frame_store, sizeof *admission);
    qa_create_world_surface_admission(source, admission);
    store_add(&p->frame_store, (void (*)(void *))qa_world_surface_admission_destroy, admission);
    input->source = admission;
  }
  input->camera = camera; input->target_is_seat = true; input->target_seat = seat_id(p); input->time = time;
  if (p->options.rerelease != NULL) {
    qa_app_rerelease_view view = qa_app_rerelease_view_of(p->options.rerelease, actor, p->prepared_time);
    if (view.has_fog) { input->has_q2_fog = true; input->q2_fog = view.fog.fog; }
    input->q2_sky = view.sky;
  }
  input->source_sky = qa_app_q1_service_presentation_view(p->q1_services, actor);
  if (has_fog) { input->has_q1_fog = true; input->q1_fog = fog; }
  qa_render_view_clear *clear = store_alloc(&p->frame_store, sizeof *clear);
  clear->depth = 1;
  qa_vec4 *clear_color = store_alloc(&p->frame_store, sizeof *clear_color);
  *clear_color = no_world_model ? (qa_vec4){0.3, 0.3, 0.3, 1} : (qa_vec4){0, 0, 0, 1};
  clear->color = clear_color;
  clear->stencil = false;
  input->clear = clear;
  input->lights = effects.lights; input->light_count = effects.light_count; input->has_lights = true;
  input->visibility.q3_lights = effects.q3_lights; input->visibility.q3_light_count = effects.q3_light_count;
  qa_app_world_scene_styles styles = qa_app_application_world_scene_styles(p->scene);
  input->q1_styles = styles.q1_styles; input->q1_style_count = styles.q1_style_count;
  input->q2_styles = styles.q2_styles; input->q2_style_count = styles.q2_style_count;
  bool has_native_frame = false;
  const qa_render_command *native_commands = NULL; size_t native_command_count = 0;
  if (p->has_q3_client) {
    q3_hooks_state *hooks_state = store_alloc(&p->frame_store, sizeof *hooks_state);
    *hooks_state = (q3_hooks_state){ p, no_world_model, infrared, time, has_fog, fog };
    qa_app_seat_q3_frame_hooks hooks;
    memset(&hooks, 0, sizeof hooks);
    hooks.ctx = hooks_state; hooks.effects = q3_effects; hooks.camera = q3_camera_hook;
    hooks.no_world_model = no_world_model; hooks.source_sky = input->source_sky;
    hooks.has_q1_fog = has_fog; hooks.q1_fog = fog;
    qa_app_sim_player_view controlled;
    if (!component_view(p, &controlled)) {
      qa_app_sim_player_view view = player_view(p);
      hooks.has_client_view_offset_delta = view.has_client_view_offset_delta; hooks.client_view_offset_delta = view.client_view_offset_delta;
    }
    has_native_frame = p->q3_client.frame(p->q3_client.ctx, &hooks, &native_commands, &native_command_count);
  }
  qa_scene_frame_builder_begin_default(&p->frames);
  qa_rect area = qa_app_world_seat_presentation_viewport(p);
  state->viewport = area;
  if (camera.viewport.x != area.x || camera.viewport.y != area.y || camera.viewport.width != area.width || camera.viewport.height != area.height) {
    qa_render_view_clear *black = store_alloc(&p->frame_store, sizeof *black);
    qa_vec4 *black_color = store_alloc(&p->frame_store, sizeof *black_color);
    *black_color = (qa_vec4){0, 0, 0, 1};
    black->depth = 1; black->color = black_color; black->stencil = false;
    qa_render_view view = overlay_view(p, time, area, NULL, 0);
    view.state.clear = black;
    qa_scene_frame_builder_view(&p->frames, view);
  }
  if (!has_native_frame) {
    if (source == NULL) qa_raise("Error", 0, 0, "Shared view lost its prepared effects");
    size_t light_count = 0;
    qa_scene_light *lights = qa_app_application_effects_shadow_scene_lights(p->options.effects, camera, style_of, p,
                                                                           has_viewer ? &actor : NULL, &light_count);
    if (lights != NULL) store_add(&p->frame_store, free, lights);
    qa_ui_hud_rects occupied;
    if (!qa_app_seat_ui_weapon_occlusion(p->options.ui, draw_context(p), !finale_active(p), &occupied)) qa_range_error("Out of memory");
    qa_scene_camera weapon_camera = qa_app_weapon_view_camera(camera, occupied.rects, occupied.count);
    free(occupied.rects);
    qa_prepared_world_view *prepared = store_alloc(&p->frame_store, sizeof *prepared);
    qa_app_application_world_scene_view(p->scene, input, effects.operations, effects.operation_count, lights, light_count, infrared,
                                        &weapon_camera, prepared);
    store_add(&p->frame_store, (void (*)(void *))qa_prepared_world_view_destroy, prepared);
    qa_render_prepared_world_view world = { prepared->image_operations, prepared->image_operation_count, prepared->view };
    qa_scene_frame_builder_world(&p->frames, world);
  } else for (size_t i = 0; i < native_command_count; i++) {
    if (native_commands[i].kind == QA_RENDER_COMMAND_SWAP_BUFFERS) qa_raise("Error", 0, 0, "Cgame cannot present the shared framebuffer");
    qa_scene_frame_builder_command(&p->frames, native_commands[i]);
  }
  qa_renderer_image white = qa_app_application_assets_world(p->assets)->shaders->textures->white->image;
  size_t debug_count = 0;
  const qa_debug_line *debug = p->options.debug_shapes != NULL ? p->options.debug_shapes->lines(p->options.debug_shapes->ctx, &debug_count) : NULL;
  size_t line_count = debug_count + p->component_line_count;
  if (line_count > 0) {
    qa_debug_line *lines = store_alloc(&p->frame_store, line_count * sizeof *lines);
    if (debug_count != 0) memcpy(lines, debug, debug_count * sizeof *lines);
    if (p->component_line_count != 0) memcpy(lines + debug_count, p->component_lines, p->component_line_count * sizeof *lines);
    qa_scene_debug_batches *storage = store_alloc(&p->frame_store, sizeof *storage);
    size_t batch_count = 0;
    qa_draw_batch *batches = qa_prepare_debug_shapes(lines, line_count, camera, white,
        p->options.debug_shapes != NULL ? p->options.debug_shapes->line_width(p->options.debug_shapes->ctx) : 2, storage, &batch_count);
    store_add(&p->frame_store, (void (*)(void *))qa_scene_debug_batches_destroy, storage);
    qa_render_operation *operation = store_alloc(&p->frame_store, sizeof *operation);
    operation->kind = QA_RENDER_OP_DRAW; operation->batches = batches; operation->batch_count = batch_count;
    qa_scene_frame_builder_view(&p->frames, overlay_view(p, time, camera.viewport, operation, 1));
  }
  if (p->world_text_count > 0) {
    qa_scene_world_text_batches *storage = store_alloc(&p->frame_store, sizeof *storage);
    size_t batch_count = 0;
    bool has_factor = p->options.world_text_cull_factor != NULL;
    double factor = has_factor ? p->options.world_text_cull_factor(p->options.world_text_cull_factor_ctx) : 0;
    qa_draw_batch *batches = qa_prepare_world_text(p->world_text, p->world_text_count, camera, world_font_for, p, has_factor, factor,
                                                   storage, &batch_count);
    store_add(&p->frame_store, (void (*)(void *))qa_scene_world_text_batches_destroy, storage);
    qa_render_operation *operation = store_alloc(&p->frame_store, sizeof *operation);
    operation->kind = QA_RENDER_OP_DRAW; operation->batches = batches; operation->batch_count = batch_count;
    qa_scene_frame_builder_view(&p->frames, overlay_view(p, time, camera.viewport, operation, 1));
  }
  qa_text_command_sink *sink = store_alloc(&p->frame_store, sizeof *sink);
  qa_text_command_sink_init(sink, seat_id(p), area, emit_command, state, material_text, state);
  qa_draw2d draw = { qa_text_command_sink_sink(sink), QA_COORD_PIXELS };
  qa_app_sim_player_view controlled;
  bool has_controlled = component_view(p, &controlled);
  qa_app_sim_player_view source_view = has_controlled ? controlled : player_view(p);
  bool has_blend = source_view.has_blend || player.has_blend;
  qa_vec4 blend = source_view.has_blend ? source_view.blend : player.blend;
  qa_picture_asset white_pic = white_picture(p);
  if ((has_controlled || !p->has_q3_client) && has_blend)
    qa_draw2d_fill_rect(&draw, (qa_rect){0, 0, area.width, area.height}, blend, &white_pic);
  if ((has_controlled || !p->has_q3_client) && source_view.has_damage_blend) {
    qa_draw_batch *batch = store_alloc(&p->frame_store, sizeof *batch);
    qa_render_vertex *vertices = store_alloc(&p->frame_store, 8 * sizeof *vertices);
    double *indices = store_alloc(&p->frame_store, 24 * sizeof *indices);
    size_t count = qa_app_prepare_q2_damage_blend(source_view.damage_blend, camera.viewport, white, 0.2, batch, vertices, indices);
    if (count > 0) {
      qa_render_operation *operation = store_alloc(&p->frame_store, sizeof *operation);
      operation->kind = QA_RENDER_OP_DRAW; operation->batches = batch; operation->batch_count = count;
      qa_scene_frame_builder_view(&p->frames, overlay_view(p, time, camera.viewport, operation, 1));
    }
  }
  qa_app_source_finale_resize(p->finale, qa_draw2d_width(&draw), qa_draw2d_height(&draw));
  p->current_draw = &draw;
  p->finale_current = NULL;
  const char *finale_content = qa_app_source_finale_content(p->finale);
  for (size_t i = 0; finale_content != NULL && i < p->banner_count; i++)
    if (strcmp(p->banners[i].content, finale_content) == 0) p->finale_current = &p->banners[i];
  qa_app_source_finale_draw(p->finale, p->prepared_time);
  if (p->options.rerelease != NULL) {
    qa_app_rerelease_draw story = { p, qa_draw2d_width(&draw), qa_draw2d_height(&draw), seat_id(p), story_layout, story_draw };
    qa_app_rerelease_draw_story(p->options.rerelease, actor, story, qa_js_max(1, area.height / 480));
  }
  p->current_draw = NULL;
  bool native_replacement = false;
  const qa_mod_client_presentation_frame *qc_status = NULL;
  for (size_t i = 0; i < p->component_client_count; i++) {
    const qa_mod_client_presentation_frame *frame = &p->component_clients[i].frame;
    const char *mode = frame->kind == QA_MOD_CLIENT_PRESENTATION_NATIVE ? (frame->has_native_hud ? frame->native_hud_mode : NULL)
        : frame->kind == QA_MOD_CLIENT_PRESENTATION_QVM ? frame->qvm_hud_mode : NULL;
    if (frame->kind != QA_MOD_CLIENT_PRESENTATION_QUAKEC && mode != NULL && strcmp(mode, "replace-status") == 0) native_replacement = true;
    if (qc_status == NULL && frame->kind == QA_MOD_CLIENT_PRESENTATION_QUAKEC && frame->has_quakec_hud) qc_status = frame;
  }
  qa_app_seat_ui_sinks sinks = { state, ui_emit, ui_material };
  qa_app_seat_ui_draw_flags flags = qa_app_seat_ui_default_draw_flags();
  qa_app_q1_view_settings size;
  double view_size_value = view_size(p, &size) ? size.size : 100;
  bool hud_visible = true, aggregate_warning = true;
  if (p->has_q3_client) p->q3_client.weapon_hud_view(p->q3_client.ctx, &hud_visible, &aggregate_warning);
  flags.game_visible = !finale_active(p) && view_size_value < 120 && hud_visible;
  flags.crosshair_visible = !(p->options.rerelease != NULL && qa_app_rerelease_story_active(p->options.rerelease, actor));
  flags.native_status = native_replacement || p->has_q3_client || p->options.has_native_q2;
  flags.show_aggregate_warning = aggregate_warning;
  flags.native_crosshair = p->has_q3_client;
  qa_app_seat_ui_source_status status = { NULL, 0, 0 };
  const qa_app_seat_ui_source_status *source_status = NULL;
  if (native_replacement) { status.kind = "native"; source_status = &status; }
  else if (qc_status != NULL) { status.kind = "vitals"; status.health = qc_status->quakec_health; status.armor = qc_status->quakec_armor; source_status = &status; }
  qa_app_seat_ui_draw(p->options.ui, draw_context(p), &camera, sinks, flags, source_status);
  if (p->has_native_q2_frame && !native_replacement && qc_status == NULL)
    qa_app_seat_ui_draw_native_q2_hud(p->options.ui, &p->native_q2_frame, draw_context(p), sinks, NULL, NULL, NULL);
  for (size_t i = 0; i < p->component_client_count; i++) {
    component_client *client = &p->component_clients[i];
    if (client->frame.kind == QA_MOD_CLIENT_PRESENTATION_NATIVE && client->frame.has_native_hud) {
      qa_app_seat_ui_q2_component component = { client->hud, client->frame.native_hud_mode, NULL, NULL };
      qa_app_seat_ui_draw_native_q2_hud(p->options.ui, &client->frame.native_hud_frame, draw_context(p), sinks, NULL, NULL, &component);
    } else if (client->frame.kind == QA_MOD_CLIENT_PRESENTATION_QVM)
      for (size_t k = 0; k < p->component_effect_count; k++) {
        const component_effect *effect = &p->component_effects[k];
        if (effect->has_overlay && qa_same_presentation_owner(&effect->overlay.owner, client->source.owner))
          effect->overlay.draw(effect->overlay.ctx, &p->frames, camera);
      }
  }
  if (p->options.graph_overlay != NULL)
    p->options.graph_overlay(p->options.graph_overlay_ctx, &draw,
        (qa_rect){camera.viewport.x - area.x, camera.viewport.y - area.y, camera.viewport.width, camera.viewport.height}, p->component_drawings);
  qa_seat_input_focus focus = qa_seat_input_get_focus(p->local->input);
  if (strcmp(focus.kind, "console") == 0) {
    double height = trunc(area.height * 0.5);
    qa_sdl_window *window = qa_app_native_renderer_window(p->options.renderer);
    qa_sdl_size logical = qa_sdl_window_logical_size(window), drawable = qa_sdl_window_drawable_size(window);
    qa_console_metrics_options metric_options = { area.width, area.height,
        qa_js_max((double)drawable.width / logical.width, (double)drawable.height / logical.height),
        p->options.console_scale != NULL ? p->options.console_scale(p->options.console_scale_ctx) : 0, &p->text.font };
    qa_console_metrics metrics = qa_console_metrics_for(&metric_options);
    qa_draw2d_fill_rect(&draw, (qa_rect){0, 0, area.width, height}, (qa_vec4){0, 0, 0, 0.85}, &white_pic);
    qa_seat_console *console = p->local->console;
    if ((double)console->buffer.width != metrics.columns) qa_console_buffer_resize(&console->buffer, (int64_t)metrics.columns);
    size_t row_count = 0;
    qa_console_row *rows = qa_console_buffer_visible(&console->buffer, (int64_t)qa_js_max(1, trunc(height / metrics.line_height)), &row_count);
    if (rows != NULL) store_add(&p->frame_store, free, rows);
    char *field_text = store_alloc(&p->frame_store, console->field.length + 1);
    memcpy(field_text, console->field.characters, console->field.length);
    qa_console_field_view field = { field_text, console->field.cursor, console->field.scroll, console->field.overstrike };
    console_draw_state draw_state = { p, &draw };
    qa_console_draw_target target = { &draw_state, seat_id(p), qa_draw2d_width(&draw), qa_draw2d_height(&draw), console_draw_pic };
    qa_console_text_presentation text = { &draw_state, seat_id(p), &p->text.font, console_glyph_metrics, console_layout_width, console_draw_character };
    qa_console_draw_options options;
    memset(&options, 0, sizeof options);
    options.draw = &target; options.text = &text; options.rows = rows; options.row_count = row_count;
    options.field = &field; options.height = height; options.scale = metrics.scale;
    options.has_cell_width = true; options.cell_width = metrics.cell_width;
    options.now_ms = p->prepared_time * 1000; options.background = NULL;
    options.selected_entry = qa_seat_console_selected_completion_entry(console);
    qa_draw_console(&options);
  }
  return qa_scene_frame_builder_finish(&p->frames, false);
}

void qa_app_world_seat_presentation_render(qa_app_world_seat_presentation *p, qa_render_frame frame, qa_renderer_backend backend) {
  if (backend.ctx != qa_app_native_renderer_backend(p->options.renderer).ctx) qa_raise("Error", 0, 0, "Seat presentation uses another renderer");
  qa_app_native_renderer_execute(p->options.renderer, frame);
}

/* ---- prepareImageRefresh ---- */
struct qa_app_world_seat_image_refresh {
  qa_app_world_seat_presentation *p;
  qa_app_prepared_images *images;
  world_font *replacements; size_t replacement_count;
  qa_app_seat_ui_image_refresh *hud;
  qa_app_q1_sky_refresh *sky;
};
static bool refresh_texture(void *ctx, qa_content_id content, const char *path, qa_renderer_image *out) {
  qa_app_world_seat_image_refresh *refresh = ctx;
  qa_scene_texture_load_options options = { true, false, "clamp", "q1", "sky" };
  const qa_scene_texture *texture = qa_scene_texture_loader_load(qa_app_prepared_images_provider(refresh->images, content).textures, path, &options);
  if (texture == NULL) return false;
  *out = texture->image;
  return true;
}
static qa_renderer_image refresh_missing(void *ctx, qa_content_id content) {
  qa_app_world_seat_image_refresh *refresh = ctx;
  return qa_app_prepared_images_provider(refresh->images, content).textures->missing->image;
}
static void close_replacements(qa_app_world_seat_image_refresh *refresh) {
  for (size_t i = 0; i < refresh->replacement_count; i++) {
    qa_app_world_font_close(refresh->replacements[i].font);
    free(refresh->replacements[i].content);
  }
  free(refresh->replacements);
  refresh->replacements = NULL; refresh->replacement_count = 0;
}
static void refresh_release(void *value) {
  qa_app_world_seat_image_refresh *refresh = value;
  close_replacements(refresh);
  free(refresh);
}
qa_app_world_seat_image_refresh *qa_app_world_seat_presentation_prepare_image_refresh(qa_app_world_seat_presentation *p,
                                                                                    qa_app_prepared_images *images) {
  qa_app_world_seat_image_refresh *refresh = calloc(1, sizeof *refresh);
  if (refresh == NULL) qa_range_error("Out of memory");
  refresh->p = p; refresh->images = images;
  qa_cleanup guard;
  qa_cleanup_push(&guard, refresh_release, refresh);
  refresh->replacements = calloc(p->world_font_count == 0 ? 1 : p->world_font_count, sizeof *refresh->replacements);
  if (refresh->replacements == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < p->world_font_count; i++) {
    (void)qa_app_prepared_images_provider(images, p->world_fonts[i].content);
    qa_app_world_font *font = qa_app_application_assets_load_world_font(p->assets, p->world_fonts[i].content,
                                                                        qa_app_prepared_images_policy(images));
    refresh->replacements[refresh->replacement_count++] = (world_font){ heap_text(p->world_fonts[i].content), font };
  }
  refresh->hud = qa_app_seat_ui_prepare_image_refresh(p->options.ui, images);
  qa_app_q1_service_assets assets = { refresh, refresh_texture, refresh_missing };
  refresh->sky = qa_app_q1_service_presentation_prepare_image_refresh(p->q1_services, &assets);
  qa_cleanup_pop(&guard);
  return refresh;
}
void qa_app_world_seat_image_refresh_commit(qa_app_world_seat_image_refresh *refresh) {
  qa_app_world_seat_presentation *p = refresh->p;
  p->text.font = qa_app_prepared_images_font(refresh->images);
  qa_app_seat_ui_refresh_images(p->options.ui, qa_app_prepared_images_font(refresh->images), qa_app_prepared_images_typography(refresh->images));
  qa_app_seat_ui_image_refresh_commit(refresh->hud);
  qa_app_q1_sky_refresh_apply(refresh->sky);
  qa_app_q1_sky_refresh_destroy(refresh->sky);
  for (size_t i = 0; i < p->component_client_count; i++) qa_app_q2_native_hud_clear(p->component_clients[i].hud);
  for (size_t i = 0; i < p->world_font_count; i++) { qa_app_world_font_close(p->world_fonts[i].font); free(p->world_fonts[i].content); }
  free(p->world_fonts);
  p->world_fonts = refresh->replacements; p->world_font_count = refresh->replacement_count; p->world_font_capacity = refresh->replacement_count;
  free(refresh);
}
void qa_app_world_seat_image_refresh_discard(qa_app_world_seat_image_refresh *refresh) {
  qa_app_q1_sky_refresh_destroy(refresh->sky);
  refresh_release(refresh);
}

/* ---- close ---- */
typedef struct close_part { void (*fn)(void *ctx); void *ctx; } close_part;
static void close_q3(void *ctx) { qa_app_seat_q3_client *client = ctx; client->close(client->ctx); }
static void close_ui(void *ctx) { qa_app_seat_ui_close(ctx); }
static void close_scene(void *ctx) { qa_app_application_world_scene_close(ctx); }
static void close_services(void *ctx) { qa_app_q1_service_presentation_close(ctx); }
static void close_effects(void *ctx) { qa_app_application_effects_close(ctx); }
static void close_font(void *ctx) { qa_app_world_font_close(ctx); }
void qa_app_world_seat_presentation_close(qa_app_world_seat_presentation *p) {
  p->closed = true;
  for (size_t i = 0; i < p->component_client_count; i++) { qa_app_q2_native_hud_clear(p->component_clients[i].hud); qa_app_q2_native_hud_destroy(p->component_clients[i].hud); }
  p->component_client_count = 0;
  p->component_effect_count = 0;
  size_t errors = 0;
  char first[QA_CAUGHT_MESSAGE_CAPACITY] = "";
  qa_caught_error caught;
  for (size_t i = 0; i < p->world_font_count; i++) {
    if (!qa_try(close_font, p->world_fonts[i].font, &caught) && errors++ == 0) snprintf(first, sizeof first, "%s", caught.message);
    free(p->world_fonts[i].content);
  }
  p->world_font_count = 0;
  free(p->world_text); p->world_text = NULL; p->world_text_count = 0;
  p->has_native_q2_frame = false;
  qa_app_component_drawings_clear(p->component_drawings);
  p->component_lines = NULL; p->component_line_count = 0;
  close_part parts[5]; size_t part_count = 0;
  if (p->has_q3_client) parts[part_count++] = (close_part){ close_q3, &p->q3_client };
  parts[part_count++] = (close_part){ close_ui, p->options.ui };
  parts[part_count++] = (close_part){ close_scene, p->scene };
  parts[part_count++] = (close_part){ close_services, p->q1_services };
  if (p->options.has_native_q2 && p->options.native_q2_owns_effects) parts[part_count++] = (close_part){ close_effects, p->options.effects };
  for (size_t i = 0; i < part_count; i++)
    if (!qa_try(parts[i].fn, parts[i].ctx, &caught) && errors++ == 0) snprintf(first, sizeof first, "%s", caught.message);
  if (errors > 0) qa_raise("AggregateError", 0, 0, "Failed to close seat presentation");
}
void qa_app_world_seat_presentation_destroy(qa_app_world_seat_presentation *p) {
  if (p == NULL) return;
  if (!p->closed) qa_app_world_seat_presentation_close(p);
  store_release(&p->frame_store); free(p->frame_store.items);
  store_release(&p->effect_store); free(p->effect_store.items);
  if (p->has_hud_active) qa_seat_hud_active_free(&p->hud_active);
  free(p->component_clients); free(p->component_effects); free(p->world_fonts);
  for (size_t i = 0; i < p->banner_count; i++) free(p->banners[i].content);
  free(p->banners);
  size_t count = 0;
  pending_message *messages = qa_event_ring_drain_all(&p->pending_messages, &count);
  for (size_t i = 0; i < count; i++) free(messages[i].text);
  free(messages);
  qa_app_sim_presentation_event *events = qa_event_ring_drain_all(&p->pending_q1, &count);
  for (size_t i = 0; i < count; i++) qa_app_sim_presentation_event_release(&events[i]);
  free(events);
  events = qa_event_ring_drain_all(&p->pending_q2, &count);
  for (size_t i = 0; i < count; i++) qa_app_sim_presentation_event_release(&events[i]);
  free(events);
  qa_event_ring_destroy(&p->pending_messages); qa_event_ring_destroy(&p->pending_q1); qa_event_ring_destroy(&p->pending_q2);
  qa_scene_frame_builder_destroy(&p->frames);
  qa_app_application_world_scene_destroy(p->scene);
  qa_app_q1_service_presentation_destroy(p->q1_services);
  qa_app_component_drawings_destroy(p->component_drawings);
  qa_app_source_finale_destroy(p->finale);
  qa_app_q1_message_localization_destroy(p->q1_messages);
  qa_app_q1_map_fog_destroy(p->q1_fog);
  free(p);
}

/* ---- SeatPresentation and ViewingSeat ---- */
static void seat_receive(void *ctx, const qa_simulation_event *events, size_t count) { qa_app_world_seat_presentation_receive(ctx, events, count); }
static qa_render_frame seat_frame(void *ctx, qa_world_snapshot snapshot) { return qa_app_world_seat_presentation_frame(ctx, &snapshot); }
static void seat_render(void *ctx, qa_render_frame frame, qa_renderer_backend backend) { qa_app_world_seat_presentation_render(ctx, frame, backend); }
qa_seat_presentation qa_app_world_seat_presentation_seat(qa_app_world_seat_presentation *p) {
  qa_seat_presentation seat = { p, qa_app_world_seat_presentation_state(p), seat_receive, seat_frame, seat_render };
  return seat;
}
static qa_actor_id viewing_actor(void *ctx) { return local_actor(ctx); }
static qa_seat_id viewing_seat(void *ctx) { return seat_id(ctx); }
static qa_client_id viewing_client(void *ctx) { return client_id(ctx); }
static qa_scene_camera viewing_camera(void *ctx) { return qa_app_world_seat_presentation_camera(ctx); }
static qa_rect viewing_viewport(void *ctx) { return qa_app_world_seat_presentation_viewport(ctx); }
static bool viewing_split(void *ctx) { return qa_app_world_seat_presentation_split_screen(ctx); }
static int64_t viewing_bind(void *ctx, qa_app_component_effect_frame frame, const qa_app_component_overlay *overlay) {
  return qa_app_world_seat_presentation_bind_component_effects(ctx, frame, overlay);
}
static void viewing_unbind(void *ctx, int64_t binding) { qa_app_world_seat_presentation_unbind_component_effects(ctx, binding); }
static void viewing_center_print(void *ctx, const char *text, double time_milliseconds, double duration_milliseconds) {
  qa_app_world_seat_presentation *p = ctx;
  qa_app_seat_ui_center_print(p->options.ui, text, time_milliseconds, duration_milliseconds);
}
qa_app_viewing_seat qa_app_world_seat_presentation_viewing_seat(qa_app_world_seat_presentation *p) {
  qa_app_viewing_seat seat = { p, viewing_actor, viewing_seat, viewing_client, viewing_camera, viewing_viewport, viewing_split,
                               viewing_bind, viewing_unbind, viewing_center_print };
  return seat;
}
