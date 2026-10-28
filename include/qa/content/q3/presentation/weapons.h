/* Port of src/content/q3/presentation/weapons.ts. GPL-2.0-or-later.
 * Weapon registration and presentation from id Software's cg_weapons.c.
 * TS async registration is synchronous and serialized by call order;
 * register_* return false only on OOM. */
#ifndef QA_CONTENT_Q3_PRESENTATION_WEAPONS_H
#define QA_CONTENT_Q3_PRESENTATION_WEAPONS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/client_info.h"
#include "qa/content/q3/presentation/effects.h"
#include "qa/content/q3/presentation/entities.h"
#include "qa/content/q3/presentation/marks.h"
#include "qa/content/q3/presentation/movement_host.h"
#include "qa/content/q3/presentation/scene_host.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
typedef enum qa_q3_pres_impact_sound {
  QA_Q3_PRES_IMPACT_SOUND_DEFAULT = 0, QA_Q3_PRES_IMPACT_SOUND_METAL = 1, QA_Q3_PRES_IMPACT_SOUND_FLESH = 2
} qa_q3_pres_impact_sound;
/* TS BulletHit. */
typedef struct qa_q3_pres_bullet_hit {
  bool is_wall; /* TS kind: "wall" | "flesh" */
  qa_vec3 normal; /* wall */
  double entity_num; /* flesh */
} qa_q3_pres_bullet_hit;
/* TS ShotgunPresentationHost<Target> with Target an entity number
 * (-1 in target is TS null via has_target). */
typedef struct qa_q3_pres_shotgun_trace {
  qa_vec3 end, normal; double surface_flags; bool has_target; double target;
} qa_q3_pres_shotgun_trace;
typedef struct qa_q3_pres_shotgun_presentation_host {
  bool smoke_enabled;
  void *ctx;
  qa_q3_pres_shotgun_trace (*trace)(void *ctx, qa_vec3 start, qa_vec3 end);
  qa_vec3 (*water)(void *ctx, qa_vec3 start, qa_vec3 end);
  double (*contents)(void *ctx, qa_vec3 point);
  bool (*is_player)(void *ctx, double target);
  void (*blood)(void *ctx, qa_vec3 point, qa_vec3 normal, double target);
  void (*wall)(void *ctx, qa_vec3 point, qa_vec3 normal, qa_q3_pres_impact_sound sound);
  void (*bubbles)(void *ctx, qa_vec3 start, qa_vec3 end);
  void (*smoke)(void *ctx, qa_vec3 origin);
} qa_q3_pres_shotgun_presentation_host;
/* TS Q3ShotgunEvent (base/game/hitscan.ts shape). */
typedef struct qa_q3_pres_shotgun_event { qa_vec3 muzzle, direction; double seed; } qa_q3_pres_shotgun_event;
void qa_q3_pres_emit_shotgun_presentation(qa_q3_pres_shotgun_presentation_host host, qa_q3_pres_shotgun_event shot);
typedef enum qa_q3_pres_eject_brass {
  QA_Q3_PRES_EJECT_NONE, QA_Q3_PRES_EJECT_MACHINEGUN, QA_Q3_PRES_EJECT_SHOTGUN, QA_Q3_PRES_EJECT_NAILGUN
} qa_q3_pres_eject_brass;
const char *qa_q3_pres_eject_brass_name(qa_q3_pres_eject_brass brass);
/* TS ClientWeaponInfo extends PacketWeaponInfo. item NULL is TS null. */
typedef struct qa_q3_pres_client_weapon_info {
  qa_q3_pres_packet_weapon_info base;
  const qa_q3_base_item_definition *item;
  qa_q3_pres_scene_model hands_model, flash_model, ammo_model;
  const qa_q3_pres_scene_shader *weapon_icon, *ammo_icon; /* nullable */
  qa_vec3 flash_dlight_color;
  qa_pcm_sound *flash_sounds[4]; /* borrowed, nullable */
  qa_q3_pres_eject_brass eject_brass;
  qa_pcm_sound *ready_sound, *firing_sound; /* borrowed, nullable */
  bool loop_fire_sound;
} qa_q3_pres_client_weapon_info;
/* TS RegisteredWeaponEffects. */
typedef struct qa_q3_pres_registered_weapon_effects {
  const qa_q3_pres_scene_shader *lightning_shader; /* nullable */
  qa_q3_pres_scene_model lightning_explosion_model;
  qa_pcm_sound *lightning_hit_sounds[3]; /* borrowed, nullable */
  const qa_q3_pres_scene_shader *bullet_explosion_shader, *rocket_explosion_shader;
  const qa_q3_pres_scene_shader *grenade_explosion_shader, *plasma_explosion_shader;
  const qa_q3_pres_scene_shader *rail_explosion_shader, *bfg_explosion_shader;
  const qa_q3_pres_scene_shader *rail_rings_shader, *rail_core_shader; /* nullable */
} qa_q3_pres_registered_weapon_effects;
void qa_q3_pres_registered_weapon_effects_init(qa_q3_pres_registered_weapon_effects *effects);
/* Landed-consumer view of RegisteredWeaponEffects (live getters). */
typedef struct qa_q3_pres_registered_weapon_effects_view {
  void *ctx;
  const qa_q3_pres_scene_shader *(*lightning_shader)(void *ctx);
  const qa_q3_pres_scene_shader *(*rocket_explosion_shader)(void *ctx);
} qa_q3_pres_registered_weapon_effects_view;
/* TS WeaponRegistrationAudio + Pick<RendererResources,...> merged host.
 * register_* false only on OOM; sound/shader *out NULL is TS null. */
typedef struct qa_q3_pres_weapon_registration_host {
  void *ctx;
  bool (*register_model)(void *ctx, const char *path, qa_q3_pres_scene_model *out);
  bool (*register_shader)(void *ctx, const char *path, const qa_q3_pres_scene_shader **out);
  bool (*register_sound)(void *ctx, const char *path, bool compressed, qa_pcm_sound **out);
} qa_q3_pres_weapon_registration_host;
typedef struct qa_q3_pres_client_weapon_media_registry qa_q3_pres_client_weapon_media_registry;
/* NULL only on OOM. */
qa_q3_pres_client_weapon_media_registry *qa_q3_pres_client_weapon_media_registry_create(
    qa_q3_base_product product, qa_q3_pres_weapon_registration_host host);
void qa_q3_pres_client_weapon_media_registry_destroy(qa_q3_pres_client_weapon_media_registry *registry);
qa_q3_base_product qa_q3_pres_client_weapon_media_registry_product(qa_q3_pres_client_weapon_media_registry *registry);
qa_q3_pres_registered_weapon_effects *qa_q3_pres_client_weapon_media_registry_effects(
    qa_q3_pres_client_weapon_media_registry *registry);
const qa_q3_pres_client_weapon_info *qa_q3_pres_client_weapon_media_registry_weapons(
    qa_q3_pres_client_weapon_media_registry *registry, size_t *out_count);
const qa_q3_pres_packet_item_visual *qa_q3_pres_client_weapon_media_registry_items(
    qa_q3_pres_client_weapon_media_registry *registry, size_t *out_count);
/* Bad indices abort with the TS RangeError text. */
qa_q3_pres_client_weapon_info *qa_q3_pres_client_weapon_media_registry_weapon(
    qa_q3_pres_client_weapon_media_registry *registry, double number);
qa_q3_pres_client_weapon_info *qa_q3_pres_client_weapon_media_registry_require_weapon(
    qa_q3_pres_client_weapon_media_registry *registry, double number);
bool qa_q3_pres_client_weapon_media_registry_register_weapon(qa_q3_pres_client_weapon_media_registry *registry,
                                                             double number);
bool qa_q3_pres_client_weapon_media_registry_register_item_visuals(qa_q3_pres_client_weapon_media_registry *registry,
                                                                   double number);
/* Landed-consumer view of the registry (media.ts pick). The items /
 * weapons spans are packet views rebuilt by the consumer from the
 * registry accessors; this view carries effects + registerItemVisuals. */
typedef struct qa_q3_pres_client_weapon_media_registry_view {
  const qa_q3_pres_packet_item_visual *items; size_t item_count;
  const qa_q3_pres_packet_weapon_info *weapons; size_t weapon_count;
  qa_q3_pres_registered_weapon_effects_view effects;
  void *ctx;
  bool (*register_item_visuals)(void *ctx, double number);
} qa_q3_pres_client_weapon_media_registry_view;
/* Builds a view whose packet spans live in caller-provided scratch.
 * False only on OOM. */
bool qa_q3_pres_client_weapon_media_registry_make_view(qa_q3_pres_client_weapon_media_registry *registry,
    qa_q3_pres_packet_weapon_info **scratch, qa_q3_pres_client_weapon_media_registry_view *out);
/* TS WeaponPresentationMedia. */
typedef struct qa_q3_pres_weapon_presentation_media {
  struct {
    qa_q3_pres_scene_model machinegun_brass, shotgun_brass, dish_flash, ring_flash, bullet_flash;
  } models;
  struct {
    const qa_q3_pres_scene_shader *smoke_puff, *nail_puff, *shotgun_smoke_puff, *invis, *battle_weapon;
    const qa_q3_pres_scene_shader *quad_weapon, *select, *noammo, *hole_mark, *burn_mark, *energy_mark;
    const qa_q3_pres_scene_shader *bullet_mark, *tracer;
  } shaders; /* nullable fields */
  struct {
    qa_pcm_sound *quad, *nail_hit_flesh, *nail_hit_metal, *nail_hit, *prox_explosion, *rocket_explosion;
    qa_pcm_sound *plasma_explosion, *chaingun_hit_flesh, *chaingun_hit_metal, *chaingun_hit;
    qa_pcm_sound *ricochet1, *ricochet2, *ricochet3, *tracer;
  } sounds; /* borrowed, nullable fields */
} qa_q3_pres_weapon_presentation_media;
/* TS WeaponPresentationSettings. */
typedef struct qa_q3_pres_weapon_presentation_settings {
  double brass_time, rail_trail_time;
  bool old_rail, no_projectile_trail, old_plasma, old_rocket;
  double true_lightning;
  bool draw_gun;
  double fov, gun_x, gun_y, gun_z, gun_frame, tracer_length, tracer_width, tracer_chance;
  bool ragepro; /* TS hardware === "ragepro" */
} qa_q3_pres_weapon_presentation_settings;
/* TS WeaponSelectionDrawing. fade false is TS null. */
typedef struct qa_q3_pres_weapon_selection_drawing {
  void *ctx;
  bool (*fade_color)(void *ctx, double start, double duration, qa_vec4 *out);
  void (*set_color)(void *ctx, const qa_vec4 *color);
  void (*draw_pic)(void *ctx, double x, double y, double width, double height,
                   const qa_q3_pres_scene_shader *shader);
  double (*draw_string_length)(void *ctx, const char *text);
  void (*draw_big_string_color)(void *ctx, double x, double y, const char *text, qa_vec4 color);
} qa_q3_pres_weapon_selection_drawing;
/* TS ClientWeaponHost. prediction is the landed runtime; collision
 * point/trace go through collision fns. start_sound origin NULL is
 * TS null. client_info returns the landed record. */
typedef struct qa_q3_pres_client_weapon_host {
  void *ctx;
  void (*add_ref_entity)(void *ctx, const qa_q3_pres_ref_entity *entity);
  void (*add_light)(void *ctx, qa_q3_pres_dynamic_light light);
  void (*start_sound)(void *ctx, const qa_vec3 *origin, double entity, double channel, qa_pcm_sound *sound);
  void (*add_loop_sound)(void *ctx, double entity, qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *sound, bool real_loop);
  void *prediction_ctx;
  qa_q3_pres_movement_trace (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                                     double skip_number, double mask);
  double (*point_contents)(void *ctx, qa_vec3 point, double pass_entity);
  double (*collision_point_contents)(void *ctx, qa_vec3 point);
  qa_vec3 (*collision_trace_end)(void *ctx, qa_vec3 start, qa_vec3 end, double mask);
  void *random_ctx;
  double (*rand)(void *ctx);
  double (*random)(void *ctx);
  double (*crandom)(void *ctx);
  qa_q3_pres_local_entity_pool *local_entities;
  void *effects_ctx;
  qa_q3_pres_local_entity *(*smoke_puff)(void *ctx, qa_q3_pres_smoke_puff_options options);
  void (*bubble_trail)(void *ctx, qa_vec3 start, qa_vec3 end, double spacing);
  qa_q3_pres_local_entity *(*make_explosion)(void *ctx, qa_q3_pres_explosion_options options);
  void (*bleed)(void *ctx, qa_vec3 origin, double entity_num);
  void *marks_ctx;
  bool (*impact_mark)(void *ctx, qa_q3_pres_impact_mark_request request,
                      qa_q3_pres_ref_poly **out_polys, size_t *out_count);
  void *particles_ctx;
  void (*explosion)(void *ctx, qa_vec3 origin, qa_vec3 velocity, double duration, double size_start, double size_end);
  qa_q3_pres_weapon_presentation_media media;
  qa_q3_pres_weapon_selection_drawing drawing;
  qa_q3_pres_weapon_presentation_settings (*settings)(void *ctx);
  qa_q3_pres_client_info *(*client_info)(void *ctx, double number);
  qa_pcm_sound *(*sound)(void *ctx, const char *path);
  void (*add_poly)(void *ctx, const qa_q3_pres_ref_poly *poly);
} qa_q3_pres_client_weapon_host;
/* TS ClientWeaponSelection. selected NULL is the TS omitted callback. */
typedef struct qa_q3_pres_client_weapon_selection {
  qa_q3_pres_client_game_state *state;
  void *selected_ctx;
  void (*selected)(void *ctx, double weapon);
} qa_q3_pres_client_weapon_selection;
void qa_q3_pres_client_weapon_selection_init(qa_q3_pres_client_weapon_selection *selection,
    qa_q3_pres_client_game_state *state, void (*selected)(void *ctx, double weapon), void *selected_ctx);
void qa_q3_pres_client_weapon_selection_next_weapon(qa_q3_pres_client_weapon_selection *selection);
void qa_q3_pres_client_weapon_selection_previous_weapon(qa_q3_pres_client_weapon_selection *selection);
void qa_q3_pres_client_weapon_selection_select_weapon(qa_q3_pres_client_weapon_selection *selection, double weapon);
void qa_q3_pres_client_weapon_selection_out_of_ammo_change(qa_q3_pres_client_weapon_selection *selection);
/* TS ClientWeaponRuntime extends ClientWeaponSelection. */
typedef struct qa_q3_pres_client_weapon_runtime {
  qa_q3_pres_client_weapon_selection selection;
  qa_q3_pres_client_weapon_media_registry *registry;
  qa_q3_pres_client_weapon_host host;
} qa_q3_pres_client_weapon_runtime;
/* Product mismatch aborts with the TS Error text. */
void qa_q3_pres_client_weapon_runtime_init(qa_q3_pres_client_weapon_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_client_weapon_media_registry *registry,
    qa_q3_pres_client_weapon_host host);
void qa_q3_pres_client_weapon_runtime_fire_weapon(qa_q3_pres_client_weapon_runtime *runtime,
                                                  qa_q3_pres_client_entity *entity);
void qa_q3_pres_client_weapon_runtime_rail_trail(qa_q3_pres_client_weapon_runtime *runtime, double client_num,
                                                 qa_vec3 *start, qa_vec3 end);
void qa_q3_pres_client_weapon_runtime_missile_trail(qa_q3_pres_client_weapon_runtime *runtime,
    qa_q3_pres_missile_trail kind, qa_q3_pres_client_entity *entity, const qa_q3_pres_packet_weapon_info *weapon);
void qa_q3_pres_client_weapon_runtime_grapple_trail(qa_q3_pres_client_weapon_runtime *runtime,
    qa_q3_pres_client_entity *entity, const qa_q3_pres_packet_weapon_info *weapon);
void qa_q3_pres_client_weapon_runtime_missile_hit_wall(qa_q3_pres_client_weapon_runtime *runtime, double weapon,
    double client_num, qa_vec3 origin, qa_vec3 direction, qa_q3_pres_impact_sound sound_type);
void qa_q3_pres_client_weapon_runtime_missile_hit_player(qa_q3_pres_client_weapon_runtime *runtime, double weapon,
    qa_vec3 origin, qa_vec3 direction, double entity_num);
void qa_q3_pres_client_weapon_runtime_shotgun_fire(qa_q3_pres_client_weapon_runtime *runtime,
                                                   qa_q3_base_entity_state *state);
void qa_q3_pres_client_weapon_runtime_bullet(qa_q3_pres_client_weapon_runtime *runtime, qa_vec3 end,
                                             double source, qa_q3_pres_bullet_hit hit);
void qa_q3_pres_client_weapon_runtime_tracer(qa_q3_pres_client_weapon_runtime *runtime, qa_vec3 source,
                                             qa_vec3 destination);
void qa_q3_pres_client_weapon_runtime_out_of_ammo_change(qa_q3_pres_client_weapon_runtime *runtime);
void qa_q3_pres_client_weapon_runtime_next_weapon(qa_q3_pres_client_weapon_runtime *runtime);
void qa_q3_pres_client_weapon_runtime_previous_weapon(qa_q3_pres_client_weapon_runtime *runtime);
void qa_q3_pres_client_weapon_runtime_select_weapon(qa_q3_pres_client_weapon_runtime *runtime, double weapon);
void qa_q3_pres_client_weapon_runtime_add_player_weapon(qa_q3_pres_client_weapon_runtime *runtime,
    const qa_q3_pres_ref_model_entity *parent, qa_q3_base_source_player_state *player_state,
    qa_q3_pres_client_entity *entity, qa_q3_base_team team);
void qa_q3_pres_client_weapon_runtime_add_view_weapon(qa_q3_pres_client_weapon_runtime *runtime,
                                                      qa_q3_base_source_player_state *player_state);
void qa_q3_pres_client_weapon_runtime_draw_weapon_select(qa_q3_pres_client_weapon_runtime *runtime);
/* Landed-consumer view (events.ts pick). */
typedef struct qa_q3_pres_client_weapon_runtime_view {
  void *ctx;
  void (*out_of_ammo_change)(void *ctx);
  void (*fire_weapon)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*missile_hit_player)(void *ctx, double weapon, qa_vec3 origin, qa_vec3 direction, double entity_num);
  void (*missile_hit_wall)(void *ctx, double weapon, double client_num, qa_vec3 origin, qa_vec3 direction,
                           qa_q3_pres_impact_sound sound_type);
  void (*rail_trail)(void *ctx, double client_num, qa_vec3 start, qa_vec3 end);
  void (*bullet)(void *ctx, qa_vec3 end, double source, qa_q3_pres_bullet_hit hit);
  void (*shotgun_fire)(void *ctx, qa_q3_base_entity_state *state);
} qa_q3_pres_client_weapon_runtime_view;
qa_q3_pres_client_weapon_runtime_view qa_q3_pres_client_weapon_runtime_as_view(
    qa_q3_pres_client_weapon_runtime *runtime);
/* TS emitWeaponImpact / emitRailTrail / emitPlasmaTrail free functions. */
void qa_q3_pres_emit_weapon_impact(qa_q3_base_product product,
    qa_q3_pres_client_weapon_media_registry *registry, qa_q3_pres_client_weapon_host *host, double weapon,
    double client_num, qa_vec3 origin, qa_vec3 direction, qa_q3_pres_impact_sound sound_type);
void qa_q3_pres_emit_rail_trail(double time, qa_q3_pres_client_weapon_media_registry *registry,
    qa_q3_pres_client_weapon_host *host, double client_num, qa_vec3 *start, qa_vec3 end);
void qa_q3_pres_emit_plasma_trail(double time, qa_vec3 origin, qa_vec3 angles, double weapon,
    qa_q3_pres_client_weapon_media_registry *registry, qa_q3_pres_client_weapon_host *host);
#endif
