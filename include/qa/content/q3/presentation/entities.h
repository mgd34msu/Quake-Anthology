/* Port of src/content/q3/presentation/entities.ts. GPL-2.0-or-later.
 * Packet entity presentation from id Software's code/cgame/cg_ents.c.
 * TS PacketEntityMedia arrays are borrowed pointer+count spans; a null
 * SceneModel slot (barrelModel, models[1]) is has_* false. */
#ifndef QA_CONTENT_Q3_PRESENTATION_ENTITIES_H
#define QA_CONTENT_Q3_PRESENTATION_ENTITIES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/scene_host.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
typedef enum qa_q3_pres_missile_trail {
  QA_Q3_PRES_TRAIL_ROCKET, QA_Q3_PRES_TRAIL_GRENADE, QA_Q3_PRES_TRAIL_GRAPPLE,
  QA_Q3_PRES_TRAIL_NAIL, QA_Q3_PRES_TRAIL_PLASMA
} qa_q3_pres_missile_trail;
const char *qa_q3_pres_missile_trail_name(qa_q3_pres_missile_trail trail);
typedef struct qa_q3_pres_packet_weapon_info {
  qa_q3_pres_scene_model weapon_model;
  qa_vec3 weapon_midpoint;
  bool has_barrel_model;
  qa_q3_pres_scene_model barrel_model;
  qa_q3_pres_scene_model missile_model;
  double missile_renderfx;
  qa_pcm_sound *missile_sound; /* borrowed, nullable */
  double missile_dlight;
  qa_vec3 missile_dlight_color;
  bool has_missile_trail;
  qa_q3_pres_missile_trail missile_trail;
  double trail_radius, trail_time;
} qa_q3_pres_packet_weapon_info;
typedef struct qa_q3_pres_packet_item_visual {
  qa_q3_pres_scene_model models[2];
  bool has_second_model;
  const qa_q3_pres_scene_shader *icon; /* nullable */
} qa_q3_pres_packet_item_visual;
typedef struct qa_q3_pres_packet_mission_media {
  qa_pcm_sound *weapon_hover_sound; /* borrowed, nullable */
  qa_q3_pres_scene_model blue_prox_mine, overload_base_model, overload_energy_model;
  qa_q3_pres_scene_model overload_lights_model, overload_target_model;
  qa_pcm_sound *obelisk_respawn_sound; /* borrowed, nullable */
  qa_q3_pres_scene_model harvester_model, harvester_neutral_model;
  const qa_q3_pres_scene_skin *harvester_red_skin, *harvester_blue_skin; /* nullable */
} qa_q3_pres_packet_mission_media;
typedef struct qa_q3_pres_inline_model_entry {
  qa_q3_pres_scene_model model;
  qa_vec3 midpoint;
} qa_q3_pres_inline_model_entry;
typedef struct qa_q3_pres_packet_entity_media {
  const qa_q3_pres_scene_model *game_models; size_t game_model_count;
  qa_pcm_sound *const *game_sounds; size_t game_sound_count; /* borrowed, nullable entries */
  const qa_q3_pres_inline_model_entry *inline_models; size_t inline_model_count;
  const qa_q3_pres_packet_item_visual *items; size_t item_count;
  const qa_q3_pres_packet_weapon_info *weapons; size_t weapon_count;
  const qa_q3_pres_scene_shader *plasma_ball_shader; /* nullable */
  qa_q3_pres_scene_model red_flag_base_model, blue_flag_base_model, neutral_flag_base_model;
  qa_q3_base_product product; /* TS variant.product */
  qa_q3_pres_packet_mission_media mission; /* valid when product is missionpack */
} qa_q3_pres_packet_entity_media;
/* TS PacketEntityImports: optional body_hidden/pose are NULL when absent.
 * start_sound's TS `origin: Vec3 | null` is has_origin + origin. */
typedef struct qa_q3_pres_packet_entity_imports {
  void *ctx;
  bool (*body_hidden)(void *ctx, double entity);
  void (*pose)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*add_ref_entity)(void *ctx, const qa_q3_pres_ref_entity *entity);
  void (*add_light)(void *ctx, qa_q3_pres_dynamic_light light);
  void (*update_sound_position)(void *ctx, double entity, qa_vec3 origin);
  void (*add_loop_sound)(void *ctx, double entity, qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *sound, bool real_loop);
  void (*start_sound)(void *ctx, bool has_origin, qa_vec3 origin, double entity, double channel, qa_pcm_sound *sound);
  double (*random_integer)(void *ctx);
  void (*player)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*missile_trail)(void *ctx, qa_q3_pres_missile_trail kind, qa_q3_pres_client_entity *entity,
                        const qa_q3_pres_packet_weapon_info *weapon);
  void (*grapple_trail)(void *ctx, qa_q3_pres_client_entity *entity, const qa_q3_pres_packet_weapon_info *weapon);
  void (*add_entity_with_powerups)(void *ctx, const qa_q3_pres_ref_model_entity *entity,
                                   const qa_q3_base_entity_state *state, qa_q3_base_team team);
} qa_q3_pres_packet_entity_imports;
typedef struct qa_q3_pres_packet_entity_options {
  qa_q3_base_game_type game_type;
  bool smooth_clients, simple_items;
  double obelisk_respawn_delay;
} qa_q3_pres_packet_entity_options;
void qa_q3_pres_position_entity_on_tag(qa_q3_pres_ref_model_entity *entity,
    const qa_q3_pres_ref_model_entity *parent, const qa_q3_pres_scene_model *parent_model, const char *tag_name);
void qa_q3_pres_position_rotated_entity_on_tag(qa_q3_pres_ref_model_entity *entity,
    const qa_q3_pres_ref_model_entity *parent, const qa_q3_pres_scene_model *parent_model, const char *tag_name);
qa_vec3 qa_q3_pres_adjust_position_for_mover(qa_q3_pres_client_game_state *state, qa_vec3 input,
    double mover_num, double from_time, double to_time);
typedef struct qa_q3_pres_packet_entity_presenter {
  qa_q3_pres_client_game_state *state;
  const qa_q3_pres_packet_entity_media *media;
  qa_q3_pres_packet_entity_imports imports;
} qa_q3_pres_packet_entity_presenter;
/* TS constructor: product mismatch aborts with the TS Error text. */
void qa_q3_pres_packet_entity_presenter_init(qa_q3_pres_packet_entity_presenter *presenter,
    qa_q3_pres_client_game_state *state, const qa_q3_pres_packet_entity_media *media,
    qa_q3_pres_packet_entity_imports imports);
void qa_q3_pres_packet_entity_presenter_set_entity_sound_position(qa_q3_pres_packet_entity_presenter *presenter,
    qa_q3_pres_client_entity *entity);
void qa_q3_pres_packet_entity_presenter_calculate_lerp_positions(qa_q3_pres_packet_entity_presenter *presenter,
    qa_q3_pres_client_entity *entity, bool smooth_clients);
void qa_q3_pres_packet_entity_presenter_beam(qa_q3_pres_packet_entity_presenter *presenter,
    qa_q3_pres_client_entity *entity);
void qa_q3_pres_packet_entity_presenter_add_entity(qa_q3_pres_packet_entity_presenter *presenter,
    qa_q3_pres_client_entity *entity, qa_q3_pres_packet_entity_options options);
void qa_q3_pres_packet_entity_presenter_add_packet_entities(qa_q3_pres_packet_entity_presenter *presenter,
    qa_q3_pres_packet_entity_options options);
#endif
