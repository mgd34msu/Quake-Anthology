#include "internal.h"
#include "entities/internal.h"
#include "player/internal.h"
#include "monsters/internal.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q2_source.h"
#include "qa/text.h"
#include <math.h>

static bool idle(const qa_q2_game *g, qa_error *error)
{
    return (g && !g->current_actor.registry && !g->restoring_continuation &&
        !g->continuation_pending && !g->continuation_failed && !g->release_failed &&
        qa_session_safe(g->services.session) && qa_world_idle(g->services.world)) ||
        (qa_error_set(error, QA_ERROR_ARGUMENT, 0,
            "Q2 wire observation requires its completed GAME owner"), false);
}

bool qa_q2_wire_configure(qa_q2_game *g, uint32_t capacity,
    uint32_t clients, qa_error *error)
{
    if (!idle(g, error)) return false;
    if (g->first_actor || !clients || clients > 256 || capacity <= clients || capacity > 65536) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
            "Q2 wire construction requires an empty representable source edict table");
        return false;
    }
    qa_actor_id *actors = calloc(capacity, sizeof(*actors));
    uint64_t *freed = calloc(capacity, sizeof(*freed));
    if (!actors || !freed) {
        free(actors); free(freed);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q2 physical source edicts");
        return false;
    }
    free(g->wire_actors); free(g->wire_freed_ns);
    free(g->wire_references); g->wire_references = NULL;
    g->wire_reference_count = g->wire_reference_capacity = 0;
    g->wire_actors = actors; g->wire_freed_ns = freed;
    g->wire_capacity = capacity; g->wire_clients = clients; g->wire_extent = clients + 1;
    g->wire_frame = 0;
    return true;
}

void q2_wire_reset(qa_q2_game *g)
{
    memset(g->wire_actors, 0, g->wire_capacity * sizeof(*g->wire_actors));
    memset(g->wire_freed_ns, 0, g->wire_capacity * sizeof(*g->wire_freed_ns));
    g->wire_extent = g->wire_clients + 1; g->wire_frame = 0;
    g->wire_reference_count = 0;
    memset(g->wire_lightstyles, 0, sizeof(g->wire_lightstyles));
    memset(g->wire_shadows, 0, sizeof(g->wire_shadows)); g->wire_shadow_count = 0;
    g->wire_music = 0; g->wire_music_present = false;
}

static bool bind(qa_q2_game *g, q2_actor *a, qa_actor_id id,
    uint32_t slot, qa_error *error)
{
    if (slot >= g->wire_capacity ||
        (g->wire_actors[slot].registry && !qa_actor_id_equal(g->wire_actors[slot], id))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot,
            "Q2 physical source edict is occupied or outside its admitted table");
        return false;
    }
    if (a->wire_bound && a->wire_slot != slot &&
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], id)) {
            qa_error_set(error, QA_ERROR_FORMAT, a->wire_slot,
                "Q2 actor lost its prior physical source edict");
            return false;
    }
    size_t reference = 0;
    while (reference < g->wire_reference_count &&
        !qa_actor_id_equal(g->wire_references[reference].actor, id)) ++reference;
    if (reference == g->wire_reference_capacity) {
        size_t capacity = reference ? reference * 2 : 32;
        if (capacity < reference || capacity > SIZE_MAX / sizeof(*g->wire_references)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Q2 Engine provenance extent overflows"); return false;
        }
        q2_wire_reference *rows = realloc(g->wire_references, capacity * sizeof(*rows));
        if (!rows) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Q2 Engine admission provenance"); return false; }
        g->wire_references = rows; g->wire_reference_capacity = capacity;
    }
    g->wire_references[reference] = (q2_wire_reference){id, slot};
    if (reference == g->wire_reference_count) ++g->wire_reference_count;
    if (a->wire_bound && a->wire_slot != slot) {
        g->wire_actors[a->wire_slot] = (qa_actor_id){0};
        g->wire_freed_ns[a->wire_slot] = g->now_ns;
        a->wire_lifetime = (qa_q2_wire_lifetime){0};
    }
    g->wire_actors[slot] = id; g->wire_freed_ns[slot] = 0;
    a->wire_slot = slot; a->wire_bound = true;
    if (g->wire_extent <= slot) g->wire_extent = slot + 1;
    return true;
}

bool q2_wire_bind(qa_q2_game *g, q2_actor *a, uint32_t slot, qa_error *error)
{
    return bind(g, a, a->id, slot, error);
}

static bool spawn_slot(const qa_q2_game *g,uint32_t *out,qa_error *error)
{
    uint32_t slot=g->wire_extent;
    for(uint32_t i=g->wire_clients+1;i<g->wire_extent;++i) {
        uint64_t freed=g->wire_freed_ns[i];
        if(!g->wire_actors[i].registry&&(freed<2*Q2_NS||
            (g->now_ns>freed&&g->now_ns-freed>Q2_NS/2))) {
            slot=i;break;
        }
    }
    if(slot>=g->wire_capacity) {
        qa_error_set(error,QA_ERROR_MEMORY,slot,"Q2 physical Source edict table is full");
        return false;
    }
    *out=slot;return true;
}

bool qa_q2_wire_spawn_slot(const qa_q2_game *g,uint32_t *out,qa_error *error)
{
    if(!g||!out||g->restoring_continuation||g->continuation_pending||g->continuation_failed||
        g->release_failed||!g->wire_actors||!g->wire_freed_ns||
        g->wire_extent<g->wire_clients+1||g->wire_extent>g->wire_capacity) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q2 Source spawn requires its actual physical edict namespace");
        return false;
    }
    return spawn_slot(g,out,error);
}

bool q2_wire_admit(qa_q2_game *g, q2_actor *a, qa_actor_id id, qa_error *error)
{
    if (g->restoring_continuation || a->wire_bound) return true;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
    if (!record) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 physical source actor is retired");
        return false;
    }
    if (record->has_source && record->owner == g->options.owner)
        return bind(g, a, id, record->source_slot, error);
    uint32_t slot;
    if(!spawn_slot(g,&slot,error))return false;
    if (record->owner == g->options.owner &&
        !qa_actors_bind_source(qa_session_actors(g->services.session), id, slot, error)) return false;
    return bind(g, a, id, slot, error);
}

void q2_wire_release(qa_q2_game *g, q2_actor *a)
{
    for (uint32_t i = 0; i < g->wire_shadow_count; ++i)
        if (qa_actor_id_equal(g->wire_shadows[i].actor, a->id))
            g->wire_shadows[i] = (qa_q2_wire_shadow_light){0};
    if (a->wire_bound && a->wire_slot < g->wire_extent &&
        qa_actor_id_equal(g->wire_actors[a->wire_slot], a->id)) {
        g->wire_actors[a->wire_slot] = (qa_actor_id){0};
        g->wire_freed_ns[a->wire_slot] = g->now_ns;
    }
    a->wire_bound = false;
}

bool qa_q2_wire_extent(const qa_q2_game *g, uint32_t *out, qa_error *error)
{
    if (!out || !idle(g, error)) return false;
    *out = g->wire_extent;
    return true;
}

bool qa_q2_wire_policy(const qa_q2_game *g, uint32_t *capacity,
    uint32_t *clients, qa_error *error)
{
    if (!capacity || !clients || !idle(g, error)) return false;
    if (g->wire_clients != g->player_runtime->rules.max_clients) {
        qa_error_set(error, QA_ERROR_FORMAT, 0,
            "Q2 physical source reservation differs from its active maxclients");
        return false;
    }
    *capacity = g->wire_capacity; *clients = g->wire_clients;
    return true;
}

bool qa_q2_wire_admit_actor(qa_q2_game *g, qa_actor_id id, qa_error *error)
{
    if (!g || g->restoring_continuation || g->continuation_pending || g->continuation_failed ||
        g->release_failed || !qa_actors_get(qa_session_actors(g->services.session), id)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 Engine admission requires a live physical World generation");
        return false;
    }
    return q2_actor_get(g, id, true, error) != NULL;
}

bool qa_q2_wire_entity_number(qa_q2_game *g, qa_actor_id id, uint32_t *out, qa_error *error)
{
    if (!g || !out || !id.registry || g->restoring_continuation || g->continuation_pending ||
        g->continuation_failed || g->release_failed) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 entity reference requires its actual Engine namespace"); return false;
    }
    for (size_t i = 0; i < g->wire_reference_count; ++i)
        if (qa_actor_id_equal(g->wire_references[i].actor, id)) {
            *out = g->wire_references[i].number; return true;
        }
    if (!qa_q2_wire_admit_actor(g, id, error)) return false;
    q2_actor *a = g->actors[id.slot];
    if (!a || !a->wire_bound || a->wire_slot >= g->wire_extent ||
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], id)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 emitted actor lost its genuine Engine admission"); return false;
    }
    *out = a->wire_slot; return true;
}

bool qa_q2_wire_linked(qa_q2_game *g, const qa_linked_body *linked, qa_error *error)
{
    if (!linked || !qa_vec_finite(linked->state.origin) ||
        !qa_q2_wire_admit_actor(g, linked->actor, error)) return false;
    q2_actor *a = g->actors[linked->actor.slot];
    if (!a->wire_bound || a->wire_slot >= g->wire_extent ||
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], linked->actor) || a->wire_lifetime.link_count == UINT64_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Source link lost its actual physical Engine membership");
        return false;
    }
    if (!a->wire_lifetime.present)
        a->wire_lifetime = (qa_q2_wire_lifetime){.creation_origin = linked->state.origin,
            .creation_frame = g->wire_frame, .present = true};
    ++a->wire_lifetime.link_count;
    a->wire_lifetime.origins[g->wire_frame & 7u] = (qa_q2_wire_origin){
        .source_frame = g->wire_frame, .origin = linked->state.origin, .present = true};
    return true;
}

bool qa_q2_wire_lightstyle_revision(const qa_q2_game *g, uint64_t *out, qa_error *error) {
    if (!out || !idle(g, error)) return false;
    *out = g->wire_lightstyle_revision;
    return true;
}
bool qa_q2_wire_lightstyle_read(const qa_q2_game *g, uint32_t style,
    qa_string_id *out, qa_error *error)
{
    if (!out || !idle(g, error)) return false;
    uint32_t count = 256;
    if (style >= count) { qa_error_set(error, QA_ERROR_ARGUMENT, style, "Q2 lightstyle leaves its actual Source table"); return false; }
    *out = g->wire_lightstyles[style];
    return true;
}

bool qa_q2_wire_map_read(const qa_q2_game *g, qa_q2_wire_map *out, qa_error *error)
{
    if (!out || !idle(g, error)) return false;
    const q2_entities *state = g->entity_runtime;
    *out = (qa_q2_wire_map){.music = g->wire_music, .music_present = g->wire_music_present,
        .sky = state->sky, .sky_axis = state->sky_axis, .sky_rotation = state->sky_rotation,
        .sky_auto = state->sky_auto};
    return true;
}

bool qa_q2_wire_shadow_read(const qa_q2_game *g, uint32_t index,
    qa_q2_wire_shadow_light *out, qa_error *error)
{
    if (!out || index >= 256 || !idle(g, error)) return false;
    qa_q2_wire_shadow_light value = index < g->wire_shadow_count ?
        g->wire_shadows[index] : (qa_q2_wire_shadow_light){0};
    if (value.present) {
        qa_q2_wire_binding binding;
        if (!qa_q2_wire_actor(g, value.actor, &binding, error) || binding.source_slot != value.source_slot)
            return false;
    }
    *out = value;
    return true;
}

bool q2_wire_shadow_event(qa_q2_game *g, const qa_q2_map_event *event, qa_error *error)
{
    q2_actor *a = event->actor.slot < g->capacity ? g->actors[event->actor.slot] : NULL;
    if (g->options.edition != QA_Q2_RERELEASE || !a || !qa_actor_id_equal(a->id, event->actor) ||
        !a->entity || a->entity->kind != Q2E_DYNAMIC_LIGHT || a->entity->stage != 1) return true;
    if (event->radius <= 0) return true;
    float cone = q2_field_float(g, a->entity, "shadowlightconeangle", 45);
    if (!a->wire_bound || a->wire_slot >= g->wire_extent ||
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], a->id) ||
        !qa_vec_finite(event->direction) || !isfinite(event->radius) || !isfinite(event->intensity) ||
        !isfinite(event->fade_start) || !isfinite(event->fade_end) || !isfinite(cone) ||
        event->style < -1 || event->style >= 256) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 shadow light lost its actual physical Source fields");
        return false;
    }
    uint32_t index = 0;
    while (index < g->wire_shadow_count && !qa_actor_id_equal(g->wire_shadows[index].actor, a->id)) ++index;
    if (index >= 256) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Source shadow-light table is full"); return false; }
    if (index == g->wire_shadow_count) ++g->wire_shadow_count;
    g->wire_shadows[index] = (qa_q2_wire_shadow_light){.actor = a->id, .source_slot = a->wire_slot,
        .type = (event->flags & 1) ? 1u : 0u, .resolution = event->resolution, .radius = event->radius,
        .intensity = event->intensity, .fade_start = event->fade_start, .fade_end = event->fade_end,
        .style = event->style, .direction = event->direction,
        .cone_angle = cone, .present = true};
    return true;
}

static bool current(const qa_q2_game *g, uint32_t slot,
    qa_q2_wire_binding *out, qa_error *error)
{
    if (slot >= g->wire_extent) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q2 edict leaves its physical source extent");
        return false;
    }
    qa_actor_id id = g->wire_actors[slot];
    qa_q2_wire_binding value = {.source_owner = g->options.owner, .source_slot = slot};
    if (id.registry) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(g->services.session), id);
        q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
        if (!record || !a || !qa_actor_id_equal(a->id, id) ||
            !a->wire_bound || a->wire_slot != slot ||
            (record->owner == g->options.owner && record->has_source && record->source_slot != slot)) {
            qa_error_set(error, QA_ERROR_FORMAT, slot, "Q2 physical source binding lost its full actor generation");
            return false;
        }
        value.actor = id; value.in_use = true;
    }
    *out = value;
    return true;
}

bool qa_q2_wire_binding_read(const qa_q2_game *g, uint32_t slot,
    qa_q2_wire_binding *out, qa_error *error)
{
    return out && idle(g, error) && current(g, slot, out, error);
}

bool qa_q2_wire_actor(const qa_q2_game *g, qa_actor_id id,
    qa_q2_wire_binding *out, qa_error *error)
{
    if (!out || !idle(g, error)) return false;
    const q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
    if (!a || !qa_actor_id_equal(a->id, id) || !a->wire_bound) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Actor has no admitted Q2 physical source edict");
        return false;
    }
    return current(g, a->wire_slot, out, error);
}

bool qa_q2_wire_view_read(const qa_q2_game *g, qa_actor_id id,
    qa_q2_wire_view *out, qa_error *error)
{
    qa_q2_wire_binding binding;
    if (!out || !qa_q2_wire_actor(g, id, &binding, error)) return false;
    const q2_actor *a = g->actors[id.slot];
    if (!a->client || !a->client->info.connected || binding.source_slot != a->client->info.slot + 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 VIEW has no physical admitted source player");
        return false;
    }
    if (a->wire_view.present && (a->wire_view.frame > g->wire_frame || a->wire_view.time_ns > g->now_ns)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 retained VIEW exceeds its actual source clock");
        return false;
    }
    *out = a->wire_view;
    if (!out->present) out->view.fov = a->client->fov;
    return true;
}

static bool movement_valid(const qa_q2_game *g, const qa_q2_wire_movement *value,
    qa_error *error)
{
    qa_movement_kind kind = g->options.edition == QA_Q2_RERELEASE ?
        QA_MOVEMENT_Q2_RERELEASE : QA_MOVEMENT_Q2_CLASSIC;
    if (!value->present || value->state.kind != kind || value->frame > g->wire_frame ||
        value->time_ns > g->now_ns || !qa_vec_finite(value->view_angles) ||
        !qa_vec_finite(value->view_offset) || !qa_vec_finite(value->command_angles) || !qa_vec_finite(value->bounds.mins) ||
        !qa_vec_finite(value->bounds.maxs) || !isfinite(value->view_height) ||
        value->water_level < 0 || value->water_level > 3 ||
        (!value->command_seen && value->source_sequence) ||
        (!value->command_pending && value->pending_sequence) ||
        (value->command_pending && value->command_seen && value->pending_sequence <= value->source_sequence)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Source pmove receipt has invalid identity, clock or fields");
        return false;
    }
    if (kind == QA_MOVEMENT_Q2_CLASSIC) {
        if (value->state.data.q2.type < 0 || value->state.data.q2.type > 4 ||
            value->state.data.q2.flags > UINT8_MAX) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Classic Q2 Source pmove leaves its SDK fields");
            return false;
        }
    } else {
        const qa_q2r_movement_state *s = &value->state.data.q2r;
        if (s->type < 0 || s->type > 6 || s->flags > UINT16_MAX || s->time_ms > UINT16_MAX ||
            !qa_vec_finite(s->origin) || !qa_vec_finite(s->velocity) || !qa_vec_finite(s->delta_angles) ||
            !isfinite(s->view_height) || s->view_height < INT8_MIN || s->view_height > INT8_MAX) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Rerelease Q2 Source pmove leaves its SDK fields");
            return false;
        }
    }
    return true;
}

bool qa_q2_wire_movement_read(const qa_q2_game *g, qa_actor_id id,
    qa_q2_wire_movement *out, qa_error *error)
{
    if (!g || !out || g->restoring_continuation || g->continuation_pending || g->continuation_failed)
        return false;
    const q2_actor *a = id.slot < g->capacity ? g->actors[id.slot] : NULL;
    if (!qa_actors_get(qa_session_actors(g->services.session), id) || !a ||
        !qa_actor_id_equal(a->id, id) || !a->client || !a->client->info.connected ||
        !a->wire_bound || a->wire_slot >= g->wire_extent || a->wire_slot != a->client->info.slot + 1 ||
        !qa_actor_id_equal(g->wire_actors[a->wire_slot], id)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 pmove has no physical Source client");
        return false;
    }
    if (!movement_valid(g, &a->wire_movement, error)) return false;
    *out = a->wire_movement;
    return true;
}

bool qa_q2_wire_movement_publish(qa_q2_game *g, qa_actor_id id,
    const qa_q2_wire_movement *value, qa_error *error)
{
    qa_q2_wire_movement prior;
    if (!value || !qa_q2_wire_movement_read(g, id, &prior, error) ||
        !movement_valid(g, value, error) || value->frame != g->wire_frame ||
        value->time_ns != g->now_ns || !value->command_seen || value->command_pending ||
        (prior.command_seen && value->source_sequence <= prior.source_sequence)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 Source pmove completion lost its actual command turn");
        return false;
    }
    g->actors[id.slot]->wire_movement = *value;
    return true;
}

static int16_t source_short(float value)
{
    uint16_t bits = (uint16_t)(uint32_t)qa_source_float_to_i32(value);
    int16_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static qa_vec3 source_command_angles(const qa_movement_command *command)
{
    return command->kind == QA_MOVEMENT_Q2_CLASSIC ?
        qa_v3((float)command->angle_words[0] * (360.f / 65536.f),
            (float)command->angle_words[1] * (360.f / 65536.f),
            (float)command->angle_words[2] * (360.f / 65536.f)) : command->angles;
}

bool qa_q2_wire_movement_prepare(qa_q2_game *g, qa_actor_id id,
    const qa_movement_command *command, qa_q2_wire_movement *out,
    bool *run_pmove, qa_error *error)
{
    qa_q2_wire_movement value;
    if (!command || !out || !run_pmove || !qa_q2_wire_movement_read(g, id, &value, error) ||
        command->kind != value.state.kind || !qa_vec_finite(command->angles) ||
        value.command_pending || (value.command_seen && command->sequence <= value.source_sequence)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 ClientThink lost its literal physical Source command");
        return false;
    }
    q2_actor *a = g->actors[id.slot];
    q2_client_state *client = a->client;
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    value.frame = g->wire_frame; value.time_ns = g->now_ns;
    value.command_angles = source_command_angles(command);
    *run_pmove = !g->player_runtime->intermission && !(rr && client->awaiting_respawn) &&
        !client->info.chase_target.registry;
    int32_t type;
    if (g->player_runtime->intermission || (rr && client->awaiting_respawn)) {
        type = rr ? 6 : 4;
        if (rr) {
            value.view_height = g->player_runtime->intermission &&
                g->options.product == QA_Q2_N64 && !g->options.deathmatch ? 0 : 22;
            value.state.data.q2r.view_height = value.view_height;
            client->info.view_height = value.view_height;
        }
    } else if (client->info.chase_target.registry) {
        type = rr ? value.state.data.q2r.type : value.state.data.q2.type;
    } else {
        type = client->info.noclip ? (rr ? (client->info.spectator ? 3 : 2) : 1) :
            client->gibbed ? (rr ? 5 : 3) : client->info.dead ? (rr ? 4 : 2) :
            rr && a->grapples[QA_Q2_CTF_GRAPPLE].hook.registry &&
                a->grapples[QA_Q2_CTF_GRAPPLE].phase >= QA_Q2_GRAPPLE_PULL ? 1 : 0;
        float gravity = g->services.physics->gravity;
        if (!rr && !qa_q2_source_value(g, "sv_gravity", 800, &gravity, error)) return false;
        if (rr) gravity *= a->physics_bound ? a->physics.gravity_scale : 1;
        if (!isfinite(gravity)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 ClientThink Source gravity is not finite");
            return false;
        }
        if (rr) {
            qa_q2r_movement_state *state = &value.state.data.q2r;
            state->gravity = source_short(gravity);
            bool collide = !g->options.cooperative ||
                (g->player_runtime->rules.coop_player_collision && client->player_collision);
            state->flags = collide ? state->flags & ~UINT32_C(512) : state->flags | UINT32_C(512);
        } else value.state.data.q2.gravity = source_short(gravity);
    }
    if (rr) value.state.data.q2r.type = type;
    else value.state.data.q2.type = type;
    if (!movement_valid(g, &value, error)) return false;
    if (!*run_pmove) {
        value.source_sequence = command->sequence; value.command_seen = true;
        if (!qa_q2_wire_movement_publish(g, id, &value, error)) return false;
    } else {
        value.command_pending = true; value.pending_sequence = command->sequence;
        a->wire_movement = value;
    }
    *out = value;
    return true;
}

bool qa_q2_wire_movement_complete(qa_q2_game *g, qa_actor_id id,
    const qa_movement_result *result, const qa_movement_command *command,
    bool source_movement, qa_error *error)
{
    qa_q2_wire_movement value;
    if (!command || !qa_q2_wire_movement_read(g, id, &value, error) ||
        command->kind != value.state.kind || !value.command_pending ||
        value.pending_sequence != command->sequence || value.frame != g->wire_frame ||
        value.time_ns != g->now_ns || (!result && source_movement) ||
        (result && (result->status != QA_MOVEMENT_ACTIVE || !qa_actor_id_equal(result->actor, id) ||
            result->command_sequence != command->sequence ||
            (source_movement && result->state.kind != value.state.kind)))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 Pmove completion lost its actual Source command/result");
        return false;
    }
    value.command_pending = false; value.pending_sequence = 0;
    if (!result) {
        value.source_sequence = command->sequence; value.command_seen = true;
        return qa_q2_wire_movement_publish(g, id, &value, error);
    }
    qa_vec3 origin = qa_movement_origin(&result->state), velocity = qa_movement_velocity(&result->state);
    if (!qa_vec_finite(origin) || !qa_vec_finite(velocity) || !qa_vec_finite(command->angles)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 Pmove completion has invalid physical fields");
        return false;
    }
    if (source_movement) value.state = result->state;
    else if (value.state.kind == QA_MOVEMENT_Q2_RERELEASE) {
        value.state.data.q2r.origin = origin; value.state.data.q2r.velocity = velocity;
        value.state.data.q2r.view_height = result->view_height;
    } else {
        const float positions[3] = {origin.x, origin.y, origin.z};
        const float speeds[3] = {velocity.x, velocity.y, velocity.z};
        for (size_t i = 0; i < 3; ++i) {
            value.state.data.q2.origin_eighths[i] = source_short(positions[i] * 8.0f);
            value.state.data.q2.velocity_eighths[i] = source_short(speeds[i] * 8.0f);
        }
    }
    value.view_angles = result->view_angles; value.view_offset = result->view_offset;
    value.command_angles = source_command_angles(command); value.bounds = result->bounds; value.ground = result->ground;
    value.water_level = result->water_level; value.water_type = (uint32_t)result->water_type;
    value.view_height = result->view_height;
    value.frame = g->wire_frame; value.time_ns = g->now_ns;
    value.source_sequence = command->sequence; value.command_seen = true;
    if (!qa_q2_wire_movement_publish(g, id, &value, error)) return false;
    g->actors[id.slot]->client->info.view_height = value.view_height;
    return true;
}

bool q2_wire_player_motion(qa_q2_game *g, q2_actor *a,
    const qa_q2_player_motion *change, qa_error *error)
{
    if (!a->client || !qa_vec_finite(change->origin) || !qa_vec_finite(change->velocity) ||
        !qa_vec_finite(change->angles) || !qa_vec_finite(change->command_angles) ||
        (change->has_command_view_angles && !qa_vec_finite(change->command_view_angles))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 Source motion has invalid physical client fields");
        return false;
    }
    bool rr = g->options.edition == QA_Q2_RERELEASE;
    qa_q2_wire_movement value = a->wire_movement;
    if (change->kind == QA_Q2_PLAYER_SPAWN && !change->preserve_view_angles) {
        bool seen = value.command_seen; uint64_t sequence = value.source_sequence;
        bool pending = value.command_pending; uint64_t pending_sequence = value.pending_sequence;
        value = (qa_q2_wire_movement){.state.kind = rr ? QA_MOVEMENT_Q2_RERELEASE : QA_MOVEMENT_Q2_CLASSIC,
            .command_seen = seen, .source_sequence = sequence,
            .command_pending = pending, .pending_sequence = pending_sequence};
        a->wire_view = (qa_q2_wire_view){0};
        a->wire_event = 0; a->wire_event_frame = g->wire_frame;
    }
    value.present = true; value.frame = g->wire_frame; value.time_ns = g->now_ns;
    bool force_view = change->kind != QA_Q2_PLAYER_NOCLIP && !change->preserve_view_angles;
    if (force_view) {
        value.view_angles = change->angles; value.command_angles = change->command_angles;
    }
    value.view_height = a->client->info.view_height;
    value.view_offset = rr ? qa_v3(0, 0, 0) : qa_v3(0, 0, value.view_height);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, a->id, &body, error)) return false;
    value.bounds = body.bounds;
    qa_vec3 command_view = change->has_command_view_angles ? change->command_view_angles : change->angles;
    qa_vec3 delta = qa_vec_sub(command_view, change->command_angles);
    if (force_view && !qa_vec_finite(delta)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 Source motion has a nonfinite command angle offset");
        return false;
    }
    if (rr) {
        qa_q2r_movement_state *s = &value.state.data.q2r;
        if (change->kind != QA_Q2_PLAYER_NOCLIP) {
            s->origin = change->origin; s->velocity = change->velocity;
            if (force_view) s->delta_angles = delta;
        }
        s->view_height = value.view_height;
        s->type = change->kind == QA_Q2_PLAYER_FREEZE ? 6 :
            change->kind == QA_Q2_PLAYER_NOCLIP ? (change->enabled ? 2 : 0) : change->spectator ? 3 : 0;
        if (change->hold_ns) { s->flags |= 32; s->time_ms = (uint32_t)fmin((double)(change->hold_ns / Q2_MS), UINT16_MAX); }
    } else {
        qa_q2_movement_state *s = &value.state.data.q2;
        if (change->kind != QA_Q2_PLAYER_NOCLIP) {
            const float origin[] = {change->origin.x, change->origin.y, change->origin.z};
            const float velocity[] = {change->velocity.x, change->velocity.y, change->velocity.z};
            const float angles[] = {delta.x, delta.y, delta.z};
            for (size_t i = 0; i < 3; ++i) {
                s->origin_eighths[i] = source_short(origin[i] * 8.0f);
                s->velocity_eighths[i] = source_short(velocity[i] * 8.0f);
                if (force_view) {
                    uint16_t bits = qa_angle_to_word(angles[i]);
                    memcpy(&s->delta_angle_shorts[i], &bits, sizeof(bits));
                }
            }
        }
        s->type = change->kind == QA_Q2_PLAYER_FREEZE ? 4 :
            change->kind == QA_Q2_PLAYER_NOCLIP ? (change->enabled ? 1 : 0) : change->spectator ? 1 : 0;
        if (change->hold_ns) { s->flags |= 32; s->time_eight_ms = (uint8_t)fmin((double)(change->hold_ns / (8 * Q2_MS)), UINT8_MAX); }
    }
    if (!movement_valid(g, &value, error)) return false;
    a->wire_movement = value;
    return true;
}

bool qa_q2_wire_next(const qa_q2_game *g, uint64_t *order,
    qa_q2_wire_binding *out, bool *found, qa_error *error)
{
    if (!order || !out || !found || !idle(g, error)) return false;
    *found = false;
    for (const q2_actor *a = g->first_actor; a; a = a->live_next) {
        if (a->source_order <= *order || !a->wire_bound) continue;
        if (!current(g, a->wire_slot, out, error)) return false;
        *order = a->source_order; *found = true;
        return true;
    }
    return true;
}

bool qa_q2_wire_entity_read(qa_q2_game *g, uint32_t slot,
    qa_q2_wire_source_entity *out, qa_error *error)
{
    qa_q2_wire_source_entity value = {0};
    if (!out || !qa_q2_wire_binding_read(g, slot, &value.binding, error)) return false;
    if (!value.binding.in_use) { *out = value; return true; }
    q2_actor *a = g->actors[value.binding.actor.slot];
    value.has_visual = qa_q2_presentation_read(g, a->id, &value.visual);
    value.body_serial = qa_world_body_storage_serial(g->services.world, a->id);
    if (!value.body_serial || !qa_world_body_read(g->services.world, a->id, &value.body, error)) return false;
    value.previous_origin = a->entity && (value.visual.render_flags & 128u)
        ? a->entity->beam_end : value.body.origin;
    value.model_beam = qa_q2_model_beam(g->options.edition, value.visual.render_flags,
        value.visual.models[0] != QA_STRING_NONE);
    value.solid = a->physics_bound ? a->physics.solid : QA_PHYSICS_NOT_SOLID;
    qa_actor_collision collision;
    qa_error collision_error = {0};
    bool has_collision = qa_world_get_collision(g->services.world, a->id, &collision, &collision_error);
    if (!has_collision && collision_error.code != QA_OK) {
        if (error) *error = collision_error;
        return false;
    }
    if (has_collision) {
        value.owner = collision.owner;
        if (collision.monster) value.server_flags |= 4;
        if (collision.dead_monster) value.server_flags |= 2;
        if (!a->physics_bound)
            value.solid = collision.role == QA_COLLISION_TRIGGER ? QA_PHYSICS_TRIGGER :
                collision.inline_model ? QA_PHYSICS_BRUSH : QA_PHYSICS_BOX;
    }
    value.has_weapon = a->weapon_bound;
    if (value.has_weapon) value.weapon = a->weapon;
    if (a->projectile.kind != Q2_PROJECTILE_NONE) {
        value.classname = a->projectile.classname; value.loop_sound = a->projectile.loop_sound;
        value.owner = a->projectile.owner;
    } else if (a->client) {
        value.classname = a->entity ? a->entity->classname : 0;
        value.loop_sound = a->client->loop_sound;
    } else if (a->monster) {
        value.classname = a->monster->controller_kind != Q2M_CONTROLLER_NONE && a->entity
            ? a->entity->classname : a->monster->classname;
        if (a->monster->controller_kind != Q2M_CONTROLLER_NONE && a->entity)
            value.owner = a->entity->owner;
        value.loop_sound = a->monster->weapon_sound;
    } else if (a->item) {
        value.classname = a->item->definition ? a->item->definition->classname_id : 0;
        value.owner = a->item->owner;
        if (a->item->companion) value.loop_sound = a->item->companion->loop_sound;
    } else if (a->entity) {
        value.classname = a->entity->classname; value.owner = a->entity->owner;
        value.spawn_flags = a->entity->spawnflags;
        value.volume = a->entity->volume; value.attenuation = a->entity->attenuation;
        if (a->entity->kind == Q2E_SPEAKER && (a->entity->spawnflags & 3) && a->entity->active) {
            value.loop_sound = a->entity->noise;
            value.volume = 1;
            if (g->options.edition == QA_Q2_RERELEASE && a->entity->attenuation == -1)
                value.server_flags |= 1024;
        }
        value.solid = a->entity->collision.inline_model ? QA_PHYSICS_BRUSH : value.solid;
        if (a->entity->kind == Q2E_FLARE) {
            value.flare = true; value.flare_image = q2_field_id(g, a->entity, "image");
            value.flare_start = q2_field_float(g, a->entity, "fade_start_dist", 96);
            value.flare_end = q2_field_float(g, a->entity, "fade_end_dist", 384);
        }
    }
    value.lifetime = a->wire_lifetime;
    value.event = a->wire_event_frame == g->wire_frame ? a->wire_event : 0;
    qa_q2_wire_binding after;
    if (!qa_q2_wire_actor(g, a->id, &after, error) ||
        value.body_serial != qa_world_body_storage_serial(g->services.world, a->id) ||
        !qa_actor_id_equal(after.actor, value.binding.actor) || after.source_slot != slot) {
        qa_error_set(error, QA_ERROR_ARGUMENT, slot, "Q2 wire body/source binding changed during observation");
        return false;
    }
    *out = value;
    return true;
}
