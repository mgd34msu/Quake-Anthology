#ifndef QA_GAME_Q2_BOTS_H
#define QA_GAME_Q2_BOTS_H
#include "qa/game_q2.h"

typedef struct qa_q2_bot_entity {
    qa_string_id model,classname;
    float max_health;
    int32_t frame;
    bool present,hidden,worldspawn;
} qa_q2_bot_entity;
typedef struct qa_q2_bot_arsenal_configuration {
    qa_q2_edition edition;
    bool deathmatch;
} qa_q2_bot_arsenal_configuration;
/* Native launch recipe observation. Random spreads/damage are nominal means;
 * observation never advances Source RNG or executes a weapon callback. */
typedef struct qa_q2_bot_weapon_fact {
    qa_item_id item, ammo;
    double damage, splash_damage, effect_damage, ammo_per_shot, required_ammo;
    float speed, range, radius, effect_radius, cycle, activate, spin_up, launch_yaw_offset;
    float spread_x, spread_y, spread_degrees_x, spread_degrees_y, gravity_acceleration, extra_z_velocity, fuse, launch_delay;
    qa_vec3 offset, muzzle_offsets[2], launch_angles, launch_velocity;
    uint32_t shots;
    uint8_t muzzle_count;
    bool owned, available, ammo_reserved, melee, ballistic, homing, deployable, grapple;
    bool conditional, has_cycle, range_from_bounds, timed_detonation, requires_release, pitch_clamped;
} qa_q2_bot_weapon_fact;
qa_vec3 qa_q2_bot_weapon_launch_velocity(const qa_q2_bot_weapon_fact *,qa_vec3 angles);
bool qa_q2_bot_weapon_read(qa_q2_game *,qa_actor_id,qa_q2_weapon,
                           qa_q2_bot_weapon_fact *,bool *found,qa_error *);
bool qa_q2_bot_arsenal_configuration_read(const qa_q2_game *,qa_q2_bot_arsenal_configuration *,qa_error *);
bool qa_q2_bot_arsenal_register_multiplayer(qa_q2_game *,qa_q2_weapon_rules,
                                           bool native_hook,qa_q2_edition hook_edition,qa_error *);
bool qa_q2_bot_arsenal_rules_read(const qa_q2_game *,qa_q2_weapon_rules *,qa_error *);
bool qa_q2_bot_equipment_register_hook(qa_q2_game *,qa_q2_weapon_rules,
                                      qa_q2_edition,qa_error *);
bool qa_q2_bot_arsenal_definition_read(const qa_q2_game *,uint32_t ordinal,
                                      const qa_q2_weapon_definition **,qa_error *);
bool qa_q2_bot_arsenal_definition_count(const qa_q2_game *,uint32_t *,qa_error *);
bool qa_q2_bot_entity_read(qa_q2_game *,qa_actor_id,qa_q2_bot_entity *,qa_error *);
bool qa_q2_bot_clock_read(const qa_q2_game *,uint64_t *,bool *,uint64_t *,qa_error *);
bool qa_q2_bot_max_clients(const qa_q2_game *,uint32_t *,qa_error *);
bool qa_q2_bot_activate(qa_q2_game *,qa_actor_id,qa_error *);
qa_actor_id qa_q2_bot_world_actor(const qa_q2_game *);
/* The caller frees genuine supply receipts with qa_supply_preview_free. */
bool qa_q2_bot_supply_preview(qa_q2_game *,qa_actor_id pickup,qa_actor_id recipient,
                              qa_supply_preview_result *,bool *eligible,bool *found,qa_error *);
#endif
