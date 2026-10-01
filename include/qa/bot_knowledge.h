#ifndef QA_BOT_KNOWLEDGE_H
#define QA_BOT_KNOWLEDGE_H

#include "qa/bot_runtime.h"
#include "qa/inventory.h"

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
    float maximum_range;
    uint32_t travel_modes; /* Actual selected weapon travel, not its learned role. */
    int32_t personality_role; /* -1 derives a learned role from ballistics. */
    bool ranged_limit, melee;
    bool has_supply, owned;
    qa_item_id supply_weapon, supply_ammo; /* Ammo 0 denotes no consumable. */
    double ammo_per_shot;
    /* Native source muzzle vectors are relative to canonical actor origin;
     * a zero count preserves library eye-relative weapon offsets. */
    qa_vec3 muzzle_offsets[2];
    uint8_t muzzle_count;
    float launch_delay, gravity_acceleration;
} qa_bot_weapon_knowledge;
typedef struct qa_bot_weapon_tactics {
    bool melee, ranged_limit, predict_occluded_splash;
    float maximum_range, weakness;
    int32_t accuracy_characteristic, skill_characteristic; /* -1: use general. */
} qa_bot_weapon_tactics;
int32_t qa_bot_weapon_role(const qa_bot_weapon_knowledge *);
qa_bot_weapon_tactics qa_bot_weapon_tactics_for(const qa_bot_weapon_knowledge *);
/* Candidates preserve selected-arsenal order. No candidate becomes a source
 * weapon merely by sharing its learned personality role. The caller owns a
 * retained scratch inventory, distinct from its canonical observation. */
bool qa_bot_knowledge_choose(qa_bot_runtime *,uint32_t weapon_state,
                              const qa_bot_weapon_knowledge *,size_t,
                              const int32_t inventory[QA_BOT_INVENTORY_SIZE],
                              int32_t scratch[QA_BOT_INVENTORY_SIZE],int32_t *,qa_error *);
int32_t qa_bot_knowledge_activation(const qa_bot_weapon_knowledge *,size_t,
                                    const int32_t inventory[QA_BOT_INVENTORY_SIZE],bool team_arena);
int32_t qa_bot_knowledge_travel(const qa_bot_weapon_knowledge *,size_t,
                                const int32_t inventory[QA_BOT_INVENTORY_SIZE],qa_nav_travel);
float qa_bot_knowledge_aggression(const qa_bot_weapon_knowledge *,size_t,int32_t current_weapon,
                                   const int32_t inventory[QA_BOT_INVENTORY_SIZE]);
/* Uses the same canonical preview receipts as actual pickup planning. */
bool qa_bot_knowledge_pickup(const qa_bot_weapon_knowledge *,size_t,
                              const qa_supply_preview_result *,double *,qa_error *);

#endif
