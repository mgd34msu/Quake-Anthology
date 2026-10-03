/* Snapshot collision continuation follows cg_snapshot.c, cg_ents.c and
 * cg_predict.c, Copyright (C) 1999-2005 Id Software, Inc., GPL-2.0-or-later. */
#include "qa/network_q3_prediction_scene.h"
#include "qa/physics.h"
#include "save_fields.h"
#include <limits.h>

typedef struct prediction_entity {
    qa_q3_entity current, next;
    qa_vec3 origin, angles;
    int32_t publication_message;
    bool valid, interpolate, published;
} prediction_entity;
struct qa_q3_prediction_scene {
    qa_q3_product product;
    qa_q3_snapshot_slot snap, next;
    prediction_entity entities[QA_Q3_ENTITIES];
    uint16_t solids[256], triggers[256];
    size_t solid_count, trigger_count;
    int32_t processed, latest, time, physics_time;
    uint64_t revision;
    bool has_snap, has_next, this_teleport, next_teleport, physics_next;
};
static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
static int32_t signed_word(uint32_t value)
{ return value <= INT32_MAX ? (int32_t)value : (int32_t)((int64_t)value - INT64_C(4294967296)); }
static int32_t difference(int32_t a, int32_t b)
{ return signed_word((uint32_t)a - (uint32_t)b); }
static qa_vec3 vector(const float *v) { return qa_v3(v[0], v[1], v[2]); }
static bool position(const qa_q3_trajectory *source, int32_t time, qa_vec3 *out, qa_error *error)
{
    qa_trajectory t = {(qa_trajectory_type)source->type, source->time, source->duration,
        vector(source->base), vector(source->delta)};
    return qa_trajectory_position(&t, time, 800, out, error);
}
static bool changed(qa_q3_prediction_scene *s, qa_error *error)
{
    if (s->revision == UINT64_MAX) return fail(error, QA_ERROR_FORMAT, "Q3 prediction scene revision is exhausted");
    ++s->revision; return true;
}
bool qa_q3_prediction_scene_create(qa_q3_product product, qa_q3_prediction_scene **out, qa_error *error)
{
    if (!out || *out || (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 prediction scene constructor");
    qa_q3_prediction_scene *s = calloc(1, sizeof(*s));
    if (!s) return fail(error, QA_ERROR_MEMORY, "Allocating Q3 prediction scene");
    s->product = product; s->revision = 1; *out = s; return true;
}
void qa_q3_prediction_scene_destroy(qa_q3_prediction_scene *s)
{ if (s) { free(s->snap.entities); free(s->next.entities); free(s); } }
void qa_q3_prediction_scene_clear(qa_q3_prediction_scene *s)
{
    if (!s) return;
    qa_q3_product product = s->product;
    uint64_t revision = s->revision == UINT64_MAX ? UINT64_MAX : s->revision + 1;
    free(s->snap.entities); free(s->next.entities); memset(s, 0, sizeof(*s));
    s->product = product; s->revision = revision;
}
bool qa_q3_prediction_scene_restart(qa_q3_prediction_scene *s, qa_error *error)
{
    if (!s || !changed(s, error)) return false;
    s->this_teleport = true; return true;
}
static bool store(qa_q3_snapshot_slot *slot, const qa_q3_snapshot *input, qa_q3_product product, qa_error *error)
{
    if (!input || !input->valid || input->message_number < 0 || input->player.product != product ||
        input->player.clientNum < 0 || input->player.clientNum >= QA_Q3_ENTITIES ||
        (input->entity_count && !input->entities))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid actual Q3 collision snapshot");
    qa_q3_snapshot retail = *input;
    if (retail.entity_count > 256) retail.entity_count = 256;
    for (size_t i = 0; i < retail.entity_count; ++i)
        if (retail.entities[i].number < 0 || retail.entities[i].number >= QA_Q3_ENTITY_NONE)
            return fail(error, QA_ERROR_FORMAT, "Invalid actual Q3 collision entity number");
    return qa_q3_slot_store(slot, &retail, error);
}
/* Only collision kinematics of BG_PlayerStateToEntityState are needed here.
 * The source program remains the owner of predictable-event consumption. */
static void player_kinematics(const qa_q3_player *p, qa_q3_entity *e)
{
    e->number = p->clientNum;
    e->eType = p->pmType == 2 || p->pmType == 5 || p->stats[0] <= -40 ? 10 : 1;
    e->pos.type = 1; memcpy(e->pos.base, p->origin, sizeof(e->pos.base));
    memcpy(e->pos.delta, p->velocity, sizeof(e->pos.delta));
    e->apos.type = 1; memcpy(e->apos.base, p->viewangles, sizeof(e->apos.base));
    e->eFlags = p->stats[0] <= 0 ? p->eFlags | 1 : p->eFlags & ~1;
    e->groundEntityNum = p->groundEntityNum;
}
static void solid_list(qa_q3_prediction_scene *s)
{
    const qa_q3_snapshot *snap = s->has_next && !s->next_teleport && !s->this_teleport ?
        &s->next.value : &s->snap.value;
    s->solid_count = 0; s->trigger_count = 0;
    for (size_t i = 0; i < snap->entity_count; ++i) {
        uint16_t number = (uint16_t)snap->entities[i].number;
        const prediction_entity *e = &s->entities[number];
        if (e->current.eType == 2 || e->current.eType == 8 || e->current.eType == 9) {
            s->triggers[s->trigger_count++] = number; continue;
        }
        if (e->next.solid) s->solids[s->solid_count++] = number;
    }
}
static void reset_pose(prediction_entity *e)
{ e->origin = vector(e->current.origin); e->angles = vector(e->current.angles); }
static void published(prediction_entity *e, const qa_q3_snapshot *snap)
{ e->published = true; e->publication_message = snap->message_number; }
static bool initial(qa_q3_prediction_scene *s, const qa_q3_snapshot *snap, qa_error *error)
{
    if (!store(&s->snap, snap, s->product, error)) return false;
    s->has_snap = true;
    player_kinematics(&s->snap.value.player, &s->entities[s->snap.value.player.clientNum].current);
    published(&s->entities[s->snap.value.player.clientNum], &s->snap.value);
    solid_list(s);
    for (size_t i = 0; i < s->snap.value.entity_count; ++i) {
        const qa_q3_entity *row = &s->snap.value.entities[i]; prediction_entity *e = &s->entities[row->number];
        e->current = *row; published(e, &s->snap.value);
        e->valid = true; e->interpolate = false; reset_pose(e);
    }
    return true;
}
static bool next_snapshot(qa_q3_prediction_scene *s, const qa_q3_snapshot *snap, qa_error *error)
{
    if (!store(&s->next, snap, s->product, error)) return false;
    s->has_next = true;
    player_kinematics(&s->next.value.player, &s->entities[s->next.value.player.clientNum].next);
    s->entities[s->snap.value.player.clientNum].interpolate = true;
    for (size_t i = 0; i < s->next.value.entity_count; ++i) {
        const qa_q3_entity *row = &s->next.value.entities[i]; prediction_entity *e = &s->entities[row->number];
        e->next = *row; e->interpolate = e->valid && !((e->current.eFlags ^ row->eFlags) & 4);
    }
    s->next_teleport = ((s->next.value.player.eFlags ^ s->snap.value.player.eFlags) & 4) != 0 ||
        s->next.value.player.clientNum != s->snap.value.player.clientNum ||
        ((s->next.value.flags ^ s->snap.value.flags) & 4) != 0;
    solid_list(s); return true;
}
static void transition(qa_q3_prediction_scene *s)
{
    bool teleport = ((s->next.value.player.eFlags ^ s->snap.value.player.eFlags) & 4) != 0;
    for (size_t i = 0; i < s->snap.value.entity_count; ++i)
        s->entities[s->snap.value.entities[i].number].valid = false;
    qa_q3_snapshot_slot old = s->snap; s->snap = s->next; s->next = old;
    player_kinematics(&s->snap.value.player, &s->entities[s->snap.value.player.clientNum].current);
    published(&s->entities[s->snap.value.player.clientNum], &s->snap.value);
    s->entities[s->snap.value.player.clientNum].interpolate = false;
    for (size_t i = 0; i < s->snap.value.entity_count; ++i) {
        prediction_entity *e = &s->entities[s->snap.value.entities[i].number];
        e->current = e->next; published(e, &s->snap.value); e->valid = true;
        if (!e->interpolate) reset_pose(e);
        e->interpolate = false;
    }
    s->has_next = false;
    if (teleport) s->this_teleport = true;
}
static const qa_q3_snapshot *read_next(qa_q3_prediction_scene *s,
    qa_q3_snapshot_lookup lookup, void *context)
{
    while (s->processed < s->latest) {
        ++s->processed;
        const qa_q3_snapshot *snap = lookup(context, s->processed);
        if (snap) return snap;
    }
    return NULL;
}
bool qa_q3_prediction_scene_process(qa_q3_prediction_scene *s, int32_t latest,
    qa_q3_snapshot_lookup lookup, void *context, int32_t time, qa_error *error)
{
    if (!s || !lookup || latest < s->latest || !changed(s, error))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 collision snapshot source went backwards");
    s->latest = latest; s->time = time;
    while (!s->has_snap) {
        const qa_q3_snapshot *snap = read_next(s, lookup, context);
        if (!snap) return true;
        if (!(snap->flags & 2) && !initial(s, snap, error)) return false;
    }
    for (;;) {
        if (!s->has_next) {
            const qa_q3_snapshot *snap = read_next(s, lookup, context);
            if (!snap) break;
            if (!next_snapshot(s, snap, error)) return false;
            if (s->next.value.server_time < s->snap.value.server_time)
                return fail(error, QA_ERROR_FORMAT, "Q3 collision snapshot time went backwards");
        }
        if (s->time >= s->snap.value.server_time && s->time < s->next.value.server_time) break;
        transition(s);
    }
    if (s->time < s->snap.value.server_time) s->time = s->snap.value.server_time;
    s->physics_next = s->has_next && !s->next_teleport && !s->this_teleport;
    s->physics_time = s->physics_next ?
        s->next.value.server_time : s->snap.value.server_time;
    return true;
}
bool qa_q3_prediction_scene_read(const qa_q3_prediction_scene *s, qa_q3_prediction_scene_view *out)
{
    if (!s || !out || !s->has_snap) return false;
    *out = (qa_q3_prediction_scene_view){.snapshot = &s->snap.value,
        .next_snapshot = s->has_next ? &s->next.value : NULL,
        .prediction_snapshot = s->physics_next ? &s->next.value : &s->snap.value,
        .time = s->time, .physics_time = s->physics_time, .processed_snapshot = s->processed,
        .this_frame_teleport = s->this_teleport, .next_frame_teleport = s->next_teleport, .revision = s->revision};
    return true;
}
bool qa_q3_prediction_scene_current(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v)
{
    qa_q3_prediction_scene_view now;
    return v && qa_q3_prediction_scene_read(s, &now) && v->snapshot == now.snapshot &&
        v->next_snapshot == now.next_snapshot && v->prediction_snapshot == now.prediction_snapshot &&
        v->time == now.time && v->physics_time == now.physics_time &&
        v->processed_snapshot == now.processed_snapshot && v->this_frame_teleport == now.this_frame_teleport &&
        v->next_frame_teleport == now.next_frame_teleport && v->revision == now.revision;
}
static float angle(float from, float to, float fraction)
{
    if (to - from > 180) to -= 360;
    if (to - from < -180) to += 360;
    return from + fraction * (to - from);
}
static bool adjust_mover(const qa_q3_prediction_scene *s, qa_vec3 origin, int32_t mover,
    int32_t from, int32_t to, qa_vec3 *out, qa_error *error)
{
    *out = origin;
    if (mover <= 0 || mover >= QA_Q3_ENTITY_WORLD || s->entities[mover].current.eType != 4) return true;
    const qa_q3_entity *row = &s->entities[mover].current;
    qa_vec3 old, now, ignored;
    if (!position(&row->pos, from, &old, error) || !position(&row->apos, from, &ignored, error) ||
        !position(&row->pos, to, &now, error) || !position(&row->apos, to, &ignored, error)) return false;
    *out = qa_vec_add(origin, qa_vec_sub(now, old)); return true;
}
bool qa_q3_prediction_scene_adjust_mover(const qa_q3_prediction_scene *s,
    const qa_q3_prediction_scene_view *v, qa_vec3 origin, int32_t mover,
    int32_t from, int32_t to, qa_vec3 *out, qa_error *error)
{
    if (!out || !qa_vec_finite(origin) || !qa_q3_prediction_scene_current(s, v))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 mover query lost its actual scene");
    return adjust_mover(s, origin, mover, from, to, out, error);
}
static bool pose(qa_q3_prediction_scene *s, prediction_entity *e, bool smooth, qa_error *error)
{
    if (!smooth && e->current.number < 64) { e->current.pos.type = 1; e->next.pos.type = 1; }
    if (e->interpolate && (e->current.pos.type == 1 || (e->current.pos.type == 3 && e->current.number < 64))) {
        if (!s->has_next) return fail(error, QA_ERROR_FORMAT, "Q3 collision interpolated entity lost its next snapshot");
        int32_t delta = difference(s->next.value.server_time, s->snap.value.server_time);
        float fraction = delta ? (float)difference(s->time, s->snap.value.server_time) / (float)delta : 0;
        qa_vec3 a, b;
        if (!position(&e->current.pos, s->snap.value.server_time, &a, error) ||
            !position(&e->next.pos, s->next.value.server_time, &b, error)) return false;
        e->origin = qa_vec_lerp(a, b, fraction);
        if (!position(&e->current.apos, s->snap.value.server_time, &a, error) ||
            !position(&e->next.apos, s->next.value.server_time, &b, error)) return false;
        e->angles = qa_v3(angle(a.x, b.x, fraction), angle(a.y, b.y, fraction), angle(a.z, b.z, fraction));
        return true;
    }
    if (!position(&e->current.pos, s->time, &e->origin, error) ||
        !position(&e->current.apos, s->time, &e->angles, error)) return false;
    return adjust_mover(s, e->origin, e->current.groundEntityNum,
        s->snap.value.server_time, s->time, &e->origin, error);
}
bool qa_q3_prediction_scene_publish_poses(qa_q3_prediction_scene *s, bool smooth, qa_error *error)
{
    if (!s || !s->has_snap || !changed(s, error)) return false;
    /* CG_AddPacketEntities first calculates the actual viewed player, then
     * visits each source append row, including repeated entity numbers. */
    if (!pose(s, &s->entities[s->snap.value.player.clientNum], smooth, error)) return false;
    for (size_t i = 0; i < s->snap.value.entity_count; ++i) {
        prediction_entity *e = &s->entities[s->snap.value.entities[i].number];
        if (e->current.eType < 13 && !pose(s, e, smooth, error)) return false;
    }
    return true;
}
bool qa_q3_prediction_scene_consume_teleport(qa_q3_prediction_scene *s, qa_error *error)
{
    if (!s || !changed(s, error)) return false;
    s->this_teleport = false; return true;
}
bool qa_q3_prediction_scene_mark_teleport(qa_q3_prediction_scene *s,
    const qa_q3_prediction_scene_view *view, qa_error *error)
{
    if (!qa_q3_prediction_scene_current(s, view))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 teleport feedback lost its actual prediction scene");
    if (s->this_teleport && !s->physics_next) return true;
    if (!changed(s, error)) return false;
    s->this_teleport = true;
    s->physics_next = false;
    s->physics_time = s->snap.value.server_time;
    return true;
}
static bool entity_at(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const uint16_t *list, size_t count, size_t ordinal, qa_q3_prediction_scene_entity_view *out,
    bool *present, qa_error *error)
{
    if (!out || !present || !qa_q3_prediction_scene_current(s, v))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 entity observation lost its actual scene");
    *out = (qa_q3_prediction_scene_entity_view){0}; *present = ordinal < count;
    if (*present) {
        uint32_t number = list[ordinal]; const prediction_entity *e = &s->entities[number];
        *out = (qa_q3_prediction_scene_entity_view){&e->current, number,
            e->publication_message, e->published, e->valid};
    }
    return true;
}
bool qa_q3_prediction_scene_solid_at(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    size_t ordinal, qa_q3_prediction_scene_entity_view *out, bool *present, qa_error *error)
{ return entity_at(s, v, s ? s->solids : NULL, s ? s->solid_count : 0, ordinal, out, present, error); }
bool qa_q3_prediction_scene_trigger_count(const qa_q3_prediction_scene *s,
    const qa_q3_prediction_scene_view *v, size_t *out, qa_error *error)
{
    if (!out || !qa_q3_prediction_scene_current(s, v))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 trigger observation lost its actual scene");
    *out = s->trigger_count; return true;
}
bool qa_q3_prediction_scene_trigger_at(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    size_t ordinal, qa_q3_prediction_scene_entity_view *out, bool *present, qa_error *error)
{ return entity_at(s, v, s ? s->triggers : NULL, s ? s->trigger_count : 0, ordinal, out, present, error); }
bool qa_q3_prediction_scene_entity_current(const qa_q3_prediction_scene *s,
    const qa_q3_prediction_scene_view *v, const qa_q3_prediction_scene_entity_view *retained)
{
    if (!retained || retained->source_number >= QA_Q3_ENTITY_NONE ||
        !qa_q3_prediction_scene_current(s, v)) return false;
    const prediction_entity *e = &s->entities[retained->source_number];
    return retained->entity == &e->current && retained->publication_message == e->publication_message &&
        retained->published == e->published && retained->current_valid == e->valid;
}
bool qa_q3_prediction_scene_trigger_overlap(const qa_q3_prediction_scene *s,
    const qa_q3_prediction_scene_view *v, qa_collision_geometry *geometry,
    const qa_q3_prediction_scene_entity_view *retained, qa_vec3 origin, qa_bounds bounds,
    bool *out, qa_error *error)
{
    if (!out || !geometry || qa_collision_geometry_family(geometry) != QA_COLLISION_Q3 ||
        !qa_q3_prediction_scene_entity_current(s, v, retained))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 trigger query lost its actual scene or map");
    const qa_q3_entity *row = retained->entity; *out = false;
    if ((row->eType != 8 && row->eType != 9) || row->solid != 0xffffff || row->modelindex == 0) return true;
    if (row->modelindex < 0) return fail(error, QA_ERROR_FORMAT, "Negative Q3 trigger inline model");
    qa_trace_query query = {.start = origin, .end = origin, .shape = {QA_SHAPE_BOX, bounds},
        .policy = qa_collision_default_policy(QA_COLLISION_Q3)};
    query.policy.contents_mask = UINT32_MAX;
    qa_trace_result trace;
    if (!qa_collision_trace_q3_model(geometry, &query, (uint32_t)row->modelindex, false, &trace, error)) return false;
    *out = trace.start_solid || trace.all_solid;
    return qa_q3_prediction_scene_entity_current(s, v, retained) ||
        fail(error, QA_ERROR_ARGUMENT, "Q3 trigger changed during overlap query");
}
bool qa_q3_prediction_scene_item_position(const qa_q3_prediction_scene *s,
    const qa_q3_prediction_scene_view *v, const qa_q3_prediction_scene_entity_view *retained,
    qa_vec3 *out, qa_error *error)
{
    if (!out || !qa_q3_prediction_scene_entity_current(s, v, retained) || retained->entity->eType != 2)
        return fail(error, QA_ERROR_ARGUMENT, "Q3 item query lost its actual current item");
    return position(&retained->entity->pos, v->time, out, error);
}
static bool collision_valid(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_q3_prediction_scene_collision *c, qa_error *error)
{
    return (qa_q3_prediction_scene_current(s, v) && c && c->geometry && c->actor_at && c->number_of &&
        qa_collision_geometry_family(c->geometry) == QA_COLLISION_Q3) ||
        fail(error, QA_ERROR_ARGUMENT, "Q3 prediction collision lost its actual scene and map owner");
}
static bool pass_number(const qa_q3_prediction_scene_collision *c, qa_actor_id actor,
    uint32_t *number, bool *present, qa_error *error)
{
    *present = false; *number = QA_Q3_ENTITY_NONE;
    return !actor.registry || c->number_of(c->context, actor, number, present, error);
}
static void adapt_trace(qa_trace_result *result, qa_collision_family family)
{
    if (family == QA_COLLISION_Q3) return;
    result->contents = qa_collision_convert_contents(result->contents, QA_COLLISION_Q3, family);
    result->surface_flags = qa_collision_convert_surface_flags(result->surface_flags, QA_COLLISION_Q3, family);
    result->surface.flags = qa_collision_convert_surface_flags(result->surface.flags, QA_COLLISION_Q3, family);
    result->secondary_surface.flags = qa_collision_convert_surface_flags(result->secondary_surface.flags, QA_COLLISION_Q3, family);
    result->family = family;
}
static bool scene_trace(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_q3_prediction_scene_collision *c, const qa_trace_query *query, qa_trace_result *out,
    int32_t *number, qa_error *error)
{
    if (!query || !out || !collision_valid(s, v, c, error)) return false;
    qa_trace_query q = *query; q.target = (qa_collision_target){0};
    q.policy.contents_mask = qa_collision_geometry_mask(&query->policy, QA_COLLISION_Q3);
    q.policy.family = QA_COLLISION_Q3;
    uint32_t skip; bool has_skip;
    if (!pass_number(c, q.pass_actor, &skip, &has_skip, error) ||
        !qa_collision_trace_q3_model(c->geometry, &q, 0, false, out, error)) return false;
    out->hit = out->fraction != 1 ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_NONE;
    *number = out->hit == QA_TRACE_HIT_WORLD ? QA_Q3_ENTITY_WORLD : QA_Q3_ENTITY_NONE;
    out->actor = (qa_actor_id){0};
    for (size_t i = 0; i < s->solid_count; ++i) {
        const prediction_entity *e = &s->entities[s->solids[i]];
        const qa_q3_entity *row = &e->current;
        if (has_skip && (uint32_t)row->number == skip) continue;
        qa_trace_result result;
        if (row->solid == 0xffffff) {
            if (row->modelindex < 0 || !position(&row->pos, v->physics_time, &q.target.origin, error)) return false;
            q.target.angles = e->angles;
            if (!qa_collision_trace_q3_model(c->geometry, &q, (uint32_t)row->modelindex, true, &result, error)) return false;
        } else {
            int32_t x = row->solid & 255, zd = (row->solid >> 8) & 255, zu = ((row->solid >> 16) & 255) - 32;
            qa_bounds bounds = {qa_v3(-(float)x, -(float)x, -(float)zd), qa_v3((float)x, (float)x, (float)zu)};
            q.target.origin = e->origin; q.target.angles = qa_v3(0, 0, 0);
            if (!qa_collision_trace_q3_box(&q, bounds, true, &result, error)) return false;
        }
        if (result.all_solid || result.fraction < out->fraction) {
            qa_actor_id actor = {0}; bool present = false;
            if (row->number != QA_Q3_ENTITY_WORLD) {
                if (!c->actor_at(c->context, (uint32_t)row->number, &actor, &present, error)) return false;
                if (!present) return fail(error, QA_ERROR_ARGUMENT, "Q3 collision hit lost its actual projected actor");
            }
            *out = result; out->hit = row->number == QA_Q3_ENTITY_WORLD ? QA_TRACE_HIT_WORLD : QA_TRACE_HIT_ACTOR;
            *number = row->number;
            out->actor = actor;
            out->model = row->solid == 0xffffff ? (uint32_t)row->modelindex : 0;
        } else if (result.start_solid) out->start_solid = true;
        if (out->all_solid) break;
    }
    adapt_trace(out, query->policy.family);
    return qa_q3_prediction_scene_current(s, v) || fail(error, QA_ERROR_ARGUMENT, "Q3 scene changed during trace");
}
bool qa_q3_prediction_scene_trace(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_q3_prediction_scene_collision *c, const qa_trace_query *query, qa_trace_result *out, qa_error *error)
{
    int32_t number;
    return scene_trace(s, v, c, query, out, &number, error);
}
bool qa_q3_prediction_scene_trace_with_number(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_q3_prediction_scene_collision *c, const qa_trace_query *query, qa_trace_result *out,
    int32_t *number, qa_error *error)
{
    if (!number) return fail(error, QA_ERROR_ARGUMENT, "Q3 trace requires its actual source-number output");
    return scene_trace(s, v, c, query, out, number, error);
}
bool qa_q3_prediction_scene_point_contents(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_q3_prediction_scene_collision *c, const qa_point_query *query, qa_point_contents *out, qa_error *error)
{
    if (!query || !out || !collision_valid(s, v, c, error)) return false;
    qa_point_query q = *query; q.target = (qa_collision_target){0};
    uint32_t skip; bool has_skip;
    if (!pass_number(c, q.pass_actor, &skip, &has_skip, error) ||
        !qa_collision_point_contents(c->geometry, &q, out, error)) return false;
    for (size_t i = 0; i < s->solid_count; ++i) {
        const qa_q3_entity *row = &s->entities[s->solids[i]].current;
        if ((has_skip && (uint32_t)row->number == skip) || row->solid != 0xffffff || !row->modelindex) continue;
        if (row->modelindex < 0) return fail(error, QA_ERROR_FORMAT, "Negative Q3 collision inline model");
        q.target = (qa_collision_target){true, (uint32_t)row->modelindex, vector(row->origin), vector(row->angles)};
        qa_point_contents result;
        if (!qa_collision_point_contents(c->geometry, &q, &result, error)) return false;
        out->contents |= result.contents; out->stored |= result.stored; out->merged |= result.merged;
    }
    return qa_q3_prediction_scene_current(s, v) || fail(error, QA_ERROR_ARGUMENT, "Q3 scene changed during contents query");
}
bool qa_q3_prediction_scene_is_bsp(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_q3_prediction_scene_collision *c, const qa_trace_result *trace, bool *out, qa_error *error)
{
    if (!trace || !out || !collision_valid(s, v, c, error)) return false;
    *out = trace->hit == QA_TRACE_HIT_WORLD;
    if (trace->hit == QA_TRACE_HIT_ACTOR) {
        uint32_t number; bool present;
        if (!c->number_of(c->context, trace->actor, &number, &present, error)) return false;
        if (!present || number >= QA_Q3_ENTITY_NONE) return fail(error, QA_ERROR_ARGUMENT, "Q3 BSP hit lost its actual source entity");
        *out = s->entities[number].current.solid == 0xffffff;
    }
    return true;
}
static bool zero_vector(const float value[3])
{ return value[0] == 0 && value[1] == 0 && value[2] == 0; }
static bool cold_trajectory(const qa_q3_trajectory *t)
{ return !t->type && !t->time && !t->duration && zero_vector(t->base) && zero_vector(t->delta); }
static bool cold_entity(const qa_q3_entity *e)
{
    return !e->number && !e->eType && !e->eFlags && cold_trajectory(&e->pos) && cold_trajectory(&e->apos) &&
        !e->time && !e->time2 && zero_vector(e->origin) && zero_vector(e->origin2) &&
        zero_vector(e->angles) && zero_vector(e->angles2) && !e->otherEntityNum && !e->otherEntityNum2 &&
        !e->groundEntityNum && !e->constantLight && !e->loopSound && !e->modelindex && !e->modelindex2 &&
        !e->clientNum && !e->frame && !e->solid && !e->event && !e->eventParm && !e->powerups &&
        !e->weapon && !e->legsAnim && !e->torsoAnim && !e->generic1;
}
static bool scene_valid(const qa_q3_prediction_scene *s, qa_error *error)
{
    if (!s || (s->product != QA_Q3_ARENA && s->product != QA_Q3_TEAM_ARENA) || !s->revision || s->processed < 0 ||
        s->latest < s->processed || s->solid_count > 256 || s->trigger_count > 256 ||
        s->solid_count + s->trigger_count > 256 || (s->has_next && !s->has_snap) ||
        (s->physics_next && !s->has_next))
        return fail(error, QA_ERROR_FORMAT, "Invalid Q3 prediction scene owner cut");
    if (s->has_snap && (!s->snap.value.valid || s->snap.value.entity_count > 256 ||
        s->snap.value.message_number < 0 || s->snap.value.message_number > s->processed || s->snap.value.player.clientNum < 0 ||
        s->snap.value.player.clientNum >= QA_Q3_ENTITIES || s->time < s->snap.value.server_time))
        return fail(error, QA_ERROR_FORMAT, "Invalid Q3 prediction current snapshot cut");
    if (s->has_next && (!s->next.value.valid || s->next.value.entity_count > 256 ||
        s->next.value.message_number < 0 ||
        s->next.value.message_number <= s->snap.value.message_number ||
        s->next.value.message_number > s->processed || s->next.value.player.clientNum < 0 ||
        s->next.value.player.clientNum >= QA_Q3_ENTITIES || s->next.value.server_time <= s->time))
        return fail(error, QA_ERROR_FORMAT, "Invalid Q3 prediction future snapshot cut");
    if (!s->has_snap && (s->solid_count || s->trigger_count || s->has_next))
        return fail(error, QA_ERROR_FORMAT, "Cold Q3 scene retains collision membership");
    if (s->has_snap && s->physics_time != (s->physics_next ? s->next.value.server_time : s->snap.value.server_time))
        return fail(error, QA_ERROR_FORMAT, "Q3 physics clock differs from its actual selected prediction snapshot");
    for (size_t i = 0; i < s->solid_count; ++i)
        if (s->solids[i] >= QA_Q3_ENTITY_NONE)
            return fail(error, QA_ERROR_FORMAT, "Invalid Q3 prediction solid entity identity");
    for (size_t i = 0; i < s->trigger_count; ++i)
        if (s->triggers[i] >= QA_Q3_ENTITY_NONE)
            return fail(error, QA_ERROR_FORMAT, "Invalid Q3 prediction trigger entity identity");
    for (size_t i = 0; i < QA_Q3_ENTITIES; ++i) {
        const prediction_entity *e = &s->entities[i];
        if (!qa_vec_finite(e->origin) || !qa_vec_finite(e->angles) ||
            (e->current.number != 0 && e->current.number != (int32_t)i) ||
            (e->next.number != 0 && e->next.number != (int32_t)i) ||
            (e->published && (!s->has_snap || e->publication_message < 0 ||
                e->publication_message > s->snap.value.message_number || e->current.number != (int32_t)i)) ||
            (e->valid && !e->published) ||
            (!e->published && (e->publication_message != 0 || !cold_entity(&e->current) || e->interpolate ||
                e->origin.x != 0 || e->origin.y != 0 || e->origin.z != 0 ||
                e->angles.x != 0 || e->angles.y != 0 || e->angles.z != 0)))
            return fail(error, QA_ERROR_FORMAT, "Invalid retained Q3 centity identity or pose");
    }
    return true;
}
bool qa_q3_prediction_scene_checkpoint(const qa_q3_prediction_scene *s, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !scene_valid(s, error)) return false;
    /* Fixed physical centity array plus two retail snapshots. The field
     * primitive includes all scalar rows, never native structure padding. */
    size_t capacity = 4096 + (2 * QA_Q3_ENTITIES + 512) * Q3_SAVE_ENTITY_BYTES +
        QA_Q3_ENTITIES * 31;
    qa_buffer bytes = {malloc(capacity), 0};
    if (!bytes.data) return fail(error, QA_ERROR_MEMORY, "Capturing Q3 prediction scene");
    qa_net_writer w; qa_net_writer_init(&w, bytes.data, capacity, error);
    bool ok = qa_net_write_data(&w, "QPSC", 4) &&
        qa_net_write_u32(&w, s->product) && qa_net_write_u64(&w, s->revision) &&
        qa_net_write_i32(&w, s->processed) && qa_net_write_i32(&w, s->latest) &&
        qa_net_write_i32(&w, s->time) && qa_net_write_i32(&w, s->physics_time) &&
        qa_net_write_u8(&w, s->has_snap) && qa_net_write_u8(&w, s->has_next) &&
        qa_net_write_u8(&w, s->this_teleport) && qa_net_write_u8(&w, s->next_teleport) &&
        qa_net_write_u8(&w, s->physics_next);
    if (ok && s->has_snap) ok = q3_save_snapshot(&w, &s->snap);
    if (ok && s->has_next) ok = q3_save_snapshot(&w, &s->next);
    if (ok) ok = qa_net_write_u32(&w, (uint32_t)s->solid_count);
    for (size_t i = 0; ok && i < s->solid_count; ++i) ok = qa_net_write_u16(&w, s->solids[i]);
    if (ok) ok = qa_net_write_u32(&w, (uint32_t)s->trigger_count);
    for (size_t i = 0; ok && i < s->trigger_count; ++i) ok = qa_net_write_u16(&w, s->triggers[i]);
    for (size_t i = 0; ok && i < QA_Q3_ENTITIES; ++i) {
        const prediction_entity *e = &s->entities[i];
        const float origin[3] = {e->origin.x, e->origin.y, e->origin.z};
        const float angles[3] = {e->angles.x, e->angles.y, e->angles.z};
        ok = q3_save_entity(&w, &e->current) && q3_save_entity(&w, &e->next) &&
            q3_save_floats(&w, origin, 3) && q3_save_floats(&w, angles, 3) &&
            qa_net_write_u8(&w, e->valid) && qa_net_write_u8(&w, e->interpolate) &&
            qa_net_write_u8(&w, e->published) && qa_net_write_i32(&w, e->publication_message);
    }
    if (!ok) { qa_buffer_free(&bytes); return false; }
    bytes.size = qa_net_writer_size(&w); *out = bytes; return true;
}
bool qa_q3_prediction_scene_restore(qa_bytes bytes, qa_q3_product product,
    qa_q3_prediction_scene **out, qa_error *error)
{
    if (!out || *out || (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA) || (bytes.size && !bytes.data))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 prediction scene import");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error); char magic[4];
    if (!qa_net_read_data(&r, magic, sizeof(magic)) || memcmp(magic, "QPSC", 4) ||
        qa_net_read_u32(&r) != (uint32_t)product)
        return qa_net_reader_fail(&r, "Q3 prediction scene schema or actual product differs");
    qa_q3_prediction_scene *s = NULL;
    if (!qa_q3_prediction_scene_create(product, &s, error)) return false;
    s->revision = qa_net_read_u64(&r); s->processed = qa_net_read_i32(&r); s->latest = qa_net_read_i32(&r);
    s->time = qa_net_read_i32(&r); s->physics_time = qa_net_read_i32(&r);
    s->has_snap = q3_save_bool(&r); s->has_next = q3_save_bool(&r);
    s->this_teleport = q3_save_bool(&r); s->next_teleport = q3_save_bool(&r);
    s->physics_next = q3_save_bool(&r);
    bool ok = !r.failed;
    if (ok && s->has_snap) ok = q3_restore_snapshot(&r, &s->snap, product);
    if (ok && s->has_next) ok = q3_restore_snapshot(&r, &s->next, product);
    if (ok) {
        s->solid_count = qa_net_read_u32(&r);
        if (s->solid_count > 256) ok = qa_net_reader_fail(&r, "Q3 prediction solid list exceeds physical retail capacity");
    }
    for (size_t i = 0; ok && i < s->solid_count; ++i) s->solids[i] = qa_net_read_u16(&r);
    if (ok) {
        s->trigger_count = qa_net_read_u32(&r);
        if (s->trigger_count > 256 || s->solid_count + s->trigger_count > 256)
            ok = qa_net_reader_fail(&r, "Q3 prediction trigger list exceeds physical retail capacity");
    }
    for (size_t i = 0; ok && i < s->trigger_count; ++i) s->triggers[i] = qa_net_read_u16(&r);
    for (size_t i = 0; ok && i < QA_Q3_ENTITIES; ++i) {
        prediction_entity *e = &s->entities[i]; float origin[3], angles[3];
        ok = q3_restore_entity(&r, &e->current) && q3_restore_entity(&r, &e->next) &&
            q3_restore_floats(&r, origin, 3) && q3_restore_floats(&r, angles, 3);
        if (ok) { e->origin = vector(origin); e->angles = vector(angles);
            e->valid = q3_save_bool(&r); e->interpolate = q3_save_bool(&r);
            e->published = q3_save_bool(&r); e->publication_message = qa_net_read_i32(&r); ok = !r.failed; }
    }
    if (ok) ok = qa_net_reader_finish(&r) && scene_valid(s, error);
    if (!ok) { qa_q3_prediction_scene_destroy(s); return false; }
    *out = s; return true;
}
