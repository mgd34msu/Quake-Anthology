#include "frame_internal.h"
#include "qa/unified_frame_prediction.h"
#include "qa/unified_frame_player.h"
#include "qa/unified_frame_visuals.h"
#include "qa/unified_frame_q3.h"
#include "qa/unified_frame_components.h"
#include "qa/unified_frame_events.h"
#include "qa/unified_frame_metadata.h"
#include "qa/network_unified_session.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

static const qa_unified_record_layout handshake_hello_layout = {sizeof(qa_unified_token), NULL, 0, SIZE_MAX, QA_UNIFIED_KEY_NONE};
static const qa_unified_field handshake_token_fields[] = {QA_UNIFIED_RAW(qa_unified_token, bytes)};
static const qa_unified_record_layout handshake_token_layout = QA_UNIFIED_LAYOUT(qa_unified_token, handshake_token_fields);
static const qa_unified_record_layout *const handshake_variants[] = {
    &handshake_hello_layout, &handshake_token_layout, &handshake_token_layout,
};
static const qa_unified_field handshake_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_handshake, kind, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_RAW(qa_unified_handshake, nonce),
    QA_UNIFIED_VARIANT(qa_unified_handshake, token, kind, handshake_variants),
};
const qa_unified_record_layout qa_unified_handshake_layout = QA_UNIFIED_LAYOUT(qa_unified_handshake, handshake_fields);

const qa_unified_record_layout qa_unified_component_frame_layout;
static const qa_unified_record_layout qa_unified_component_source_layout;
static const qa_unified_record_layout qa_unified_component_binding_layout;
const qa_unified_record_layout qa_unified_actor_layout;
static const qa_unified_record_layout qa_q3_snapshot_layout;
static const qa_unified_record_layout uint8_t_layout;
static const qa_unified_record_layout qa_q3_entity_layout;
static const qa_unified_record_layout float_layout;
static const qa_unified_record_layout qa_q3_trajectory_layout;
static const qa_unified_record_layout qa_q3_player_layout;
static const qa_unified_record_layout int32_t_layout;
static const qa_unified_record_layout qa_unified_component_owner_layout;
static const qa_unified_record_layout qa_unified_native_component_layout;
static const qa_unified_record_layout qa_unified_native_camera_layout;
static const qa_unified_record_layout double_layout;
const qa_unified_record_layout qa_unified_vector_layout;
static const qa_unified_record_layout qa_unified_native_hud_layout;
static const qa_unified_record_layout int16_t_layout;
const qa_unified_record_layout qa_unified_q3_frame_layout;
static const qa_unified_record_layout qa_unified_q3_source_layout;
static const qa_unified_record_layout uint64_t_layout;
static const qa_unified_record_layout qa_q3_gamestate_layout;
static const qa_unified_record_layout uint16_t_layout;
static const qa_unified_record_layout bool_layout;
static const qa_unified_record_layout qa_unified_q3_visibility_layout;
static const qa_unified_record_layout qa_unified_q3_client_layout;
static const qa_unified_record_layout qa_unified_q3_entity_layout;
const qa_unified_record_layout qa_unified_bounds_layout;
const qa_unified_record_layout qa_unified_visual_frame_layout;
static const qa_unified_record_layout qa_unified_world_text_layout;
static const qa_unified_record_layout qa_unified_character_state_layout;
static const qa_unified_record_layout qa_unified_model_state_layout;
static const qa_unified_record_layout qa_unified_model_attachment_layout;
static const qa_unified_record_layout qa_unified_weapon_anchor_layout;
static const qa_unified_record_layout qa_unified_q3_weapon_view_layout;
static const qa_unified_record_layout qa_unified_q2_flare_layout;
static const qa_unified_record_layout qa_unified_source_identity_layout;
const qa_unified_record_layout qa_unified_player_frame_layout;
static const qa_unified_record_layout qa_unified_client_presentation_layout;
static const qa_unified_record_layout qa_unified_player_ui_layout;
static const qa_unified_record_layout qa_unified_q1_team_face_layout;
static const qa_unified_record_layout qa_unified_native_inventory_layout;
static const qa_unified_record_layout qa_unified_inventory_presentation_layout;
const qa_unified_record_layout qa_unified_provider_layout;
static const qa_unified_record_layout qa_unified_native_inventory_item_layout;
static const qa_unified_record_layout qa_unified_weapon_status_layout;
static const qa_unified_record_layout qa_unified_ui_item_layout;
static const qa_unified_record_layout qa_unified_powerup_state_layout;
const qa_unified_record_layout qa_unified_inventory_entry_layout;
static const qa_unified_record_layout qa_unified_armor_state_layout;
static const qa_unified_record_layout qa_unified_player_view_layout;
const qa_unified_record_layout qa_unified_prediction_layout;
static const qa_unified_record_layout qa_spatial_actor_layout;
static const qa_unified_record_layout qa_actor_collision_layout;
static const qa_unified_record_layout reference_layout;
static const qa_unified_record_layout qa_linked_body_layout;
const qa_unified_record_layout qa_unified_body_layout;
static const qa_unified_record_layout qa_movement_ground_layout;
static const qa_unified_record_layout qa_movement_environment_layout;
static const qa_unified_record_layout qa_movement_posture_layout;
static const qa_unified_record_layout qa_unified_animation_state_layout;
static const qa_unified_record_layout qa_unified_weapon_state_layout;
static const qa_unified_record_layout qa_movement_state_layout;
static const qa_unified_record_layout qa_movement_profile_layout;
static const qa_unified_record_layout qa_unified_movement_numeric_layout;
static const qa_unified_record_layout qa_clock_config_layout;
static const qa_unified_record_layout qa_unified_world_frame_layout;
static const qa_unified_record_layout qa_unified_resource_state_layout;
static const qa_unified_record_layout qa_unified_configuration_state_layout;
static const qa_unified_record_layout qa_unified_body_state_layout;
static const qa_unified_record_layout qa_unified_actor_state_layout;
static const qa_unified_record_layout qa_source_frame_layout;
const qa_unified_record_layout qa_unified_frame_layout;
static const qa_unified_record_layout qa_unified_inventory_state_layout;
static const qa_unified_record_layout qa_nq_movement_state_layout;
static const qa_unified_record_layout qa_qw_movement_state_layout;
static const qa_unified_record_layout qa_q2r_movement_state_layout;
static const qa_unified_record_layout qa_q3_movement_state_layout;
static const qa_unified_record_layout qa_q1_movement_parameters_layout;
static const qa_unified_record_layout qa_qw_origin_layout;

static const qa_unified_field qa_unified_frame_components_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_frame_components, revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_ARRAY(qa_unified_frame_components, native, native_count, qa_unified_native_component_layout, 256),
    QA_UNIFIED_ARRAY(qa_unified_frame_components, sources, source_count, qa_unified_component_source_layout, 256),
};
const qa_unified_record_layout qa_unified_component_frame_layout = QA_UNIFIED_LAYOUT(qa_unified_frame_components, qa_unified_frame_components_fields);

static const qa_unified_field qa_unified_component_source_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_component_source, owner, qa_unified_component_owner_layout),
    QA_UNIFIED_FIELD(qa_unified_component_source, abi, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_component_source, viewer, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_component_source, client_number, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_component_source, game_state_revision, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_FIELD(qa_unified_component_source, weapon_presented, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_component_source, scene, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_component_source, scene_revision, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_RECORD(qa_unified_component_source, snapshot, qa_q3_snapshot_layout),
    QA_UNIFIED_ARRAY(qa_unified_component_source, bindings, binding_count, qa_unified_component_binding_layout, QA_Q3_ENTITIES),
};
static const qa_unified_record_layout qa_unified_component_source_layout = QA_UNIFIED_LAYOUT(qa_unified_component_source, qa_unified_component_source_fields);

static const qa_unified_field qa_unified_component_binding_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_component_binding, slot, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_RECORD(qa_unified_component_binding, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_component_binding, owned, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_component_binding_layout = QA_UNIFIED_LAYOUT(qa_unified_component_binding, qa_unified_component_binding_fields);

static const qa_unified_field qa_actor_id_fields[] = {
    QA_UNIFIED_FIELD(qa_actor_id, registry, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_actor_id, generation, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_actor_id, slot, QA_UNIFIED_FIELD_U32),
};
const qa_unified_record_layout qa_unified_actor_layout = QA_UNIFIED_LAYOUT(qa_actor_id, qa_actor_id_fields);

static const qa_unified_field qa_q3_snapshot_fields[] = {
    QA_UNIFIED_FIELD(qa_q3_snapshot, valid, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_q3_snapshot, message_number, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_snapshot, server_time, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_snapshot, delta_number, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_snapshot, server_command_number, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_snapshot, parse_entities_number, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_q3_snapshot, flags, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_FIELD(qa_q3_snapshot, area_bytes, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_FIXED(qa_q3_snapshot, area_mask, uint8_t_layout, 32),
    QA_UNIFIED_RECORD(qa_q3_snapshot, player, qa_q3_player_layout),
    QA_UNIFIED_ARRAY(qa_q3_snapshot, entities, entity_count, qa_q3_entity_layout, 256),
};
static const qa_unified_record_layout qa_q3_snapshot_layout = QA_UNIFIED_LAYOUT(qa_q3_snapshot, qa_q3_snapshot_fields);

static const qa_unified_field uint8_t_fields[] = {
    {QA_UNIFIED_FIELD_U8, 0, NULL, 0, 0, NULL},
};
static const qa_unified_record_layout uint8_t_layout = {sizeof(uint8_t), uint8_t_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};

static const qa_unified_field qa_q3_entity_fields[] = {
    QA_UNIFIED_FIELD(qa_q3_entity, number, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, eType, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, eFlags, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_q3_entity, pos, qa_q3_trajectory_layout),
    QA_UNIFIED_RECORD(qa_q3_entity, apos, qa_q3_trajectory_layout),
    QA_UNIFIED_FIELD(qa_q3_entity, time, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, time2, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_entity, origin, float_layout, 3),
    QA_UNIFIED_FIXED(qa_q3_entity, origin2, float_layout, 3),
    QA_UNIFIED_FIXED(qa_q3_entity, angles, float_layout, 3),
    QA_UNIFIED_FIXED(qa_q3_entity, angles2, float_layout, 3),
    QA_UNIFIED_FIELD(qa_q3_entity, otherEntityNum, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, otherEntityNum2, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, groundEntityNum, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, constantLight, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, loopSound, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, modelindex, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, modelindex2, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, clientNum, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, frame, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, solid, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, event, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, eventParm, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, powerups, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, weapon, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, legsAnim, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, torsoAnim, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_entity, generic1, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout qa_q3_entity_layout = QA_UNIFIED_NUMBER_LAYOUT(qa_q3_entity, qa_q3_entity_fields, number, QA_UNIFIED_KEY_I32);

static const qa_unified_field float_fields[] = {
    {QA_UNIFIED_FIELD_F32, 0, NULL, 0, 0, NULL},
};
static const qa_unified_record_layout float_layout = {sizeof(float), float_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};

static const qa_unified_field qa_q3_trajectory_fields[] = {
    QA_UNIFIED_FIELD(qa_q3_trajectory, type, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_trajectory, time, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_trajectory, duration, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_trajectory, base, float_layout, 3),
    QA_UNIFIED_FIXED(qa_q3_trajectory, delta, float_layout, 3),
};
static const qa_unified_record_layout qa_q3_trajectory_layout = QA_UNIFIED_LAYOUT(qa_q3_trajectory, qa_q3_trajectory_fields);

static const qa_unified_field qa_q3_player_fields[] = {
    QA_UNIFIED_FIELD(qa_q3_player, product, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, commandTime, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, pmType, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, bobCycle, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, pmFlags, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, pmTime, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_player, origin, float_layout, 3),
    QA_UNIFIED_FIXED(qa_q3_player, velocity, float_layout, 3),
    QA_UNIFIED_FIELD(qa_q3_player, weaponTime, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, gravity, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, speed, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_player, deltaAngles, int32_t_layout, 3),
    QA_UNIFIED_FIELD(qa_q3_player, groundEntityNum, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, legsTimer, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, legsAnim, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, torsoTimer, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, torsoAnim, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, movementDir, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_player, grapplePoint, float_layout, 3),
    QA_UNIFIED_FIELD(qa_q3_player, eFlags, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, eventSequence, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_player, events, int32_t_layout, 2),
    QA_UNIFIED_FIXED(qa_q3_player, eventParms, int32_t_layout, 2),
    QA_UNIFIED_FIELD(qa_q3_player, externalEvent, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, externalEventParm, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, externalEventTime, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, clientNum, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, weapon, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, weaponState, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_player, viewangles, float_layout, 3),
    QA_UNIFIED_FIELD(qa_q3_player, viewheight, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, damageEvent, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, damageYaw, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, damagePitch, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, damageCount, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_player, stats, int32_t_layout, 16),
    QA_UNIFIED_FIXED(qa_q3_player, persistant, int32_t_layout, 16),
    QA_UNIFIED_FIXED(qa_q3_player, powerups, int32_t_layout, 16),
    QA_UNIFIED_FIXED(qa_q3_player, ammo, int32_t_layout, 16),
    QA_UNIFIED_FIELD(qa_q3_player, generic1, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, loopSound, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, jumppadEnt, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, ping, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, pmoveFramecount, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, jumppadFrame, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_player, entityEventSequence, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout qa_q3_player_layout = QA_UNIFIED_LAYOUT(qa_q3_player, qa_q3_player_fields);

static const qa_unified_field int32_t_fields[] = {
    {QA_UNIFIED_FIELD_I32, 0, NULL, 0, 0, NULL},
};
static const qa_unified_record_layout int32_t_layout = {sizeof(int32_t), int32_t_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};

static const qa_unified_field qa_unified_component_owner_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_component_owner, provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_component_owner, generation, QA_UNIFIED_FIELD_U64),
};
static const qa_unified_record_layout qa_unified_component_owner_layout = QA_UNIFIED_LAYOUT(qa_unified_component_owner, qa_unified_component_owner_fields);

static const qa_unified_field qa_unified_native_component_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_native_component, owner, qa_unified_component_owner_layout),
    QA_UNIFIED_FIELD(qa_unified_native_component, generation, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_RECORD(qa_unified_native_component, viewer, qa_unified_actor_layout),
    QA_UNIFIED_POINTER(qa_unified_native_component, hud, qa_unified_native_hud_layout),
    QA_UNIFIED_POINTER(qa_unified_native_component, view, qa_unified_native_camera_layout),
};
static const qa_unified_record_layout qa_unified_native_component_layout = QA_UNIFIED_LAYOUT(qa_unified_native_component, qa_unified_native_component_fields);

static const qa_unified_field qa_unified_native_camera_fields[] = {
    QA_UNIFIED_FIXED(qa_unified_native_camera, origin, double_layout, 3),
    QA_UNIFIED_FIXED(qa_unified_native_camera, angles, double_layout, 3),
    QA_UNIFIED_FIELD(qa_unified_native_camera, view_height, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIXED(qa_unified_native_camera, kick_angles, double_layout, 3),
    QA_UNIFIED_FIELD(qa_unified_native_camera, field_of_view, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIXED(qa_unified_native_camera, blend, double_layout, 4),
    QA_UNIFIED_FIXED(qa_unified_native_camera, damage_blend, double_layout, 4),
    QA_UNIFIED_FIELD(qa_unified_native_camera, rerelease, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_native_camera, movement_origin, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_native_camera, render_flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_native_camera, position_prediction, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_native_camera, angular_prediction, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_native_camera, weapon_visible, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_native_camera_layout = QA_UNIFIED_LAYOUT(qa_unified_native_camera, qa_unified_native_camera_fields);

static const qa_unified_field double_fields[] = {
    {QA_UNIFIED_FIELD_F64, 0, NULL, 0, 0, NULL},
};
static const qa_unified_record_layout double_layout = {sizeof(double), double_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};

static const qa_unified_field qa_vec3_fields[] = {
    QA_UNIFIED_FIELD(qa_vec3, x, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_vec3, y, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_vec3, z, QA_UNIFIED_FIELD_F32),
};
const qa_unified_record_layout qa_unified_vector_layout = QA_UNIFIED_LAYOUT(qa_vec3, qa_vec3_fields);

static const qa_unified_field qa_unified_native_hud_fields[] = {
    QA_UNIFIED_FIXED(qa_unified_native_hud, stats, int16_t_layout, 64),
    QA_UNIFIED_FIELD(qa_unified_native_hud, stat_count, QA_UNIFIED_FIELD_SIZE),
    QA_UNIFIED_FIELD(qa_unified_native_hud, server_frame, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_native_hud, time_ms, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_native_hud, frame_time_ms, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_native_hud, has_frame_time, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_native_hud_layout = QA_UNIFIED_LAYOUT(qa_unified_native_hud, qa_unified_native_hud_fields);

static const qa_unified_field int16_t_fields[] = {
    {QA_UNIFIED_FIELD_I16, 0, NULL, 0, 0, NULL},
};
static const qa_unified_record_layout int16_t_layout = {sizeof(int16_t), int16_t_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};

static const qa_unified_field qa_unified_frame_q3_fields[] = {
    QA_UNIFIED_ARRAY(qa_unified_frame_q3, sources, source_count, qa_unified_q3_source_layout, 256),
};
const qa_unified_record_layout qa_unified_q3_frame_layout = QA_UNIFIED_LAYOUT(qa_unified_frame_q3, qa_unified_frame_q3_fields);

static const qa_unified_field qa_unified_q3_source_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q3_source, provider_name, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q3_source, instance, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q3_source, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q3_source, publication, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_q3_source, map_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_q3_source, configuration_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_q3_source, product, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_source, server_time, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_source, level_start, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_source, game_type, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_source, max_clients, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q3_source, snapshot_bit, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_RECORD(qa_unified_q3_source, viewer, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q3_source, has_client, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q3_source, client_number, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_ARRAY(qa_unified_q3_source, entities, entity_count, qa_unified_q3_entity_layout, QA_Q3_ENTITIES),
    QA_UNIFIED_ARRAY(qa_unified_q3_source, clients, client_count, qa_unified_q3_client_layout, 64),
    QA_UNIFIED_POINTER(qa_unified_q3_source, visibility, qa_unified_q3_visibility_layout),
};
static const qa_unified_record_layout qa_unified_q3_source_layout = QA_UNIFIED_LAYOUT(qa_unified_q3_source, qa_unified_q3_source_fields);

static const qa_unified_field uint64_t_fields[] = {
    {QA_UNIFIED_FIELD_U64, 0, NULL, 0, 0, NULL},
};
static const qa_unified_record_layout uint64_t_layout = {sizeof(uint64_t), uint64_t_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};

static const qa_unified_field qa_q3_gamestate_fields[] = {
    QA_UNIFIED_FIELD(qa_q3_gamestate, command_sequence, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_gamestate, client_number, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_gamestate, checksum_feed, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_gamestate, config_offsets, uint16_t_layout, QA_Q3_CONFIGSTRINGS),
    QA_UNIFIED_RAW(qa_q3_gamestate, strings),
    QA_UNIFIED_FIELD(qa_q3_gamestate, string_bytes, QA_UNIFIED_FIELD_SIZE),
    QA_UNIFIED_FIXED(qa_q3_gamestate, baseline_present, bool_layout, QA_Q3_ENTITIES),
    QA_UNIFIED_FIXED(qa_q3_gamestate, baselines, qa_q3_entity_layout, QA_Q3_ENTITIES),
};
static const qa_unified_record_layout qa_q3_gamestate_layout = QA_UNIFIED_LAYOUT(qa_q3_gamestate, qa_q3_gamestate_fields);

static const qa_unified_field uint16_t_fields[] = {
    {QA_UNIFIED_FIELD_U16, 0, NULL, 0, 0, NULL},
};
static const qa_unified_record_layout uint16_t_layout = {sizeof(uint16_t), uint16_t_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};

static const qa_unified_field bool_fields[] = {
    {QA_UNIFIED_FIELD_BOOL, 0, NULL, 0, 0, NULL},
};
static const qa_unified_record_layout bool_layout = {sizeof(bool), bool_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};

static const qa_unified_field qa_unified_q3_visibility_fields[] = {
    QA_UNIFIED_FIXED(qa_unified_q3_visibility, area_mask, uint8_t_layout, 32),
    QA_UNIFIED_ARRAY(qa_unified_q3_visibility, entities, entity_count, int32_t_layout, 256),
};
static const qa_unified_record_layout qa_unified_q3_visibility_layout = QA_UNIFIED_LAYOUT(qa_unified_q3_visibility, qa_unified_q3_visibility_fields);

static const qa_unified_field qa_unified_q3_client_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q3_client, source_number, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q3_client, client_slot, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q3_client, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_client, state, qa_q3_player_layout),
};
static const qa_unified_record_layout qa_unified_q3_client_layout = QA_UNIFIED_LAYOUT(qa_unified_q3_client, qa_unified_q3_client_fields);

static const qa_unified_field qa_unified_q3_entity_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q3_entity, number, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_RECORD(qa_unified_q3_entity, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_entity, state, qa_q3_entity_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_entity, origin, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_q3_entity, linked, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q3_entity, server_flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q3_entity, single_client, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q3_entity, link_bounds, qa_unified_bounds_layout),
};
static const qa_unified_record_layout qa_unified_q3_entity_layout = QA_UNIFIED_NUMBER_LAYOUT(qa_unified_q3_entity, qa_unified_q3_entity_fields, number, QA_UNIFIED_KEY_U32);

static const qa_unified_field qa_bounds_fields[] = {
    QA_UNIFIED_RECORD(qa_bounds, mins, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_bounds, maxs, qa_unified_vector_layout),
};
const qa_unified_record_layout qa_unified_bounds_layout = QA_UNIFIED_LAYOUT(qa_bounds, qa_bounds_fields);

static const qa_unified_field qa_unified_frame_visuals_fields[] = {
    QA_UNIFIED_ARRAY(qa_unified_frame_visuals, models, model_count, qa_unified_model_state_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_frame_visuals, characters, character_count, qa_unified_character_state_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_frame_visuals, world_text, world_text_count, qa_unified_world_text_layout, 65536),
};
const qa_unified_record_layout qa_unified_visual_frame_layout = QA_UNIFIED_LAYOUT(qa_unified_frame_visuals, qa_unified_frame_visuals_fields);

static const qa_unified_field qa_unified_world_text_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_world_text, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_world_text, text, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_world_text, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_world_text, angles, qa_unified_vector_layout),
    QA_UNIFIED_FIXED(qa_unified_world_text, color, float_layout, 4),
    QA_UNIFIED_FIELD(qa_unified_world_text, cell_size, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_world_text, distance_cull_factor, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_world_text, billboard, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_world_text, depth_test, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_world_text_layout = QA_UNIFIED_LAYOUT(qa_unified_world_text, qa_unified_world_text_fields);

static const qa_unified_field qa_unified_character_state_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_character_state, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_character_state, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_character_state, angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_character_state, velocity, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_character_state, movement_direction, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_character_state, legs, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_character_state, torso, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_character_state, legs_timer_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_character_state, torso_timer_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_character_state, team, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_character_state, source_flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_character_state, powerups, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIXED(qa_unified_character_state, color, float_layout, 4),
    QA_UNIFIED_FIELD(qa_unified_character_state, scale, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_character_state, opacity, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_character_state, has_team, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_character_state_layout = QA_UNIFIED_LAYOUT(qa_unified_character_state, qa_unified_character_state_fields);

static const qa_unified_field qa_unified_model_state_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_model_state, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_model_state, family, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_model_state, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_model_state, path, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_model_state, skin_path, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_model_state, weapon_item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_model_state, frame, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_FIELD(qa_unified_model_state, old_frame, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_FIELD(qa_unified_model_state, skin, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_FIELD(qa_unified_model_state, effects, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_model_state, q1_effects, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_model_state, render_flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_RECORD(qa_unified_model_state, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_model_state, angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_model_state, previous_origin, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_model_state, scale, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_model_state, alpha, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_model_state, back_lerp, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_model_state, visible, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_model_state, view_weapon, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_model_state, native_held_weapon, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_model_state, has_previous_origin, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_model_state, has_alpha, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_model_state, has_player_colors, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_model_state, player_colors, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_POINTER(qa_unified_model_state, render_source, qa_unified_source_identity_layout),
    QA_UNIFIED_POINTER(qa_unified_model_state, render_equipment, qa_unified_source_identity_layout),
    QA_UNIFIED_FIELD(qa_unified_model_state, equipment_slot, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_POINTER(qa_unified_model_state, flare, qa_unified_q2_flare_layout),
    QA_UNIFIED_POINTER(qa_unified_model_state, q3_weapon, qa_unified_q3_weapon_view_layout),
    QA_UNIFIED_POINTER(qa_unified_model_state, anchor, qa_unified_weapon_anchor_layout),
    QA_UNIFIED_ARRAY(qa_unified_model_state, attachments, attachment_count, qa_unified_model_attachment_layout, 256),
};
static const qa_unified_record_layout qa_unified_model_state_layout = QA_UNIFIED_LAYOUT(qa_unified_model_state, qa_unified_model_state_fields);

static const qa_unified_field qa_unified_model_attachment_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_model_attachment, path, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_model_attachment, tag, QA_UNIFIED_FIELD_STRING),
};
static const qa_unified_record_layout qa_unified_model_attachment_layout = QA_UNIFIED_LAYOUT(qa_unified_model_attachment, qa_unified_model_attachment_fields);

static const qa_unified_field qa_unified_weapon_anchor_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_weapon_anchor, path, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_weapon_anchor, tag, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_weapon_anchor, offset, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_weapon_anchor, fov_above, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_weapon_anchor, fov_scale, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_unified_weapon_anchor_layout = QA_UNIFIED_LAYOUT(qa_unified_weapon_anchor, qa_unified_weapon_anchor_fields);

static const qa_unified_field qa_unified_q3_weapon_view_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q3_weapon_view, time_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_weapon_view, torso_animation, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_weapon_view, last_fire_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_weapon_view, bob_cycle, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_weapon_view, weapon, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_weapon_view, has_last_fire_ms, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q3_weapon_view, firing, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q3_weapon_view, horizontal_speed, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_unified_q3_weapon_view_layout = QA_UNIFIED_LAYOUT(qa_unified_q3_weapon_view, qa_unified_q3_weapon_view_fields);

static const qa_unified_field qa_unified_q2_flare_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q2_flare, image, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_flare, fade_start, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_flare, fade_end, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_flare, scale, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_RECORD(qa_unified_q2_flare, color, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_flare, rim_color, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_flare, has_rim_color, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_flare, lock_angle, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_q2_flare_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_flare, qa_unified_q2_flare_fields);

static const qa_unified_field qa_unified_source_identity_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_source_identity, provider, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_source_identity, instance, QA_UNIFIED_FIELD_STRING),
};
static const qa_unified_record_layout qa_unified_source_identity_layout = QA_UNIFIED_LAYOUT(qa_unified_source_identity, qa_unified_source_identity_fields);

static const qa_unified_field qa_unified_frame_player_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_frame_player, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_player, ui_actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_player, view, qa_unified_player_view_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_player, ui, qa_unified_player_ui_layout),
    QA_UNIFIED_POINTER(qa_unified_frame_player, client_presentation, qa_unified_client_presentation_layout),
};
const qa_unified_record_layout qa_unified_player_frame_layout = QA_UNIFIED_LAYOUT(qa_unified_frame_player, qa_unified_frame_player_fields);

static const qa_unified_field qa_unified_client_presentation_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_client_presentation, recipient, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_client_presentation, has_hud, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_client_presentation, has_view, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_client_presentation, hud_source, qa_unified_source_identity_layout),
    QA_UNIFIED_RECORD(qa_unified_client_presentation, view_source, qa_unified_source_identity_layout),
    QA_UNIFIED_FIELD(qa_unified_client_presentation, health, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_client_presentation, armor, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_RECORD(qa_unified_client_presentation, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_client_presentation, angles, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_client_presentation, view_height, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_unified_client_presentation_layout = QA_UNIFIED_LAYOUT(qa_unified_client_presentation, qa_unified_client_presentation_fields);

static const qa_unified_field qa_unified_player_ui_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_player_ui, health, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_RECORD(qa_unified_player_ui, armor, qa_unified_armor_state_layout),
    QA_UNIFIED_ARRAY(qa_unified_player_ui, inventory, inventory_count, qa_unified_inventory_entry_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_player_ui, powerups, powerup_count, qa_unified_powerup_state_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_player_ui, items, item_count, qa_unified_ui_item_layout, 65536),
    QA_UNIFIED_POINTER(qa_unified_player_ui, weapon_status, qa_unified_weapon_status_layout),
    QA_UNIFIED_FIELD(qa_unified_player_ui, arsenal_warning, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_player_ui, active_weapon, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_player_ui, ammo_item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_player_ui, has_ammo, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_ui, selected_arsenal, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_ui, ammo_count, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_POINTER(qa_unified_player_ui, native_inventory, qa_unified_native_inventory_layout),
    QA_UNIFIED_POINTER(qa_unified_player_ui, q1_team_face, qa_unified_q1_team_face_layout),
};
static const qa_unified_record_layout qa_unified_player_ui_layout = QA_UNIFIED_LAYOUT(qa_unified_player_ui, qa_unified_player_ui_fields);

static const qa_unified_field qa_unified_q1_team_face_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q1_team_face, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q1_team_face, colors, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_FIELD(qa_unified_q1_team_face, frags, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_unified_q1_team_face_layout = QA_UNIFIED_LAYOUT(qa_unified_q1_team_face, qa_unified_q1_team_face_fields);

static const qa_unified_field qa_unified_native_inventory_fields[] = {
    QA_UNIFIED_ARRAY(qa_unified_native_inventory, items, item_count, qa_unified_native_inventory_item_layout, 65536),
    QA_UNIFIED_FIELD(qa_unified_native_inventory, selected, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_native_inventory, presentation, qa_unified_inventory_presentation_layout),
};
static const qa_unified_record_layout qa_unified_native_inventory_layout = QA_UNIFIED_LAYOUT(qa_unified_native_inventory, qa_unified_native_inventory_fields);

static const qa_unified_field qa_unified_inventory_presentation_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_inventory_presentation, source, qa_unified_provider_layout),
    QA_UNIFIED_FIELD(qa_unified_inventory_presentation, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_inventory_presentation, icon_kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_inventory_presentation, weapon, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_inventory_presentation, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_inventory_presentation, path, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_inventory_presentation, lump, QA_UNIFIED_FIELD_STRING),
};
static const qa_unified_record_layout qa_unified_inventory_presentation_layout = QA_UNIFIED_LAYOUT(qa_unified_inventory_presentation, qa_unified_inventory_presentation_fields);

static const qa_unified_field qa_unified_provider_state_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_provider_state, provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_provider_state, content, QA_UNIFIED_FIELD_STRING),
};
const qa_unified_record_layout qa_unified_provider_layout = QA_UNIFIED_LAYOUT(qa_unified_provider_state, qa_unified_provider_state_fields);

static const qa_unified_field qa_unified_native_inventory_item_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_native_inventory_item, item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_native_inventory_item, label, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_native_inventory_item, count, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_unified_native_inventory_item_layout = QA_UNIFIED_LAYOUT(qa_unified_native_inventory_item, qa_unified_native_inventory_item_fields);

static const qa_unified_field qa_unified_weapon_status_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_weapon_status, source, qa_unified_provider_layout),
    QA_UNIFIED_FIELD(qa_unified_weapon_status, item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_weapon_status, label, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_weapon_status, ammo_item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_weapon_status, finite, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_weapon_status, has_ammo_to_start, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_weapon_status, low, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_weapon_status, count, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_unified_weapon_status_layout = QA_UNIFIED_LAYOUT(qa_unified_weapon_status, qa_unified_weapon_status_fields);

static const qa_unified_field qa_unified_ui_item_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_ui_item, id, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_ui_item, label, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_ui_item, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_ui_item, source_ordinal, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_FIELD(qa_unified_ui_item, owned, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_ui_item, has_ammo, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_ui_item, has_count, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_ui_item, count, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_ui_item, warning_count, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_unified_ui_item_layout = QA_UNIFIED_LAYOUT(qa_unified_ui_item, qa_unified_ui_item_fields);

static const qa_unified_field qa_unified_powerup_state_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_powerup_state, id, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_powerup_state, label, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_powerup_state, seconds, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_unified_powerup_state_layout = QA_UNIFIED_LAYOUT(qa_unified_powerup_state, qa_unified_powerup_state_fields);

static const qa_unified_field qa_unified_inventory_entry_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_inventory_entry, item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_inventory_entry, count, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_inventory_entry, capacity, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_inventory_entry, policy, QA_UNIFIED_FIELD_I32),
};
const qa_unified_record_layout qa_unified_inventory_entry_layout = QA_UNIFIED_LAYOUT(qa_unified_inventory_entry, qa_unified_inventory_entry_fields);

static const qa_unified_field qa_unified_armor_state_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_armor_state, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_armor_state, source, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_armor_state, item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_armor_state, points, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_armor_state, absorption, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_armor_state, normal, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_armor_state, energy, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_armor_state, protection, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_armor_state, powered_kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_armor_state, cells, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_unified_armor_state_layout = QA_UNIFIED_LAYOUT(qa_unified_armor_state, qa_unified_armor_state_fields);

static const qa_unified_field qa_unified_player_view_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_player_view, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_player_view, angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_player_view, kick_angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_player_view, client_view_offset_delta, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_player_view, view_height, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_player_view, field_of_view, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIXED(qa_unified_player_view, blend, float_layout, 4),
    QA_UNIFIED_FIXED(qa_unified_player_view, damage_blend, float_layout, 4),
    QA_UNIFIED_FIELD(qa_unified_player_view, has_field_of_view, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_view, has_client_view_offset_delta, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_view, has_blend, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_view, has_damage_blend, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_view, foreign_character_death, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_view, has_pitch_drift, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_view, grounded, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_view, pitch_drift_disabled, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_player_view, ideal_pitch, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_unified_player_view_layout = QA_UNIFIED_LAYOUT(qa_unified_player_view, qa_unified_player_view_fields);

static const qa_unified_field qa_unified_frame_prediction_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, sequence, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, command_time_ms, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, profile_id, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, clock, qa_clock_config_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, numeric, qa_unified_movement_numeric_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, profile, qa_movement_profile_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, state, qa_movement_state_layout),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, arsenal_provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, active_weapon, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, character_provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, weapon, qa_unified_weapon_state_layout),
    QA_UNIFIED_ARRAY(qa_unified_frame_prediction, ammo, ammo_count, qa_unified_inventory_entry_layout, 65536),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, animation, qa_unified_animation_state_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, standing, qa_movement_posture_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, crouched, qa_movement_posture_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, dead, qa_movement_posture_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, invulnerability_bounds, qa_unified_bounds_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, bounds, qa_unified_bounds_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, view_angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, view_offset, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, view_height, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, environment, qa_movement_environment_layout),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, has_client_view_offset, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, client_view_offset, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, ground, qa_movement_ground_layout),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, water_level, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, water_type, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_frame_prediction, has_rerelease_origin, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_frame_prediction, rerelease_origin, qa_unified_vector_layout),
};
const qa_unified_record_layout qa_unified_prediction_layout = QA_UNIFIED_LAYOUT(qa_unified_frame_prediction, qa_unified_frame_prediction_fields);

static const qa_unified_field qa_spatial_actor_fields[] = {
    QA_UNIFIED_RECORD(qa_spatial_actor, body, qa_linked_body_layout),
    QA_UNIFIED_RECORD(qa_spatial_actor, collision, qa_actor_collision_layout),
};
static const qa_unified_record_layout qa_spatial_actor_layout = QA_UNIFIED_LAYOUT(qa_spatial_actor, qa_spatial_actor_fields);

static const qa_unified_field qa_actor_collision_fields[] = {
    QA_UNIFIED_FIELD(qa_actor_collision, family, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_actor_collision, shape, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_actor_collision, inline_model, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_actor_collision, model, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_actor_collision, contents, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_actor_collision, owner, reference_layout),
    QA_UNIFIED_FIELD(qa_actor_collision, role, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_actor_collision, monster, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_actor_collision, dead_monster, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_actor_collision, q1_corpse, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_actor_collision, has_q3_owner, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_actor_collision, q3_entity_number, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_actor_collision, q3_owner_number, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout qa_actor_collision_layout = QA_UNIFIED_LAYOUT(qa_actor_collision, qa_actor_collision_fields);

static const qa_unified_field qa_linked_body_fields[] = {
    QA_UNIFIED_RECORD(qa_linked_body, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_linked_body, state, qa_unified_body_layout),
    QA_UNIFIED_RECORD(qa_linked_body, absolute_bounds, qa_unified_bounds_layout),
    QA_UNIFIED_FIELD(qa_linked_body, link_count, QA_UNIFIED_FIELD_U64),
};
static const qa_unified_record_layout qa_linked_body_layout = QA_UNIFIED_LAYOUT(qa_linked_body, qa_linked_body_fields);

static const qa_unified_record_layout reference_none_layout = {sizeof(qa_actor_id), NULL, 0, SIZE_MAX, QA_UNIFIED_KEY_NONE};
static const qa_unified_field reference_source_fields[] = {
    {QA_UNIFIED_FIELD_U32, 0, NULL, 0, 0, NULL},
    {QA_UNIFIED_FIELD_U32, sizeof(qa_actor_owner), NULL, 0, 0, NULL},
};
static const qa_unified_record_layout reference_source_layout = {
    2 * sizeof(uint32_t), reference_source_fields, 2, SIZE_MAX, QA_UNIFIED_KEY_NONE};
static const qa_unified_record_layout *const reference_variants[] = {
    &reference_none_layout, &qa_unified_actor_layout, &reference_source_layout};
static const qa_unified_field reference_fields[] = {
    QA_UNIFIED_FIELD(qa_actor_reference, kind, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_VARIANT(qa_actor_reference, value, kind, reference_variants),
};
static const qa_unified_record_layout reference_layout = QA_UNIFIED_LAYOUT(qa_actor_reference, reference_fields);

static const qa_unified_field qa_body_state_fields[] = {
    QA_UNIFIED_RECORD(qa_body_state, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_body_state, angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_body_state, velocity, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_body_state, bounds, qa_unified_bounds_layout),
    QA_UNIFIED_RECORD(qa_body_state, ground, reference_layout),
};
const qa_unified_record_layout qa_unified_body_layout = QA_UNIFIED_LAYOUT(qa_body_state, qa_body_state_fields);

static const qa_unified_field qa_movement_ground_fields[] = {
    QA_UNIFIED_FIELD(qa_movement_ground, hit, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_movement_ground, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_movement_ground, model, QA_UNIFIED_FIELD_U32),
};
static const qa_unified_record_layout qa_movement_ground_layout = QA_UNIFIED_LAYOUT(qa_movement_ground, qa_movement_ground_fields);

static const qa_unified_field qa_movement_environment_fields[] = {
    QA_UNIFIED_FIELD(qa_movement_environment, health, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_movement_environment, flight, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_movement_environment, haste, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_movement_environment, invulnerable, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_movement_environment, gravity_multiplier, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_movement_environment, speed_multiplier, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_movement_environment, fixed_pose, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_movement_environment, fixed_crouched, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_movement_environment, pose, qa_movement_posture_layout),
    QA_UNIFIED_FIELD(qa_movement_environment, has_body_bounds, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_movement_environment, body_bounds, qa_unified_bounds_layout),
    QA_UNIFIED_FIELD(qa_movement_environment, has_mode, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_movement_environment, mode, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_movement_environment, has_stance, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_movement_environment, crouched, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_movement_environment_layout = QA_UNIFIED_LAYOUT(qa_movement_environment, qa_movement_environment_fields);

static const qa_unified_field qa_movement_posture_fields[] = {
    QA_UNIFIED_RECORD(qa_movement_posture, bounds, qa_unified_bounds_layout),
    QA_UNIFIED_FIELD(qa_movement_posture, view_height, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_movement_posture_layout = QA_UNIFIED_LAYOUT(qa_movement_posture, qa_movement_posture_fields);

static const qa_unified_field qa_unified_animation_state_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_animation_state, family, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_animation_state, frame, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_animation_state, next_frame_seconds, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_animation_state, end_frame, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_animation_state, priority, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_animation_state, legs, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_animation_state, torso, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_animation_state, legs_timer_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_animation_state, torso_timer_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_animation_state, duck, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_animation_state, run, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_animation_state_layout = QA_UNIFIED_LAYOUT(qa_unified_animation_state, qa_unified_animation_state_fields);

static const qa_unified_field qa_unified_weapon_state_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_weapon_state, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, frame, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, attack_finished_seconds, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, source_weapon, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, gun_frame, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, state, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, machinegun_shots, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, time_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, pending_weapon, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, grenade_milliseconds, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, grenade_blew_up, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, grenade_seconds, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_weapon_state, grenade_time_ms, QA_UNIFIED_FIELD_I64),
};
static const qa_unified_record_layout qa_unified_weapon_state_layout = QA_UNIFIED_LAYOUT(qa_unified_weapon_state, qa_unified_weapon_state_fields);

static const qa_unified_field qa_unified_movement_numeric_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_movement_numeric, id, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_movement_numeric, radix, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_movement_numeric, scalar_mantissa_bits, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_movement_numeric, double_mantissa_bits, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_movement_numeric, evaluation_method, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_movement_numeric, rounding, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_movement_numeric, native_c, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_movement_numeric, qw_origin_binary64, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_movement_numeric_layout = QA_UNIFIED_LAYOUT(qa_unified_movement_numeric, qa_unified_movement_numeric_fields);

static const qa_unified_field qa_clock_config_fields[] = {
    QA_UNIFIED_FIELD(qa_clock_config, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_clock_config, initial_time_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_clock_config, interval_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_clock_config, minimum_frame_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_clock_config, maximum_frame_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_clock_config, initial_lead_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_clock_config, maximum_steps, QA_UNIFIED_FIELD_U32),
};
static const qa_unified_record_layout qa_clock_config_layout = QA_UNIFIED_LAYOUT(qa_clock_config, qa_clock_config_fields);

static const qa_unified_field qa_unified_world_frame_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_world_frame, source, qa_source_frame_layout),
    QA_UNIFIED_FIELD(qa_unified_world_frame, presentation_seconds, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_ARRAY(qa_unified_world_frame, collisions, collision_count, qa_spatial_actor_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_world_frame, actors, actor_count, qa_unified_actor_state_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_world_frame, bodies, body_count, qa_unified_body_state_layout, 65536),
    QA_UNIFIED_POINTER(qa_unified_world_frame, world, qa_unified_resource_state_layout),
    QA_UNIFIED_FIELD(qa_unified_world_frame, area_bits, QA_UNIFIED_FIELD_BYTES),
};
static const qa_unified_record_layout qa_unified_world_frame_layout = QA_UNIFIED_LAYOUT(qa_unified_world_frame, qa_unified_world_frame_fields);



static const qa_unified_field qa_unified_resource_state_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_resource_state, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_resource_state, path, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_resource_state, byte_length, QA_UNIFIED_FIELD_U64),
};
static const qa_unified_record_layout qa_unified_resource_state_layout = QA_UNIFIED_LAYOUT(qa_unified_resource_state, qa_unified_resource_state_fields);

static const qa_unified_field qa_unified_configuration_state_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_configuration_state, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_configuration_state, movement, qa_unified_provider_layout),
    QA_UNIFIED_RECORD(qa_unified_configuration_state, character, qa_unified_provider_layout),
    QA_UNIFIED_RECORD(qa_unified_configuration_state, appearance, qa_unified_provider_layout),
    QA_UNIFIED_RECORD(qa_unified_configuration_state, inventory, qa_unified_provider_layout),
    QA_UNIFIED_ARRAY(qa_unified_configuration_state, weapons, weapon_count, qa_unified_provider_layout, 256),
};
static const qa_unified_record_layout qa_unified_configuration_state_layout = QA_UNIFIED_ACTOR_LAYOUT(qa_unified_configuration_state, qa_unified_configuration_state_fields, actor);

static const qa_unified_field qa_unified_body_state_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_body_state, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_body_state, body, qa_unified_body_layout),
};
static const qa_unified_record_layout qa_unified_body_state_layout = QA_UNIFIED_ACTOR_LAYOUT(qa_unified_body_state, qa_unified_body_state_fields, actor);

static const qa_unified_field qa_unified_actor_state_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_actor_state, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_actor_state, owner, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_actor_state, definition, QA_UNIFIED_FIELD_STRING),
};
static const qa_unified_record_layout qa_unified_actor_state_layout = QA_UNIFIED_ACTOR_LAYOUT(qa_unified_actor_state, qa_unified_actor_state_fields, actor);

static const qa_unified_field qa_source_frame_fields[] = {
    QA_UNIFIED_FIELD(qa_source_frame, provider, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_source_frame, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_source_frame, phase, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_source_frame, number, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_source_frame, start_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_source_frame, elapsed_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_source_frame, time_ns, QA_UNIFIED_FIELD_U64),
};
static const qa_unified_record_layout qa_source_frame_layout = QA_UNIFIED_LAYOUT(qa_source_frame, qa_source_frame_fields);

static const qa_unified_field qa_unified_frame_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_frame, epoch, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_frame, acknowledged_input, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_POINTER(qa_unified_frame, world, qa_unified_world_frame_layout),
    QA_UNIFIED_ARRAY(qa_unified_frame, inventories, inventory_count, qa_unified_inventory_state_layout, 65536),
    QA_UNIFIED_POINTER(qa_unified_frame, prediction, qa_unified_prediction_layout),
    QA_UNIFIED_POINTER(qa_unified_frame, player, qa_unified_player_frame_layout),
    QA_UNIFIED_POINTER(qa_unified_frame, visuals, qa_unified_visual_frame_layout),
    QA_UNIFIED_POINTER(qa_unified_frame, q3, qa_unified_q3_frame_layout),
    QA_UNIFIED_POINTER(qa_unified_frame, components, qa_unified_component_frame_layout),
};
const qa_unified_record_layout qa_unified_frame_layout = QA_UNIFIED_LAYOUT(qa_unified_frame, qa_unified_frame_fields);

static const qa_unified_field qa_unified_inventory_state_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_inventory_state, actor, qa_unified_actor_layout),
    QA_UNIFIED_ARRAY(qa_unified_inventory_state, entries, entry_count, qa_unified_inventory_entry_layout, 65536),
};
static const qa_unified_record_layout qa_unified_inventory_state_layout = QA_UNIFIED_ACTOR_LAYOUT(qa_unified_inventory_state, qa_unified_inventory_state_fields, actor);

static const qa_unified_field qa_nq_movement_state_fields[] = {
    QA_UNIFIED_RECORD(qa_nq_movement_state, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_nq_movement_state, velocity, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_nq_movement_state, angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_nq_movement_state, old_origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_nq_movement_state, angular_velocity, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_nq_movement_state, view_angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_nq_movement_state, punch_angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_nq_movement_state, water_jump_direction, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_nq_movement_state, move_type, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_nq_movement_state, health, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_nq_movement_state, flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_RECORD(qa_nq_movement_state, ground, qa_movement_ground_layout),
    QA_UNIFIED_FIELD(qa_nq_movement_state, water_level, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_nq_movement_state, water_type, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_nq_movement_state, teleport_time_seconds, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_nq_movement_state, ideal_pitch, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_nq_movement_state, fix_angle, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_nq_movement_state_layout = QA_UNIFIED_LAYOUT(qa_nq_movement_state, qa_nq_movement_state_fields);

static const qa_unified_field qa_qw_movement_state_fields[] = {
    QA_UNIFIED_RECORD(qa_qw_movement_state, origin, qa_qw_origin_layout),
    QA_UNIFIED_RECORD(qa_qw_movement_state, velocity, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_qw_movement_state, angles, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_qw_movement_state, old_buttons, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_qw_movement_state, water_jump_time_seconds, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_qw_movement_state, dead, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_qw_movement_state, spectator, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_qw_movement_state, ground, qa_movement_ground_layout),
};
static const qa_unified_record_layout qa_qw_movement_state_layout = QA_UNIFIED_LAYOUT(qa_qw_movement_state, qa_qw_movement_state_fields);

static const qa_unified_field qa_q2r_movement_state_fields[] = {
    QA_UNIFIED_FIELD(qa_q2r_movement_state, type, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_q2r_movement_state, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_q2r_movement_state, velocity, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_q2r_movement_state, flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_q2r_movement_state, time_ms, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_q2r_movement_state, gravity, QA_UNIFIED_FIELD_I16),
    QA_UNIFIED_RECORD(qa_q2r_movement_state, delta_angles, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_q2r_movement_state, view_height, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_q2r_movement_state_layout = QA_UNIFIED_LAYOUT(qa_q2r_movement_state, qa_q2r_movement_state_fields);

static const qa_unified_field qa_q3_movement_state_fields[] = {
    QA_UNIFIED_FIELD(qa_q3_movement_state, command_time_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_movement_state, movement_type, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_movement_state, bob_cycle, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_movement_state, movement_flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_q3_movement_state, movement_time_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_q3_movement_state, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_q3_movement_state, velocity, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_q3_movement_state, gravity, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_movement_state, speed, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIXED(qa_q3_movement_state, delta_angle_words, int32_t_layout, 3),
    QA_UNIFIED_FIELD(qa_q3_movement_state, movement_direction, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_q3_movement_state, grapple_point, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_q3_movement_state, flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_RECORD(qa_q3_movement_state, view_angles, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_q3_movement_state, view_height, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_RECORD(qa_q3_movement_state, ground, qa_movement_ground_layout),
    QA_UNIFIED_FIELD(qa_q3_movement_state, event_sequence, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_RECORD(qa_q3_movement_state, jump_pad, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_q3_movement_state, movement_frame, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q3_movement_state, jump_pad_frame, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout qa_q3_movement_state_layout = QA_UNIFIED_LAYOUT(qa_q3_movement_state, qa_q3_movement_state_fields);

static const qa_unified_field qa_q1_movement_parameters_fields[] = {
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, gravity, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, stop_speed, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, max_speed, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, spectator_max_speed, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, accelerate, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, air_accelerate, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, water_accelerate, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, friction, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, water_friction, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q1_movement_parameters, entity_gravity, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_q1_movement_parameters_layout = QA_UNIFIED_LAYOUT(qa_q1_movement_parameters, qa_q1_movement_parameters_fields);

static const qa_unified_field qa_qw_origin_fields[] = {
    QA_UNIFIED_FIELD(qa_qw_origin, x, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_qw_origin, y, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_qw_origin, z, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_qw_origin_layout = QA_UNIFIED_LAYOUT(qa_qw_origin, qa_qw_origin_fields);


#define MEMBER_FIELD(type, arm, member, kind) \
    {kind, offsetof(type, arm.member) - offsetof(type, arm), NULL, 0, 0, NULL}
#define MEMBER_RECORD(type, arm, member, layout) \
    {QA_UNIFIED_FIELD_RECORD, offsetof(type, arm.member) - offsetof(type, arm), &(layout), 0, 0, NULL}
#define MEMBER_FIXED(type, arm, member, layout, count) \
    {QA_UNIFIED_FIELD_FIXED, offsetof(type, arm.member) - offsetof(type, arm), &(layout), 0, count, NULL}
#define MEMBER_LAYOUT(type, arm, fields) \
    {sizeof(((type *)0)->arm), fields, sizeof(fields) / sizeof((fields)[0]), SIZE_MAX, QA_UNIFIED_KEY_NONE}

static const qa_unified_field q2_narrow_fields[] = {
    {QA_UNIFIED_FIELD_FIXED, 0, &int16_t_layout, 0, 3, NULL},
    {QA_UNIFIED_FIELD_FIXED, offsetof(qa_q2_movement_state, velocity_eighths) - offsetof(qa_q2_movement_state, origin_eighths), &int16_t_layout, 0, 3, NULL},
};
static const qa_unified_record_layout q2_narrow_layout = {
    offsetof(qa_q2_movement_state, flags) - offsetof(qa_q2_movement_state, origin_eighths),
    q2_narrow_fields, 2, SIZE_MAX, QA_UNIFIED_KEY_NONE};
static const qa_unified_field q2_wide_fields[] = {
    MEMBER_FIXED(qa_q2_movement_state, wide, origin_eighths, int32_t_layout, 3),
    MEMBER_FIXED(qa_q2_movement_state, wide, velocity_eighths, int32_t_layout, 3),
    MEMBER_FIELD(qa_q2_movement_state, wide, time_ms, QA_UNIFIED_FIELD_U16),
};
static const qa_unified_record_layout q2_wide_layout = MEMBER_LAYOUT(qa_q2_movement_state, wide, q2_wide_fields);
static const qa_unified_record_layout *const q2_coordinate_layouts[] = {&q2_narrow_layout, &q2_wide_layout};
static const qa_unified_field q2_state_fields[] = {
    QA_UNIFIED_FIELD(qa_q2_movement_state, type, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_q2_movement_state, wide_coordinates, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_VARIANT_BOOL(qa_q2_movement_state, origin_eighths, wide_coordinates, q2_coordinate_layouts),
    QA_UNIFIED_FIELD(qa_q2_movement_state, flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_q2_movement_state, time_eight_ms, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_FIELD(qa_q2_movement_state, gravity, QA_UNIFIED_FIELD_I16),
    QA_UNIFIED_FIXED(qa_q2_movement_state, delta_angle_shorts, int16_t_layout, 3),
};
static const qa_unified_record_layout qa_q2_movement_state_layout = QA_UNIFIED_LAYOUT(qa_q2_movement_state, q2_state_fields);
static const qa_unified_record_layout *const movement_state_layouts[] = {
    &qa_nq_movement_state_layout, &qa_qw_movement_state_layout, &qa_q2_movement_state_layout,
    &qa_q2r_movement_state_layout, &qa_q3_movement_state_layout};
static const qa_unified_field movement_state_fields[] = {
    QA_UNIFIED_FIELD(qa_movement_state, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_VARIANT(qa_movement_state, data, kind, movement_state_layouts),
};
static const qa_unified_record_layout qa_movement_state_layout = QA_UNIFIED_LAYOUT(qa_movement_state, movement_state_fields);
static const qa_unified_field profile_nq_fields[] = {
    MEMBER_RECORD(qa_movement_profile, data.nq, parameters, qa_q1_movement_parameters_layout),
    MEMBER_FIELD(qa_movement_profile, data.nq, edition, QA_UNIFIED_FIELD_I32),
    MEMBER_FIELD(qa_movement_profile, data.nq, edge_friction, QA_UNIFIED_FIELD_F32),
    MEMBER_FIELD(qa_movement_profile, data.nq, max_velocity, QA_UNIFIED_FIELD_F32),
    MEMBER_FIELD(qa_movement_profile, data.nq, ideal_pitch_scale, QA_UNIFIED_FIELD_F32),
    MEMBER_FIELD(qa_movement_profile, data.nq, roll_speed, QA_UNIFIED_FIELD_F32),
    MEMBER_FIELD(qa_movement_profile, data.nq, roll_angle, QA_UNIFIED_FIELD_F32),
    MEMBER_FIELD(qa_movement_profile, data.nq, no_clip_angle_hack, QA_UNIFIED_FIELD_BOOL),
    MEMBER_FIELD(qa_movement_profile, data.nq, no_step, QA_UNIFIED_FIELD_BOOL),
    MEMBER_FIELD(qa_movement_profile, data.nq, source_jump_authority, QA_UNIFIED_FIELD_BOOL),
    MEMBER_FIELD(qa_movement_profile, data.nq, preserve_fixangle_roll, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout profile_nq_layout = MEMBER_LAYOUT(qa_movement_profile, data.nq, profile_nq_fields);
static const qa_unified_field profile_qw_fields[] = {
    MEMBER_RECORD(qa_movement_profile, data.qw, parameters, qa_q1_movement_parameters_layout),
    MEMBER_FIELD(qa_movement_profile, data.qw, maximum_command_ms, QA_UNIFIED_FIELD_U32),
    MEMBER_FIELD(qa_movement_profile, data.qw, shared_controls, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout profile_qw_layout = MEMBER_LAYOUT(qa_movement_profile, data.qw, profile_qw_fields);
static const qa_unified_field profile_q2_fields[] = {
    MEMBER_FIELD(qa_movement_profile, data.q2, air_accelerate, QA_UNIFIED_FIELD_F32),
    MEMBER_FIELD(qa_movement_profile, data.q2, snap_initial, QA_UNIFIED_FIELD_BOOL),
    MEMBER_FIELD(qa_movement_profile, data.q2, strafejump_hack, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout profile_q2_layout = MEMBER_LAYOUT(qa_movement_profile, data.q2, profile_q2_fields);
static const qa_unified_field profile_q2r_fields[] = {
    MEMBER_FIELD(qa_movement_profile, data.q2r, air_accelerate, QA_UNIFIED_FIELD_F32),
    MEMBER_FIELD(qa_movement_profile, data.q2r, n64_physics, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout profile_q2r_layout = MEMBER_LAYOUT(qa_movement_profile, data.q2r, profile_q2r_fields);
static const qa_unified_field profile_q3_fields[] = {
    MEMBER_FIELD(qa_movement_profile, data.q3, missionpack, QA_UNIFIED_FIELD_BOOL),
    MEMBER_FIELD(qa_movement_profile, data.q3, no_footsteps, QA_UNIFIED_FIELD_BOOL),
    MEMBER_FIELD(qa_movement_profile, data.q3, fixed_ms, QA_UNIFIED_FIELD_U32),
};
static const qa_unified_record_layout profile_q3_layout = MEMBER_LAYOUT(qa_movement_profile, data.q3, profile_q3_fields);
static const qa_unified_record_layout *const movement_profile_layouts[] = {
    &profile_nq_layout, &profile_qw_layout, &profile_q2_layout, &profile_q2r_layout, &profile_q3_layout};
static const qa_unified_field movement_profile_fields[] = {
    QA_UNIFIED_FIELD(qa_movement_profile, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_VARIANT(qa_movement_profile, data, kind, movement_profile_layouts),
};
static const qa_unified_record_layout qa_movement_profile_layout = QA_UNIFIED_LAYOUT(qa_movement_profile, movement_profile_fields);

qa_unified_world_frame *qa_unified_world_frame_create(qa_unified_frame_pool *pool, qa_error *error)
{
    qa_unified_frame_lease *lease = pool ? qa_unified_frame_lease_acquire(pool, error) : NULL;
    if (pool && !lease) return NULL;
    qa_unified_world_frame *world = lease ? qa_unified_frame_lease_alloc(lease, 1, sizeof(*world), _Alignof(qa_unified_world_frame), error) :
        calloc(1, sizeof(*world));
    if (!world) {
        qa_unified_frame_lease_release(lease);
        if (!lease) qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating actual Unified world frame");
        return NULL;
    }
    world->lease = lease; world->references = 1; return world;
}
bool qa_unified_world_frame_retain(qa_unified_world_frame *world, qa_error *error)
{
    if (!world || !world->references || world->references == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unified world frame lost its immutable owner"); return false;
    }
    ++world->references; return true;
}
void qa_unified_world_frame_destroy(qa_unified_world_frame *world)
{
    if (!world || (world->references && --world->references)) return;
    if (world->lease) qa_unified_frame_lease_release(world->lease);
    else { qa_unified_record_dispose(&qa_unified_world_frame_layout, world); free(world); }
}
qa_unified_frame *qa_unified_frame_create(qa_unified_frame_pool *pool, qa_error *error)
{
    qa_unified_frame_lease *lease = pool ? qa_unified_frame_lease_acquire(pool, error) : NULL;
    if (pool && !lease) return NULL;
    qa_unified_frame *frame = lease ? qa_unified_frame_lease_alloc(lease, 1, sizeof(*frame), _Alignof(qa_unified_frame), error) : calloc(1, sizeof(*frame));
    if (!frame) {
        qa_unified_frame_lease_release(lease);
        if (!lease) qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating actual Unified frame");
        return NULL;
    }
    frame->lease = lease; return frame;
}
void qa_unified_frame_destroy(qa_unified_frame *frame)
{
    if (!frame) return;
    qa_unified_world_frame *world = frame->world; frame->world = NULL;
    qa_unified_frame_lease *lease = frame->lease;
    if (!lease) qa_unified_record_dispose(&qa_unified_frame_layout, frame);
    qa_unified_world_frame_destroy(world);
    if (lease) qa_unified_frame_lease_release(lease); else free(frame);
}
bool qa_unified_frame_equal(const qa_unified_frame *a, const qa_unified_frame *b)
{ return qa_unified_record_equal(&qa_unified_frame_layout, a, b); }

static const qa_unified_record_layout qa_unified_presentation_owner_layout;
const qa_unified_record_layout qa_unified_presentation_event_layout;
const qa_unified_record_layout qa_unified_simulation_event_layout;
const qa_unified_record_layout qa_unified_events_layout;
static const qa_unified_record_layout qa_unified_message_arg_layout;
static const qa_unified_record_layout qa_unified_prompt_choice_layout;
static const qa_unified_record_layout qa_unified_builtin_event_layout;
static const qa_unified_record_layout qa_q2_blend_layout;
static const qa_unified_record_layout qa_unified_q2_player_view_layout;
static const qa_unified_record_layout qa_unified_q2_score_row_layout;
static const qa_unified_record_layout qa_unified_q2_player_event_layout;
static const qa_unified_record_layout qa_q2_fog_layout;
static const qa_unified_record_layout qa_unified_q2_campaign_level_layout;
static const qa_unified_record_layout qa_unified_q2_map_event_layout;
static const qa_unified_record_layout qa_unified_q2_temp_field_layout;
static const qa_unified_record_layout qa_unified_q2_temporary_layout;
static const qa_unified_record_layout qa_unified_q2_muzzle_layout;
static const qa_unified_record_layout qa_unified_q2_poi_layout;
static const qa_unified_record_layout qa_unified_q2_protocol_event_layout;
static const qa_unified_record_layout qa_unified_mod_identity_layout;
static const qa_unified_record_layout qa_unified_q3_event_layout;
static const qa_unified_record_layout qa_unified_q3_character_event_layout;
static const qa_unified_record_layout qa_unified_q3_ballistic_event_layout;
static const qa_unified_record_layout qa_unified_owner_event_layout;
const qa_unified_record_layout qa_unified_presentation_payload_layout;
static const qa_unified_record_layout qa_unified_sound_event_layout;
static const qa_unified_record_layout qa_unified_message_event_layout;
static const qa_unified_record_layout qa_unified_damage_attack_layout;
static const qa_unified_record_layout qa_unified_damage_request_layout;
static const qa_unified_record_layout qa_damage_result_layout;
static const qa_unified_record_layout qa_damage_inflictor_center_layout;
static const qa_unified_record_layout qa_unified_damage_mutation_layout;
static const qa_unified_record_layout qa_unified_damage_event_layout;
const qa_unified_record_layout qa_unified_simulation_payload_layout;
static const qa_unified_record_layout qa_damage_cause_layout;
static const qa_unified_field qa_unified_presentation_owner_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_presentation_owner, provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_presentation_owner, generation, QA_UNIFIED_FIELD_U64),
};
static const qa_unified_record_layout qa_unified_presentation_owner_layout = QA_UNIFIED_LAYOUT(qa_unified_presentation_owner, qa_unified_presentation_owner_fields);

static const qa_unified_field qa_unified_presentation_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_presentation_event, sequence, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_presentation_event, seconds, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_presentation_event, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_presentation_event, provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_presentation_event, family, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_presentation_event, owner, qa_unified_presentation_owner_layout),
    QA_UNIFIED_RECORD(qa_unified_presentation_event, recipient, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_presentation_event, q2_profile, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_FIELD(qa_unified_presentation_event, q2_interval_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_presentation_event, source_entity, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_presentation_event, has_source_entity, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_presentation_event, payload, qa_unified_presentation_payload_layout),
};
const qa_unified_record_layout qa_unified_presentation_event_layout = QA_UNIFIED_LAYOUT(qa_unified_presentation_event, qa_unified_presentation_event_fields);

static const qa_unified_field qa_unified_simulation_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_simulation_event, sequence, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_simulation_event, time, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_simulation_event, milliseconds, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_simulation_event, private_audience, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_simulation_event, client_slot, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_simulation_event, client_generation, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_RECORD(qa_unified_simulation_event, payload, qa_unified_simulation_payload_layout),
};
const qa_unified_record_layout qa_unified_simulation_event_layout = QA_UNIFIED_LAYOUT(qa_unified_simulation_event, qa_unified_simulation_event_fields);

static const qa_unified_field qa_unified_frame_events_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_frame_events, epoch, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_frame_events, frame, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_ARRAY(qa_unified_frame_events, presentation, presentation_count, qa_unified_presentation_event_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_frame_events, simulation, simulation_count, qa_unified_simulation_event_layout, 65536),
};
const qa_unified_record_layout qa_unified_events_layout = QA_UNIFIED_LAYOUT(qa_unified_frame_events, qa_unified_frame_events_fields);

static const qa_unified_field qa_unified_message_arg_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_message_arg, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_message_arg, text, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_message_arg, number, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_unified_message_arg_layout = QA_UNIFIED_LAYOUT(qa_unified_message_arg, qa_unified_message_arg_fields);

static const qa_unified_field qa_unified_prompt_choice_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_prompt_choice, label, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_prompt_choice, impulse, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout qa_unified_prompt_choice_layout = QA_UNIFIED_LAYOUT(qa_unified_prompt_choice, qa_unified_prompt_choice_fields);

static const qa_unified_field qa_unified_builtin_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_builtin_event, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, family, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_builtin_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_builtin_event, other, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, resource, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, text, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_builtin_event, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_builtin_event, end, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_builtin_event, direction, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_builtin_event, muzzle_angles, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, volume, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, attenuation, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, muzzle_scale, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, value, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, code, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, channel, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, count, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, frame, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, has_muzzle_pose, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_ARRAY(qa_unified_builtin_event, arguments, argument_count, qa_unified_message_arg_layout, 65536),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, ctf_red, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, ctf_blue, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, ctf_flags, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, ctf_rune_items, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, ctf_capture_total, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, ctf_capture_blue, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, q1_power, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_builtin_event, q1_power_expires, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_ARRAY(qa_unified_builtin_event, prompt_choices, prompt_choice_count, qa_unified_prompt_choice_layout, 65536),
};
static const qa_unified_record_layout qa_unified_builtin_event_layout = QA_UNIFIED_LAYOUT(qa_unified_builtin_event, qa_unified_builtin_event_fields);

static const qa_unified_field qa_q2_blend_fields[] = {
    QA_UNIFIED_FIELD(qa_q2_blend, x, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q2_blend, y, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q2_blend, z, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q2_blend, w, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_q2_blend_layout = QA_UNIFIED_LAYOUT(qa_q2_blend, qa_q2_blend_fields);

static const qa_unified_field qa_unified_q2_player_view_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_q2_player_view, angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_player_view, offset, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_player_view, kick_angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_player_view, gun_angles, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_player_view, gun_offset, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_player_view, blend, qa_q2_blend_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, fov, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, health, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, ammo, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, armor, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, ammo_icon, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, armor_icon, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, selected_item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, timer_item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, ammo_count, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, score, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, flashes, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, layouts, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, hit_marker_damage, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, timer_seconds, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, underwater, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_player_view, spectator, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_q2_player_view_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_player_view, qa_unified_q2_player_view_fields);

static const qa_unified_field qa_unified_q2_score_row_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q2_score_row, slot, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_score_row, name, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_score_row, score, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_score_row, ping, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_score_row, minutes, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_score_row, spectator, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_q2_score_row_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_score_row, qa_unified_q2_score_row_fields);

static const qa_unified_field qa_unified_q2_player_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q2_player_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_player_event, target, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, text, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, skin, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, selected_item, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_q2_player_event, view, qa_unified_q2_player_view_layout),
    QA_UNIFIED_ARRAY(qa_unified_q2_player_event, scores, score_count, qa_unified_q2_score_row_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_q2_player_event, inventory, inventory_count, qa_unified_inventory_entry_layout, 65536),
    QA_UNIFIED_RECORD(qa_unified_q2_player_event, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_player_event, direction, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, time_ns, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, slot, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, level, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, lives, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, damage, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, alpha, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, respawn_status, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, hand, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, visible, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, reliable, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, health, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, armor, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, shield, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_player_event, first, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_q2_player_event_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_player_event, qa_unified_q2_player_event_fields);

static const qa_unified_field qa_q2_fog_fields[] = {
    QA_UNIFIED_FIELD(qa_q2_fog, density, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q2_fog, sky_factor, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_RECORD(qa_q2_fog, color, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_q2_fog, start_color, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_q2_fog, end_color, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_q2_fog, start_distance, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q2_fog, end_distance, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q2_fog, falloff, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_q2_fog, height_density, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_q2_fog_layout = QA_UNIFIED_LAYOUT(qa_q2_fog, qa_q2_fog_fields);

static const qa_unified_field qa_unified_q2_campaign_level_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q2_campaign_level, map, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_campaign_level, name, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_campaign_level, visit_order, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_campaign_level, total_secrets, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_campaign_level, found_secrets, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_campaign_level, total_monsters, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_campaign_level, killed_monsters, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_campaign_level, time_seconds, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout qa_unified_q2_campaign_level_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_campaign_level, qa_unified_q2_campaign_level_fields);

static const qa_unified_field qa_unified_q2_map_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q2_map_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_map_event, recipient, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_map_event, target, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, text, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, resource, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_q2_map_event, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_map_event, direction, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_map_event, color, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_map_event, fog, qa_q2_fog_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, value, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, duration, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, radius, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, alpha, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, intensity, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, fade_start, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, fade_end, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, cone_cosine, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, count, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, style, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, slot, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, resolution, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, visible, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_ARRAY(qa_unified_q2_map_event, arguments, argument_count, qa_unified_message_arg_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_q2_map_event, levels, level_count, qa_unified_q2_campaign_level_layout, 65536),
    QA_UNIFIED_FIELD(qa_unified_q2_map_event, button_time_ns, QA_UNIFIED_FIELD_U64),
};
static const qa_unified_record_layout qa_unified_q2_map_event_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_map_event, qa_unified_q2_map_event_fields);

static const qa_unified_field qa_unified_q2_temp_field_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q2_temp_field, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_temp_field, name, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_temp_field, integer, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q2_temp_field, vector, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_temp_field, actor, qa_unified_actor_layout),
};
static const qa_unified_record_layout qa_unified_q2_temp_field_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_temp_field, qa_unified_q2_temp_field_fields);

static const qa_unified_field qa_unified_q2_temporary_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q2_temporary, type, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_FIELD(qa_unified_q2_temporary, rerelease, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_ARRAY(qa_unified_q2_temporary, fields, field_count, qa_unified_q2_temp_field_layout, 65536),
};
static const qa_unified_record_layout qa_unified_q2_temporary_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_temporary, qa_unified_q2_temporary_fields);

static const qa_unified_field qa_unified_q2_muzzle_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_q2_muzzle, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_muzzle, entity, QA_UNIFIED_FIELD_U16),
    QA_UNIFIED_FIELD(qa_unified_q2_muzzle, flash, QA_UNIFIED_FIELD_U16),
    QA_UNIFIED_FIELD(qa_unified_q2_muzzle, monster, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_muzzle, silenced, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_muzzle, has_pose, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_q2_muzzle, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_muzzle, direction, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_muzzle, angles, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_muzzle, scale, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_unified_q2_muzzle_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_muzzle, qa_unified_q2_muzzle_fields);

static const qa_unified_field qa_unified_q2_poi_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_q2_poi, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_poi, key, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_poi, image, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q2_poi, position, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_poi, duration, QA_UNIFIED_FIELD_U16),
    QA_UNIFIED_FIELD(qa_unified_q2_poi, color, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_FIELD(qa_unified_q2_poi, flags, QA_UNIFIED_FIELD_U8),
    QA_UNIFIED_FIELD(qa_unified_q2_poi, remove, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_q2_poi_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_poi, qa_unified_q2_poi_fields);

static const qa_unified_field qa_unified_q2_protocol_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q2_protocol_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, text, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, resource, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, level, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, index, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, instant, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, reliable, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_ARRAY(qa_unified_q2_protocol_event, arguments, argument_count, qa_unified_message_arg_layout, 65536),
    QA_UNIFIED_RECORD(qa_unified_q2_protocol_event, temporary, qa_unified_q2_temporary_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_protocol_event, fog, qa_q2_fog_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, transition_ms, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_RECORD(qa_unified_q2_protocol_event, muzzle, qa_unified_q2_muzzle_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_protocol_event, poi, qa_unified_q2_poi_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_protocol_event, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q2_protocol_event, direction, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, volume, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, attenuation, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, damage, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, channel, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, health, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, armor, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, shield, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q2_protocol_event, first, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_q2_protocol_event_layout = QA_UNIFIED_LAYOUT(qa_unified_q2_protocol_event, qa_unified_q2_protocol_event_fields);

static const qa_unified_field qa_unified_mod_identity_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_mod_identity, id, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_mod_identity, artifact_path, QA_UNIFIED_FIELD_STRING),
};
static const qa_unified_record_layout qa_unified_mod_identity_layout = QA_UNIFIED_LAYOUT(qa_unified_mod_identity, qa_unified_mod_identity_fields);

static const qa_unified_field qa_unified_q3_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q3_event, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q3_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q3_event, text, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q3_event, resource, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q3_event, client, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, time_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, event, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, parameter, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, source_sequence, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, index, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, execute_now, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q3_event, external, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_q3_event, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_event, velocity, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_event, entity, qa_q3_entity_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_event, player, qa_q3_player_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_event, module, qa_unified_mod_identity_layout),
    QA_UNIFIED_FIELD(qa_unified_q3_event, abi, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, sound, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, channel, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, volume, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q3_event, loop, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_q3_event_layout = QA_UNIFIED_LAYOUT(qa_unified_q3_event, qa_unified_q3_event_fields);

static const qa_unified_field qa_unified_q3_character_event_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_q3_character_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q3_character_event, event, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_character_event, parameter, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_character_event, time_ms, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout qa_unified_q3_character_event_layout = QA_UNIFIED_LAYOUT(qa_unified_q3_character_event, qa_unified_q3_character_event_fields);

static const qa_unified_field qa_unified_q3_ballistic_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, target, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, weapon, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, time_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, surface, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, contact, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, count, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, until_ms, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, origin, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, end, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, normal, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, point, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, start, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, direction, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_q3_ballistic_event, trajectory, qa_q3_trajectory_layout),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, seed, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, volume, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, flesh, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_q3_ballistic_event, rail_surface, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_q3_ballistic_event_layout = QA_UNIFIED_LAYOUT(qa_unified_q3_ballistic_event, qa_unified_q3_ballistic_event_fields);

static const qa_unified_field qa_unified_owner_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_owner_event, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_owner_event, owner, qa_unified_presentation_owner_layout),
    QA_UNIFIED_RECORD(qa_unified_owner_event, recipient, qa_unified_actor_layout),
};
static const qa_unified_record_layout qa_unified_owner_event_layout = QA_UNIFIED_LAYOUT(qa_unified_owner_event, qa_unified_owner_event_fields);

static const qa_unified_field qa_unified_visibility_event_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_visibility_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_visibility_event, visible, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_visibility_event_layout = QA_UNIFIED_LAYOUT(qa_unified_visibility_event, qa_unified_visibility_event_fields);
static const qa_unified_record_layout *const qa_unified_presentation_payload_variants[] = {&qa_unified_builtin_event_layout, &qa_unified_q2_player_event_layout, &qa_unified_q2_map_event_layout, &qa_unified_q2_protocol_event_layout, &qa_unified_q2_temporary_layout, &qa_unified_model_state_layout, &qa_unified_q3_event_layout, &qa_unified_q3_character_event_layout, &qa_unified_q3_ballistic_event_layout, &qa_unified_owner_event_layout, &qa_unified_visibility_event_layout};
static const qa_unified_field qa_unified_presentation_payload_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_presentation_payload, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_VARIANT(qa_unified_presentation_payload, value, kind, qa_unified_presentation_payload_variants),
};
const qa_unified_record_layout qa_unified_presentation_payload_layout = QA_UNIFIED_LAYOUT(qa_unified_presentation_payload, qa_unified_presentation_payload_fields);

static const qa_unified_field qa_unified_sound_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_sound_event, resource, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_sound_event, actor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_sound_event, origin, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_sound_event, channel, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_sound_event, volume, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_sound_event, attenuation, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_unified_sound_event_layout = QA_UNIFIED_LAYOUT(qa_unified_sound_event, qa_unified_sound_event_fields);

static const qa_unified_field qa_unified_message_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_message_event, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_message_event, text, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_message_event, level, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_message_event, index, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_ARRAY(qa_unified_message_event, counts, count, int16_t_layout, 65536),
    QA_UNIFIED_FIELD(qa_unified_message_event, entity, QA_UNIFIED_FIELD_U16),
    QA_UNIFIED_FIELD(qa_unified_message_event, flash, QA_UNIFIED_FIELD_U16),
    QA_UNIFIED_FIELD(qa_unified_message_event, monster, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_message_event_layout = QA_UNIFIED_LAYOUT(qa_unified_message_event, qa_unified_message_event_fields);

static const qa_unified_field qa_unified_damage_attack_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_damage_attack, sequence, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, time, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, milliseconds, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_damage_attack, attacker, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_attack, inflictor, qa_unified_actor_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_attack, projectile, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, weapon, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, weapon_provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, combat_provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, inventory_provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, movement_provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, powerup_owner, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_damage_attack, cause, qa_damage_cause_layout),
    QA_UNIFIED_FIELD(qa_unified_damage_attack, q1_death_type, QA_UNIFIED_FIELD_STRING),
};
static const qa_unified_record_layout qa_unified_damage_attack_layout = QA_UNIFIED_LAYOUT(qa_unified_damage_attack, qa_unified_damage_attack_fields);

static const qa_unified_field qa_unified_damage_request_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_damage_request, attack, qa_unified_damage_attack_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_request, target, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_damage_request, amount, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_damage_request, knockback, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_RECORD(qa_unified_damage_request, direction, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_request, point, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_request, normal, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_damage_request, radius, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_unified_damage_request_layout = QA_UNIFIED_LAYOUT(qa_unified_damage_request, qa_unified_damage_request_fields);

static const qa_unified_field qa_damage_result_fields[] = {
    QA_UNIFIED_FIELD(qa_damage_result, applied_damage, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_damage_result, reaction, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_damage_result, feedback_family, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_damage_result, has_feedback, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_damage_result, battlesuit, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_damage_result, power_saved, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_damage_result, armor_saved, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_damage_result, blood, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_damage_result, knockback, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_damage_result, has_q2_damage, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_damage_result, q2_damage, QA_UNIFIED_FIELD_F32),
};
static const qa_unified_record_layout qa_damage_result_layout = QA_UNIFIED_LAYOUT(qa_damage_result, qa_damage_result_fields);

static const qa_unified_field qa_damage_inflictor_center_fields[] = {
    QA_UNIFIED_RECORD(qa_damage_inflictor_center, inflictor, qa_unified_actor_layout),
    QA_UNIFIED_FIXED(qa_damage_inflictor_center, center, double_layout, 3),
    QA_UNIFIED_FIELD(qa_damage_inflictor_center, present, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout qa_damage_inflictor_center_layout = QA_UNIFIED_LAYOUT(qa_damage_inflictor_center, qa_damage_inflictor_center_fields);

static const qa_unified_field qa_unified_damage_mutation_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_damage_mutation, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_damage_mutation, health_before, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_FIELD(qa_unified_damage_mutation, health_after, QA_UNIFIED_FIELD_F32),
    QA_UNIFIED_RECORD(qa_unified_damage_mutation, armor_before, qa_unified_armor_state_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_mutation, armor_after, qa_unified_armor_state_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_mutation, before, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_mutation, after, qa_unified_vector_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_mutation, impulse, qa_unified_vector_layout),
    QA_UNIFIED_FIELD(qa_unified_damage_mutation, movement_provider, QA_UNIFIED_FIELD_STRING),
};
static const qa_unified_record_layout qa_unified_damage_mutation_layout = QA_UNIFIED_LAYOUT(qa_unified_damage_mutation, qa_unified_damage_mutation_fields);

static const qa_unified_field qa_unified_damage_event_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_damage_event, stale, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_damage_event, survived, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_damage_event, request, qa_unified_damage_request_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_event, result, qa_damage_result_layout),
    QA_UNIFIED_RECORD(qa_unified_damage_event, inflictor_center, qa_damage_inflictor_center_layout),
    QA_UNIFIED_ARRAY(qa_unified_damage_event, mutations, mutation_count, qa_unified_damage_mutation_layout, 65536),
};
static const qa_unified_record_layout qa_unified_damage_event_layout = QA_UNIFIED_LAYOUT(qa_unified_damage_event, qa_unified_damage_event_fields);

static const qa_unified_record_layout *const qa_unified_simulation_payload_variants[] = {&qa_unified_sound_event_layout, &qa_unified_message_event_layout, &qa_unified_damage_event_layout};
static const qa_unified_field qa_unified_simulation_payload_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_simulation_payload, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_simulation_payload, linked_presentation, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_simulation_payload, source_presentation_sequence, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_VARIANT(qa_unified_simulation_payload, value, kind, qa_unified_simulation_payload_variants),
};
const qa_unified_record_layout qa_unified_simulation_payload_layout = QA_UNIFIED_LAYOUT(qa_unified_simulation_payload, qa_unified_simulation_payload_fields);

static const qa_unified_field cause_q1_fields[] = {
    MEMBER_FIELD(qa_damage_cause, source.q1, death_type, QA_UNIFIED_FIELD_U32),
    MEMBER_FIELD(qa_damage_cause, source.q1, armor, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout cause_q1_layout = MEMBER_LAYOUT(qa_damage_cause, source.q1, cause_q1_fields);
static const qa_unified_field cause_q2_fields[] = {
    MEMBER_FIELD(qa_damage_cause, source.q2, means_of_death, QA_UNIFIED_FIELD_I32),
    MEMBER_FIELD(qa_damage_cause, source.q2, flags, QA_UNIFIED_FIELD_U32),
    MEMBER_FIELD(qa_damage_cause, source.q2, native, QA_UNIFIED_FIELD_I32),
    MEMBER_FIELD(qa_damage_cause, source.q2, native_value, QA_UNIFIED_FIELD_I32),
    MEMBER_FIELD(qa_damage_cause, source.q2, classic_product, QA_UNIFIED_FIELD_U32),
    MEMBER_FIELD(qa_damage_cause, source.q2, friendly_fire, QA_UNIFIED_FIELD_BOOL),
    MEMBER_FIELD(qa_damage_cause, source.q2, no_point_loss, QA_UNIFIED_FIELD_BOOL),
};
static const qa_unified_record_layout cause_q2_layout = MEMBER_LAYOUT(qa_damage_cause, source.q2, cause_q2_fields);
static const qa_unified_field cause_q3_fields[] = {
    MEMBER_FIELD(qa_damage_cause, source.q3, means_of_death, QA_UNIFIED_FIELD_I32),
    MEMBER_FIELD(qa_damage_cause, source.q3, flags, QA_UNIFIED_FIELD_U32),
};
static const qa_unified_record_layout cause_q3_layout = MEMBER_LAYOUT(qa_damage_cause, source.q3, cause_q3_fields);
static const qa_unified_record_layout *const cause_variants[] = {&cause_q1_layout, &cause_q2_layout, &cause_q3_layout, &int32_t_layout};
static const qa_unified_field cause_fields[] = {
    QA_UNIFIED_FIELD(qa_damage_cause, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_VARIANT(qa_damage_cause, source, kind, cause_variants),
};
static const qa_unified_record_layout qa_damage_cause_layout = QA_UNIFIED_LAYOUT(qa_damage_cause, cause_fields);
void qa_unified_presentation_payload_dispose(qa_unified_presentation_payload *value)
{ qa_unified_record_dispose(&qa_unified_presentation_payload_layout, value); }
void qa_unified_simulation_payload_dispose(qa_unified_simulation_payload *value)
{ qa_unified_record_dispose(&qa_unified_simulation_payload_layout, value); }
bool qa_unified_presentation_payload_clone(const qa_unified_presentation_payload *from,
    qa_unified_presentation_payload *out, qa_error *error)
{ return qa_unified_record_clone(&qa_unified_presentation_payload_layout, from, out, error); }
bool qa_unified_simulation_payload_clone(const qa_unified_simulation_payload *from,
    qa_unified_simulation_payload *out, qa_error *error)
{ return qa_unified_record_clone(&qa_unified_simulation_payload_layout, from, out, error); }
void qa_unified_presentation_event_dispose(qa_unified_presentation_event *value)
{ qa_unified_record_dispose(&qa_unified_presentation_event_layout, value); }
bool qa_unified_presentation_event_clone(const qa_unified_presentation_event *from,
    qa_unified_presentation_event *out, qa_error *error)
{ return qa_unified_record_clone(&qa_unified_presentation_event_layout, from, out, error); }
bool qa_unified_presentation_event_encode(const qa_unified_presentation_event *value,
    qa_buffer *out, qa_error *error)
{ return qa_unified_record_delta_encode(&qa_unified_presentation_event_layout, value, NULL, 32u * 1024u * 1024u, out, error); }
static bool presentation_check(const qa_unified_presentation_event *, qa_error *);
bool qa_unified_presentation_event_decode(qa_bytes bytes,
    qa_unified_presentation_event *out, qa_error *error)
{
    size_t measured;
    bool okay=qa_unified_record_delta_decode(&qa_unified_presentation_event_layout,bytes,NULL,out,NULL,error) &&
        qa_unified_record_measure(&qa_unified_presentation_event_layout,out,&measured,error) && presentation_check(out,error);
    if (!okay) { qa_unified_presentation_event_dispose(out); memset(out,0,sizeof(*out)); }
    return okay;
}
void qa_unified_frame_events_destroy(qa_unified_frame_events *value)
{ qa_unified_record_dispose(&qa_unified_events_layout, value); free(value); }

static const qa_unified_field unified_vec_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_vec3, x, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_vec3, y, QA_UNIFIED_FIELD_F64),
    QA_UNIFIED_FIELD(qa_unified_vec3, z, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout unified_vec_layout = QA_UNIFIED_LAYOUT(qa_unified_vec3, unified_vec_fields);
static const qa_unified_field input_nq_fields[] = {
    MEMBER_FIELD(qa_unified_movement, data.nq, acknowledged_seconds, QA_UNIFIED_FIELD_F64),
    MEMBER_RECORD(qa_unified_movement, data.nq, angles, unified_vec_layout),
    MEMBER_FIELD(qa_unified_movement, data.nq, forward, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.nq, side, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.nq, up, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.nq, buttons, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.nq, impulse, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout input_nq_layout = MEMBER_LAYOUT(qa_unified_movement, data.nq, input_nq_fields);
static const qa_unified_field input_qw_fields[] = {
    MEMBER_FIELD(qa_unified_movement, data.qw, milliseconds, QA_UNIFIED_FIELD_F64),
    MEMBER_RECORD(qa_unified_movement, data.qw, angles, unified_vec_layout),
    MEMBER_FIELD(qa_unified_movement, data.qw, forward, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.qw, side, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.qw, up, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.qw, buttons, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.qw, impulse, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout input_qw_layout = MEMBER_LAYOUT(qa_unified_movement, data.qw, input_qw_fields);
static const qa_unified_field input_q2_fields[] = {
    MEMBER_FIELD(qa_unified_movement, data.q2, milliseconds, QA_UNIFIED_FIELD_F64),
    MEMBER_FIXED(qa_unified_movement, data.q2, angle_shorts, double_layout, 3),
    MEMBER_FIELD(qa_unified_movement, data.q2, forward, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q2, side, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q2, up, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q2, buttons, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q2, impulse, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q2, light_level, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout input_q2_layout = MEMBER_LAYOUT(qa_unified_movement, data.q2, input_q2_fields);
static const qa_unified_field input_q2r_fields[] = {
    MEMBER_FIELD(qa_unified_movement, data.q2r, milliseconds, QA_UNIFIED_FIELD_F64),
    MEMBER_RECORD(qa_unified_movement, data.q2r, angles, unified_vec_layout),
    MEMBER_FIELD(qa_unified_movement, data.q2r, forward, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q2r, side, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q2r, buttons, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q2r, server_frame, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout input_q2r_layout = MEMBER_LAYOUT(qa_unified_movement, data.q2r, input_q2r_fields);
static const qa_unified_field input_q3_fields[] = {
    MEMBER_FIELD(qa_unified_movement, data.q3, server_time_ms, QA_UNIFIED_FIELD_F64),
    MEMBER_FIXED(qa_unified_movement, data.q3, angle_words, double_layout, 3),
    MEMBER_FIELD(qa_unified_movement, data.q3, buttons, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q3, weapon, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q3, forward, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q3, right, QA_UNIFIED_FIELD_F64),
    MEMBER_FIELD(qa_unified_movement, data.q3, up, QA_UNIFIED_FIELD_F64),
};
static const qa_unified_record_layout input_q3_layout = MEMBER_LAYOUT(qa_unified_movement, data.q3, input_q3_fields);
static const qa_unified_record_layout *const input_movement_variants[] = {
    &input_nq_layout, &input_qw_layout, &input_q2_layout, &input_q2r_layout, &input_q3_layout};
static const qa_unified_field input_movement_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_movement, kind, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_VARIANT(qa_unified_movement, data, kind, input_movement_variants),
};
static const qa_unified_record_layout input_movement_layout = QA_UNIFIED_LAYOUT(qa_unified_movement, input_movement_fields);
static const qa_unified_field input_arsenal_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_arsenal, use_holdable, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_arsenal, has_impulse, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_arsenal, impulse, QA_UNIFIED_FIELD_U8),
};
static const qa_unified_record_layout input_arsenal_layout = QA_UNIFIED_LAYOUT(qa_unified_arsenal, input_arsenal_fields);
static const qa_unified_field input_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_input, sequence, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_RECORD(qa_unified_input, command, input_movement_layout),
    QA_UNIFIED_FIELD(qa_unified_input, has_arsenal, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_RECORD(qa_unified_input, arsenal, input_arsenal_layout),
};
static const qa_unified_record_layout input_layout = QA_UNIFIED_LAYOUT(qa_unified_input, input_fields);
static const qa_unified_field buffer_fields[] = {{QA_UNIFIED_FIELD_BYTES, 0, NULL, 0, 8192, NULL}};
static const qa_unified_record_layout buffer_layout = {sizeof(qa_buffer), buffer_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};
static const qa_unified_field input_batch_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_input_batch, epoch, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_INLINE_ARRAY(qa_unified_input_batch, commands, count, input_layout, 64),
    QA_UNIFIED_INLINE_ARRAY(qa_unified_input_batch, providers, count, buffer_layout, 64),
    QA_UNIFIED_INLINE_ARRAY(qa_unified_input_batch, weapons, count, buffer_layout, 64),
};
const qa_unified_record_layout qa_unified_inputs_layout = QA_UNIFIED_LAYOUT(qa_unified_input_batch, input_batch_fields);

static bool frame_bad(qa_error *error, const char *message)
{ qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false; }
static bool same_source(qa_actor_id actor, uint64_t registry)
{ return registry != 0 && actor.registry == registry; }
static bool world_actor(const qa_unified_world_frame *world, qa_actor_id actor)
{
    size_t low = 0, high = world->actor_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2; qa_actor_id current = world->actors[middle].actor;
        if (current.slot < actor.slot || (current.slot == actor.slot && current.generation < actor.generation)) low = middle + 1;
        else high = middle;
    }
    return low < world->actor_count && qa_actor_id_equal(world->actors[low].actor, actor);
}
static bool finite_record(const qa_unified_record_layout *layout, const void *record)
{
    for (size_t i = 0; i < layout->field_count; ++i) {
        const qa_unified_field *f = layout->fields + i; const uint8_t *field = (const uint8_t *)record + f->offset;
        if (f->kind == QA_UNIFIED_FIELD_F32 && !isfinite(*(const float *)field)) return false;
        if (f->kind == QA_UNIFIED_FIELD_F64 && !isfinite(*(const double *)field)) return false;
        if (f->kind == QA_UNIFIED_FIELD_RECORD && !finite_record(f->record, field)) return false;
        if (f->kind == QA_UNIFIED_FIELD_FIXED)
            for (size_t row = 0; row < f->maximum; ++row)
                if (!finite_record(f->record, field + row * f->record->size)) return false;
    }
    return true;
}
static bool q3_gamestate_check(const qa_q3_gamestate *state, qa_error *error)
{
    if (!state || !state->string_bytes || state->string_bytes > sizeof(state->strings) || state->strings[0])
        return frame_bad(error, "Typed Q3 gamestate lost its actual string storage");
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) {
        size_t offset = state->config_offsets[i];
        if (offset >= state->string_bytes || !memchr(state->strings + offset, 0, state->string_bytes - offset))
            return frame_bad(error, "Typed Q3 configstring exceeds its actual gamestate");
    }
    for (size_t i = 0; i < QA_Q3_ENTITIES; ++i)
        if (state->baseline_present[i] && (i > QA_Q3_ENTITY_WORLD || state->baselines[i].number != (int32_t)i ||
            !finite_record(&qa_q3_entity_layout, state->baselines + i)))
            return frame_bad(error, "Typed Q3 baseline lost its actual entity number or finite state");
    return true;
}
static bool q3_sources_check(const qa_unified_frame_q3 *section, uint64_t registry, qa_error *error)
{
    if (!section) return true;
    for (size_t s = 0; s < section->source_count; ++s) {
        const qa_unified_q3_source *source = section->sources + s;
        const qa_unified_q3_entity *entities[QA_Q3_ENTITIES] = {0};
        if (!source->provider_name || !*source->provider_name || !source->instance || !*source->instance ||
            !source->content || !*source->content || !source->max_clients || source->max_clients > 64 ||
            (source->snapshot_bit != 0 && source->snapshot_bit != 4) || source->entity_count > QA_Q3_ENTITIES ||
            source->client_count > source->max_clients)
            return frame_bad(error, "Typed Q3 source lost its actual owner or client bounds");
        for (size_t i = 0; i < source->entity_count; ++i) {
            const qa_unified_q3_entity *row = source->entities + i;
            if (row->number > QA_Q3_ENTITY_WORLD || entities[row->number] || row->state.number != (int32_t)row->number ||
                !same_source(row->actor, registry) || !finite_record(&qa_q3_entity_layout, &row->state) ||
                !finite_record(&qa_unified_vector_layout, &row->origin) ||
                !finite_record(&qa_unified_bounds_layout, &row->link_bounds) ||
                row->link_bounds.mins.x > row->link_bounds.maxs.x || row->link_bounds.mins.y > row->link_bounds.maxs.y ||
                row->link_bounds.mins.z > row->link_bounds.maxs.z)
                return frame_bad(error, "Typed Q3 entity lost its actual physical identity or finite link state");
            entities[row->number] = row;
        }
        bool viewer = false; uint64_t clients = 0;
        for (size_t i = 0; i < source->client_count; ++i) {
            const qa_unified_q3_client *row = source->clients + i;
            if (row->source_number > QA_Q3_ENTITY_WORLD || !entities[row->source_number] || row->client_slot < 0 ||
                (uint32_t)row->client_slot >= source->max_clients || (clients & (UINT64_C(1) << (unsigned)row->client_slot)) ||
                !qa_actor_id_equal(entities[row->source_number]->actor, row->actor) ||
                !finite_record(&qa_q3_player_layout, &row->state))
                return frame_bad(error, "Typed Q3 client lost its actual entity or unique physical slot");
            clients |= UINT64_C(1) << (unsigned)row->client_slot;
            if (qa_actor_id_equal(source->viewer, row->actor) && source->client_number == (uint32_t)row->client_slot) viewer = true;
        }
        if (source->has_client && (!viewer || !source->visibility || source->client_number >= source->max_clients))
            return frame_bad(error, "Typed Q3 viewer is not an actual source client");
        if (source->visibility) {
            const qa_unified_q3_visibility *visible = source->visibility;
            if (visible->entity_count > 256) return frame_bad(error, "Typed Q3 visibility exceeds its actual snapshot");
            for (size_t i = 0; i < visible->entity_count; ++i) {
                int32_t number = visible->entities[i];
                if (number < 0 || number > QA_Q3_ENTITY_WORLD || !entities[number] ||
                    (i && visible->entities[i - 1] >= number))
                    return frame_bad(error, "Typed Q3 visibility lost its ordered current source entities");
            }
        }
    }
    return true;
}
static bool components_check(const qa_unified_frame_components *section, uint64_t registry, qa_error *error)
{
    if (!section) return true;
    for (size_t s = 0; s < section->source_count; ++s) {
        const qa_unified_component_source *source = section->sources + s;
        bool bound[QA_Q3_ENTITIES] = {0}, entities[QA_Q3_ENTITIES] = {0};
        if (!source->owner.provider || !source->owner.generation || !same_source(source->viewer, registry) ||
            (source->abi != QA_QVM_Q3_MODERN && source->abi != QA_QVM_Q3_116N) ||
            source->binding_count > QA_Q3_ENTITIES || source->snapshot.entity_count > 256 || source->snapshot.area_bytes > 32 ||
            !finite_record(&qa_q3_player_layout, &source->snapshot.player))
            return frame_bad(error, "Typed Q3 component lost its actual ABI, owner or snapshot bounds");
        for (size_t i = 0; i < source->binding_count; ++i) {
            const qa_unified_component_binding *row = source->bindings + i;
            if (row->slot > QA_Q3_ENTITY_WORLD || bound[row->slot] || !same_source(row->actor, registry))
                return frame_bad(error, "Typed Q3 component has a foreign or repeated physical binding");
            bound[row->slot] = true;
        }
        for (size_t i = 0; i < source->snapshot.entity_count; ++i) {
            const qa_q3_entity *row = source->snapshot.entities + i;
            if (row->number < 0 || row->number > QA_Q3_ENTITY_WORLD || entities[row->number] ||
                !finite_record(&qa_q3_entity_layout, row))
                return frame_bad(error, "Typed Q3 component snapshot has an invalid actual entity");
            entities[row->number] = true;
        }
    }
    for (size_t i = 0; i < section->native_count; ++i) {
        const qa_unified_native_component *row = section->native + i;
        if (!row->owner.provider || !row->owner.generation || !same_source(row->viewer, registry) ||
            (row->hud && (row->hud->stat_count > 64 || !finite_record(&qa_unified_native_hud_layout, row->hud))) ||
            (row->view && !finite_record(&qa_unified_native_camera_layout, row->view)))
            return frame_bad(error, "Typed native component lost its real viewer or finite presentation state");
    }
    return true;
}
bool qa_unified_frame_check(const qa_unified_frame *frame, size_t *bytes, qa_error *error)
{
    if (!frame || !frame->epoch || frame->acknowledged_input < -1 ||
        frame->acknowledged_input > (int64_t)QA_UNIFIED_SAFE_INTEGER || !frame->world || !frame->player ||
        !qa_unified_record_measure(&qa_unified_frame_layout, frame, bytes, error))
        return frame_bad(error, "Unified FRAME requires its actual typed Source cut and recipient");
    const qa_unified_world_frame *world = frame->world;
    if (!world->actor_count || !world->source.provider || world->source.kind < QA_CLOCK_NETQUAKE || world->source.kind > QA_CLOCK_Q3 ||
        world->source.phase < QA_FRAME_ENTRY || world->source.phase > QA_FRAME_EXIT)
        return frame_bad(error, "Unified FRAME lost its actual Source roster or completed clock");
    uint64_t registry = world->actors[0].actor.registry;
    for (size_t i = 0; i < world->actor_count; ++i)
        if (!same_source(world->actors[i].actor, registry) || !world->actors[i].owner || !world->actors[i].definition)
            return frame_bad(error, "Unified FRAME mixes Source registries or missing actor ownership");
    if (!world_actor(world, frame->player->actor) || (frame->prediction &&
        (!qa_actor_id_equal(frame->player->actor, frame->prediction->actor) || frame->prediction->sequence < -1)))
        return frame_bad(error, "Unified FRAME recipient or prediction is not an actual roster member");
    for (size_t i = 0; i < world->body_count; ++i)
        if (!world_actor(world, world->bodies[i].actor) || !finite_record(&qa_unified_body_layout, &world->bodies[i].body))
            return frame_bad(error, "Unified FRAME body lost its actual Source actor or finite physics state");
    for (size_t i = 0; i < frame->inventory_count; ++i)
        if (!world_actor(world, frame->inventories[i].actor)) return frame_bad(error, "Unified FRAME inventory has no actual Source actor");
    return q3_sources_check(frame->q3, registry, error) && components_check(frame->components, registry, error);
}

static bool message_arguments_check(const qa_unified_message_arg *rows, size_t count)
{
    for (size_t i=0;i<count;++i)
        if (rows[i].kind!=QA_BUILTIN_MESSAGE_STRING && rows[i].kind!=QA_BUILTIN_MESSAGE_NUMBER) return false;
    return true;
}
static bool q2_temporary_check(const qa_unified_q2_temporary *event)
{
    for (size_t i=0;i<event->field_count;++i) {
        const qa_unified_q2_temp_field *field=event->fields+i;
        if ((field->kind!=QA_Q2_TEMP_INTEGER && field->kind!=QA_Q2_TEMP_VECTOR) ||
            field->name<QA_Q2_TEMP_ENTITY1 || field->name>QA_Q2_TEMP_OFFSET) return false;
    }
    return true;
}
static bool presentation_check(const qa_unified_presentation_event *row, qa_error *error)
{
    if (!isfinite(row->seconds) || row->seconds<0 || row->family<QA_GAME_Q1 || row->family>QA_GAME_Q3 ||
        row->q2_profile>2 || (row->owner.generation && !row->owner.provider))
        return frame_bad(error,"Typed presentation lost its actual Source envelope");
    const qa_unified_presentation_payload *payload=&row->payload;
    bool okay=true;
    switch (payload->kind) {
    case QA_UNIFIED_PRESENTATION_BUILTIN: {
        const qa_unified_builtin_event *event=&payload->value.builtin;
        okay=event->kind>=QA_BUILTIN_SOUND && event->kind<=QA_BUILTIN_Q2_ENTITY_EVENT &&
            event->family>=QA_GAME_Q1 && event->family<=QA_GAME_Q3 &&
            message_arguments_check(event->arguments,event->argument_count);
        break;
    }
    case QA_UNIFIED_PRESENTATION_Q2_PLAYER: {
        const qa_unified_q2_player_event *event=&payload->value.q2_player;
        okay=event->kind>=QA_Q2_PLAYER_PRINT && event->kind<=QA_Q2_PLAYER_ALPHA &&
            event->respawn_status>=QA_Q2_RESPAWN_READY && event->respawn_status<=QA_Q2_RESPAWN_NO_LIVES &&
            event->hand>=QA_Q2_RIGHT_HAND && event->hand<=QA_Q2_CENTER_HAND;
        break;
    }
    case QA_UNIFIED_PRESENTATION_Q2_MAP: {
        const qa_unified_q2_map_event *event=&payload->value.q2_map;
        okay=event->kind>=QA_Q2_MAP_HELP && event->kind<=QA_Q2_MAP_HELP_COMPUTER &&
            message_arguments_check(event->arguments,event->argument_count);
        break;
    }
    case QA_UNIFIED_PRESENTATION_Q2_PROTOCOL: {
        const qa_unified_q2_protocol_event *event=&payload->value.q2_protocol;
        okay=event->kind>=QA_Q2_SVC_NOP && event->kind<=QA_Q2_SVC_PRIVATE &&
            message_arguments_check(event->arguments,event->argument_count) && q2_temporary_check(&event->temporary);
        break;
    }
    case QA_UNIFIED_PRESENTATION_Q2_TEMPORARY: okay=q2_temporary_check(&payload->value.q2_temporary); break;
    case QA_UNIFIED_PRESENTATION_Q3: {
        const qa_unified_q3_event *event=&payload->value.q3;
        okay=event->kind>=QA_UNIFIED_Q3_PRINT && event->kind<=QA_UNIFIED_Q3_SOUND;
        if (okay && event->kind==QA_UNIFIED_Q3_PLAYER_EVENT)
            okay=event->module.id && event->module.artifact_path &&
                (event->abi==QA_QVM_Q3_MODERN || event->abi==QA_QVM_Q3_116N) && event->time_ms>=0 &&
                finite_record(&qa_q3_player_layout,&event->player) && finite_record(&qa_unified_vector_layout,&event->origin);
        if (okay && event->kind==QA_UNIFIED_Q3_ENTITY_EVENT)
            okay=finite_record(&qa_q3_entity_layout,&event->entity);
        break;
    }
    case QA_UNIFIED_PRESENTATION_Q3_BALLISTIC:
        okay=payload->value.q3_ballistic.kind>=QA_UNIFIED_Q3_REMOVE && payload->value.q3_ballistic.kind<=QA_UNIFIED_Q3_RAIL_AWARD; break;
    case QA_UNIFIED_PRESENTATION_OWNER:
        okay=payload->value.owner.kind>=QA_UNIFIED_OWNER_RETIRED && payload->value.owner.kind<=QA_UNIFIED_OWNER_REFRESHED &&
            payload->value.owner.owner.provider && payload->value.owner.owner.generation; break;
    case QA_UNIFIED_PRESENTATION_MODEL: case QA_UNIFIED_PRESENTATION_Q3_CHARACTER: case QA_UNIFIED_PRESENTATION_VISIBILITY: break;
    default: okay=false; break;
    }
    return okay || frame_bad(error,"Typed presentation has an invalid actual Source variant");
}
bool qa_unified_events_check(const qa_unified_frame_events *events, size_t *bytes, qa_error *error)
{
    if (!events || !events->epoch || !qa_unified_record_measure(&qa_unified_events_layout,events,bytes,error)) return false;
    for (size_t i=0;i<events->presentation_count;++i)
        if (!presentation_check(events->presentation+i,error)) return false;
    for (size_t i=0;i<events->simulation_count;++i) {
        const qa_unified_simulation_event *row=events->simulation+i;
        if (!isfinite(row->time) || row->time<0 ||
            (row->payload.kind==QA_UNIFIED_SIMULATION_SOUND && !row->payload.value.sound.resource) ||
            (row->payload.kind==QA_UNIFIED_SIMULATION_MESSAGE &&
                (row->payload.value.message.kind<QA_UNIFIED_MESSAGE_PRINT || row->payload.value.message.kind>QA_UNIFIED_MESSAGE_DISCONNECT)))
            return frame_bad(error,"Typed simulation lost its actual time or Source variant");
    }
    return true;
}

static const qa_unified_field style_pattern_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_style_pattern, family, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_style_pattern, index, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_style_pattern, pattern, QA_UNIFIED_FIELD_STRING),
};
static const qa_unified_record_layout style_pattern_layout = QA_UNIFIED_LAYOUT(qa_unified_style_pattern, style_pattern_fields);
static const qa_unified_field q3_configuration_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q3_configuration, provider_name, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q3_configuration, instance, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q3_configuration, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q3_configuration, publication, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_q3_configuration, map_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_q3_configuration, configuration_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_POINTER(qa_unified_q3_configuration, game_state, qa_q3_gamestate_layout),
    QA_UNIFIED_FIXED(qa_unified_q3_configuration, config_revisions, uint64_t_layout, QA_Q3_CONFIGSTRINGS),
};
const qa_unified_record_layout qa_unified_q3_configuration_layout = QA_UNIFIED_LAYOUT(qa_unified_q3_configuration, q3_configuration_fields);
static const qa_unified_field q1_world_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_q1_world_state, level, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_q1_world_state, total_secrets, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q1_world_state, total_monsters, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q1_world_state, found_secrets, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_FIELD(qa_unified_q1_world_state, killed_monsters, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout q1_world_layout = QA_UNIFIED_LAYOUT(qa_unified_q1_world_state, q1_world_fields);
static const qa_unified_field metadata_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, epoch, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, frame, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, configuration_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, roster_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, style_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, q1_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, replace_configurations, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, replace_styles, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, replace_q3, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_frame_metadata, replace_q1, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_ARRAY(qa_unified_frame_metadata, configurations, configuration_count, qa_unified_configuration_state_layout, 65536),
    QA_UNIFIED_ARRAY(qa_unified_frame_metadata, styles, style_count, style_pattern_layout, 320),
    QA_UNIFIED_ARRAY(qa_unified_frame_metadata, q3_configurations, q3_configuration_count, qa_unified_q3_configuration_layout, 256),
    QA_UNIFIED_POINTER(qa_unified_frame_metadata, q1, q1_world_layout),
};
const qa_unified_record_layout qa_unified_metadata_layout = QA_UNIFIED_LAYOUT(qa_unified_frame_metadata, metadata_fields);
void qa_unified_frame_metadata_destroy(qa_unified_frame_metadata *value)
{ qa_unified_record_dispose(&qa_unified_metadata_layout, value); free(value); }

bool qa_unified_metadata_check(const qa_unified_frame_metadata *value, size_t *bytes, qa_error *error)
{
    if (!value || !value->epoch || (!value->replace_configurations && value->configuration_count) ||
        (!value->replace_styles && value->style_count) ||
        (!value->replace_q3 && value->q3_configuration_count) ||
        (!value->replace_q1 && value->q1) ||
        (value->q1 && !value->q1->level) ||
        !qa_unified_record_measure(&qa_unified_metadata_layout,value,bytes,error))
        return frame_bad(error,"Unified metadata lost its explicit replacement domains");
    uint64_t registry=value->configuration_count?value->configurations[0].actor.registry:0;
    for (size_t i=0;i<value->configuration_count;++i) {
        const qa_unified_configuration_state *row=value->configurations+i;
        if (row->actor.registry!=registry || !row->movement.provider || !row->movement.content ||
            !row->character.provider || !row->character.content || !row->appearance.provider || !row->appearance.content ||
            !row->inventory.provider || !row->inventory.content)
            return frame_bad(error,"Unified configuration lost its actual Source actor or providers");
        for (size_t weapon=0;weapon<row->weapon_count;++weapon)
            if (!row->weapons[weapon].provider || !row->weapons[weapon].content)
                return frame_bad(error,"Unified configuration lost its selected weapon provider");
    }
    for (size_t i=0;i<value->style_count;++i) {
        const qa_unified_style_pattern *row=value->styles+i;
        if (!row->pattern || (row->family!=QA_GAME_Q1 && row->family!=QA_GAME_Q2) ||
            row->index>=(row->family==QA_GAME_Q1?64u:256u) ||
            (i && (value->styles[i-1].family>row->family ||
                (value->styles[i-1].family==row->family && value->styles[i-1].index>=row->index))))
            return frame_bad(error,"Unified lightstyle metadata has an invalid or repeated Source pattern");
    }
    for (size_t i=0;i<value->q3_configuration_count;++i) {
        const qa_unified_q3_configuration *row=value->q3_configurations+i;
        if (!row->provider_name || !*row->provider_name || !row->instance || !*row->instance ||
            !row->content || !*row->content || !q3_gamestate_check(row->game_state,error))
            return frame_bad(error,"Unified Q3 metadata lost its actual Source owner or gamestate");
        for (size_t prior=0;prior<i;++prior)
            if (!strcmp(value->q3_configurations[prior].provider_name,row->provider_name))
                return frame_bad(error,"Unified Q3 metadata repeats its actual Source provider");
    }
    return true;
}

static const qa_unified_field control_string_fields[] = {{QA_UNIFIED_FIELD_STRING, 0, NULL, 0, 8192, NULL}};
static const qa_unified_record_layout control_string_layout = {sizeof(char *), control_string_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};
static const qa_unified_field control_arguments_fields[] = {
    QA_UNIFIED_ARRAY(qa_unified_control_arguments, values, count, control_string_layout, 128),
};
static const qa_unified_record_layout control_arguments_layout = QA_UNIFIED_LAYOUT(qa_unified_control_arguments, control_arguments_fields);
static const qa_unified_field component_identity_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_component_identity, runtime, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_component_identity, product, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_component_identity, id, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_component_identity, provider, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_FIELD(qa_unified_component_identity, content, QA_UNIFIED_FIELD_STRING),
    QA_UNIFIED_RECORD(qa_unified_component_identity, module, qa_unified_mod_identity_layout),
};
static const qa_unified_record_layout component_identity_layout = QA_UNIFIED_LAYOUT(qa_unified_component_identity, component_identity_fields);
static const qa_unified_field component_command_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_component_command, sequence, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_RECORD(qa_unified_component_command, arguments, control_arguments_layout),
};
static const qa_unified_record_layout component_command_layout = QA_UNIFIED_LAYOUT(qa_unified_component_command, component_command_fields);
static const qa_unified_field component_q3_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_component_q3, owner, qa_unified_component_owner_layout),
    QA_UNIFIED_RECORD(qa_unified_component_q3, identity, component_identity_layout),
    QA_UNIFIED_FIELD(qa_unified_component_q3, generation, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_component_q3, abi, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_component_q3, scene, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_FIELD(qa_unified_component_q3, game_state_revision, QA_UNIFIED_FIELD_I64),
    QA_UNIFIED_POINTER(qa_unified_component_q3, game_state, qa_q3_gamestate_layout),
    QA_UNIFIED_FIELD(qa_unified_component_q3, command_base, QA_UNIFIED_FIELD_I32),
    QA_UNIFIED_ARRAY(qa_unified_component_q3, commands, command_count, component_command_layout, 64),
};
static const qa_unified_record_layout component_q3_layout = QA_UNIFIED_LAYOUT(qa_unified_component_q3, component_q3_fields);
static const qa_unified_field component_configstring_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_component_configstring, index, QA_UNIFIED_FIELD_U32),
    {QA_UNIFIED_FIELD_STRING, offsetof(qa_unified_component_configstring, value), NULL, 0, 8192, NULL},
};
static const qa_unified_record_layout component_configstring_layout = QA_UNIFIED_LAYOUT(qa_unified_component_configstring, component_configstring_fields);
static const qa_unified_field component_protocol_fields[] = {
    QA_UNIFIED_FIELD(qa_net_protocol_id, kind, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_net_protocol_id, flags, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_net_protocol_id, revision, QA_UNIFIED_FIELD_U32),
};
static const qa_unified_record_layout component_protocol_layout = QA_UNIFIED_LAYOUT(qa_net_protocol_id, component_protocol_fields);
static const qa_unified_field component_q2_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_component_q2, owner, qa_unified_component_owner_layout),
    QA_UNIFIED_RECORD(qa_unified_component_q2, identity, component_identity_layout),
    QA_UNIFIED_FIELD(qa_unified_component_q2, generation, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_component_q2, hud, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_RECORD(qa_unified_component_q2, protocol, component_protocol_layout),
    QA_UNIFIED_FIELD(qa_unified_component_q2, replace_configstrings, QA_UNIFIED_FIELD_BOOL),
    QA_UNIFIED_ARRAY(qa_unified_component_q2, configstrings, configstring_count, component_configstring_layout, 16384),
    {QA_UNIFIED_FIELD_STRING, offsetof(qa_unified_component_q2, layout), NULL, 0, 65536, NULL},
    QA_UNIFIED_FIXED(qa_unified_component_q2, inventory, int16_t_layout, 256),
    QA_UNIFIED_FIELD(qa_unified_component_q2, player_number, QA_UNIFIED_FIELD_I32),
};
static const qa_unified_record_layout component_q2_layout = QA_UNIFIED_LAYOUT(qa_unified_component_q2, component_q2_fields);
static const qa_unified_field control_components_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_components_control, revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_ARRAY(qa_unified_components_control, sources, source_count, component_q3_layout, 256),
    QA_UNIFIED_ARRAY(qa_unified_components_control, native, native_count, component_q2_layout, 256),
};
static const qa_unified_record_layout control_components_layout = QA_UNIFIED_LAYOUT(qa_unified_components_control, control_components_fields);

bool qa_unified_component_identity_clone(const qa_unified_component_identity *source,
    qa_unified_component_identity *out, qa_error *error)
{ return source && out && qa_unified_record_clone(&component_identity_layout,source,out,error); }
bool qa_unified_component_identity_equal(const qa_unified_component_identity *a,const qa_unified_component_identity *b)
{ return a && b && qa_unified_record_equal(&component_identity_layout,a,b); }
void qa_unified_component_identity_dispose(qa_unified_component_identity *value)
{ if (value) { qa_unified_record_dispose(&component_identity_layout,value); memset(value,0,sizeof(*value)); } }
bool qa_unified_component_identity_write(const qa_unified_component_identity *value,qa_buffer *out,qa_error *error)
{ return value && out && !out->data && !out->size &&
    qa_unified_record_delta_encode(&component_identity_layout,value,NULL,32u * 1024u * 1024u,out,error); }
bool qa_unified_component_identity_read(qa_bytes bytes,qa_unified_component_identity *out,qa_error *error)
{ return out && qa_unified_record_delta_decode(&component_identity_layout,bytes,NULL,out,NULL,error); }

static const qa_unified_field control_ready_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_ready_control, composition, QA_UNIFIED_FIELD_U64),
    {QA_UNIFIED_FIELD_STRING, offsetof(qa_unified_ready_control, userinfo), NULL, 0, 8192, NULL},
};
static const qa_unified_record_layout control_ready_layout = QA_UNIFIED_LAYOUT(qa_unified_ready_control, control_ready_fields);
static const qa_unified_field control_client_fields[] = {
    QA_UNIFIED_FIELD(qa_net_client_id, owner, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_net_client_id, slot, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_net_client_id, generation, QA_UNIFIED_FIELD_U64),
};
static const qa_unified_record_layout control_client_layout = QA_UNIFIED_LAYOUT(qa_net_client_id, control_client_fields);
static const qa_unified_field control_admitted_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_admitted_control, client, control_client_layout),
    QA_UNIFIED_RECORD(qa_unified_admitted_control, actor, qa_unified_actor_layout),
    QA_UNIFIED_FIELD(qa_unified_admitted_control, source_entity, QA_UNIFIED_FIELD_U32),
};
static const qa_unified_record_layout control_admitted_layout = QA_UNIFIED_LAYOUT(qa_unified_admitted_control, control_admitted_fields);
static const qa_unified_field control_resource_fields[] = {
    {QA_UNIFIED_FIELD_STRING, offsetof(qa_unified_resource_declaration, identity), NULL, 0, sizeof("resource:unified:") + 20 - 1, NULL},
    QA_UNIFIED_RECORD(qa_unified_resource_declaration, resource, qa_unified_resource_state_layout),
};
static const qa_unified_record_layout control_resource_layout = QA_UNIFIED_LAYOUT(qa_unified_resource_declaration, control_resource_fields);
static const qa_unified_field control_resources_fields[] = {
    QA_UNIFIED_ARRAY(qa_unified_resources_control, values, count, control_resource_layout, 32768),
};
static const qa_unified_record_layout control_resources_layout = QA_UNIFIED_LAYOUT(qa_unified_resources_control, control_resources_fields);
static const qa_unified_field control_command_fields[] = {
    {QA_UNIFIED_FIELD_STRING, offsetof(qa_unified_command_control, name), NULL, 0, 128, NULL},
    QA_UNIFIED_RECORD(qa_unified_command_control, arguments, control_arguments_layout),
};
static const qa_unified_record_layout control_command_layout = QA_UNIFIED_LAYOUT(qa_unified_command_control, control_command_fields);
static const qa_unified_field control_source_command_fields[] = {
    {QA_UNIFIED_FIELD_STRING, offsetof(qa_unified_source_command_control, instance), NULL, 0, 8192, NULL},
    QA_UNIFIED_FIELD(qa_unified_source_command_control, publication, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_FIELD(qa_unified_source_command_control, map_revision, QA_UNIFIED_FIELD_U64),
    QA_UNIFIED_RECORD(qa_unified_source_command_control, arguments, control_arguments_layout),
};
static const qa_unified_record_layout control_source_command_layout = QA_UNIFIED_LAYOUT(qa_unified_source_command_control, control_source_command_fields);
static const qa_unified_field control_component_command_fields[] = {
    QA_UNIFIED_RECORD(qa_unified_component_command_control, owner, qa_unified_component_owner_layout),
    QA_UNIFIED_RECORD(qa_unified_component_command_control, arguments, control_arguments_layout),
};
static const qa_unified_record_layout control_component_command_layout = QA_UNIFIED_LAYOUT(qa_unified_component_command_control, control_component_command_fields);
static const qa_unified_field control_disconnect_fields[] = {{QA_UNIFIED_FIELD_STRING, 0, NULL, 0, 4096, NULL}};
static const qa_unified_record_layout control_disconnect_layout = {sizeof(char *), control_disconnect_fields, 1, SIZE_MAX, QA_UNIFIED_KEY_NONE};
static const qa_unified_record_layout *const control_variants[] = {
    &control_ready_layout, &control_admitted_layout, &control_resources_layout,
    &control_string_layout, &control_command_layout, &control_source_command_layout,
    &control_disconnect_layout, NULL, &control_components_layout, &control_component_command_layout,
};
static const qa_unified_field control_fields[] = {
    QA_UNIFIED_FIELD(qa_unified_control, kind, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_FIELD(qa_unified_control, epoch, QA_UNIFIED_FIELD_U32),
    QA_UNIFIED_VARIANT(qa_unified_control, value, kind, control_variants),
};
const qa_unified_record_layout qa_unified_control_layout = QA_UNIFIED_LAYOUT(qa_unified_control, control_fields);

static bool control_arguments_check(const qa_unified_control_arguments *args, bool required, qa_error *error)
{
    if ((required && !args->count) || args->count>128 || (args->count && !args->values))
        return frame_bad(error,"Unified control has an invalid actual argument extent");
    for (size_t i=0;i<args->count;++i)
        if (!args->values[i]) return frame_bad(error,"Unified control has an absent lexical argument");
    return true;
}
static bool component_identity_check(const qa_unified_component_identity *identity)
{
    return (identity->runtime==QA_PROGRAM_QVM || identity->runtime==QA_PROGRAM_NATIVE) &&
        identity->product && *identity->product && identity->id && *identity->id &&
        identity->provider && *identity->provider && identity->content && *identity->content &&
        identity->module.id && *identity->module.id && identity->module.artifact_path && *identity->module.artifact_path;
}
static bool control_components_check(const qa_unified_components_control *update,qa_error *error)
{
    if (!update->revision || update->source_count+update->native_count>256)
        return frame_bad(error,"Unified component update has no actual reliable revision or owner extent");
    for (size_t i=0;i<update->source_count;++i) {
        const qa_unified_component_q3 *row=update->sources+i;
        if (!row->owner.provider || !*row->owner.provider || !row->owner.generation ||
            row->generation!=row->owner.generation || !component_identity_check(&row->identity) ||
            row->identity.runtime!=QA_PROGRAM_QVM || (row->abi!=QA_QVM_Q3_MODERN && row->abi!=QA_QVM_Q3_116N) ||
            row->game_state_revision<0 || row->command_base<0 ||
            (row->game_state && !q3_gamestate_check(row->game_state,error)) ||
            (uint64_t)row->command_base+row->command_count>INT32_MAX || (!row->scene && row->command_count))
            return frame_bad(error,"Unified Q3 component lost its actual identity, ABI or reliable state");
        for (size_t k=0;k<i;++k) if (!strcmp(row->owner.provider,update->sources[k].owner.provider))
            return frame_bad(error,"Unified component update repeats its actual Source owner");
        for (size_t k=0;k<row->command_count;++k)
            if (row->commands[k].sequence!=(int32_t)((uint64_t)row->command_base+k+1) ||
                !control_arguments_check(&row->commands[k].arguments,false,error))
                return frame_bad(error,"Unified component reliable commands are not consecutive");
    }
    for (size_t i=0;i<update->native_count;++i) {
        const qa_unified_component_q2 *row=update->native+i;
        qa_q2_config_layout layout; qa_q2_codec codec={.protocol=row->protocol};
        if (!row->owner.provider || !*row->owner.provider || !row->owner.generation ||
            !component_identity_check(&row->identity) || row->identity.runtime!=QA_PROGRAM_NATIVE ||
            (unsigned)row->hud>QA_UNIFIED_COMPONENT_HUD_REPLACE ||
            (row->hud!=QA_UNIFIED_COMPONENT_HUD_NONE && (!row->layout || row->player_number<0 || row->player_number>=256 ||
                !qa_q2_config_layout_read(&codec,&layout,error))) ||
            (!row->replace_configstrings && row->configstring_count))
            return frame_bad(error,"Unified native component lost its real HUD or declared identity");
        for (size_t k=0;k<update->source_count;++k) if (!strcmp(row->owner.provider,update->sources[k].owner.provider))
            return frame_bad(error,"Unified native and QVM component owners alias");
        for (size_t k=0;k<i;++k) if (!strcmp(row->owner.provider,update->native[k].owner.provider))
            return frame_bad(error,"Unified native component owner is repeated");
        for (size_t k=0;k<row->configstring_count;++k) {
            if (row->hud==QA_UNIFIED_COMPONENT_HUD_NONE || row->configstrings[k].index>=layout.max_configs || !row->configstrings[k].value ||
                (k && row->configstrings[k-1].index>=row->configstrings[k].index))
                return frame_bad(error,"Unified Q2 configstrings exceed their actual ordered Source table");
        }
    }
    return true;
}
bool qa_unified_control_check(const qa_unified_control *v, size_t *bytes, qa_error *error)
{
    if (!v || !bytes || ((unsigned)v->kind>QA_UNIFIED_CONTROL_DISCONNECT && v->kind!=QA_UNIFIED_CONTROL_COMPONENT_COMMAND && v->kind!=QA_UNIFIED_CONTROL_COMPONENTS) ||
        (v->kind!=QA_UNIFIED_CONTROL_DISCONNECT && !v->epoch) ||
        !qa_unified_record_measure(&qa_unified_control_layout,v,bytes,error))
        return frame_bad(error,"Unified control requires its actual typed kind and epoch");
    switch (v->kind) {
    case QA_UNIFIED_CONTROL_READY:
        return v->value.ready.userinfo || frame_bad(error,"Unified readiness has no real userinfo");
    case QA_UNIFIED_CONTROL_ADMITTED:
        return (v->value.admitted.client.owner && v->value.admitted.client.generation &&
            v->value.admitted.actor.registry) || frame_bad(error,"Unified admission has no actual Source identity");
    case QA_UNIFIED_CONTROL_RESOURCES:
        for (size_t i=0;i<v->value.resources.count;++i) {
            const qa_unified_resource_declaration *row=v->value.resources.values+i;
            const char *path=row->resource.path;
            static const char prefix[]="resource:unified:";
            if (!row->identity || strncmp(row->identity,prefix,sizeof(prefix)-1) ||
                !row->resource.content || !*row->resource.content || !path || !*path || *path=='/' || strchr(path,'\\'))
                return frame_bad(error,"Unified declaration has an invalid actual resource identity or path");
            const unsigned char *serial_text=(const unsigned char *)row->identity+sizeof(prefix)-1;
            if (*serial_text<'1' || *serial_text>'9')
                return frame_bad(error,"Unified resource identity has no canonical nonzero Source serial");
            uint64_t serial=0;
            for (;*serial_text;++serial_text) {
                if (*serial_text<'0' || *serial_text>'9' || serial>(UINT64_MAX-(uint64_t)(*serial_text-'0'))/10)
                    return frame_bad(error,"Unified resource identity is outside its Source serial range");
                serial=serial*10+(uint64_t)(*serial_text-'0');
            }
            for (const char *at=path;*at;) {
                const char *end=strchr(at,'/'); size_t size=end?(size_t)(end-at):strlen(at);
                if (!size || (size==1 && *at=='.') || (size==2 && at[0]=='.' && at[1]=='.') || (end && !end[1]))
                    return frame_bad(error,"Unified declaration is outside its relative resource path");
                if (!end) break;
                at=end+1;
            }
        }
        return true;
    case QA_UNIFIED_CONTROL_USERINFO:
        return v->value.userinfo || frame_bad(error,"Unified userinfo control has no actual text");
    case QA_UNIFIED_CONTROL_COMMAND:
        if (!v->value.command.name || !*v->value.command.name)
            return frame_bad(error,"Unified command has no actual command name");
        for (const unsigned char *p=(const unsigned char *)v->value.command.name;*p;++p)
            if (!((*p>='a' && *p<='z') || (*p>='A' && *p<='Z') || *p=='_' || *p=='+' ||
                (p!=(const unsigned char *)v->value.command.name && (*p=='-' || (*p>='0' && *p<='9')))))
                return frame_bad(error,"Unified command name is outside its Source namespace");
        return control_arguments_check(&v->value.command.arguments,false,error);
    case QA_UNIFIED_CONTROL_SOURCE_COMMAND:
        return (v->value.source_command.instance && *v->value.source_command.instance &&
            v->value.source_command.publication && control_arguments_check(&v->value.source_command.arguments,true,error)) ||
            frame_bad(error,"Unified Source command has no actual activation");
    case QA_UNIFIED_CONTROL_DISCONNECT:
        return v->value.disconnect || frame_bad(error,"Unified disconnect has no actual reason");
    case QA_UNIFIED_CONTROL_COMPONENTS: return control_components_check(&v->value.components,error);
    case QA_UNIFIED_CONTROL_COMPONENT_COMMAND: {
        const qa_unified_component_command_control *command=&v->value.component_command;
        const char *colon=command->owner.provider?strchr(command->owner.provider,':'):NULL;
        return (colon && colon!=command->owner.provider && colon[1] &&
            command->owner.generation && control_arguments_check(&command->arguments,true,error)) ||
            frame_bad(error,"Unified component command has no actual provider activation");
    }
    default: return false;
    }
}
