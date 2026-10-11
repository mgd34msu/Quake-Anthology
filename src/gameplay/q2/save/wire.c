#include "internal.h"
#include "../player/internal.h"
#include "../entities/internal.h"

static bool short_field(q2_save_io *io, int16_t *field)
{
    int32_t value = *field;
    if (!q2_save_i32(io, &value)) return false;
    if (value < INT16_MIN || value > INT16_MAX)
        return q2_save_fail(io, "Q2 Source pmove short leaves its SDK range");
    if (io->reading) *field = (int16_t)value;
    return true;
}

static bool movement_fields(q2_save_io *io, q2_actor *actor, bool player)
{
    /* This is the old positional file boundary, not a retained runtime row. */
    bool present = player;
    if (!q2_save_bool(io, &present)) return false;
    if (present != player) return q2_save_fail(io, "Q2 continuation movement has invalid player membership");
    if (!present) return true;
    qa_movement_result movement = {0};
    qa_vec3 command_angles = actor->source_pm.command_angles;
    if (!io->reading && !qa_q2_player_movement_read(io->game, actor->id,
            &movement, &command_angles, io->error)) return false;
    qa_q2_player_pm_rules *rules = &actor->source_pm;
    if (!q2_save_u64(io, &rules->frame) || !q2_save_u64(io, &rules->time_ns) ||
        !q2_save_u64(io, &rules->source_sequence) || !q2_save_bool(io, &rules->command_seen) ||
        !q2_save_u64(io, &rules->pending_sequence) || !q2_save_bool(io, &rules->command_pending) ||
        !q2_save_vec(io, &movement.view_angles) || !q2_save_vec(io, &movement.view_offset) ||
        !q2_save_vec(io, &command_angles) || !q2_save_vec(io, &movement.bounds.mins) ||
        !q2_save_vec(io, &movement.bounds.maxs)) return false;
    uint32_t water = (uint32_t)movement.water_type, kind = (uint32_t)movement.state.kind;
    if (!q2_save_u32(io, &water) || !q2_save_i32(io, &movement.water_level) ||
        !q2_save_f32(io, &movement.view_height) || !q2_save_u32(io, &kind)) return false;
    qa_q2_game *g = io->game;
    if (kind != (uint32_t)(g->options.rules) ||
        movement.water_level < 0 || movement.water_level > 3 ||
        (!rules->command_seen && rules->source_sequence) || rules->command_pending || rules->pending_sequence)
        return q2_save_fail(io, "Q2 Source pmove continuation has invalid edition or command state");
    movement.state.kind = (qa_ruleset_id)kind;
    qa_q2_saved_reference ground = {0};
    uint32_t hit = movement.ground.hit, model = movement.ground.model;
    if (!io->reading && !q2_save_reference(g, movement.ground.actor, &ground, io->error)) return false;
    if (!q2_save_u32(io, &hit) || !q2_save_u32(io, &model) || !q2_save_ref(io, &ground)) return false;
    if (hit > QA_TRACE_HIT_ACTOR || ((hit == QA_TRACE_HIT_ACTOR) != ground.present))
        return q2_save_fail(io, "Q2 Source pmove continuation has invalid physical ground");
    if (movement.state.kind == QA_RULESET_Q2_CLASSIC) {
        qa_q2_movement_state *state = &movement.state.data.q2;
        if (!q2_save_i32(io, &state->type) || !q2_save_u32(io, &state->flags) ||
            state->type < 0 || state->type > 4 || state->flags > UINT8_MAX)
            return q2_save_fail(io, "Invalid classic Q2 Source pmove");
        for (size_t i = 0; i < 3; ++i)
            if (!short_field(io, &state->origin_eighths[i]) || !short_field(io, &state->velocity_eighths[i]) ||
                !short_field(io, &state->delta_angle_shorts[i])) return false;
        uint32_t time = state->time_eight_ms;
        if (!q2_save_u32(io, &time) || time > UINT8_MAX || !short_field(io, &state->gravity))
            return q2_save_fail(io, "Invalid classic Q2 Source pmove time");
        if (io->reading) {
            rules->type = state->type; rules->flags = state->flags; rules->time = time;
            rules->gravity = state->gravity;
            memcpy(rules->delta.words, state->delta_angle_shorts, sizeof(rules->delta.words));
        }
    } else {
        qa_q2r_movement_state *state = &movement.state.data.q2r;
        if (!q2_save_i32(io, &state->type) || !q2_save_u32(io, &state->flags) || !q2_save_u32(io, &state->time_ms) ||
            !short_field(io, &state->gravity) || !q2_save_vec(io, &state->origin) || !q2_save_vec(io, &state->velocity) ||
            !q2_save_vec(io, &state->delta_angles) || !q2_save_f32(io, &state->view_height)) return false;
        if (state->type < 0 || state->type > 6 || state->flags > UINT16_MAX || state->time_ms > UINT16_MAX ||
            state->view_height < INT8_MIN || state->view_height > INT8_MAX)
            return q2_save_fail(io, "Invalid rerelease Q2 Source pmove");
        if (io->reading) {
            rules->type = state->type; rules->flags = state->flags; rules->time = state->time_ms;
            rules->gravity = state->gravity; rules->delta.angles = state->delta_angles;
        }
    }
    if (io->reading) rules->command_angles = command_angles;
    /* Common motion belongs to QA_SAVE_CONTROLS/QA_SAVE_WORLD. The legacy
     * duplicate fields above are consumed, not installed in another owner. */
    return true;
}

static bool view_fields(q2_save_io *io, qa_q2_wire_view *s)
{
    Q2B(present); Q2T(frame); Q2T(time_ns);
    qa_q2_player_view *v = &s->view;
    return q2_save_vec(io, &v->angles) && q2_save_vec(io, &v->offset) &&
        q2_save_vec(io, &v->kick_angles) && q2_save_vec(io, &v->gun_angles) &&
        q2_save_vec(io, &v->gun_offset) &&
        q2_save_f32(io, &v->blend.x) && q2_save_f32(io, &v->blend.y) &&
        q2_save_f32(io, &v->blend.z) && q2_save_f32(io, &v->blend.w) &&
        q2_save_f32(io, &v->fov) && q2_save_f32(io, &v->health) &&
        q2_save_f64(io, &v->armor) && q2_save_f32(io, &v->ammo) &&
        q2_save_string(io, &v->ammo_icon) && q2_save_string(io, &v->armor_icon) &&
        q2_save_i32(io, &v->ammo_count) &&
        q2_save_i32(io, &v->score) && q2_save_i32(io, &v->flashes) &&
        q2_save_i32(io, &v->layouts) && q2_save_i32(io, &v->hit_marker_damage) &&
        q2_save_string(io, &v->selected_item) && q2_save_string(io, &v->timer_item) &&
        q2_save_i32(io, &v->timer_seconds) && q2_save_bool(io, &v->underwater) &&
        q2_save_bool(io, &v->spectator);
}

static bool lifetime_fields(q2_save_io *io, qa_q2_wire_lifetime *lifetime, uint64_t frame)
{
    if (!q2_save_bool(io, &lifetime->present) || !q2_save_u64(io, &lifetime->link_count) ||
        !q2_save_u64(io, &lifetime->creation_frame) || !q2_save_vec(io, &lifetime->creation_origin)) return false;
    if ((lifetime->present && (!lifetime->link_count || lifetime->creation_frame > frame)) ||
        (!lifetime->present && (lifetime->link_count || lifetime->creation_frame ||
            lifetime->creation_origin.x != 0 || lifetime->creation_origin.y != 0 || lifetime->creation_origin.z != 0)))
        return q2_save_fail(io, "Q2 Source creation exceeds its physical clock");
    for (size_t i = 0; i < 8; ++i) {
        qa_q2_wire_origin *origin = &lifetime->origins[i];
        if (!q2_save_bool(io, &origin->present) || !q2_save_u64(io, &origin->source_frame) ||
            !q2_save_vec(io, &origin->origin)) return false;
        if (origin->present ? (!lifetime->present || origin->source_frame > frame ||
                origin->source_frame < lifetime->creation_frame || (origin->source_frame & 7u) != i) :
                (origin->source_frame || origin->origin.x != 0 || origin->origin.y != 0 || origin->origin.z != 0))
            return q2_save_fail(io, "Q2 Source origin history leaves its actual link frame");
    }
    return true;
}

bool q2_save_wire(q2_save_io *io)
{
    qa_q2_game *g = io->game;
    uint32_t capacity = g->wire_capacity, clients = g->wire_clients, extent = g->wire_extent;
    uint64_t frame = g->wire_frame;
    if (!q2_save_u32(io, &capacity) || !q2_save_u32(io, &clients) ||
        !q2_save_u32(io, &extent) || !q2_save_u64(io, &frame)) return false;
    qa_string_id styles[256];
    memcpy(styles, g->wire_lightstyles.patterns, sizeof(styles));
    for (size_t i = 0; i < 256; ++i)
        if (!q2_save_string(io, &styles[i])) return false;
    if (io->legacy_map_state) {
        /* Consume the old duplicate; the retained event store owns music. */
        qa_string_id music = 0;
        bool present = false;
        if (!q2_save_bool(io, &present) || !q2_save_string(io, &music)) return false;
    }
    if (!capacity || capacity > 65536 || !clients || clients > 256 ||
        clients >= capacity || extent < clients + 1 || extent > capacity ||
        clients != g->player_runtime->rules.max_clients)
        return q2_save_fail(io, "Q2 continuation wire namespace differs from its source client policy");
    qa_clock_state clock;
    if (!qa_session_clock(g->services.session, g->options.owner, &clock) || frame != clock.frame_number)
        return q2_save_fail(io, "Q2 continuation wire frame differs from its restored source clock");
    qa_actor_id *actors = io->reading ? calloc(capacity, sizeof(*actors)) : NULL;
    uint64_t *freed = io->reading ? calloc(capacity, sizeof(*freed)) : g->wire_freed_ns;
    uint8_t *seen = calloc(g->capacity, 1);
    qa_actor_index index = {0};
    if ((io->reading && !actors) || !freed || !seen) {
        if (io->reading) { free(actors); free(freed); }
        free(seen);
        qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Retaining Q2 physical edict continuation");
        return false;
    }
    if (io->reading && (!qa_actor_index_create(&index, qa_session_actors(g->services.session), io->error) ||
        !qa_actor_index_reverse_reserve(&index, capacity, io->error))) {
        qa_actor_index_destroy(&index); free(actors); free(freed); free(seen); return false;
    }
    bool ok = true;
    for (uint32_t slot = 0; ok && slot < extent; ++slot) {
        qa_q2_saved_reference ref = {0};
        qa_actor_id id = io->reading ? (qa_actor_id){0} : q2_wire_actor_at(g, slot);
        if (!io->reading) ok = q2_save_reference(g, id, &ref, io->error);
        ok = ok && q2_save_ref(io, &ref) && q2_save_u64(io, &freed[slot]);
        if (!ok) break;
        if (freed[slot] > g->now_ns || (ref.present && freed[slot])) {
            ok = q2_save_fail(io, "Invalid Q2 source edict lifetime in continuation");
            break;
        }
        if (!ref.present) continue;
        const qa_actor_record *record = io->reading
            ? qa_actors_resolve_saved(qa_session_actors(g->services.session), ref.actor)
            : qa_actors_get(qa_session_actors(g->services.session), id);
        q2_actor *a = record && record->id.slot < g->capacity ? g->actors[record->id.slot] : NULL;
        bool player = a && a->client && !a->client->rule.corpse;
        uint32_t number;
        if (!a || !qa_actor_id_equal(a->id, record->id) || seen[record->id.slot] ||
            (record->owner == g->options.owner && record->has_source && record->source_slot != slot) ||
            (player && a->client->info.slot + 1 != slot) ||
            (!player && slot && slot <= clients) ||
            (!io->reading && (!q2_wire_binding_number(g, record->id, &number) || number != slot))) {
            ok = q2_save_fail(io, "Q2 continuation physical edict aliases an actor or client");
            break;
        }
        seen[record->id.slot] = 1;
        if (io->reading) {
            actors[slot] = record->id;
            if (record->owner != g->options.owner) qa_actor_index_put(&index, record->id, slot);
        }
        qa_q2_wire_view value = a->wire_view;
        qa_q2_wire_lifetime lifetime = a->wire_lifetime;
        uint32_t event = a->wire_event;
        uint64_t event_frame = a->wire_event_frame;
        ok = view_fields(io, &value) && movement_fields(io, a, player) && lifetime_fields(io, &lifetime, frame) &&
            q2_save_u32(io, &event) && q2_save_u64(io, &event_frame);
        if (ok && ((value.present && (!player || value.frame > frame || value.time_ns > g->now_ns)) ||
            value.view.hit_marker_damage < INT16_MIN || value.view.hit_marker_damage > INT16_MAX ||
            (player && (a->source_pm.frame > frame || a->source_pm.time_ns > g->now_ns)) || event_frame > frame || event > 255))
            ok = q2_save_fail(io, "Q2 continuation VIEW/event exceeds its actual source publication");
        if (ok && io->reading) {
            a->wire_view = value; a->wire_event = event; a->wire_event_frame = event_frame;
            a->wire_lifetime = lifetime;
        }
    }
    for (q2_actor *a = g->first_actor; ok && a; a = a->live_next) {
        uint32_t number;
        bool bound = io->reading ? seen[a->id.slot] != 0 : q2_wire_binding_number(g, a->id, &number);
        if (!bound && (!a->hand_grenade_bound || a->client || a->entity || a->monster || a->item ||
            a->weapon_bound || a->physics_bound || a->projectile.kind != Q2_PROJECTILE_NONE))
            ok = q2_save_fail(io, "Q2 continuation removed physical membership from a source extension");
        if (ok && bound && !seen[a->id.slot])
            ok = q2_save_fail(io, "Q2 continuation omits an admitted physical source edict");
    }
    uint32_t shadow_count = g->wire_shadow_count;
    q2_shadow_slot shadows[256];
    memcpy(shadows, g->wire_shadows, sizeof(shadows));
    if (ok && (!q2_save_u32(io, &shadow_count) || shadow_count > 256 ||
        (shadow_count && g->options.rules != QA_RULESET_Q2_RERELEASE)))
        ok = q2_save_fail(io, "Q2 continuation shadow-light table exceeds its Source edition");
    if (io->reading) memset(shadows, 0, sizeof(shadows));
    for (uint32_t i = 0; ok && i < shadow_count; ++i) {
        q2_shadow_slot *s = &shadows[i];
        bool present = s->actor.registry != 0;
        ok = q2_save_bool(io, &present);
        if (!ok || !present) continue;
        qa_q2_saved_reference ref = {0};
        if (!io->reading) ok = q2_save_reference(g, s->actor, &ref, io->error);
        ok = ok && q2_save_ref(io, &ref) && q2_save_u32(io, &s->source_slot);
        if (ok && io->legacy_map_state) {
            /* Old saves repeated 11 light parameters after this mapping.
             * Consume them only; presentation rebuilds from retained events. */
            uint8_t discarded[11 * 4];
            ok = q2_save_raw(io, discarded, sizeof(discarded));
        }
        if (!ok) break;
        const qa_actor_record *record = ref.present ?
            qa_actors_resolve_saved(qa_session_actors(g->services.session), ref.actor) : NULL;
        q2_actor *a = record && record->id.slot < g->capacity ? g->actors[record->id.slot] : NULL;
        if (!a || !a->entity || a->entity->kind != Q2E_DYNAMIC_LIGHT || a->entity->stage != 1 ||
            s->source_slot >= extent || !qa_actor_id_equal(io->reading ? actors[s->source_slot] :
                q2_wire_actor_at(g, s->source_slot), record->id)) {
            ok = q2_save_fail(io, "Q2 continuation shadow light lost its true Source entity");
            break;
        }
        for (uint32_t prior = 0; prior < i; ++prior)
            if (qa_actor_id_equal(shadows[prior].actor, record->id))
                ok = q2_save_fail(io, "Q2 continuation aliases a Source shadow light");
        if (ok && io->reading) s->actor = record->id;
    }
    size_t reference_count = io->reading ? 0 : qa_actor_index_retained_count(&g->wire_index);
    if (ok && reference_count > UINT32_MAX) ok = q2_save_fail(io, "Invalid Q2 continuation array");
    uint32_t reference_word = (uint32_t)reference_count;
    if (ok) ok = q2_save_u32(io, &reference_word);
    if (ok && io->reading && reference_word > (io->input.size - io->offset) / 17)
        ok = q2_save_fail(io, "Invalid Q2 continuation array extent");
    reference_count = reference_word;
    qa_actor_index *references = io->reading ? &index : &g->wire_index;
    for (size_t i = 0; ok && i < reference_count; ++i) {
        qa_q2_saved_reference saved = {0};
        qa_actor_id actor = {0};
        size_t value = 0;
        if (!io->reading) ok = qa_actor_index_retained_at(references, i, &actor, &value) &&
            q2_save_reference(g, actor, &saved, io->error);
        uint32_t number = (uint32_t)value;
        ok = ok && q2_save_ref(io, &saved) && saved.present &&
            q2_save_u32(io, &number) && number < extent;
        if (ok && io->reading) ok = q2_resolve_reference(g, saved, &actor, io->error);
        if (ok && io->reading && qa_actor_index_find_retained(references, actor, &value))
            ok = q2_save_fail(io, "Q2 Engine provenance aliases a full actor generation");
        const qa_actor_record *live = ok ? qa_actors_get(qa_session_actors(g->services.session), actor) : NULL;
        if (live && !qa_actor_id_equal(io->reading ? actors[number] : q2_wire_actor_at(g, number), live->id))
            ok = q2_save_fail(io, "Q2 live Engine provenance differs from its physical admission");
        if (ok && io->reading) ok = qa_actor_index_remember(references, actor, number, io->error);
    }
    for (uint32_t slot = 0; ok && slot < extent; ++slot) {
        qa_actor_id actor = io->reading ? actors[slot] : q2_wire_actor_at(g, slot);
        if (!actor.registry) continue;
        size_t number;
        if (!qa_actor_index_find_retained(references, actor, &number) || number != slot)
            ok = q2_save_fail(io, "Q2 Engine row lost its actual admission provenance");
    }
    if (ok && io->reading) {
        qa_actor_index_destroy(&g->wire_index); free(g->wire_freed_ns);
        g->wire_index = index; index = (qa_actor_index){0}; g->wire_freed_ns = freed;
        g->wire_capacity = capacity; g->wire_clients = clients; g->wire_extent = extent;
        g->wire_frame = frame;
        for (uint32_t i = 0; i < 256; ++i) qa_lightstyles_set(&g->wire_lightstyles, i, styles[i]);
        memcpy(g->wire_shadows, shadows, sizeof(shadows)); g->wire_shadow_count = shadow_count;
    } else if (io->reading) free(freed);
    qa_actor_index_destroy(&index);
    free(actors);
    free(seen);
    return ok;
}
