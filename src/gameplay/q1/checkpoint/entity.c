#include "internal.h"

static bool projectile(q1_save_io *io, q1_projectile *p) {
    Q1_SAVE_ENUM(io, p->kind, Q1_FINAL_ROCK);
    Q1_SAVE_ENUM(io, p->weapon, QA_Q1_WEAPON_COUNT);
    if (!q1_save_attack(io, &p->attack))
        return false;
    Q1_SAVE(io, actor, p->enemy);
    Q1_SAVE(io, actor, p->activator);
    Q1_SAVE(io, actor, p->surface);
    for (size_t i = 0; i < 3; ++i)
        Q1_SAVE(io, actor, p->links[i]);
    Q1_SAVE(io, vector, p->right);
    Q1_SAVE(io, vector, p->movedir);
    Q1_SAVE(io, vector, p->launch_angles);
    Q1_SAVE(io, float, p->damage);
    Q1_SAVE(io, float, p->radius_damage);
    Q1_SAVE(io, double, p->expires);
    Q1_SAVE(io, u32, p->count);
    Q1_SAVE(io, bool, p->remove_touch);
    Q1_SAVE(io, bool, p->mini);
    Q1_SAVE(io, bool, p->detonating);
    return true;
}
static bool pickup(q1_save_io *io, q1_pickup *p) {
    Q1_SAVE(io, string, p->item);
    Q1_SAVE(io, string, p->original_model);
    Q1_SAVE(io, string, p->sound);
    Q1_SAVE(io, float, p->count);
    Q1_SAVE(io, float, p->respawn);
    Q1_SAVE(io, u32, p->kind);
    Q1_SAVE(io, u32, p->upgrade_flag);
    Q1_SAVE(io, u8, p->upgrade);
    Q1_SAVE(io, actor, p->holder);
    Q1_SAVE(io, bool, p->hidden);
    Q1_SAVE(io, bool, p->mega);
    Q1_SAVE(io, bool, p->artifact);
    Q1_SAVE(io, bool, p->mission);
    Q1_SAVE(io, bool, p->random);
    Q1_SAVE(io, bool, p->external);
    Q1_SAVE(io, bool, p->backpack_rank);
    Q1_SAVE(io, bool, p->avoid_underwater_lightning);
    Q1_SAVE(io, float, p->absorption);
    Q1_SAVE(io, float, p->duration);
    Q1_SAVE_ENUM(io, p->weapon, QA_Q1_WEAPON_COUNT);
    Q1_SAVE_ENUM(io, p->drop, Q1_DROP_ROGUE_WEAPON);
    Q1_SAVE(io, float, p->owner_delay);
    for (size_t i = 0; i < QA_Q1_AMMO_COUNT; ++i)
        Q1_SAVE(io, float, p->ammo[i]);
    return true;
}
static bool timer(q1_save_io *io, q1_actor *a) {
    if (!a->classname)
        return true;
    const char *name = qa_strings_cstr(qa_session_strings(io->game->services.session), a->classname);
    if (!name)
        return q1_save_fail(io, "Invalid Q1 timer classname");
    if (!strcmp(name, "wizard_fastfire") || !strcmp(name, "hipnotic_hammer_strike") ||
        !strcmp(name, "hipnotic_mjolnir_base") || !strcmp(name, "hipnotic_mjolnir_lightning"))
        return projectile(io, &a->state.projectile);
    if (!strcmp(name, "hip_multi_explosion") || !strcmp(name, "func_multi_exploder")) {
        Q1_SAVE(io, double, a->state.effect.expires);
        Q1_SAVE(io, float, a->state.effect.volume);
    } else if (!strcmp(name, "armagon_body"))
        Q1_SAVE(io, u32, a->state.effect.count);
    /* Sprite explosions reuse a projectile slot, but its old union is dead. */
    return true;
}
static bool continuation(q1_save_io *io, const q1_actor *a, q1_think_kind think) {
    bool valid = true;
    switch (think) {
    case Q1_THINK_MONSTER_START:
    case Q1_THINK_MONSTER_FRAME:
    case Q1_THINK_MONSTER_FOUND:
    case Q1_THINK_HEAVY_SOURCE_DIE:
        valid = a->kind == Q1_MONSTER;
        break;
    case Q1_THINK_BOSS_CHILD:
        valid = a->kind == Q1_BOSS_CHILD;
        break;
    case Q1_THINK_RESPAWN:
    case Q1_THINK_MEGA_ROT:
    case Q1_THINK_ITEM_PLACE:
    case Q1_THINK_MG3_ITEM_START:
        valid = a->kind == Q1_PICKUP;
        break;
    case Q1_THINK_EXPLODE:
    case Q1_THINK_VORE:
    case Q1_THINK_HIP_LASER:
    case Q1_THINK_PROX_WATCH:
    case Q1_THINK_PROX_EXPLODE:
    case Q1_THINK_MULTI_SPLIT:
    case Q1_THINK_MINI_EXPLODE:
    case Q1_THINK_MULTI_EXPLODE:
    case Q1_THINK_MULTI_ACQUIRE:
    case Q1_THINK_MULTI_HOME:
    case Q1_THINK_PLASMA_LAUNCH:
    case Q1_THINK_SPHERE_ORBIT:
    case Q1_THINK_SPHERE_ATTACK:
    case Q1_THINK_HOOK_FLY:
    case Q1_THINK_HOOK_TRACK:
    case Q1_THINK_HOOK_RESET:
    case Q1_THINK_WRATH_HOME:
    case Q1_THINK_WRATH_EXPLODE:
    case Q1_THINK_DEMODOG_EXPLODE:
    case Q1_THINK_HOMING_FLAME:
        valid = a->kind == Q1_PROJECTILE;
        break;
    case Q1_THINK_WIZARD:
    case Q1_THINK_AXE:
    case Q1_THINK_DEATH_BUBBLES:
    case Q1_THINK_BUBBLE:
    case Q1_THINK_HAMMER_STRIKE:
    case Q1_THINK_HAMMER_BOLT:
    case Q1_THINK_SHIELD:
    case Q1_THINK_HOOK_LINK:
    case Q1_THINK_SCOURGE_TRIGGER:
    case Q1_THINK_TELEPORT_FOG:
    case Q1_THINK_ARMAGON_BODY:
    case Q1_THINK_ARMAGON_EXPLOSION:
    case Q1_THINK_HOOK_LAUNCH:
    case Q1_THINK_MG3_HAMMER:
    case Q1_THINK_GHOST_BUBBLES:
        valid = a->kind == Q1_TIMER;
        break;
    case Q1_THINK_MULTI_EXPLOSION:
        valid = a->kind == Q1_TIMER || a->kind == Q1_MAP;
        break;
    case Q1_THINK_NONE:
    case Q1_THINK_REMOVE:
    case Q1_THINK_SPRITE:
    case Q1_THINK_MAP:
    case Q1_THINK_HORDE_HEAD_WAIT:
    case Q1_THINK_HORDE_HEAD_STEP:
    case Q1_THINK_SPAWN_TEMPLATE:
        break;
    }
    return valid || q1_save_fail(io, "Q1 saved think disagrees with its actor continuation");
}
bool q1_save_entity(q1_save_io *io, q1_actor *a) {
    Q1_SAVE(io, owned_actor, a->id);
    Q1_SAVE(io, actor, a->owner);
    Q1_SAVE(io, actor, a->activator);
    Q1_SAVE(io, string, a->classname);
    Q1_SAVE(io, string, a->model);
    Q1_SAVE(io, string, a->target);
    Q1_SAVE(io, string, a->targetname);
    Q1_SAVE(io, string, a->killtarget);
    Q1_SAVE(io, string, a->message);
    Q1_SAVE(io, vector, a->initial_angles);
    if (!q1_save_physics(io, &a->physics))
        return false;
    Q1_SAVE_ENUM(io, a->kind, Q1_BOSS_CHILD);
    Q1_SAVE_ENUM(io, a->think, Q1_THINK_SPAWN_TEMPLATE);
    Q1_SAVE(io, double, a->next_think);
    Q1_SAVE_ENUM(io, a->frozen.think, Q1_THINK_SPAWN_TEMPLATE);
    Q1_SAVE(io, double, a->frozen.next_think);
    Q1_SAVE(io, bool, a->frozen.active);
    Q1_SAVE(io, bool, a->frozen.damageable);
    if (a->frozen.active && (a->think != Q1_THINK_NONE || a->next_think != -1))
        return q1_save_fail(io, "Frozen Q1 actor retained a running continuation");
    Q1_SAVE(io, float, a->max_health);
    Q1_SAVE(io, float, a->delay);
    Q1_SAVE(io, float, a->wait);
    Q1_SAVE(io, float, a->speed);
    Q1_SAVE(io, float, a->damage);
    Q1_SAVE(io, float, a->count);
    Q1_SAVE(io, float, a->alpha);
    Q1_SAVE(io, float, a->scale);
    Q1_SAVE(io, u32, a->spawnflags);
    Q1_SAVE(io, u32, a->effects);
    Q1_SAVE(io, i32, a->frame);
    Q1_SAVE(io, i32, a->skin);
    Q1_SAVE(io, bool, a->native);
    Q1_SAVE(io, bool, a->aimed_damage);
    Q1_SAVE(io, bool, a->consumed_corpse);
    Q1_SAVE(io, bool, a->axe_hit);
    Q1_SAVE(io, bool, a->touch_disabled);
    uint64_t observation = io->reading ? 0 :
        (qa_pickups_observation_current(io->game->services.pickups, a->pickup_observation)
             ? a->pickup_observation.serial : 0);
    Q1_SAVE(io, u64, observation);
    if (observation && (a->kind != Q1_PICKUP || !a->native ||
        (!io->reading && !qa_actor_id_equal(a->id, a->pickup_observation.actor))))
        return q1_save_fail(io, "Q1 pickup lease has no matching native continuation");
    if (io->reading && observation)
        a->pickup_observation = (qa_pickup_lease){.actor = a->id, .serial = observation};
    switch (a->kind) {
    case Q1_MONSTER:
        if (!q1_save_monster(io, &a->state.monster))
            return false;
        break;
    case Q1_PROJECTILE:
        if (!projectile(io, &a->state.projectile))
            return false;
        break;
    case Q1_PICKUP:
        if (!pickup(io, &a->state.pickup))
            return false;
        break;
    case Q1_BOSS_CHILD:
        Q1_SAVE_ENUM(io, a->state.boss_child.kind, Q1_CHILD_FINAL_END);
        Q1_SAVE(io, actor, a->state.boss_child.enemy);
        Q1_SAVE(io, float, a->state.boss_child.sign);
        Q1_SAVE(io, i32, a->state.boss_child.maximum);
        Q1_SAVE(io, double, a->state.boss_child.sound_after);
        Q1_SAVE(io, bool, a->state.boss_child.reactive);
        Q1_SAVE(io, bool, a->state.boss_child.counted_death);
        break;
    case Q1_TIMER:
    case Q1_MAP:
        if (!timer(io, a))
            return false;
        break;
    case Q1_ENTITY:
    case Q1_GIB:
        break;
    }
    if (!continuation(io, a, a->think) ||
        (a->frozen.active && !continuation(io, a, a->frozen.think)))
        return false;
    if (io->reading)
        a->active = true;
    return true;
}
