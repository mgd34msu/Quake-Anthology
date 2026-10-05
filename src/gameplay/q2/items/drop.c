#include "../entities/internal.h"
#include "internal.h"
#include <ctype.h>
#include <errno.h>

bool q2_item_drop_definition(qa_q2_game *g, qa_actor_id owner, const qa_q2_item_definition *d,
                             const qa_q2_drop_options *options, int count, qa_actor_id *out,
                             qa_error *e) {
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, owner, &body, e))
        return false;
    q2_actor *source = owner.slot < g->capacity ? g->actors[owner.slot] : NULL;
    qa_vec3 angles = source && qa_actor_id_equal(source->id, owner) && source->weapon_bound
                         ? source->input.angles
                         : body.angles;
    angles.y += options->yaw_offset;
    qa_vec3 forward;
    qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
    qa_bounds bounds = {{-15, -15, -15}, {15, 15, 15}};
    qa_vec3 origin = body.origin;
    qa_builtin_actor_traits traits = {0};
    if (g->services.actor_traits && g->services.actor_traits(g->services.context, owner, &traits) &&
        traits.player) {
        if (!q2_actor_live(g, owner))
            return false;
        qa_trace_query query = {
            .start = body.origin,
            .end = qa_vec_add(qa_vec_add(body.origin, qa_vec_scale(forward, 24)), qa_v3(0, 0, -16)),
            .shape = {.kind = QA_SHAPE_BOX, .bounds = bounds},
            .pass_actor = owner,
            .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
        query.policy.contents_mask = 1;
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, e))
            return false;
        origin = trace.end;
    }
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, d->classname, &definition, e))
        return false;
    qa_vec3 velocity = qa_vec_scale(forward, 100);
    velocity.z = 300;
    qa_builtin_spawn spawn = {
        .owner = g->options.owner,
        .definition = definition,
        .body = {.origin = origin, .angles = body.angles, .velocity = velocity, .bounds = bounds}};
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(&g->services, &spawn, &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    if (!a)
        goto fail;
    a->item = calloc(1, sizeof(*a->item));
    if (!a->item) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating dropped Q2 pickup");
        goto fail;
    }
    q2_item_state *item = a->item;
    item->definition = d;
    const qa_actor_record *reference = qa_actors_get(qa_session_actors(g->services.session), owner);
    item->owner = reference && reference->owner == g->options.owner && reference->has_source ?
        qa_actor_reference_source(reference->owner, reference->source_slot) : qa_actor_reference_lifetime(owner);
    item->spawn.classname = d->classname;
    item->spawn.count = count;
    item->spawn.spawnflags = options->player_death ? 0x20000 : 0x10000;
    item->expires_ns = options->expires_ns;
    item->visible = true;
    item->touchable = true;
    item->temporary = !options->immediate_touch;
    item->visual = (qa_q2_visual){.scale = 1,
                                  .alpha = 1,
                                  .old_frame = -1,
                                  .visible = true,
                                  .effects = d->rotate ? 1 : 0,
                                  .render_flags = 512 | 0x8000};
    if (!qa_builtin_resource(&g->services, d->model, &item->visual.models[0], e))
        goto fail;
    a->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    a->physics.q2_rerelease = g->options.edition == QA_Q2_RERELEASE;
    a->physics.motion = QA_PHYSICS_TOSS;
    a->physics_bound = true;
    if (item->temporary) {
        item->think = Q2_ITEM_DROPPED;
        item->due_ns = q2_deadline(g->now_ns, Q2_NS);
    } else if (item->expires_ns || g->options.deathmatch) {
        item->think = Q2_ITEM_EXPIRE;
        item->due_ns = item->expires_ns ? item->expires_ns : q2_deadline(g->now_ns, 29 * Q2_NS);
    }
    if (!q2_item_change_collision(g, a, QA_PHYSICS_TRIGGER, e))
        goto fail;
    if (!q2_actor_live(g, id)) {
        *out = (qa_actor_id){0};
        return true;
    }
    if (!q2_item_visual(g, a, e))
        goto fail;
    *out = q2_actor_live(g, id) ? id : (qa_actor_id){0};
    return true;
fail:
    if (q2_actor_live(g, id))
        qa_session_release(g->services.session, id, NULL);
    return false;
}
bool qa_q2_item_drop(qa_q2_game *g, qa_actor_id owner, qa_item_id id,
                     const qa_q2_drop_options *options, qa_actor_id *out, bool *dropped,
                     qa_error *e) {
    if (!g || !options || !out || !dropped || !isfinite(options->yaw_offset) ||
        !q2_actor_live(g, owner)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 item drop");
        return false;
    }
    *dropped = false;
    *out = (qa_actor_id){0};
    const qa_q2_item_definition *d = q2_item_by_id(g, id);
    if (!d || !d->droppable ||
        (g->options.cooperative && !g->item_runtime->options.instanced_coop && d->coop_stay))
        return true;
    int count;
    if (!q2_count(g, owner, d->item, &count, e))
        return false;
    if (count < 1)
        return true;
    if (!options->player_death && d->weapon != QA_Q2_WEAPON_NONE) {
        bool allowed;
        if (!qa_q2_weapon_can_drop(g, owner, d->weapon, &allowed, e))
            return false;
        if (!allowed)
            return true;
    }
    int amount = d->kind == QA_Q2_ITEM_AMMO ? (count < d->quantity ? count : d->quantity) : 1;
    if (!q2_item_drop_definition(g, owner, d, options, d->kind == QA_Q2_ITEM_AMMO ? amount : 0, out,
                                 e))
        return false;
    if (!out->registry)
        return true;
    if (!options->player_death) {
        bool consumed = false;
        bool ok = q2_actor_live(g, owner) &&
                  qa_inventory_consume(g->services.inventory, owner, d->item, amount, &consumed, e);
        if (!ok || !consumed) {
            if (q2_actor_live(g, *out))
                qa_session_release(g->services.session, *out, NULL);
            *out = (qa_actor_id){0};
            return ok;
        }
    }
    *dropped = true;
    return true;
}
bool qa_q2_item_drop_monster(qa_q2_game *g, qa_actor_id owner, const char *name, qa_actor_id *out,
                             bool *dropped, qa_error *e) {
    if (!g || !out || !dropped || !name) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 monster item drop");
        return false;
    }
    *dropped = false;
    *out = (qa_actor_id){0};
    const qa_q2_item_definition *d = qa_q2_item_lookup(g, name);
    if (!d)
        return true;
    if (!q2_item_drop_definition(g, owner, d, &(qa_q2_drop_options){0}, 0, out, e))
        return false;
    *dropped = out->registry != 0;
    return true;
}
bool q2_item_food_cube(qa_q2_game *g, qa_actor_id source, qa_vec3 origin, float scale, int health,
                       qa_vec3 velocity, qa_error *e) {
    (void)source;
    const qa_q2_item_definition *d = qa_q2_item_lookup(g, "item_foodcube");
    if (!d || !isfinite(scale) || scale <= 0 || !qa_vec_finite(origin) ||
        !qa_vec_finite(velocity)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 food cube");
        return false;
    }
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(
            &g->services,
            &(qa_builtin_spawn){.owner = g->options.owner,
                                .definition = g->item_runtime->food_classname,
                                .body = {.origin = origin, .velocity = velocity}},
            &id, e))
        return false;
    bool handled;
    if (!qa_q2_item_spawn_actor(
            g, id,
            &(qa_q2_item_spawn){.classname = d->classname, .count = health, .spawnflags = 0x10000},
            &handled, e))
        goto fail;
    if (!q2_actor_live(g, id))
        return true;
    q2_actor *a = q2_actor_get(g, id, false, e);
    if (!a)
        goto fail;
    a->physics.motion = QA_PHYSICS_TOSS;
    a->item->visual.scale = scale;
    if (g->options.edition != QA_Q2_RERELEASE)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, e))
        goto fail;
    body.angles.y = q2_random(g) * 360;
    if (!qa_world_body_write(g->services.world, id, &body, e))
        goto fail;
    if (!q2_actor_live(g, id))
        return true;
    a->item->due_ns = g->now_ns;
    if (!q2_item_tick(g, a, e))
        goto fail;
    if (!q2_actor_live(g, id))
        return true;
    a->item->think = Q2_ITEM_IDLE;
    a->item->due_ns = 0;
    qa_string_id sound;
    if (!qa_world_body_read(g->services.world, id, &body, e) ||
        !qa_builtin_resource(&g->services, "misc/fhit3.wav", &sound, e) ||
        !qa_builtin_emit(&g->services,
                         &(qa_builtin_event){.kind = QA_BUILTIN_SOUND,
                                             .family = QA_GAME_Q2,
                                             .provider = g->options.owner,
                                             .actor = id,
                                             .origin = body.origin,
                                             .resource = sound,
                                             .volume = 1,
                                             .attenuation = 1,
                                             .time_ns = g->now_ns},
                         e))
        goto fail;
    return true;
fail:
    if (q2_actor_live(g, id))
        qa_session_release(g->services.session, id, NULL);
    return false;
}
bool qa_q2_item_give(qa_q2_game *g, qa_actor_id player, const char *name, int count, bool *accepted,
                     qa_error *e) {
    if (!g || !name || !accepted || !q2_actor_live(g, player)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid Q2 direct pickup grant");
        return false;
    }
    *accepted = false;
    const qa_q2_item_definition *d = qa_q2_item_lookup(g, name);
    if (!d || d->console_give == QA_Q2_GIVE_INVENTORY_ONLY)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, player, &body, e))
        return false;
    qa_actor_definition definition;
    if (!qa_builtin_resource(&g->services, d->classname, &definition, e))
        return false;
    qa_actor_id id;
    if (!qa_builtin_spawn_actor(
            &g->services,
            &(qa_builtin_spawn){.owner = g->options.owner, .definition = definition, .body = body},
            &id, e))
        return false;
    q2_actor *a = q2_actor_get(g, id, true, e);
    bool ok = false;
    if (a) {
        a->item = calloc(1, sizeof(*a->item));
        if (!a->item)
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating direct Q2 pickup");
        else {
            a->item->definition = d;
            a->item->spawn = (qa_q2_item_spawn){
                .classname = d->classname, .count = count, .spawnflags = 0x10000};
            a->item->dispatching = true;
            ok = q2_entity_bind(g, a, e) && q2_item_grant(g, a, player, accepted, e);
            if (ok && *accepted && q2_actor_live(g, id) && q2_actor_live(g, player))
                ok = q2_item_finish(g, a, player, e);
            if (q2_actor_live(g, id))
                a->item->dispatching = false;
        }
    }
    if (q2_actor_live(g, id)) {
        qa_error cleanup = {0};
        if (!qa_session_release(g->services.session, id, &cleanup) && ok) {
            if (e)
                *e = cleanup;
            ok = false;
        }
    }
    return ok;
}
static bool clear_start_item(qa_q2_game *g, qa_actor_id player, qa_item_id item, qa_error *e) {
    qa_inventory_entry entry;
    qa_error missing = {0};
    if (!qa_inventory_entry_read(g->services.inventory, player, item, &entry, &missing)) {
        if (missing.code == QA_ERROR_NOT_FOUND)
            return true;
        if (e)
            *e = missing;
        return false;
    }
    entry.count = 0;
    return qa_inventory_configure(g->services.inventory, player, &entry, NULL, NULL, e);
}
static bool clear_start_offer(qa_q2_game *g, qa_actor_id player, qa_supply *supply,
                              const qa_supply_offer *offer, bool ammo, qa_error *e) {
    qa_supply_preview_result preview = {0};
    if (!qa_supply_preview(supply, player, offer, false, &preview, e))
        return false;
    size_t count = ammo ? preview.ammo_count : preview.weapon_count;
    const qa_pickup_receipt *items = ammo ? preview.ammo : preview.weapons;
    bool okay = true;
    for (size_t i = 0; i < count && q2_actor_live(g, player); i++)
        if (!clear_start_item(g, player, items[i].item, e)) {
            okay = false;
            break;
        }
    qa_supply_preview_free(&preview);
    return okay;
}
static bool clear_start(qa_q2_game *g, qa_actor_id player, const qa_q2_item_definition *d,
                        qa_error *e) {
    qa_supply *supply;
    if (!q2_item_supply(g, player, &supply, e)) return false;
    if (d->kind == QA_Q2_ITEM_AMMO && supply && qa_supply_maps(supply, d->item, false)) {
        qa_pickup_grant grant = {.item = d->item, .amount = 0};
        if (!clear_start_offer(
                g, player, supply,
                &(qa_supply_offer){.kind = QA_SUPPLY_AMMO, .ammo = &grant, .ammo_count = 1}, true,
                e))
            return false;
        if (d->weapon != QA_Q2_WEAPON_NONE && q2_actor_live(g, player))
            return clear_start_offer(
                g, player, supply,
                &(qa_supply_offer){.kind = QA_SUPPLY_WEAPON, .item = d->item, .weapon = d->item},
                false, e);
        return true;
    }
    if (d->kind == QA_Q2_ITEM_WEAPON && d->weapon != QA_Q2_BLASTER && supply &&
        qa_supply_maps(supply, d->item, true))
        return clear_start_offer(
            g, player, supply,
            &(qa_supply_offer){.kind = QA_SUPPLY_WEAPON, .item = d->item, .weapon = d->item}, false,
            e);
    return clear_start_item(g, player, d->item, e);
}
bool qa_q2_items_start(qa_q2_game *g, qa_actor_id player, const char *expression, qa_error *e) {
    if (!g || !expression) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing Q2 starting items");
        return false;
    }
    for (unsigned pass = 0; pass < 2; pass++) {
        const char *cursor = expression;
        while (*cursor) {
            while (isspace((unsigned char)*cursor) || *cursor == ';')
                ++cursor;
            if (!*cursor)
                break;
            const char *start = cursor;
            while (isalnum((unsigned char)*cursor) || *cursor == '_')
                ++cursor;
            size_t length = (size_t)(cursor - start);
            char name[128];
            if (!length || length >= sizeof(name) ||
                (*cursor && !isspace((unsigned char)*cursor) && *cursor != ';')) {
                qa_error_set(e, QA_ERROR_FORMAT, (size_t)(cursor - expression),
                             "Invalid Q2 starting item name");
                return false;
            }
            memcpy(name, start, length);
            name[length] = 0;
            while (isspace((unsigned char)*cursor))
                ++cursor;
            int count = 1;
            if (*cursor && *cursor != ';') {
                errno = 0;
                char *end;
                long value = strtol(cursor, &end, 10);
                if (end == cursor || errno == ERANGE || value < INT_MIN || value > INT_MAX) {
                    qa_error_set(e, QA_ERROR_FORMAT, (size_t)(cursor - expression),
                                 "Invalid Q2 starting item count");
                    return false;
                }
                count = (int)value;
                cursor = end;
                while (*cursor && *cursor != ';')
                    ++cursor;
            }
            const qa_q2_item_definition *d = qa_q2_item_lookup(g, name);
            if (!d || d->console_give == QA_Q2_GIVE_INVENTORY_ONLY) {
                qa_error_set(e, QA_ERROR_FORMAT, (size_t)(start - expression),
                             "Unknown Q2 starting item: %s", name);
                return false;
            }
            if (!pass)
                continue;
            if (!count) {
                if (!clear_start(g, player, d, e))
                    return false;
                if (!q2_actor_live(g, player))
                    return true;
                continue;
            }
            bool accepted;
            if (!qa_q2_item_give(g, player, d->classname, count, &accepted, e))
                return false;
            if (!q2_actor_live(g, player))
                return true;
        }
    }
    return true;
}
