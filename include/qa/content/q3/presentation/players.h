/* Port of src/content/q3/presentation/players.ts. GPL-2.0-or-later.
 * Player media and presentation from id Software's code/cgame/cg_players.c.
 * TS async client-info loading is synchronous; the SupersededClientLoad
 * control flow becomes status codes threaded through the load chain, so
 * reentrant host callbacks that bump a request/lifecycle still abandon
 * the stale load exactly where TS check() would throw.
 * Mirrored types (TS owners unported): the effects/marks picks reuse
 * local_entities.h's *_mirror structs (effects.ts / marks.ts). */
#ifndef QA_CONTENT_Q3_PRESENTATION_PLAYERS_H
#define QA_CONTENT_Q3_PRESENTATION_PLAYERS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/client_info.h"
#include "qa/content/q3/presentation/collision_host.h"
#include "qa/content/q3/presentation/local_entities.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/resources.h"
#include "qa/content/q3/presentation/scene_host.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
#include "qa/contracts/qvm_mod_presentation.h"
extern const char *const qa_q3_pres_custom_sound_names[];
extern const size_t qa_q3_pres_custom_sound_name_count;
typedef struct qa_q3_pres_client_info_settings {
  qa_q3_base_game_type game_type;
  double max_clients;
  bool force_model;
  const char *model, *head_model, *red_team_name, *blue_team_name; /* borrowed */
  bool defer_players, build_script, loading;
} qa_q3_pres_client_info_settings;
/* TS ClientInfoHost. resources is Pick<RendererResources,
 * "registerModel"|"registerSkin">. register_sound/sound out NULL is
 * TS null; register_* false only on OOM. */
typedef struct qa_q3_pres_client_info_host {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_asset_reader assets;
  void *resources_ctx;
  bool (*register_model)(void *ctx, const char *path, qa_q3_pres_scene_model *out);
  bool (*register_skin)(void *ctx, const char *path, const qa_q3_pres_scene_skin **out);
  void *ctx;
  qa_q3_pres_client_info_settings (*settings)(void *ctx);
  double (*memory_remaining)(void *ctx);
  bool (*register_shader_no_mip)(void *ctx, const char *name, const qa_q3_pres_scene_shader **out);
  bool (*register_sound)(void *ctx, const char *name, bool compressed, qa_pcm_sound **out);
  qa_pcm_sound *(*sound)(void *ctx, const char *name, bool compressed);
  void (*print)(void *ctx, const char *message);
} qa_q3_pres_client_info_host;
typedef struct qa_q3_pres_player_media {
  const qa_q3_pres_scene_shader *connection_shader, *balloon_shader; /* nullable */
  const qa_q3_pres_scene_shader *medal_impressive, *medal_excellent, *medal_gauntlet;
  const qa_q3_pres_scene_shader *medal_defend, *medal_assist, *medal_capture, *friend_shader;
  const qa_q3_pres_scene_shader *shadow_mark_shader, *wake_mark_shader;
  const qa_q3_pres_scene_shader *invis_shader, *quad_shader, *red_quad_shader;
  const qa_q3_pres_scene_shader *regen_shader, *battle_suit_shader, *haste_puff_shader;
  qa_pcm_sound *flight_sound; /* borrowed, nullable */
  qa_q3_pres_scene_model red_flag_model, blue_flag_model, neutral_flag_model;
  qa_q3_pres_scene_model flag_pole_model, flag_flap_model;
  const qa_q3_pres_scene_skin *red_flag_flap_skin, *blue_flag_flap_skin, *neutral_flag_flap_skin; /* nullable */
} qa_q3_pres_player_media;
typedef struct qa_q3_pres_mission_player_media {
  qa_q3_pres_scene_model red_cube_model, blue_cube_model;
  qa_q3_pres_scene_model kamikaze_head_model, kamikaze_head_trail;
  qa_q3_pres_scene_model guard_powerup_model, scout_powerup_model, doubler_powerup_model;
  qa_q3_pres_scene_model ammo_regen_powerup_model, invulnerability_powerup_model, medkit_usage_model;
  const qa_q3_pres_scene_shader *shotgun_smoke_puff_shader, *dust_puff_shader; /* nullable */
} qa_q3_pres_mission_player_media;
typedef struct qa_q3_pres_player_presentation_settings {
  qa_q3_base_game_type game_type;
  bool camera_mode, no_player_animations;
  double animation_speed, swing_speed;
  bool draw_friend;
  double shadows;
  bool enable_breath, enable_dust, debug_position, debug_animation;
} qa_q3_pres_player_presentation_settings;
/* TS Pick<ClientEffects,"smokePuff"> (effects.ts mirror, no pool). */
typedef struct qa_q3_pres_player_effects_mirror {
  void *ctx;
  qa_q3_pres_local_entity *(*smoke_puff)(void *ctx, qa_q3_pres_smoke_puff_options_mirror options);
} qa_q3_pres_player_effects_mirror;
typedef struct qa_q3_pres_client_info_store qa_q3_pres_client_info_store;
/* TS PlayerPresentationHost: mission_media non-NULL only for
 * missionpack. clients is Pick<ClientInfoStore,"clientInfo">.
 * body_hidden/body_submission NULL are the TS absent optionals. */
typedef struct qa_q3_pres_player_presentation_host {
  qa_q3_base_product product;
  const qa_q3_pres_mission_player_media *mission_media;
  void *body_ctx;
  bool (*body_hidden)(void *ctx, double entity);
  bool (*body_submission)(void *ctx, double entity, qa_qvm_body_part part,
                          const qa_q3_pres_ref_model_entity *source, bool base);
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_player_media media;
  void *clients_ctx;
  qa_q3_pres_client_info *(*client_info)(void *ctx, double index);
  qa_q3_pres_collision_world collision; /* trace / point_contents only */
  qa_q3_pres_player_effects_mirror effects;
  qa_q3_game_random *random;
  void *ctx;
  qa_q3_pres_player_presentation_settings (*settings)(void *ctx);
  qa_q3_trace_result (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                              double skip_number, double mask);
  void *trace_ctx;
  void (*add_entity)(void *ctx, const qa_q3_pres_ref_entity *entity);
  void (*add_light)(void *ctx, qa_q3_pres_dynamic_light light);
  void (*add_poly)(void *ctx, const qa_q3_pres_ref_poly *poly);
  qa_q3_pres_lighting_sample (*light_for_point)(void *ctx, qa_vec3 point);
  qa_q3_pres_impact_mark_system_mirror marks;
  void (*add_looping_sound)(void *ctx, double entity_num, qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *sound);
  void (*add_player_weapon)(void *ctx, const qa_q3_pres_ref_model_entity *parent,
                            qa_q3_base_source_player_state *player_state,
                            qa_q3_pres_client_entity *entity, qa_q3_base_team team);
  void (*print)(void *ctx, const char *message);
} qa_q3_pres_player_presentation_host;
typedef struct qa_q3_pres_player_presenter {
  qa_q3_pres_player_presentation_host host;
} qa_q3_pres_player_presenter;
/* Product mismatch aborts with the TS Error text. */
void qa_q3_pres_player_presenter_init(qa_q3_pres_player_presenter *presenter,
    qa_q3_pres_player_presentation_host host);
void qa_q3_pres_player_presenter_reset_player_entity(qa_q3_pres_player_presenter *presenter,
    qa_q3_pres_client_entity *entity);
/* TS addRefEntityWithPowerups; part NULL is TS omitted. The entity is
 * mutated (customShader) exactly as in TS. False only on OOM freeing
 * mark polys is impossible here; returns void-shaped true. */
void qa_q3_pres_player_presenter_add_ref_entity_with_powerups(qa_q3_pres_player_presenter *presenter,
    qa_q3_pres_ref_model_entity *entity, const qa_q3_base_entity_state *state, qa_q3_base_team team,
    qa_qvm_body_part part);
/* TS lightVerts: vertices are colored in place; count >= 1. */
bool qa_q3_pres_player_presenter_light_verts(qa_q3_pres_player_presenter *presenter, qa_vec3 normal,
    qa_q3_pres_ref_poly_vertex *vertices, size_t vertex_count);
void qa_q3_pres_player_presenter_player(qa_q3_pres_player_presenter *presenter,
    qa_q3_pres_client_entity *entity);
/* TS ClientInfoStore over exactly 64 distinct canonical slots. */
bool qa_q3_pres_client_info_store_init(qa_q3_pres_client_info_store **out, qa_q3_pres_client_info_host host,
    qa_q3_pres_client_info *const *slots, size_t slot_count);
void qa_q3_pres_client_info_store_destroy(qa_q3_pres_client_info_store *store);
qa_q3_pres_client_info *qa_q3_pres_client_info_store_client_info(qa_q3_pres_client_info_store *store,
    double index);
/* Pick<view> adapter for PlayerPresentationHost clients / config. */
qa_q3_pres_client_info *qa_q3_pres_client_info_store_client_info_ctx(void *ctx, double index);
bool qa_q3_pres_client_info_store_new_client_info_ctx(void *ctx, double client_num, const char *config);
/* False only on OOM. */
bool qa_q3_pres_client_info_store_reset(qa_q3_pres_client_info_store *store);
qa_pcm_sound *qa_q3_pres_client_info_store_custom_sound(qa_q3_pres_client_info_store *store, double index,
    const char *name);
/* False only on OOM (including superseded loads, which TS swallows). */
bool qa_q3_pres_client_info_store_new_client_info(qa_q3_pres_client_info_store *store, double index,
    const char *configstring);
bool qa_q3_pres_client_info_store_load_deferred_players(qa_q3_pres_client_info_store *store,
    void (*reset_player_entity)(void *ctx, qa_q3_pres_client_entity *entity), void *reset_ctx);
#endif
