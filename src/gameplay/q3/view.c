#include "internal.h"

bool qa_q3_entity_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_entity_view *out,
                       qa_error *error) {
    const q3_actor *entry = q3_actor_const(game, actor);
    if (!entry || !out)
        return q3_fail(error, "missing Q3 entity view");
    qa_q3_entity_view view = {.actor = actor, .source_number = q3_entity_number(game, actor)};
    if (!qa_world_body_read(game->options.services.world, actor, &view.body, error))
        return false;
    view.position = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = view.body.origin};
    view.angular = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = view.body.angles};
    switch (entry->kind) {
    case Q3_ACTOR_PLAYER: {
        const qa_q3_player_state *player = &entry->state.player;
        view.kind = player->gibbed || player->spectator ? QA_Q3_ENTITY_HIDDEN : QA_Q3_ENTITY_PLAYER;
        view.flags = player->flags;
        view.selections = player->selections;
        view.weapon = player->weapon;
        view.legs_animation = player->legs_animation;
        view.torso_animation = player->torso_animation;
        view.loop_sound = player->loop_sound;
        view.angular.base = player->view_angles;
        view.position.delta = view.body.velocity;
        view.angular.type = view.position.type = QA_TRAJECTORY_INTERPOLATE;
        for (unsigned i = 1; i < QA_Q3_POWERUP_COUNT; ++i)
            if (player->powerups[i])
                view.powerups |= 1u << i;
        break;
    }
    case Q3_ACTOR_MISSILE: {
        const q3_missile *missile = &entry->state.missile;
        view.kind = missile->phase == Q3_MISSILE_HOOK ? QA_Q3_ENTITY_GRAPPLE
                    : missile->phase == Q3_MISSILE_EVENT || missile->phase == Q3_MISSILE_PROX_PLAYER
                        ? QA_Q3_ENTITY_HIDDEN
                        : QA_Q3_ENTITY_MISSILE;
        view.weapon = missile->weapon;
        view.flags = missile->flags;
        view.position = missile->trajectory;
        view.owner = missile->owner;
        view.attachment = missile->attached;
        view.loop_sound = missile->loop_sound;
        view.time_ms = missile->trajectory.time_ms;
        view.expire_ms = missile->think_at;
        break;
    }
    case Q3_ACTOR_ITEM: {
        const q3_item_state *item = &entry->state.item;
        view.kind = item->hidden ? QA_Q3_ENTITY_HIDDEN : QA_Q3_ENTITY_ITEM;
        view.item_index = item->spawn.item_index;
        view.position = item->trajectory;
        view.expire_ms = item->expire_at;
        size_t count;
        const qa_q3_item *table = qa_q3_items(game->options.product, &count);
        if (view.item_index < count) {
            view.model = table[view.item_index].model;
            view.secondary_model = table[view.item_index].secondary_model;
        }
        break;
    }
    case Q3_ACTOR_MOVER:
        view.kind = QA_Q3_ENTITY_MOVER;
        view.position = entry->state.mover.state.position;
        view.angular = entry->state.mover.state.angular;
        break;
    case Q3_ACTOR_PORTAL:
        view.kind = QA_Q3_ENTITY_PORTAL;
        view.owner = entry->state.portal.owner;
        view.attachment = entry->state.portal.destination;
        view.expire_ms = entry->state.portal.expire_at;
        view.model = entry->state.portal.source ? "models/powerups/teleporter/tele_enter.md3"
                                                : "models/powerups/teleporter/tele_exit.md3";
        break;
    case Q3_ACTOR_CORPSE:
        view.flags = entry->state.corpse.flags;
        view.kind = (view.flags & 0x80u) ? QA_Q3_ENTITY_HIDDEN : QA_Q3_ENTITY_CORPSE;
        view.owner = entry->state.corpse.player;
        view.position = entry->state.corpse.trajectory;
        view.legs_animation = view.torso_animation = entry->state.corpse.animation;
        view.time_ms = entry->state.corpse.timestamp;
        break;
    case Q3_ACTOR_KAMIKAZE:
        view.kind = QA_Q3_ENTITY_KAMIKAZE;
        view.owner = entry->state.kamikaze.attacker;
        view.time_ms = entry->state.kamikaze.start;
        break;
    case Q3_ACTOR_NONE:
    case Q3_ACTOR_PROX_TRIGGER:
    case Q3_ACTOR_KAMIKAZE_TIMER:
        break;
    }
    *out = view;
    return true;
}
