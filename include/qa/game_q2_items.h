#ifndef QA_GAME_Q2_ITEMS_H
#define QA_GAME_Q2_ITEMS_H
#include "qa/game_q2.h"
#include "qa/game_q2_entities.h"

typedef enum qa_q2_item_kind {
    QA_Q2_ITEM_AMMO,
    QA_Q2_ITEM_WEAPON,
    QA_Q2_ITEM_HEALTH,
    QA_Q2_ITEM_ARMOR,
    QA_Q2_ITEM_SHARD,
    QA_Q2_ITEM_POWER,
    QA_Q2_ITEM_POWER_ARMOR,
    QA_Q2_ITEM_MAX_HEALTH,
    QA_Q2_ITEM_KEY,
    QA_Q2_ITEM_PACK,
    QA_Q2_ITEM_SPHERE,
    QA_Q2_ITEM_DECOY,
    QA_Q2_ITEM_NUKE,
    QA_Q2_ITEM_FOOD,
    QA_Q2_ITEM_COMPASS,
    QA_Q2_ITEM_FLASHLIGHT
} qa_q2_item_kind;
typedef enum qa_q2_powerup {
    QA_Q2_POWER_QUAD,
    QA_Q2_POWER_INVULNERABILITY,
    QA_Q2_POWER_BREATHER,
    QA_Q2_POWER_ENVIRO,
    QA_Q2_POWER_QUADFIRE,
    QA_Q2_POWER_DOUBLE,
    QA_Q2_POWER_IR,
    QA_Q2_POWER_INVISIBILITY,
    QA_Q2_POWER_SILENCER,
    QA_Q2_POWER_COUNT
} qa_q2_powerup;
typedef struct qa_q2_powerups {
    uint64_t quad_until_ns, invulnerability_until_ns, breather_until_ns, enviro_until_ns;
    uint64_t quad_fire_until_ns, double_until_ns, ir_until_ns, invisibility_until_ns;
} qa_q2_powerups;
typedef enum qa_q2_console_give {
    QA_Q2_GIVE_PICKUP,
    QA_Q2_GIVE_INVENTORY_ONLY,
    QA_Q2_GIVE_INDIVIDUAL_ONLY,
    QA_Q2_GIVE_FORBIDDEN
} qa_q2_console_give;
typedef struct qa_q2_supplemental_item {
    qa_item_definition definition;
    const char *classname;
    double capacity;
    qa_q2_console_give console_give;
} qa_q2_supplemental_item;
enum qa_q2_item_rule_flags {
    QA_Q2_ITEM_POWER_CUBE = 1u << 0,
    QA_Q2_ITEM_EXPLOSIVE_CHARGES = 1u << 1,
    QA_Q2_ITEM_PROX_AMMO = 1u << 2,
    QA_Q2_ITEM_TESLA_AMMO = 1u << 3,
    QA_Q2_ITEM_TRAP_AMMO = 1u << 4,
    QA_Q2_ITEM_DISRUPTOR_AMMO = 1u << 5,
    QA_Q2_ITEM_COMMANDER_HEAD = 1u << 6,
    QA_Q2_ITEM_SPHERE_DEFENDER = 1u << 7,
    QA_Q2_ITEM_SPHERE_HUNTER = 1u << 8,
    QA_Q2_ITEM_SPHERE_VENGEANCE = 1u << 9
};
typedef struct qa_q2_item_definition {
    const char *classname, *name, *model, *icon, *sound;
    qa_q2_item_kind kind;
    qa_item_id item, ammo;
    qa_string_id classname_id;
    uint32_t rule_flags, disabled_weapon_mask;
    qa_q2_weapon weapon;
    int quantity, capacity;
    float respawn_seconds, normal_protection, energy_protection;
    qa_q2_powerup powerup;
    qa_power_kind powered_armor;
    bool rotate, coop_stay, droppable, ignore_maximum, timed, fill, full_pack;
    bool infinite_quantity;
    qa_q2_console_give console_give;
} qa_q2_item_definition;
typedef struct qa_q2_item_options {
    qa_supply *supply;
    bool instanced_coop, random_items, no_mines, no_nukes, no_spheres, hunter_camera;
    float weapon_respawn_seconds;
    void *context;
    bool (*supply_for)(void *, qa_actor_id, qa_supply **, qa_error *);
    bool (*player_slot)(void *, qa_actor_id, uint32_t *slot);
    bool (*visibility)(void *, qa_actor_id player, qa_actor_id pickup, bool visible, qa_error *);
    bool (*intermission)(void *);
    bool (*sphere_camera)(void *, qa_actor_id player, qa_actor_id sphere, qa_vec3 origin,
                          qa_vec3 angles, qa_error *);
    /* Read-only catalog entries borrow their strings. Their provider owns pickup
     * behavior and publishes use/drop actions to the shared inventory. */
    size_t (*supplemental_count)(void *);
    bool (*supplemental_item)(void *, size_t, qa_q2_supplemental_item *);
    bool (*supplemental_give)(void *, qa_actor_id, qa_item_id, bool direct, int count, bool *,
                              qa_error *);
} qa_q2_item_options;
typedef struct qa_q2_item_spawn {
    const char *classname;
    uint32_t spawnflags;
    int count;
    qa_string_id target, killtarget, message, team;
    float delay;
    qa_actor_reference team_master, team_next;
} qa_q2_item_spawn;
typedef struct qa_q2_drop_options {
    bool immediate_touch, player_death;
    float yaw_offset;
    uint64_t expires_ns;
} qa_q2_drop_options;

/* Definitions borrow the provider's immutable catalog. Supply is borrowed and
 * keeps foreign arsenal selection in the shared inventory/pickup services. */
bool qa_q2_items_configure(qa_q2_game *, const qa_q2_item_options *, qa_error *);
size_t qa_q2_item_count(const qa_q2_game *);
const qa_q2_item_definition *qa_q2_item_at(const qa_q2_game *, size_t);
const qa_q2_item_definition *qa_q2_item_lookup(const qa_q2_game *, const char *);
bool qa_q2_items_admit_player(qa_q2_game *, qa_actor_id, bool give_blaster, qa_error *);
bool qa_q2_item_spawn_actor(qa_q2_game *, qa_actor_id, const qa_q2_item_spawn *, bool *handled,
                            qa_error *);
bool qa_q2_item_use(qa_q2_game *, qa_actor_id, qa_item_id, bool *used, qa_error *);
bool qa_q2_item_enable(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_item_give(qa_q2_game *, qa_actor_id, const char *, int count, bool *accepted,
                     qa_error *);
bool qa_q2_item_drop(qa_q2_game *, qa_actor_id, qa_item_id, const qa_q2_drop_options *,
                     qa_actor_id *out, bool *dropped, qa_error *);
bool qa_q2_item_drop_monster(qa_q2_game *, qa_actor_id, const char *, qa_actor_id *, bool *,
                             qa_error *);
bool qa_q2_items_start(qa_q2_game *, qa_actor_id, const char *expression, qa_error *);
bool qa_q2_powerups_read(qa_q2_game *, qa_actor_id, qa_q2_powerups *, qa_error *);
bool qa_q2_powerups_clear(qa_q2_game *, qa_actor_id, qa_error *);
/* Replace quad expiration using the current Q2 source clock. */
bool qa_q2_player_quad(qa_q2_game *, qa_actor_id, uint64_t duration_ns, qa_error *);
/* Extend the actual Quad deadline; the invoking source owns activation audio. */
bool qa_q2_player_quad_stack(qa_q2_game *, qa_actor_id, uint64_t duration_ns, qa_error *);
bool qa_q2_items_publish_visibility(qa_q2_game *, qa_actor_id, qa_error *);
bool qa_q2_item_visible_to(qa_q2_game *, qa_actor_id pickup, qa_actor_id player,
    bool *, qa_error *);
bool qa_q2_pickups_rebind(qa_q2_game *, qa_error *);
typedef struct qa_q2_companion_checkpoint {
    uint32_t kind;
    qa_actor_reference owner, enemy, child, credit;
    uint64_t expires_ns, attack_ns, next_ns, turn_ns;
    qa_vec3 goal;
    int frame;
    qa_string_id loop_sound;
    bool active, decoy, camera;
} qa_q2_companion_checkpoint;
typedef struct qa_q2_item_checkpoint {
    bool present, powers_present;
    qa_string_id definition;
    qa_q2_item_spawn spawn;
    qa_actor_reference owner;
    qa_q2_saved_reference sphere;
    uint64_t due_ns, expires_ns;
    uint32_t think;
    bool targets_used, retained, visible, touchable, temporary;
    uint32_t *picked_slots;
    size_t picked_count;
    qa_entity_visual visual;
    qa_q2_companion_checkpoint companion;
    qa_q2_powerups powers;
    float maximum_health;
    uint32_t power_cubes;
    bool definitions_bound, power_inventory_bound;
} qa_q2_item_checkpoint;
typedef struct qa_q2_items_checkpoint {
    uint32_t cubes;
} qa_q2_items_checkpoint;
/* Shared inventory/combat and configured services precede restoration into a
 * prepared candidate. This state owns picked_slots; encode fields and remap
 * resource identities. spawn.classname and live actor IDs are always cleared.
 */
bool qa_q2_item_capture(qa_q2_game *, qa_actor_id, qa_q2_item_checkpoint *, qa_error *);
bool qa_q2_item_restore(qa_q2_game *, qa_actor_id, const qa_q2_item_checkpoint *, qa_error *);
void qa_q2_item_checkpoint_free(qa_q2_item_checkpoint *);
bool qa_q2_items_capture(qa_q2_game *, qa_q2_items_checkpoint *, qa_error *);
bool qa_q2_items_restore(qa_q2_game *, const qa_q2_items_checkpoint *, qa_error *);
#endif
