#ifndef QA_Q2_ITEMS_INTERNAL_H
#define QA_Q2_ITEMS_INTERNAL_H
#include "../internal.h"
#include "qa/game_q2_entities.h"
#include "qa/game_q2_items.h"

typedef enum q2_item_think {
    Q2_ITEM_IDLE,
    Q2_ITEM_FLOOR,
    Q2_ITEM_RESPAWN,
    Q2_ITEM_DROPPED,
    Q2_ITEM_EXPIRE,
    Q2_ITEM_MEGA
} q2_item_think;
typedef enum q2_companion_kind {
    Q2_COMPANION_NONE,
    Q2_SPHERE_DEFENDER,
    Q2_SPHERE_HUNTER,
    Q2_SPHERE_VENGEANCE,
    Q2_DOPPLEGANGER,
    Q2_DOPPLEGANGER_BODY
} q2_companion_kind;
typedef struct q2_item_state {
    const qa_q2_item_definition *definition;
    qa_q2_item_spawn spawn;
    qa_actor_id owner;
    uint64_t due_ns, expires_ns;
    q2_item_think think;
    bool targets_used, retained, visible, touchable, temporary, dispatching;
    uint32_t *picked_slots;
    size_t picked_count, picked_capacity;
    qa_q2_visual visual;
    struct q2_companion *companion;
    qa_pickups *observations;
    qa_pickup_lease observation;
} q2_item_state;
typedef struct q2_power_state {
    qa_q2_game *game;
    qa_actor_id actor, sphere;
    qa_q2_powerups values;
    qa_item_id cells;
    qa_inventory_lease definitions;
    float maximum_health;
    uint32_t power_cubes;
} q2_power_state;
typedef struct q2_companion {
    q2_companion_kind kind;
    qa_actor_id owner, enemy, child, credit;
    uint64_t expires_ns, attack_ns, next_ns, turn_ns;
    qa_vec3 goal;
    int frame;
    qa_string_id loop_sound;
    bool active, decoy, camera;
} q2_companion;
typedef struct q2_items {
    qa_q2_item_definition *definitions;
    size_t count;
    qa_item_definition *actions;
    qa_item_admission *admissions;
    size_t action_count;
    qa_q2_item_options options;
    uint32_t cubes;
    qa_string_id food_classname;
} q2_items;
static inline qa_string_id q2_item_classname(qa_q2_game *g, const qa_q2_item_definition *d) {
    return d->kind == QA_Q2_ITEM_FOOD ? g->item_runtime->food_classname : d->classname_id;
}

q2_power_state *q2_powers(qa_q2_game *, qa_actor_id, qa_error *);
bool q2_item_bind_actions(qa_q2_game *, qa_actor_id, q2_power_state *, qa_error *);
const qa_q2_item_definition *q2_item_by_id(qa_q2_game *, qa_item_id);
bool q2_supplemental_find(qa_q2_game *, const char *, bool names_only, qa_q2_supplemental_item *);
bool q2_item_console_pickup(qa_q2_game *, qa_actor_id, const qa_q2_item_definition *, qa_error *);
bool q2_item_catalog(qa_q2_game *, qa_error *);
bool q2_item_ensure(qa_q2_game *, qa_actor_id, const qa_q2_item_definition *, qa_error *);
bool q2_item_grant(qa_q2_game *, q2_actor *, qa_actor_id, bool *, qa_error *);
qa_pickup_offer q2_item_offer(qa_q2_game *, const q2_actor *, qa_actor_id);
bool q2_item_eligible(qa_q2_game *, q2_actor *, qa_actor_id, bool *, qa_error *);
bool q2_item_observe(qa_q2_game *, q2_actor *, qa_error *);
bool q2_item_armor_result(qa_q2_game *, const qa_q2_item_definition *,
                          const qa_regular_armor *, qa_regular_armor *);
bool q2_item_finish(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_item_sound(qa_q2_game *, qa_actor_id, const char *, qa_error *);
bool q2_item_visual(qa_q2_game *, q2_actor *, qa_error *);
bool q2_item_hide(qa_q2_game *, q2_actor *, q2_item_think, uint64_t, qa_error *);
bool q2_item_change_collision(qa_q2_game *, q2_actor *, qa_physics_solid, qa_error *);
bool q2_item_drop_definition(qa_q2_game *, qa_actor_id, const qa_q2_item_definition *,
                             const qa_q2_drop_options *, int, qa_actor_id *, qa_error *);
bool q2_item_randomize(qa_q2_game *, q2_actor **, qa_error *);
bool q2_companion_use(qa_q2_game *, qa_actor_id, const qa_q2_item_definition *, bool *, qa_error *);
bool q2_companion_tick(qa_q2_game *, q2_actor *, qa_error *);
bool q2_companion_touch(qa_q2_game *, const qa_touch_contact *, qa_error *);
bool q2_companion_reaction(qa_q2_game *, const qa_damage_outcome *, qa_error *);
bool q2_item_use_duration(qa_q2_game *, qa_actor_id, const qa_q2_item_definition *, uint64_t,
                          bool *, qa_error *);
bool q2_client_item_action(qa_q2_game *, qa_actor_id, bool flashlight, bool *, qa_error *);
bool q2_client_item_received(qa_q2_game *, qa_actor_id, const qa_q2_item_definition *, qa_error *);
bool q2_client_slot(qa_q2_game *, qa_actor_id, uint32_t *);
bool q2_client_sphere_camera(qa_q2_game *, qa_actor_id, qa_actor_id, qa_vec3, qa_vec3, qa_error *);
bool q2_publish_visual(qa_q2_game *, qa_actor_id, const qa_q2_visual *, qa_error *);
bool q2_map_event(qa_q2_game *, const qa_q2_map_event *, qa_error *);
static inline uint64_t q2_item_seconds(float seconds) {
    double nanoseconds = (double)seconds * Q2_NS;
    return nanoseconds <= 0 ? 0 : nanoseconds >= 0x1p64 ? UINT64_MAX : (uint64_t)nanoseconds;
}
static inline bool q2_item_supply(qa_q2_game *g, qa_actor_id id, qa_supply **out,
    qa_error *error) {
    qa_q2_item_options *options = &g->item_runtime->options;
    qa_supply *selected = NULL;
    if (options->supply_for && !options->supply_for(options->context, id, &selected, error))
        return false;
    *out = selected ? selected : options->supply;
    return true;
}
#endif
