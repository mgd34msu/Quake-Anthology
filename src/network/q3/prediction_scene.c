/* Snapshot collision continuation follows cg_snapshot.c, cg_ents.c and
 * cg_predict.c, Copyright (C) 1999-2005 Id Software, Inc., GPL-2.0-or-later. */
#include "qa/network_q3_prediction_scene.h"
#include "qa/physics.h"
#include "save_fields.h"
#include <limits.h>

typedef struct prediction_entity {
    qa_q3_entity current, next;
    qa_vec3 origin, angles, clip_origin;
    qa_bounds clip_bounds;
    qa_entity_body_fields fields;
    int32_t publication_message, clip_physics_time, clip_publication, clip_type;
    bool valid, interpolate, published, clip_ready;
} prediction_entity;
struct qa_q3_prediction_scene {
    qa_q3_product product;
    qa_world *world;
    qa_collision_geometry *geometry;
    const qa_actor_id *projected;
    qa_actor_owner owner;
    qa_actor_id solid_actors[256];
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
static void dispose_world(qa_q3_prediction_scene *s)
{
    qa_error ignored={0};
    (void)qa_world_destroy(s->world,&ignored);
    qa_collision_destroy(s->geometry);
    s->world=NULL; s->geometry=NULL; s->projected=NULL; s->owner=0;
}
void qa_q3_prediction_scene_destroy(qa_q3_prediction_scene *s)
{ if (s) { dispose_world(s); free(s->snap.entities); free(s->next.entities); free(s); } }
void qa_q3_prediction_scene_clear(qa_q3_prediction_scene *s)
{
    if (!s) return;
    qa_q3_product product = s->product;
    uint64_t revision = s->revision == UINT64_MAX ? UINT64_MAX : s->revision + 1;
    dispose_world(s);
    free(s->snap.entities); free(s->next.entities); memset(s, 0, sizeof(*s));
    s->product = product; s->revision = revision;
}
bool qa_q3_prediction_scene_prepare(qa_q3_prediction_scene *s, qa_actor_registry *actors,
    qa_collision_geometry *geometry, qa_error *error)
{
    if (!s || !actors || !geometry) return fail(error,QA_ERROR_ARGUMENT,"Q3 scene requires its admitted map and actors");
    if (s->world) return s->geometry==geometry && qa_world_actors(s->world)==actors;
    if (!qa_collision_retain(geometry,error)) return false;
    if (!qa_world_create(actors,geometry,NULL,0,&s->world,error)) {
        qa_collision_destroy(geometry); return false;
    }
    s->geometry=geometry;
    return true;
}
qa_world *qa_q3_prediction_scene_world(const qa_q3_prediction_scene *s)
{ return s?s->world:NULL; }
bool qa_q3_prediction_scene_number_of(const qa_q3_prediction_scene *s, qa_actor_id actor,
    uint32_t *number, bool *present, qa_error *error)
{
    (void)error; *number=QA_Q3_ENTITY_NONE; *present=false;
    if (!s || !s->world || !s->projected) return true;
    const qa_entity_body_fields *fields=qa_world_body_spatial_fields(s->world,actor);
    if (!fields) return true;
    uintptr_t first=(uintptr_t)&s->entities[0].fields, address=(uintptr_t)fields;
    if (address<first || address-first>=sizeof(s->entities) ||
        (address-first)%sizeof(s->entities[0])) return true;
    uint32_t slot=(uint32_t)((address-first)/sizeof(s->entities[0]));
    if (slot>=QA_Q3_ENTITY_NONE || !qa_actor_id_equal(s->projected[slot],actor)) return true;
    *number=slot; *present=true; return true;
}
bool qa_q3_prediction_scene_project(qa_q3_prediction_scene *s, qa_actor_owner owner,
    const qa_actor_id *projected, qa_error *error)
{
    static const qa_vec3 zero={0};
    if (!s || !s->world || !projected) return fail(error,QA_ERROR_ARGUMENT,"Q3 scene publication requires its loaded world");
    s->projected=projected; s->owner=owner;
    /* Bind every admitted physical slot once; fields retain actual currentState
     * storage and presentation poses through cold next-only list selection. */
    for (uint32_t i=0;i<QA_Q3_ENTITY_NONE;++i) {
        qa_actor_id actor=projected[i];
        if (!actor.registry) continue;
        prediction_entity *e=&s->entities[i];
        if (qa_world_body_spatial_fields(s->world,actor)==&e->fields) continue;
        e->fields.pose[QA_ENTITY_CONTROL_POSE].origin=qa_entity_vector_bytes(e->current.origin);
        e->fields.pose[QA_ENTITY_CONTROL_POSE].angles=qa_entity_vector_bytes(e->current.angles);
        e->fields.pose[QA_ENTITY_CLIP_POSE].origin=qa_entity_vector_bytes(&e->origin);
        e->fields.pose[QA_ENTITY_CLIP_POSE].angles=qa_entity_vector_bytes(&zero);
        e->fields.pose[QA_ENTITY_CONTENTS_POSE].origin=qa_entity_vector_bytes(e->current.origin);
        e->fields.pose[QA_ENTITY_CONTENTS_POSE].angles=qa_entity_vector_bytes(e->current.angles);
        e->fields.minimum=qa_entity_vector_bytes(&e->clip_bounds.mins);
        e->fields.maximum=qa_entity_vector_bytes(&e->clip_bounds.maxs);
        qa_entity_body_fields_prepare(&e->fields);
        if (!qa_world_body_spatial_bind(s->world,actor,&e->fields,error)) return false;
    }
    for (size_t i=0;i<s->solid_count;++i) {
        prediction_entity *e=&s->entities[s->solids[i]];
        const qa_q3_entity *row=&e->current;
        qa_actor_id actor=projected[s->solids[i]];
        bool brush=row->solid==0xffffff;
        if (brush) {
            if (row->modelindex<0) return false;
            if (!e->clip_ready || e->clip_physics_time!=s->physics_time ||
                e->clip_publication!=e->publication_message || e->clip_type!=row->pos.type) {
                if (!position(&row->pos,s->physics_time,&e->clip_origin,error)) return false;
                e->clip_physics_time=s->physics_time; e->clip_publication=e->publication_message;
                e->clip_type=row->pos.type; e->clip_ready=true;
            }
            e->fields.pose[QA_ENTITY_CLIP_POSE].origin=qa_entity_vector_bytes(&e->clip_origin);
            e->fields.pose[QA_ENTITY_CLIP_POSE].angles=qa_entity_vector_bytes(&e->angles);
            e->clip_bounds=(qa_bounds){0};
        } else {
            int32_t x=row->solid&255, down=(row->solid>>8)&255, up=((row->solid>>16)&255)-32;
            e->clip_bounds=(qa_bounds){qa_v3(-(float)x,-(float)x,-(float)down),qa_v3((float)x,(float)x,(float)up)};
            e->fields.pose[QA_ENTITY_CLIP_POSE].origin=qa_entity_vector_bytes(&e->origin);
            e->fields.pose[QA_ENTITY_CLIP_POSE].angles=qa_entity_vector_bytes(&zero);
        }
        qa_actor_collision collision={.family=QA_GAME_Q3,.shape=QA_SHAPE_BOX,
            .inline_model=brush,.model=brush?(uint32_t)row->modelindex:0,
            .contents=brush?(qa_collision_bits){UINT64_MAX,UINT64_MAX}:qa_collision_bit(QA_CONTENT_BODY),
            .role=QA_COLLISION_SOLID,.has_q3_owner=true,.q3_entity_number=row->number,.q3_owner_number=QA_Q3_ENTITY_NONE};
        if (!qa_world_set_collision(s->world,actor,&collision,error)) return false;
        qa_linked_body linked;
        if (!qa_world_linked(s->world,actor,&linked) && !qa_world_link(s->world,actor,NULL,error)) return false;
        s->solid_actors[i]=actor;
    }
    qa_world_query_rules rules={.actors=s->solid_actors,.count=s->solid_count,.brush_contents_only=true,
        .merge=QA_WORLD_MERGE_Q3_CLIENT};
    qa_world_set_query_rules(s->world,&rules);
    return true;
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
    return !s->projected || qa_q3_prediction_scene_project(s,s->owner,s->projected,error);
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
    return !s->projected || qa_q3_prediction_scene_project(s,s->owner,s->projected,error);
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
    const qa_q3_prediction_scene_view *v, qa_collision_geometry *geometry, qa_trace_scratch *scratch,
    const qa_q3_prediction_scene_entity_view *retained, qa_vec3 origin, qa_bounds bounds,
    bool *out, qa_error *error)
{
    if (!out || !geometry || qa_collision_geometry_family(geometry) != QA_GAME_Q3 ||
        !qa_q3_prediction_scene_entity_current(s, v, retained))
        return fail(error, QA_ERROR_ARGUMENT, "Q3 trigger query lost its actual scene or map");
    const qa_q3_entity *row = retained->entity; *out = false;
    if ((row->eType != 8 && row->eType != 9) || row->solid != 0xffffff || row->modelindex == 0) return true;
    if (row->modelindex < 0) return fail(error, QA_ERROR_FORMAT, "Negative Q3 trigger inline model");
    qa_trace_query query = {.start = origin, .end = origin, .shape = {QA_SHAPE_BOX, bounds},
        .policy = qa_collision_default_policy(QA_GAME_Q3)};
    qa_trace_result trace;
    if (!qa_collision_trace_q3_model(geometry, scratch, &query, (uint32_t)row->modelindex, false, &trace, error)) return false;
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
static qa_actor_reference pass_source(const qa_q3_prediction_scene *s,
    qa_actor_id actor, qa_actor_reference source)
{
    if (source.kind!=QA_ACTOR_REFERENCE_NONE) return source;
    uint32_t number; bool present; qa_error ignored={0};
    if (!qa_q3_prediction_scene_number_of(s,actor,&number,&present,&ignored) || !present) return (qa_actor_reference){0};
    return qa_actor_reference_source(s->owner,number);
}
static bool scene_trace(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_trace_query *query, qa_trace_result *out, int32_t *number, qa_error *error)
{
    if (!query || !out || !s || !s->world || !qa_q3_prediction_scene_current(s,v)) return false;
    qa_trace_query q=*query; q.target=(qa_collision_target){0}; q.policy.behavior=&qa_trace_behaviors[QA_RULESET_Q3];
    q.pass_source=pass_source(s,q.pass_actor,q.pass_source);
    if (!qa_world_trace(s->world,&q,out,error)) return false;
    *number=out->fraction!=1?QA_Q3_ENTITY_WORLD:QA_Q3_ENTITY_NONE;
    if (out->hit==QA_TRACE_HIT_ACTOR) {
        uint32_t slot; bool present;
        if (!qa_q3_prediction_scene_number_of(s,out->actor,&slot,&present,error) || !present) return false;
        *number=s->entities[slot].current.number;
        out->hit=*number==QA_Q3_ENTITY_WORLD?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_ACTOR;
        out->actor=*number==QA_Q3_ENTITY_WORLD?(qa_actor_id){0}:s->projected[*number];
    } else { out->actor=(qa_actor_id){0}; out->hit=*number==QA_Q3_ENTITY_WORLD?QA_TRACE_HIT_WORLD:QA_TRACE_HIT_NONE; }
    qa_collision_adapt_trace(out,&query->policy);
    return true;
}
bool qa_q3_prediction_scene_trace(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_trace_query *query, qa_trace_result *out, qa_error *error)
{ int32_t number; return scene_trace(s,v,query,out,&number,error); }
bool qa_q3_prediction_scene_trace_with_number(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_trace_query *query, qa_trace_result *out, int32_t *number, qa_error *error)
{ return number && scene_trace(s,v,query,out,number,error); }
bool qa_q3_prediction_scene_point_contents(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_point_query *query, qa_point_contents *out, qa_error *error)
{
    if (!query || !out || !s || !s->world || !qa_q3_prediction_scene_current(s,v)) return false;
    qa_point_query q=*query; q.target=(qa_collision_target){0};
    q.pass_source=pass_source(s,q.pass_actor,q.pass_source);
    return qa_world_point_contents(s->world,&q,out,error);
}
bool qa_q3_prediction_scene_is_bsp(const qa_q3_prediction_scene *s, const qa_q3_prediction_scene_view *v,
    const qa_trace_result *trace, bool *out, qa_error *error)
{
    if (!trace || !out || !qa_q3_prediction_scene_current(s,v)) return false;
    *out=trace->hit==QA_TRACE_HIT_WORLD;
    if (trace->hit==QA_TRACE_HIT_ACTOR) {
        uint32_t slot; bool present;
        if (!qa_q3_prediction_scene_number_of(s,trace->actor,&slot,&present,error) || !present) return false;
        *out=s->entities[slot].current.solid==0xffffff;
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
