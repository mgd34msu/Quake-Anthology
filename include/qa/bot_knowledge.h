#ifndef QA_BOT_KNOWLEDGE_H
#define QA_BOT_KNOWLEDGE_H

#include "qa/bot_runtime.h"
#include "qa/inventory.h"
#include "qa/bots_inventory.h"
#include "qa/bot_perception.h"
#include "qa/ruleset.h"
#include "qa/vfs.h"

typedef struct qa_bot_source_knowledge qa_bot_source_knowledge;
typedef struct qa_bot_source_movement {
    bool allow_combat_jumping, walk_only, allow_rocket_jumping;
    float jump_chance, jump_cooldown;
} qa_bot_source_movement;
typedef struct qa_bot_source_skill {
    qa_string_id name;
    bool allow_combat, allow_melee, lead_targets;
    qa_bot_senses senses;
    qa_bot_weapon_senses weapons;
    qa_bot_aiming aiming;
    qa_bot_source_movement movement;
    float weapon_fov_scalar;
} qa_bot_source_skill;
typedef struct qa_bot_source_field {
    qa_string_id key;
    const qa_string_id *values;
    size_t count;
    uint32_t line;
} qa_bot_source_field;
typedef struct qa_bot_source_block {
    const qa_string_id *header;
    size_t header_count;
    const qa_bot_source_field *fields;
    size_t field_count;
    uint32_t line;
} qa_bot_source_block;
typedef struct qa_bot_source_document {
    qa_string_id path;
    qa_bytes source;
    const qa_bot_source_block *blocks;
    size_t block_count;
} qa_bot_source_document;
typedef struct qa_bot_source_weapon {
    float ideal_fov, priority, min_range, max_range, min_height, max_height, min_ammo;
    size_t ordinal;
    bool melee, electric, needs_ammo;
} qa_bot_source_weapon;
typedef struct qa_bot_source_weapon_policy {
    const qa_bot_source_knowledge *knowledge;
    float range, height_delta;
    bool allow_melee, in_water, has_protection, target_in_water;
} qa_bot_source_weapon_policy;
/* Load once from the selected mounted scope. Views borrow the knowledge owner;
 * strings belong to the caller's session table. Missing settings return found=false. */
typedef bool (*qa_bot_source_weapon_resolve)(void *, qa_string_id authored_name,
    uint32_t authored_number, qa_item_id *, qa_error *);
bool qa_bot_source_knowledge_load(qa_vfs *, qa_strings *, qa_ruleset_id,
    qa_bot_source_weapon_resolve, void *,
    qa_bot_source_knowledge **, bool *found, qa_error *);
void qa_bot_source_knowledge_destroy(qa_bot_source_knowledge *);
size_t qa_bot_source_skill_count(const qa_bot_source_knowledge *);
const qa_bot_source_skill *qa_bot_source_skill_at(const qa_bot_source_knowledge *, size_t);
const qa_bot_source_weapon *qa_bot_source_weapon_read(const qa_bot_source_knowledge *, qa_item_id);
size_t qa_bot_source_document_count(const qa_bot_source_knowledge *);
const qa_bot_source_document *qa_bot_source_document_at(const qa_bot_source_knowledge *, size_t);

#define QA_BOT_INVENTORY_SIZE 256
enum {
    QA_BOT_INV_ARMOR=1, QA_BOT_INV_GAUNTLET=4, QA_BOT_INV_SHOTGUN=5,
    QA_BOT_INV_MACHINEGUN=6, QA_BOT_INV_GRENADE=7, QA_BOT_INV_ROCKET=8,
    QA_BOT_INV_LIGHTNING=9, QA_BOT_INV_RAIL=10, QA_BOT_INV_PLASMA=11,
    QA_BOT_INV_BFG=13, QA_BOT_INV_GRAPPLE=14, QA_BOT_INV_NAIL=15,
    QA_BOT_INV_PROX=16, QA_BOT_INV_CHAINGUN=17, QA_BOT_INV_SHELLS=18,
    QA_BOT_INV_BULLETS=19, QA_BOT_INV_GRENADES=20, QA_BOT_INV_CELLS=21,
    QA_BOT_INV_LIGHTNING_AMMO=22, QA_BOT_INV_ROCKETS=23, QA_BOT_INV_SLUGS=24,
    QA_BOT_INV_BFG_AMMO=25, QA_BOT_INV_NAILS=26, QA_BOT_INV_MINES=27,
    QA_BOT_INV_BELT=28, QA_BOT_INV_HEALTH=29, QA_BOT_INV_TELEPORTER=30,
    QA_BOT_INV_MEDKIT=31, QA_BOT_INV_KAMIKAZE=32, QA_BOT_INV_PORTAL=33,
    QA_BOT_INV_INVULNERABILITY=34, QA_BOT_INV_QUAD=35, QA_BOT_INV_ENVIRO=36,
    QA_BOT_INV_HASTE=37, QA_BOT_INV_INVISIBILITY=38, QA_BOT_INV_REGEN=39,
    QA_BOT_INV_FLIGHT=40, QA_BOT_INV_SCOUT=41, QA_BOT_INV_GUARD=42,
    QA_BOT_INV_DOUBLER=43, QA_BOT_INV_AMMO_REGEN=44, QA_BOT_INV_RED_FLAG=45,
    QA_BOT_INV_BLUE_FLAG=46, QA_BOT_INV_NEUTRAL_FLAG=47, QA_BOT_INV_RED_CUBE=48,
    QA_BOT_INV_BLUE_CUBE=49, QA_BOT_INV_ENEMY_DISTANCE=200,
    QA_BOT_INV_ENEMY_HEIGHT=201, QA_BOT_INV_ENEMIES=202, QA_BOT_INV_TEAMMATES=203
};
typedef struct qa_bot_weapon_knowledge {
    qa_bot_weapon_info weapon;
    qa_bot_projectile_info projectile;
    double selected_projectile_damage; /* Selected source means may be fractional. */
    double selected_splash_damage, effect_damage;
    float effect_radius;
    float maximum_range;
    uint32_t travel_modes; /* Actual selected weapon travel, not its learned role. */
    int32_t personality_role; /* -1 derives a learned role from ballistics. */
    bool ranged_limit, melee;
    bool has_supply, owned, available;
    qa_item_id supply_weapon, supply_ammo; /* Ammo 0 denotes no consumable. */
    double ammo_per_shot;
    /* Native source muzzle vectors are relative to canonical actor origin;
     * a zero count preserves library eye-relative weapon offsets. */
    qa_vec3 muzzle_offsets[2];
    uint8_t muzzle_count;
    float launch_delay, gravity_acceleration, launch_yaw_offset;
    bool discharge, grapple, deployable, homing;
    bool conditional_strike, conditional_trajectory, thrown, has_cycle;
    bool timed_detonation, requires_release, ammo_reserved, pitch_clamped, range_from_bounds;
    /* Pure nominal law shared with the selected source's actual firing. The
     * copied observation owns every input; this callback retains no GAME state. */
    qa_vec3 (*launch_velocity)(const struct qa_bot_weapon_knowledge *,qa_vec3 angles);
} qa_bot_weapon_knowledge;
/* Low nominal arc before collision/bounce or an authored later phase. Uses
 * the selected firing law; it never simulates RNG, homing or a delegated shot. */
bool qa_bot_weapon_launch_aim(const qa_bot_weapon_knowledge *,qa_vec3 origin,
                              qa_vec3 target,qa_vec3 *angles,float *flight);
typedef struct qa_bot_weapon_tactics {
    bool melee, ranged_limit, predict_occluded_splash;
    float maximum_range, weakness;
    int32_t accuracy_characteristic, skill_characteristic; /* -1: use general. */
} qa_bot_weapon_tactics;
int32_t qa_bot_weapon_role(const qa_bot_weapon_knowledge *);
qa_bot_weapon_tactics qa_bot_weapon_tactics_for(const qa_bot_weapon_knowledge *);
/* Candidates preserve selected-arsenal order. No candidate becomes a source
 * weapon merely by sharing its learned personality role. The caller owns a
 * retained scratch inventory for each explicit foreign-weapon source slice.
 * Ordinary fuzzy reads borrow the actual little-endian source array. */
bool qa_bot_knowledge_choose(qa_bot_runtime *,uint32_t weapon_state,
                              const qa_bot_weapon_knowledge *,size_t,
                              qa_bot_inventory_bytes inventory,
                              int32_t scratch[QA_BOT_INVENTORY_SIZE],
                              const qa_bot_source_weapon_policy *,int32_t *,qa_error *);
int32_t qa_bot_knowledge_activation(const qa_bot_weapon_knowledge *,size_t,
                                    qa_bot_inventory_bytes inventory,bool team_arena);
int32_t qa_bot_knowledge_travel(const qa_bot_weapon_knowledge *,size_t,
                                qa_bot_inventory_bytes inventory,qa_nav_travel);
float qa_bot_knowledge_aggression(const qa_bot_weapon_knowledge *,size_t,int32_t current_weapon,
                                   qa_bot_inventory_bytes inventory);
/* Uses the same canonical preview receipts as actual pickup planning. */
bool qa_bot_knowledge_pickup(const qa_bot_weapon_knowledge *,size_t,
                              const qa_supply_preview_result *,double *,qa_error *);

#endif
