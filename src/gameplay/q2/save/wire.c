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

static bool movement_fields(q2_save_io *io, qa_q2_wire_movement *s)
{
    Q2B(present);
    if (!s->present) return true;
    Q2T(frame); Q2T(time_ns); Q2T(source_sequence); Q2B(command_seen);
    Q2T(pending_sequence); Q2B(command_pending);
    Q2V(view_angles); Q2V(view_offset); Q2V(command_angles); Q2V(bounds.mins); Q2V(bounds.maxs);
    Q2U(water_type); Q2I(water_level); Q2F(view_height); Q2U(state.kind);
    qa_q2_game *g = io->game;
    if (s->state.kind != (g->options.edition == QA_Q2_RERELEASE ? QA_MOVEMENT_Q2_RERELEASE : QA_MOVEMENT_Q2_CLASSIC) ||
        s->water_level < 0 || s->water_level > 3 || (!s->command_seen && s->source_sequence) ||
        s->command_pending || s->pending_sequence)
        return q2_save_fail(io, "Q2 Source pmove continuation has invalid edition or command state");
    qa_q2_saved_reference ground = {0};
    uint32_t hit = s->ground.hit, model = s->ground.model;
    if (!io->reading && !q2_save_reference(g, s->ground.actor, &ground, io->error)) return false;
    if (!q2_save_u32(io, &hit) || !q2_save_u32(io, &model) || !q2_save_ref(io, &ground)) return false;
    if (hit > QA_TRACE_HIT_ACTOR || ((hit == QA_TRACE_HIT_ACTOR) != ground.present))
        return q2_save_fail(io, "Q2 Source pmove continuation has invalid physical ground");
    if (io->reading) {
        const qa_actor_record *record = ground.present ?
            qa_actors_resolve_saved(qa_session_actors(g->services.session), ground.actor) : NULL;
        if (ground.present && !record) return q2_save_fail(io, "Q2 Source pmove ground generation retired");
        s->ground = (qa_movement_ground){.hit = (qa_trace_hit)hit, .model = model,
            .actor = record ? record->id : (qa_actor_id){0}};
    }
    if (s->state.kind == QA_MOVEMENT_Q2_CLASSIC) {
        qa_q2_movement_state *p = &s->state.data.q2;
        if (!q2_save_i32(io, &p->type) || !q2_save_u32(io, &p->flags) ||
            p->type < 0 || p->type > 4 || p->flags > UINT8_MAX) return q2_save_fail(io, "Invalid classic Q2 Source pmove");
        for (size_t i = 0; i < 3; ++i)
            if (!short_field(io, &p->origin_eighths[i]) || !short_field(io, &p->velocity_eighths[i]) ||
                !short_field(io, &p->delta_angle_shorts[i])) return false;
        uint32_t time = p->time_eight_ms;
        if (!q2_save_u32(io, &time) || time > UINT8_MAX || !short_field(io, &p->gravity))
            return q2_save_fail(io, "Invalid classic Q2 Source pmove time");
        if (io->reading) p->time_eight_ms = (uint8_t)time;
    } else {
        qa_q2r_movement_state *p = &s->state.data.q2r;
        if (!q2_save_i32(io, &p->type) || !q2_save_u32(io, &p->flags) || !q2_save_u32(io, &p->time_ms) ||
            !short_field(io, &p->gravity) || !q2_save_vec(io, &p->origin) || !q2_save_vec(io, &p->velocity) ||
            !q2_save_vec(io, &p->delta_angles) || !q2_save_f32(io, &p->view_height)) return false;
        if (p->type < 0 || p->type > 6 || p->flags > UINT16_MAX || p->time_ms > UINT16_MAX ||
            p->view_height < INT8_MIN || p->view_height > INT8_MAX)
            return q2_save_fail(io, "Invalid rerelease Q2 Source pmove");
    }
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
            lifetime->creation_origin.x || lifetime->creation_origin.y || lifetime->creation_origin.z)))
        return q2_save_fail(io, "Q2 Source creation exceeds its physical clock");
    for (size_t i = 0; i < 8; ++i) {
        qa_q2_wire_origin *origin = &lifetime->origins[i];
        if (!q2_save_bool(io, &origin->present) || !q2_save_u64(io, &origin->source_frame) ||
            !q2_save_vec(io, &origin->origin)) return false;
        if (origin->present ? (!lifetime->present || origin->source_frame > frame ||
                origin->source_frame < lifetime->creation_frame || (origin->source_frame & 7u) != i) :
                (origin->source_frame || origin->origin.x || origin->origin.y || origin->origin.z))
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
    memcpy(styles, g->wire_lightstyles, sizeof(styles));
    for (size_t i = 0; i < 256; ++i)
        if (!q2_save_string(io, &styles[i])) return false;
    qa_string_id music = g->wire_music;
    bool music_present = g->wire_music_present;
    if (!q2_save_bool(io, &music_present) || !q2_save_string(io, &music) ||
        (!music_present && music)) return q2_save_fail(io, "Invalid retained Q2 Source music");
    if (!capacity || capacity > 65536 || !clients || clients > 256 ||
        clients >= capacity || extent < clients + 1 || extent > capacity ||
        clients != g->player_runtime->rules.max_clients)
        return q2_save_fail(io, "Q2 continuation wire namespace differs from its source client policy");
    qa_clock_state clock;
    if (!qa_session_clock(g->services.session, g->options.owner, &clock) || frame != clock.frame_number)
        return q2_save_fail(io, "Q2 continuation wire frame differs from its restored source clock");
    qa_actor_id *actors = io->reading ? calloc(capacity, sizeof(*actors)) : g->wire_actors;
    uint64_t *freed = io->reading ? calloc(capacity, sizeof(*freed)) : g->wire_freed_ns;
    uint8_t *seen = calloc(g->capacity, 1);
    if (!actors || !freed || !seen) {
        if (io->reading) { free(actors); free(freed); }
        free(seen);
        qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Retaining Q2 physical edict continuation");
        return false;
    }
    bool ok = true;
    for (uint32_t slot = 0; ok && slot < extent; ++slot) {
        qa_q2_saved_reference ref = {0};
        if (!io->reading) ok = q2_save_reference(g, actors[slot], &ref, io->error);
        ok = ok && q2_save_ref(io, &ref) && q2_save_u64(io, &freed[slot]);
        if (!ok) break;
        if (freed[slot] > g->now_ns || (ref.present && freed[slot])) {
            ok = q2_save_fail(io, "Invalid Q2 source edict lifetime in continuation");
            break;
        }
        if (!ref.present) continue;
        const qa_actor_record *record = io->reading
            ? qa_actors_resolve_saved(qa_session_actors(g->services.session), ref.actor)
            : qa_actors_get(qa_session_actors(g->services.session), actors[slot]);
        q2_actor *a = record && record->id.slot < g->capacity ? g->actors[record->id.slot] : NULL;
        bool player = a && a->client && !a->client->corpse;
        if (!a || !qa_actor_id_equal(a->id, record->id) || seen[record->id.slot] ||
            (record->owner == g->options.owner && record->has_source && record->source_slot != slot) ||
            (player && a->client->info.slot + 1 != slot) ||
            (!player && slot && slot <= clients) ||
            !a->wire_bound || (!io->reading && a->wire_slot != slot)) {
            ok = q2_save_fail(io, "Q2 continuation physical edict aliases an actor or client");
            break;
        }
        seen[record->id.slot] = 1;
        if (io->reading) { actors[slot] = record->id; a->wire_bound = true; a->wire_slot = slot; }
        qa_q2_wire_view value = a->wire_view;
        qa_q2_wire_movement movement = a->wire_movement;
        qa_q2_wire_lifetime lifetime = a->wire_lifetime;
        uint32_t event = a->wire_event;
        uint64_t event_frame = a->wire_event_frame;
        ok = view_fields(io, &value) && movement_fields(io, &movement) && lifetime_fields(io, &lifetime, frame) &&
            q2_save_u32(io, &event) && q2_save_u64(io, &event_frame);
        if (ok && ((value.present && (!player || value.frame > frame || value.time_ns > g->now_ns)) ||
            value.view.hit_marker_damage < INT16_MIN || value.view.hit_marker_damage > INT16_MAX ||
            (movement.present && (!player || movement.frame > frame || movement.time_ns > g->now_ns)) ||
            (player && !movement.present) || event_frame > frame || event > 255))
            ok = q2_save_fail(io, "Q2 continuation VIEW/event exceeds its actual source publication");
        if (ok && io->reading) {
            a->wire_view = value; a->wire_event = event; a->wire_event_frame = event_frame;
            a->wire_movement = movement;
            a->wire_lifetime = lifetime;
        }
    }
    for (q2_actor *a = g->first_actor; ok && a; a = a->live_next) {
        if (!a->wire_bound && (!a->hand_grenade_bound || a->client || a->entity || a->monster || a->item ||
            a->weapon_bound || a->physics_bound || a->projectile.kind != Q2_PROJECTILE_NONE))
            ok = q2_save_fail(io, "Q2 continuation removed physical membership from a source extension");
        if (ok && a->wire_bound && !seen[a->id.slot])
            ok = q2_save_fail(io, "Q2 continuation omits an admitted physical source edict");
    }
    uint32_t shadow_count = g->wire_shadow_count;
    qa_q2_wire_shadow_light shadows[256];
    memcpy(shadows, g->wire_shadows, sizeof(shadows));
    if (ok && (!q2_save_u32(io, &shadow_count) || shadow_count > 256 ||
        (shadow_count && g->options.edition != QA_Q2_RERELEASE)))
        ok = q2_save_fail(io, "Q2 continuation shadow-light table exceeds its Source edition");
    if (io->reading) memset(shadows, 0, sizeof(shadows));
    for (uint32_t i = 0; ok && i < shadow_count; ++i) {
        qa_q2_wire_shadow_light *s = &shadows[i];
        ok = q2_save_bool(io, &s->present);
        if (!ok || !s->present) continue;
        qa_q2_saved_reference ref = {0};
        if (!io->reading) ok = q2_save_reference(g, s->actor, &ref, io->error);
        ok = ok && q2_save_ref(io, &ref) && q2_save_u32(io, &s->source_slot) &&
            q2_save_u32(io, &s->type) && q2_save_u32(io, &s->resolution) &&
            q2_save_f32(io, &s->radius) && q2_save_f32(io, &s->intensity) &&
            q2_save_f32(io, &s->fade_start) && q2_save_f32(io, &s->fade_end) &&
            q2_save_f32(io, &s->cone_angle) && q2_save_i32(io, &s->style) &&
            q2_save_vec(io, &s->direction);
        if (!ok) break;
        const qa_actor_record *record = ref.present ?
            qa_actors_resolve_saved(qa_session_actors(g->services.session), ref.actor) : NULL;
        q2_actor *a = record && record->id.slot < g->capacity ? g->actors[record->id.slot] : NULL;
        if (!a || !a->entity || a->entity->kind != Q2E_DYNAMIC_LIGHT || a->entity->stage != 1 ||
            s->source_slot >= extent || !qa_actor_id_equal(actors[s->source_slot], record->id) ||
            s->type > 1 || s->radius <= 0 || s->style < -1 || s->style >= 256) {
            ok = q2_save_fail(io, "Q2 continuation shadow light lost its true Source entity");
            break;
        }
        for (uint32_t prior = 0; prior < i; ++prior)
            if (shadows[prior].present && qa_actor_id_equal(shadows[prior].actor, record->id))
                ok = q2_save_fail(io, "Q2 continuation aliases a Source shadow light");
        if (ok && io->reading) s->actor = record->id;
    }
    size_t reference_count = io->reading ? 0 : g->wire_reference_count;
    void *reference_storage = io->reading ? NULL : g->wire_references;
    if (ok) ok = q2_save_count(io, &reference_count, 17, sizeof(q2_wire_reference), &reference_storage);
    q2_wire_reference *references = reference_storage;
    for (size_t i = 0; ok && i < reference_count; ++i) {
        qa_q2_saved_reference saved = {0};
        if (!io->reading) ok = q2_save_reference(g, references[i].actor, &saved, io->error);
        ok = ok && q2_save_ref(io, &saved) && saved.present &&
            q2_save_u32(io, &references[i].number) && references[i].number < extent;
        if (ok && io->reading) ok = q2_resolve_reference(g, saved, &references[i].actor, io->error);
        for (size_t j = 0; ok && j < i; ++j)
            if (qa_actor_id_equal(references[i].actor, references[j].actor))
                ok = q2_save_fail(io, "Q2 Engine provenance aliases a full actor generation");
        const qa_actor_record *live = ok ? qa_actors_get(qa_session_actors(g->services.session), references[i].actor) : NULL;
        if (live && !qa_actor_id_equal(actors[references[i].number], live->id))
            ok = q2_save_fail(io, "Q2 live Engine provenance differs from its physical admission");
    }
    for (uint32_t slot = 0; ok && slot < extent; ++slot) {
        if (!actors[slot].registry) continue;
        bool admitted = false;
        for (size_t i = 0; i < reference_count; ++i)
            if (references[i].number == slot && qa_actor_id_equal(references[i].actor, actors[slot])) admitted = true;
        if (!admitted) ok = q2_save_fail(io, "Q2 Engine row lost its actual admission provenance");
    }
    if (ok && io->reading) {
        free(g->wire_actors); free(g->wire_freed_ns);
        free(g->wire_references);
        g->wire_references = references;
        g->wire_reference_count = g->wire_reference_capacity = reference_count;
        g->wire_actors = actors; g->wire_freed_ns = freed;
        g->wire_capacity = capacity; g->wire_clients = clients; g->wire_extent = extent;
        g->wire_frame = frame;
        memcpy(g->wire_lightstyles, styles, sizeof(styles));
        g->wire_music = music; g->wire_music_present = music_present;
        memcpy(g->wire_shadows, shadows, sizeof(shadows)); g->wire_shadow_count = shadow_count;
    } else if (io->reading) { free(actors); free(freed); free(references); }
    free(seen);
    return ok;
}
