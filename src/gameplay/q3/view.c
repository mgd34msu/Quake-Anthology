#include "map/internal.h"

static qa_trajectory source_trajectory(const qa_q3_trajectory *source) {
    return (qa_trajectory){.type = (qa_trajectory_type)source->type,
        .time_ms = source->time, .duration_ms = source->duration,
        .base = qa_v3(source->base[0], source->base[1], source->base[2]),
        .delta = qa_v3(source->delta[0], source->delta[1], source->delta[2])};
}

static bool source_model(const qa_q3_game *game, int32_t index, const char **out,
                           qa_error *error) {
    if (index < 0 || index >= 256)
        return q3_fail(error, "Q3 Source presentation model index is out of range");
    *out = index ? game->configstrings[32 + index] : NULL;
    return true;
}

static bool source_only_view(const qa_q3_game *game, uint32_t slot,
                               const qa_q3_map_actor_state *map,
                               qa_q3_entity_view *view, qa_error *error) {
    qa_q3_source_binding binding;
    if (!qa_q3_source_binding_read(game, slot, &binding, error)) return false;
    if (!binding.in_use || !binding.body_attached ||
        !qa_actor_id_equal(binding.actor, view->actor))
        return q3_fail(error, "Q3 presentation lost its actual Source owner");
    const qa_q3_entity *s = &view->source_entity;
    if (s->eType >= 13) {
        /* CG_AddCEntity leaves freestanding events to the event consumer. */
        view->kind = QA_Q3_ENTITY_HIDDEN;
    } else {
        switch (s->eType) {
        case 0:
            view->kind = s->modelindex ? QA_Q3_ENTITY_GENERAL : QA_Q3_ENTITY_HIDDEN;
            if (!source_model(game, s->modelindex, &view->model, error)) return false;
            break;
        case 4:
            view->kind = QA_Q3_ENTITY_MOVER;
            /* SOLID_BMODEL uses the actual inline-model collision owner, not
             * the CS_MODELS namespace. The complete ES retains its index. */
            if (s->solid != 0xffffff &&
                !source_model(game, s->modelindex, &view->model, error)) return false;
            if (!source_model(game, s->modelindex2, &view->secondary_model, error)) return false;
            break;
        case 5: view->kind = QA_Q3_ENTITY_BEAM; break;
        case 6: view->kind = QA_Q3_ENTITY_PORTAL; break;
        case 7: view->kind = QA_Q3_ENTITY_SPEAKER; break;
        case 8: case 9: case 10: view->kind = QA_Q3_ENTITY_HIDDEN; break;
        default:
            return q3_fail(error, "Q3 Source presentation lacks its typed gameplay owner");
        }
    }
    view->alpha = map ? map->alpha : 1;
    view->flags = (uint32_t)s->eFlags;
    view->constant_light = (uint32_t)s->constantLight;
    view->position = source_trajectory(&s->pos);
    view->angular = source_trajectory(&s->apos);
    /* Beam/portal endpoints and speaker scheduling/sound indices remain in
     * the raw ES, consumed by the existing native packet renderer/audio owner. */
    return true;
}

static bool entity_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_entity_view *out,
                         qa_error *error) {
    const q3_actor *entry = q3_actor_const(game, actor);
    const qa_q3_map_actor_state *map = entry ? NULL : q3_map_const(game, actor);
    bool native_owner = entry != NULL;
    bool mover_owner = map && map->kind >= QA_Q3_MAP_MOVER_DOOR &&
        map->kind <= QA_Q3_MAP_MOVER_PENDULUM;
    uint32_t source_slot;
    bool source = qa_q3_source_actor_slot(game, actor, &source_slot, NULL);
    if (!entry && !source && (!map || map->kind < QA_Q3_MAP_MOVER_DOOR ||
                   map->kind > QA_Q3_MAP_MOVER_PENDULUM ||
                   (!q3_map_text(game, map->model) && !q3_map_text(game, map->model2))))
        return q3_fail(error, "missing Q3 entity view");
    qa_q3_entity_view view = {.actor = actor, .source_number = q3_entity_number(game, actor),
                             .source_client = -1};
    if (!qa_world_body_read(game->options.services.world, actor, &view.body, error))
        return false;
    if (source) {
        qa_q3_wire_visibility visibility;
        uint32_t actual_slot;
        if (!qa_q3_wire_entity_read(game, source_slot, &view.source_entity, &visibility, error) ||
            !qa_q3_source_actor_slot(game, actor, &actual_slot, error)) return false;
        if (actual_slot != source_slot)
            return q3_fail(error, "Q3 presentation lost its actual source row during observation");
        view.has_source_entity = true;
        view.source_number = view.source_entity.number;
    }
    view.position = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = view.body.origin};
    view.angular = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = view.body.angles};
    entry = q3_actor_const(game, actor);
    map = entry ? NULL : q3_map_const(game, actor);
    if (view.has_source_entity && view.source_entity.eType == 12) {
        const qa_q3_entity *s = &view.source_entity;
        view.kind = QA_Q3_ENTITY_TEAM;
        view.alpha = entry ? entry->alpha : map ? map->alpha : 1;
        view.flags = (uint32_t)s->eFlags;
        view.constant_light = (uint32_t)s->constantLight;
        view.position = source_trajectory(&s->pos);
        view.angular = source_trajectory(&s->apos);
        *out = view;
        return true;
    }
    if (!entry && !native_owner && !mover_owner && view.has_source_entity) {
        if (!source_only_view(game, source_slot, map, &view, error)) return false;
        *out = view;
        return true;
    }
    if (!entry) {
        if (!map || map->kind < QA_Q3_MAP_MOVER_DOOR ||
            map->kind > QA_Q3_MAP_MOVER_PENDULUM ||
            (!q3_map_text(game, map->model) && !q3_map_text(game, map->model2)))
            return q3_fail(error, "Q3 authored presentation owner changed during observation");
        view.kind = map->linked ? QA_Q3_ENTITY_MOVER : QA_Q3_ENTITY_HIDDEN;
        view.alpha = map->alpha;
        q3_map_mover_presentation(game, actor, &view.model, &view.secondary_model,
                                  &view.constant_light);
        *out = view;
        return true;
    }
    entry = q3_actor_const(game, actor);
    if (!entry)
        return q3_fail(error, "Q3 presentation actor retired during observation");
    view.alpha = entry->alpha;
    switch (entry->kind) {
    case Q3_ACTOR_PODIUM: case Q3_ACTOR_VICTORY_MODEL: {
        const qa_q3_entity *s = &entry->state.postgame.entity;
        view.kind = entry->kind == Q3_ACTOR_PODIUM ? QA_Q3_ENTITY_MOVER : QA_Q3_ENTITY_PLAYER;
        view.flags = (uint32_t)s->eFlags;
        view.powerups = (uint32_t)s->powerups;
        view.weapon = (qa_q3_weapon)s->weapon;
        view.legs_animation = s->legsAnim; view.torso_animation = s->torsoAnim;
        view.position = source_trajectory(&s->pos);
        view.angular = source_trajectory(&s->apos);
        if (entry->kind == Q3_ACTOR_PODIUM && s->modelindex > 0 && s->modelindex <= 255)
            view.model = game->configstrings[32 + s->modelindex];
        uint32_t client;
        if (q3_source_client_pointer(game, actor, &client)) {
            view.source_client = s->clientNum;
            view.owner = game->source_entities[client].body_attached
                ? game->source_entities[client].actor : (qa_actor_id){0};
        }
        break;
    }
    case Q3_ACTOR_PLAYER: {
        const qa_q3_player_state *player = &entry->state.player;
        view.kind = player->gibbed || player->spectator ? QA_Q3_ENTITY_HIDDEN : QA_Q3_ENTITY_PLAYER;
        uint32_t client;
        if (q3_source_client_pointer(game, actor, &client)) view.source_client = player->client_number;
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
        view.loop_sound = entry->state.mover.loop_sound;
        q3_map_mover_presentation(game, actor, &view.model, &view.secondary_model,
                                  &view.constant_light);
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
    case Q3_ACTOR_TEMPORARY:
    case Q3_ACTOR_OBELISK:
        break;
    }
    if (view.has_source_entity && (entry->kind == Q3_ACTOR_PLAYER ||
        entry->kind == Q3_ACTOR_CORPSE || entry->kind == Q3_ACTOR_VICTORY_MODEL))
        view.source_client = view.source_entity.clientNum;
    *out = view;
    return true;
}

bool qa_q3_entity_read(const qa_q3_game *game, qa_actor_id actor, qa_q3_entity_view *out,
                       qa_error *error) {
    if (!game || !out || game->observation_depth == SIZE_MAX)
        return q3_fail(error, "invalid Q3 presentation observation");
    qa_q3_game *retained = (qa_q3_game *)game;
    ++retained->observation_depth;
    bool okay = entity_read(game, actor, out, error);
    --retained->observation_depth;
    return okay;
}
