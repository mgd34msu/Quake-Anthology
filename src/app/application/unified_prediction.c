#include "unified_prediction.h"
#include "unified_output_json.h"
#include "internal.h"
#include "guest_q3_private.h"
#include "guest_q3_weapons.h"
#include "guest_q3_catalog.h"
#include "qa/application_qc_presentation.h"
#include "qa/application_native_q2_prediction.h"

#include <stdlib.h>
#include <string.h>

typedef application_unified_json json;

static bool text(json *j, const char *v, qa_error *e)
{ return application_unified_json_text(j, v, e); }
static bool number(json *j, double v, qa_error *e)
{ return application_unified_json_number(j, v, e); }
static bool string(json *j, const char *v, qa_error *e)
{ return application_unified_json_string(j, v, e); }
static bool key(json *j, const char *name, qa_error *e)
{ return text(j, ",", e) && string(j, name, e) && text(j, ":", e); }
static bool scalar(json *j, const char *name, double v, qa_error *e)
{ return key(j, name, e) && number(j, v, e); }
static bool boolean(json *j, const char *name, bool v, qa_error *e)
{ return key(j, name, e) && text(j, v ? "true" : "false", e); }
static bool vector(json *j, const char *name, qa_vec3 v, qa_error *e)
{ return key(j, name, e) && application_unified_json_vector(j, v, e); }
static bool triple(json *j, const char *name, double x, double y, double z, qa_error *e)
{
    return key(j, name, e) && text(j, "[", e) && number(j, x, e) && text(j, ",", e) &&
        number(j, y, e) && text(j, ",", e) && number(j, z, e) && text(j, "]", e);
}

static const char *kind(qa_movement_kind k)
{
    switch (k) {
    case QA_MOVEMENT_NETQUAKE: return "q1-netquake";
    case QA_MOVEMENT_QUAKEWORLD: return "q1-quakeworld";
    case QA_MOVEMENT_Q2_CLASSIC: return "q2-classic";
    case QA_MOVEMENT_Q2_RERELEASE: return "q2-rerelease";
    case QA_MOVEMENT_Q3: return "q3";
    }
    return NULL;
}

static bool actor(json *j, const qa_actor_registry *registry, qa_actor_id v, qa_error *e)
{
    if (!v.registry) return text(j, "null", e);
    qa_saved_actor_id witness;
    return qa_actors_save_reference(registry, v, &witness, e) &&
        application_unified_json_actor(j, v, e);
}

static bool ground(json *j, const qa_actor_registry *registry, qa_movement_ground v, qa_error *e)
{
    if (v.hit == QA_TRACE_HIT_NONE) return text(j, "{\"kind\":\"none\"}", e);
    if (v.hit == QA_TRACE_HIT_WORLD)
        return text(j, "{\"kind\":\"world\",\"model\":", e) && number(j, v.model, e) && text(j, "}", e);
    if (v.hit == QA_TRACE_HIT_ACTOR && v.actor.registry)
        return text(j, "{\"kind\":\"actor\",\"actor\":", e) && actor(j, registry, v.actor, e) && text(j, "}", e);
    return application_fail(e, QA_ERROR_FORMAT, "Unified prediction has an invalid physical ground contact");
}

static bool movement(json *j, const qa_actor_registry *registry,
    const qa_movement_state *s, qa_error *e)
{
    if (!kind(s->kind) || !text(j, "{\"kind\":", e) || !string(j, kind(s->kind), e)) return false;
    bool ok = false;
    switch (s->kind) {
    case QA_MOVEMENT_NETQUAKE: {
        const qa_nq_movement_state *v = &s->data.nq;
        ok = vector(j, "origin", v->origin, e) && vector(j, "velocity", v->velocity, e) &&
            vector(j, "angles", v->angles, e) && vector(j, "oldOrigin", v->old_origin, e) &&
            vector(j, "angularVelocity", v->angular_velocity, e) && vector(j, "viewAngles", v->view_angles, e) &&
            vector(j, "punchAngles", v->punch_angles, e) && scalar(j, "moveType", v->move_type, e) &&
            scalar(j, "flags", v->flags, e) && key(j, "ground", e) && ground(j, registry, v->ground, e) &&
            scalar(j, "waterLevel", v->water_level, e) && scalar(j, "waterType", v->water_type, e) &&
            scalar(j, "teleportTimeSeconds", v->teleport_time_seconds, e) &&
            vector(j, "waterJumpDirection", v->water_jump_direction, e) && scalar(j, "idealPitch", v->ideal_pitch, e) &&
            boolean(j, "fixAngle", v->fix_angle, e) && scalar(j, "health", v->health, e);
        break;
    }
    case QA_MOVEMENT_QUAKEWORLD: {
        const qa_qw_movement_state *v = &s->data.qw;
        ok = text(j, ",\"origin\":{\"x\":", e) && number(j, v->origin.x, e) && scalar(j, "y", v->origin.y, e) &&
            scalar(j, "z", v->origin.z, e) && text(j, "}", e) && vector(j, "velocity", v->velocity, e) &&
            vector(j, "angles", v->angles, e) && scalar(j, "oldButtons", v->old_buttons, e) &&
            scalar(j, "waterJumpTimeSeconds", v->water_jump_time_seconds, e) && boolean(j, "dead", v->dead, e) &&
            scalar(j, "spectator", v->spectator, e) && key(j, "ground", e) && ground(j, registry, v->ground, e);
        break;
    }
    case QA_MOVEMENT_Q2_CLASSIC: {
        const qa_q2_movement_state *v = &s->data.q2;
        ok = scalar(j, "type", v->type, e) &&
            triple(j, "originEighths", qa_q2_movement_coordinate(v, false, 0), qa_q2_movement_coordinate(v, false, 1), qa_q2_movement_coordinate(v, false, 2), e) &&
            triple(j, "velocityEighths", qa_q2_movement_coordinate(v, true, 0), qa_q2_movement_coordinate(v, true, 1), qa_q2_movement_coordinate(v, true, 2), e) &&
            scalar(j, "flags", v->flags, e) &&
            (v->wide_coordinates ? text(j, ",\"coordinateStorage\":\"q2pro-extended-v2\"", e) &&
                scalar(j, "timeMilliseconds", v->wide.time_ms, e) : scalar(j, "timeEightMilliseconds", v->time_eight_ms, e)) &&
            scalar(j, "gravity", v->gravity, e) &&
            triple(j, "deltaAngleShorts", v->delta_angle_shorts[0], v->delta_angle_shorts[1], v->delta_angle_shorts[2], e);
        break;
    }
    case QA_MOVEMENT_Q2_RERELEASE: {
        const qa_q2r_movement_state *v = &s->data.q2r;
        ok = scalar(j, "type", v->type, e) && vector(j, "origin", v->origin, e) &&
            vector(j, "velocity", v->velocity, e) && scalar(j, "flags", v->flags, e) &&
            scalar(j, "timeMilliseconds", v->time_ms, e) && scalar(j, "gravity", v->gravity, e) &&
            vector(j, "deltaAngles", v->delta_angles, e) && scalar(j, "viewHeight", v->view_height, e);
        break;
    }
    case QA_MOVEMENT_Q3: {
        const qa_q3_movement_state *v = &s->data.q3;
        ok = scalar(j, "commandTimeMilliseconds", v->command_time_ms, e) &&
            scalar(j, "movementType", v->movement_type, e) && scalar(j, "bobCycle", v->bob_cycle, e) &&
            scalar(j, "movementFlags", v->movement_flags, e) && scalar(j, "movementTimeMilliseconds", v->movement_time_ms, e) &&
            vector(j, "origin", v->origin, e) && vector(j, "velocity", v->velocity, e) &&
            scalar(j, "gravity", v->gravity, e) && scalar(j, "speed", v->speed, e) &&
            triple(j, "deltaAngleWords", v->delta_angle_words[0], v->delta_angle_words[1], v->delta_angle_words[2], e) &&
            scalar(j, "movementDirection", v->movement_direction, e) && vector(j, "grapplePoint", v->grapple_point, e) &&
            scalar(j, "flags", v->flags, e) && vector(j, "viewAngles", v->view_angles, e) &&
            scalar(j, "viewHeight", v->view_height, e) && key(j, "ground", e) && ground(j, registry, v->ground, e) &&
            scalar(j, "predictableEventSequence", v->event_sequence, e) && key(j, "jumpPad", e) && actor(j, registry, v->jump_pad, e) &&
            scalar(j, "movementFrame", v->movement_frame, e) && scalar(j, "jumpPadFrame", v->jump_pad_frame, e);
        break;
    }
    }
    return ok && text(j, "}", e);
}

static bool linked_collision(json *j, const qa_actor_registry *registry,
    const qa_spatial_actor *v, qa_error *e)
{
    const qa_actor_collision *c = &v->collision;
    const char *family = c->family == QA_COLLISION_Q1 ? "q1" : c->family == QA_COLLISION_Q2 ? "q2" :
        c->family == QA_COLLISION_Q3 ? "q3" : NULL;
    const char *role = c->role == QA_COLLISION_SOLID ? "solid" : c->role == QA_COLLISION_TRIGGER ? "trigger" :
        c->role == QA_COLLISION_BOTH ? "both" : NULL;
    const char *shape = c->inline_model ? "model" : c->shape == QA_SHAPE_BOX ? "box" :
        c->shape == QA_SHAPE_CAPSULE ? "capsule" : NULL;
    if (!family || !role || !shape)
        return application_fail(e, QA_ERROR_FORMAT, "Unified prediction collision has no physical family, shape or role");
    if (!text(j, "{\"body\":{\"actor\":", e) || !actor(j, registry, v->body.actor, e) ||
        !text(j, ",\"state\":{\"origin\":", e) || !application_unified_json_vector(j, v->body.state.origin, e) ||
        !vector(j, "angles", v->body.state.angles, e) || !vector(j, "velocity", v->body.state.velocity, e) ||
        !key(j, "bounds", e) || !application_unified_json_bounds(j, v->body.state.bounds, e) ||
        !key(j, "ground", e) || !actor(j, registry, v->body.state.ground, e) || !text(j, "}", e) ||
        !key(j, "linkCount", e) || !application_unified_json_natural(j, v->body.link_count, e) ||
        !key(j, "absoluteBounds", e) || !application_unified_json_bounds(j, v->body.absolute_bounds, e) ||
        !text(j, "},\"collision\":{\"family\":", e) || !string(j, family, e) ||
        !text(j, ",\"shape\":{\"kind\":", e) || !string(j, shape, e) ||
        (c->inline_model && !scalar(j, "model", c->model, e)) || !text(j, "}", e) ||
        !scalar(j, "contents", c->contents, e) || !key(j, "owner", e) || !actor(j, registry, c->owner, e) ||
        !key(j, "role", e) || !string(j, role, e) || !boolean(j, "monster", c->monster, e) ||
        !boolean(j, "deadMonster", c->dead_monster, e)) return false;
    if (c->q1_corpse && !boolean(j, "q1Corpse", true, e)) return false;
    if (c->has_q3_owner && (!text(j, ",\"q3Owner\":{\"entityNumber\":", e) ||
        !number(j, c->q3_entity_number, e) || !scalar(j, "ownerNumber", c->q3_owner_number, e) || !text(j, "}", e))) return false;
    return text(j, "}}", e);
}

static bool collisions(qa_application *app, const application_unified_source *source,
    uint64_t revision, json *j, qa_error *e)
{
    const qa_actor_registry *registry = qa_session_actors(source->session);
    const qa_actor_record *record;
    uint32_t cursor = 0;
    bool first = true;
    if (!text(j, "[", e)) return false;
    while (qa_actors_next(registry, &cursor, &record)) {
        qa_spatial_actor v = {0};
        if (!qa_world_linked(source->world, record->id, &v.body)) continue;
        qa_error observed = {0};
        if (!qa_world_get_collision(source->world, record->id, &v.collision, &observed) ||
            !qa_world_body_read(source->world, record->id, &v.body.state, &observed)) {
            if (observed.code != QA_OK) { if (e) *e = observed; return false; }
            continue;
        }
        if (!application_unified_source_current(app, source) || qa_actors_revision(registry) != revision)
            return application_fail(e, QA_ERROR_ARGUMENT, "Unified prediction collision changed its Source or actor inventory");
        if ((!first && !text(j, ",", e)) || !linked_collision(j, registry, &v, e)) return false;
        first = false;
    }
    return text(j, "]", e);
}

static bool selected_item(qa_application *app, application_provider *p, qa_item_id item,
    const application_q3_catalog_weapon *original_q3, size_t original_q3_count)
{
    if (p == application_world_provider(app, QA_ROLE_ENTITIES, "")) return true;
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        for (int i = 0; i < QA_Q1_WEAPON_COUNT; ++i)
            if (qa_q1_weapon_item(p->state.q1, (qa_q1_weapon)i) == item) return true;
        for (int i = 0; i < QA_Q1_AMMO_COUNT; ++i)
            if (qa_q1_ammo_item(p->state.q1, (qa_q1_ammo)i) == item) return true;
        return false;
    }
    if (p->kind == APPLICATION_PROVIDER_Q2) {
        const char *name = qa_strings_cstr(qa_session_strings(app->session), item);
        for (int i = 1; name && i < QA_Q2_WEAPON_COUNT; ++i) {
            const qa_q2_weapon_definition *v = qa_q2_weapon_definition_at(p->state.q2, (qa_q2_weapon)i);
            if (v && ((v->item && !strcmp(v->item, name)) || (v->ammo && !strcmp(v->ammo, name)))) return true;
        }
        return false;
    }
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        for (int i = 1; i < QA_Q3_WEAPON_COUNT; ++i)
            if (qa_q3_weapon_item(p->state.q3, (qa_q3_weapon)i, false) == item ||
                qa_q3_weapon_item(p->state.q3, (qa_q3_weapon)i, true) == item) return true;
        return false;
    }
    if (p->kind == APPLICATION_PROVIDER_QVM) {
        for (size_t i = 0; i < original_q3_count; ++i)
            if (original_q3[i].item == item || original_q3[i].ammo == item) return true;
        return false;
    }
    return true;
}

static bool inventory(qa_application *app, application_provider *p, qa_actor_id id, json *j, qa_error *e)
{
    size_t count, actual;
    if (!qa_inventory_entries(app->inventory, id, NULL, 0, &count, e)) return false;
    if (count > SIZE_MAX / sizeof(qa_inventory_entry))
        return application_fail(e, QA_ERROR_MEMORY, "Unified prediction inventory exceeds its allocation extent");
    qa_inventory_entry *entries = count ? malloc(count * sizeof(*entries)) : NULL;
    if (count && !entries) return application_fail(e, QA_ERROR_MEMORY, "Allocating Unified prediction inventory");
    bool ok = qa_inventory_entries(app->inventory, id, entries, count, &actual, e);
    if (ok && actual != count) ok = application_fail(e, QA_ERROR_ARGUMENT, "Unified prediction inventory changed during enumeration");
    if (ok) ok = text(j, "[", e);
    const application_q3_catalog_weapon *original_q3 = NULL;
    size_t original_q3_count = 0;
    if (ok && p != application_world_provider(app, QA_ROLE_ENTITIES, "") && p->kind == APPLICATION_PROVIDER_QVM) {
        struct application_q3_guest *engine = q3g_engine(p);
        ok = engine && engine->game && engine->game->catalog &&
            application_q3_catalog_weapons(engine->game->catalog, &original_q3, &original_q3_count, e);
        if (!ok && (!e || e->code == QA_OK)) application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its original Q3 item catalog");
    }
    bool first = true;
    for (size_t i = 0; ok && i < count; ++i) {
        const qa_inventory_entry *v = entries + i;
        if (!selected_item(app, p, v->item, original_q3, original_q3_count)) continue;
        ok = (first || text(j, ",", e)) && application_unified_json_inventory_entry(j,
            qa_session_strings(app->session), v, e);
        first = false;
    }
    free(entries);
    return ok && text(j, "]", e);
}

static bool weapon_q2(json *j, application_provider *p, qa_actor_id id, qa_error *e)
{
    qa_q2_weapon_state v;
    if (!qa_q2_weapon_read(p->state.q2, id, &v, e)) return false;
    int state;
    switch (v.phase) {
    case QA_Q2_READY: state = 0; break;
    case QA_Q2_ACTIVATING: state = 1; break;
    case QA_Q2_DROPPING: state = 2; break;
    case QA_Q2_FIRING: state = 3; break;
    default: return application_fail(e, QA_ERROR_FORMAT, "Unified prediction has an unknown Q2 weapon phase");
    }
    const qa_q2_weapon_definition *pending = v.pending != QA_Q2_WEAPON_NONE ?
        qa_q2_weapon_definition_at(p->state.q2, v.pending) : NULL;
    if (v.pending != QA_Q2_WEAPON_NONE && (!pending || !pending->item))
        return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual pending Q2 weapon definition");
    return text(j, "{\"kind\":\"q2\"", e) && scalar(j, "gunFrame", v.frame, e) && scalar(j, "state", state, e) &&
        key(j, "pendingWeapon", e) && (pending ? string(j, pending->item, e) : text(j, "null", e)) &&
        scalar(j, "machinegunShots", v.machinegun_shots, e) &&
        text(j, ",\"grenadeTime\":{\"kind\":\"seconds\",\"value\":", e) && number(j, (double)v.grenade_ns / 1e9, e) &&
        text(j, "}", e) && boolean(j, "grenadeBlewUp", v.grenade_blew_up, e) && text(j, "}", e);
}

static bool weapon_q3(json *j, application_provider *p, qa_actor_id id, qa_error *e)
{
    application_q3_weapon_prediction v;
    if (p->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state source;
        if (!qa_q3_player_read(p->state.q3, id, &source))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual selected Q3 arsenal player");
        v = (application_q3_weapon_prediction){(int32_t)source.weapon, (int32_t)source.weapon_phase, source.weapon_time_ms};
    } else {
        struct application_q3_guest *engine = q3g_engine(p);
        if (!engine || !engine->game)
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual original Q3 GAME");
        if (engine->game->weapons) {
            if (!application_q3_weapons_prediction_read(engine->game->weapons, id, &v, e)) return false;
        } else {
            qa_q3_player source; uint32_t slot;
            if (!application_q3_guest_actor_client(p, id, &slot) ||
                !qa_q3_host_source_player(engine->game->host, slot, &source, e)) return false;
            v = (application_q3_weapon_prediction){source.weapon, source.weaponState, source.weaponTime};
        }
    }
    return text(j, "{\"kind\":\"q3\"", e) && scalar(j, "sourceWeapon", v.source_weapon, e) &&
        scalar(j, "state", v.state, e) && scalar(j, "timeMilliseconds", v.time_ms, e) && text(j, "}", e);
}

static bool weapon_qc(json *j, qa_application *app, qa_actor_id id, qa_error *e)
{
    qa_application_qc_animation v;
    return qa_application_qc_animation_read(app, id, QA_ROLE_ARSENAL, &v, e) &&
        text(j, "{\"kind\":\"q1\"", e) && scalar(j, "frame", v.frame, e) &&
        scalar(j, "attackFinishedSeconds", v.attack_finished_seconds, e) && scalar(j, "sourceWeapon", v.source_weapon, e) &&
        text(j, "}", e) && qa_application_qc_animation_current(app, &v);
}

static bool weapon_q1(json *j, application_provider *p, qa_actor_id id, qa_error *e)
{
    qa_q1_player_view v;
    if (!qa_q1_player_read(p->state.q1, id, &v))
        return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual selected Q1 arsenal");
    return text(j, "{\"kind\":\"q1\"", e) && scalar(j, "frame", v.weapon_frame, e) &&
        scalar(j, "attackFinishedSeconds", v.attack_finished, e) && scalar(j, "sourceWeapon", v.source_weapon, e) && text(j, "}", e);
}

static bool weapon_native_q2(json *j, qa_application *app, qa_actor_id id, qa_error *e)
{
    qa_application_native_q2_prediction v;
    bool found;
    if (!qa_application_native_q2_prediction_read(app, id, QA_ROLE_ARSENAL, &v, &found, e)) return false;
    if (!found) return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its original Q2 arsenal");
    bool ok = text(j, "{\"kind\":\"q2\"", e) && scalar(j, "gunFrame", v.gun_frame, e) &&
        scalar(j, "state", v.weapon_state, e) && key(j, "pendingWeapon", e) &&
        (v.pending_weapon ? string(j, qa_strings_cstr(qa_session_strings(app->session), v.pending_weapon), e) : text(j, "null", e)) &&
        scalar(j, "machinegunShots", v.machinegun_shots, e) && text(j, ",\"grenadeTime\":{\"kind\":", e);
    if (ok && v.grenade_time_kind == QA_NATIVE_Q2_PREDICTION_SECONDS)
        ok = string(j, "seconds", e) && scalar(j, "value", v.grenade_time.seconds, e);
    else if (ok && v.grenade_time_kind == QA_NATIVE_Q2_PREDICTION_MILLISECONDS) {
        if (v.grenade_time.milliseconds < -(int64_t)QA_UNIFIED_SAFE_INTEGER ||
            v.grenade_time.milliseconds > (int64_t)QA_UNIFIED_SAFE_INTEGER)
            ok = application_fail(e, QA_ERROR_FORMAT, "Native Q2 grenade timer exceeds the exact unified integer domain");
        else ok = string(j, "milliseconds", e) && scalar(j, "value", (double)v.grenade_time.milliseconds, e);
    }
    else if (ok) ok = application_fail(e, QA_ERROR_FORMAT, "Unified prediction has an unknown native Q2 grenade timer domain");
    return ok && text(j, "}", e) && boolean(j, "grenadeBlewUp", v.grenade_blew_up, e) && text(j, "}", e) &&
        qa_application_native_q2_prediction_current(app, &v);
}

static bool arsenal(json *j, qa_application *app, application_provider *p, qa_actor_id id, qa_error *e)
{
    qa_item_id active;
    if (!p->launch || !qa_application_weapon_read(app, id, &active, e) || !text(j, "{\"provider\":", e) ||
        !string(j, p->launch->selection.instance, e) || !key(j, "activeWeapon", e) ||
        !(active ? string(j, qa_strings_cstr(qa_session_strings(app->session), active), e) : text(j, "null", e)) ||
        !key(j, "state", e)) return false;
    bool ok;
    switch (p->kind) {
    case APPLICATION_PROVIDER_Q1: ok = weapon_q1(j, p, id, e); break;
    case APPLICATION_PROVIDER_Q2: ok = weapon_q2(j, p, id, e); break;
    case APPLICATION_PROVIDER_Q3: case APPLICATION_PROVIDER_QVM: ok = weapon_q3(j, p, id, e); break;
    case APPLICATION_PROVIDER_QC: ok = weapon_qc(j, app, id, e); break;
    case APPLICATION_PROVIDER_NATIVE:
        if (!p->state.native.q2_engine) ok = weapon_q3(j, p, id, e);
        else ok = weapon_native_q2(j, app, id, e);
        break;
    default: ok = false; break;
    }
    return ok && key(j, "ammo", e) && inventory(app, p, id, j, e) && text(j, "}", e);
}

static bool animation(json *j, qa_application *app, application_provider *p, qa_actor_id id, qa_error *e)
{
    if (!p->launch || !text(j, "{\"provider\":", e) || !string(j, p->launch->selection.instance, e) ||
        !key(j, "state", e)) return false;
    bool ok = false;
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_character_view v;
        if (!qa_q1_character_read(p->state.q1, id, &v))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual Q1 character animation");
        ok = text(j, "{\"kind\":\"q1\"", e) && scalar(j, "frame", v.animation_frame, e) &&
            scalar(j, "nextFrameSeconds", v.next_frame_seconds, e) && text(j, "}", e);
    } else if (p->kind == APPLICATION_PROVIDER_Q2) {
        qa_q2_player_animation_view v;
        if (!qa_q2_player_animation_read(p->state.q2, id, &v, e)) return false;
        ok = text(j, "{\"kind\":\"q2\"", e) && scalar(j, "frame", v.frame, e) &&
            scalar(j, "endFrame", v.end_frame, e) && scalar(j, "priority", v.priority, e) &&
            boolean(j, "duck", v.duck, e) && boolean(j, "run", v.run, e) && text(j, "}", e);
    } else if (p->kind == APPLICATION_PROVIDER_QC) {
        qa_application_qc_animation v;
        ok = qa_application_qc_animation_read(app, id, QA_ROLE_CHARACTER, &v, e) &&
            text(j, "{\"kind\":\"q1\"", e) && scalar(j, "frame", v.frame, e) &&
            scalar(j, "nextFrameSeconds", v.next_frame_seconds, e) && text(j, "}", e) &&
            qa_application_qc_animation_current(app, &v);
    } else if (p->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state v;
        if (!qa_q3_player_read(p->state.q3, id, &v) || !(v.selections & QA_Q3_CHARACTER))
            return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its actual selected Q3 character animation");
        ok = text(j, "{\"kind\":\"q3\"", e) && scalar(j, "legs", v.legs_animation, e) &&
            scalar(j, "torso", v.torso_animation, e) && scalar(j, "legsTimerMilliseconds", v.legs_timer_ms, e) &&
            scalar(j, "torsoTimerMilliseconds", v.torso_timer_ms, e) && text(j, "}", e);
    } else if (p->kind == APPLICATION_PROVIDER_QVM || (p->kind == APPLICATION_PROVIDER_NATIVE && !p->state.native.q2_engine)) {
        struct application_q3_guest *engine = q3g_engine(p);
        qa_q3_player v; uint32_t slot;
        if (!engine || !engine->game || !application_q3_guest_actor_client(p, id, &slot) ||
            !qa_q3_host_source_player(engine->game->host, slot, &v, e)) return false;
        ok = text(j, "{\"kind\":\"q3\"", e) && scalar(j, "legs", v.legsAnim, e) &&
            scalar(j, "torso", v.torsoAnim, e) && scalar(j, "legsTimerMilliseconds", v.legsTimer, e) &&
            scalar(j, "torsoTimerMilliseconds", v.torsoTimer, e) && text(j, "}", e);
    } else if (p->kind == APPLICATION_PROVIDER_NATIVE && p->state.native.q2_engine) {
        qa_application_native_q2_prediction v;
        bool found;
        if (!qa_application_native_q2_prediction_read(app, id, QA_ROLE_CHARACTER, &v, &found, e)) return false;
        if (!found) return application_fail(e, QA_ERROR_NOT_FOUND, "Unified prediction lost its original Q2 character");
        ok = text(j, "{\"kind\":\"q2\"", e) && scalar(j, "frame", v.animation_frame, e) &&
            scalar(j, "endFrame", v.animation_end, e) && scalar(j, "priority", v.animation_priority, e) &&
            boolean(j, "duck", v.animation_duck, e) && boolean(j, "run", v.animation_run, e) && text(j, "}", e) &&
            qa_application_native_q2_prediction_current(app, &v);
    } else {
        return application_fail(e, QA_ERROR_UNSUPPORTED, "Unified prediction requires its actual selected character animation continuation");
    }
    return ok && text(j, "}", e);
}

static bool environment(json *j, const qa_application_control_prediction_configuration *configuration, qa_error *e)
{
    const qa_movement_environment *v = &configuration->input.environment;
    if (!text(j, "{\"health\":", e) || !number(j, v->health, e) || !boolean(j, "flight", v->flight, e) ||
        !boolean(j, "haste", v->haste, e) || !boolean(j, "invulnerable", v->invulnerable, e) ||
        !scalar(j, "gravityMultiplier", v->gravity_multiplier, e) ||
        !scalar(j, "speedMultiplier", v->speed_multiplier, e) ||
        !boolean(j, "fixedPose", v->fixed_pose, e) || !boolean(j, "fixedCrouched", v->fixed_crouched, e) ||
        !key(j, "poseBounds", e) || !application_unified_json_bounds(j, v->pose.bounds, e) ||
        !scalar(j, "poseViewHeight", v->pose.view_height, e)) return false;
    if (configuration->has_client_view_offset || v->has_mode || v->has_stance || v->has_body_bounds) {
        /* The admitted control getter has already projected the actual mod
         * outputs. Presence bits distinguish source output from defaults. */
        if (!text(j, ",\"clientOutputs\":{", e)) return false;
        bool first = true;
        if (configuration->has_client_view_offset) {
            if (!text(j, "\"viewOffset\":", e) || !application_unified_json_vector(j, configuration->client_view_offset, e)) return false;
            first = false;
        }
        if (v->has_mode) {
            const char *mode = v->mode == QA_MOVEMENT_MODE_NORMAL ? "normal" :
                v->mode == QA_MOVEMENT_MODE_NOCLIP ? "noclip" : v->mode == QA_MOVEMENT_MODE_FREEZE ? "freeze" : NULL;
            if (!mode || (!first && !text(j, ",", e)) || !text(j, "\"mode\":", e) || !string(j, mode, e)) return false;
            first = false;
        }
        if (v->has_stance) {
            if ((!first && !text(j, ",", e)) || !text(j, "\"stance\":", e) || !text(j, v->crouched ? "true" : "false", e)) return false;
            first = false;
        }
        if (v->has_body_bounds && ((!first && !text(j, ",", e)) || !text(j, "\"bodyBounds\":", e) ||
            !application_unified_json_bounds(j, v->body_bounds, e))) return false;
        if (!text(j, "}", e)) return false;
    }
    return text(j, "}", e);
}

static bool parameters(json *j, const qa_q1_movement_parameters *v, qa_error *e)
{
    return text(j, "{\"gravity\":", e) && number(j, v->gravity, e) && scalar(j, "stopSpeed", v->stop_speed, e) &&
        scalar(j, "maxSpeed", v->max_speed, e) && scalar(j, "spectatorMaxSpeed", v->spectator_max_speed, e) &&
        scalar(j, "accelerate", v->accelerate, e) && scalar(j, "airAccelerate", v->air_accelerate, e) &&
        scalar(j, "waterAccelerate", v->water_accelerate, e) && scalar(j, "friction", v->friction, e) &&
        scalar(j, "waterFriction", v->water_friction, e) && scalar(j, "entityGravity", v->entity_gravity, e) && text(j, "}", e);
}

static bool clock_profile(json *j, const qa_movement_profile *p, const qa_clock_config *v, qa_error *e)
{
    if (!text(j, "{\"kind\":", e) || !string(j, kind(p->kind), e)) return false;
    bool ok = false;
    switch (p->kind) {
    case QA_MOVEMENT_NETQUAKE:
        ok = scalar(j, "minimumFrameSeconds", (double)v->minimum_frame_ns / 1e9, e) &&
            scalar(j, "maximumFrameSeconds", (double)v->maximum_frame_ns / 1e9, e) && key(j, "fixedFrameSeconds", e) &&
            (v->interval_ns ? number(j, (double)v->interval_ns / 1e9, e) : text(j, "null", e));
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        ok = scalar(j, "maximumCommandMilliseconds", p->data.qw.maximum_command_ms, e); break;
    case QA_MOVEMENT_Q2_CLASSIC:
        if (v->interval_ns != UINT64_C(100000000))
            return application_fail(e, QA_ERROR_UNSUPPORTED, "Unified Q2 classic prediction requires its actual 100ms source clock");
        ok = scalar(j, "frameMilliseconds", (double)v->interval_ns / 1e6, e); break;
    case QA_MOVEMENT_Q2_RERELEASE:
        ok = scalar(j, "frameMilliseconds", (double)v->interval_ns / 1e6, e) &&
            text(j, ",\"preparation\":\"before-frame\"", e); break;
    case QA_MOVEMENT_Q3:
        ok = scalar(j, "serverFrameMilliseconds", (double)v->interval_ns / 1e6, e) &&
            key(j, "fixedMovementMilliseconds", e) &&
            (p->data.q3.fixed_ms ? number(j, p->data.q3.fixed_ms, e) : text(j, "null", e)) &&
            scalar(j, "maximumCommandMilliseconds", 200, e); break;
    }
    return ok && text(j, "}", e);
}

static bool profile(json *j, qa_application *app,
    const qa_application_control_prediction_configuration *configuration, qa_error *e)
{
    const qa_movement_profile *p = &configuration->input.profile;
    const qa_application_movement_numeric *numeric = &configuration->prediction_numeric;
    qa_strings *strings = qa_session_strings(app->session);
    const char *id = qa_strings_cstr(strings, configuration->profile_id);
    const char *numeric_id = qa_strings_cstr(strings, numeric->id);
    const char *rounding = numeric->rounding == QA_APPLICATION_ROUND_NEAREST ? "nearest-even" :
        numeric->rounding == QA_APPLICATION_ROUND_DOWN ? "toward-negative" :
        numeric->rounding == QA_APPLICATION_ROUND_UP ? "toward-positive" :
        numeric->rounding == QA_APPLICATION_ROUND_ZERO ? "toward-zero" : NULL;
    if (!numeric->native_c || !rounding)
        return application_fail(e, QA_ERROR_UNSUPPORTED, "Unified prediction has no genuine native C arithmetic recipe");
    if (!kind(p->kind) || !id || !numeric_id || !text(j, "{\"kind\":", e) ||
        !string(j, kind(p->kind), e) || !key(j, "id", e) || !string(j, id, e) || !key(j, "clock", e) ||
        !clock_profile(j, p, &configuration->clock, e) || !text(j, ",\"numeric\":{\"id\":", e) || !string(j, numeric_id, e) ||
        !text(j, ",\"arithmetic\":{\"kind\":\"native-c\",\"kernel\":\"qa-movement\",\"version\":1,\"language\":\"c17\",\"contraction\":\"off\"", e) ||
        !scalar(j, "radix", numeric->radix, e) || !scalar(j, "scalarMantissaBits", numeric->scalar_mantissa_bits, e) ||
        !scalar(j, "doubleMantissaBits", numeric->double_mantissa_bits, e) || !scalar(j, "evaluationMethod", numeric->evaluation_method, e) ||
        !key(j, "rounding", e) || !string(j, rounding, e) || !boolean(j, "quakeWorldOriginBinary64", numeric->qw_origin_binary64, e) ||
        !text(j, "},\"scalarStorage\":\"binary32\",\"floatToInt\":\"checked-c-truncation\",\"integerOverflow\":\"wrap32\"}", e)) return false;
    bool ok = false;
    switch (p->kind) {
    case QA_MOVEMENT_NETQUAKE: {
        const char *edition = p->data.nq.edition == QA_Q1_CLASSIC ? "classic" : p->data.nq.edition == QA_Q1_RERELEASE ?
            "rerelease" : p->data.nq.edition == QA_Q1_QUAKE64 ? "quake64" : NULL;
        ok = key(j, "parameters", e) && parameters(j, &p->data.nq.parameters, e) && key(j, "edition", e) && string(j, edition, e) &&
            scalar(j, "edgeFriction", p->data.nq.edge_friction, e) && boolean(j, "noClipAngleHack", p->data.nq.no_clip_angle_hack, e) &&
            scalar(j, "maxVelocity", p->data.nq.max_velocity, e) && scalar(j, "idealPitchScale", p->data.nq.ideal_pitch_scale, e) &&
            scalar(j, "rollSpeed", p->data.nq.roll_speed, e) && scalar(j, "rollAngle", p->data.nq.roll_angle, e) &&
            boolean(j, "noStep", p->data.nq.no_step, e) && boolean(j, "sourceJumpAuthority", p->data.nq.source_jump_authority, e) &&
            boolean(j, "preserveFixAngleRoll", p->data.nq.preserve_fixangle_roll, e);
        break;
    }
    case QA_MOVEMENT_QUAKEWORLD:
        ok = key(j, "parameters", e) && parameters(j, &p->data.qw.parameters, e) &&
            boolean(j, "sharedControls", p->data.qw.shared_controls, e); break;
    case QA_MOVEMENT_Q2_CLASSIC:
        ok = scalar(j, "airAccelerate", p->data.q2.air_accelerate, e) && boolean(j, "snapInitial", p->data.q2.snap_initial, e) &&
            boolean(j, "strafejumpHack", p->data.q2.strafejump_hack, e); break;
    case QA_MOVEMENT_Q2_RERELEASE:
        ok = scalar(j, "airAccelerate", p->data.q2r.air_accelerate, e) && boolean(j, "n64Physics", p->data.q2r.n64_physics, e); break;
    case QA_MOVEMENT_Q3:
        ok = key(j, "product", e) && string(j, p->data.q3.missionpack ? "missionpack" : "baseq3", e) &&
            key(j, "fixedMilliseconds", e) && (p->data.q3.fixed_ms ? number(j, p->data.q3.fixed_ms, e) : text(j, "null", e)) &&
            boolean(j, "noFootsteps", p->data.q3.no_footsteps, e); break;
    }
    return ok && text(j, "}", e);
}

bool application_unified_prediction_build(qa_application *app,
    const application_unified_source *source, qa_net_client_id client,
    const qa_unified_session_player *player, int64_t acknowledged_input,
    qa_unified_document **out, qa_error *e)
{
    if (!app || !source || !player || !out || *out || acknowledged_input < -1 ||
        (acknowledged_input >= 0 && (uint64_t)acknowledged_input > QA_UNIFIED_SAFE_INTEGER) ||
        !application_unified_source_current(app, source) || !application_unified_player_current(app, client, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified prediction requires its actual completed Source player and acknowledgement");
    const qa_actor_registry *registry = qa_session_actors(source->session);
    uint64_t revision = qa_actors_revision(registry);
    qa_application_control_prediction_configuration configuration;
    if (!qa_application_control_prediction_read(app, player->actor, &configuration, e) ||
        !application_control_prediction_numeric_current(app, player->actor, &configuration.prediction_numeric, e)) return false;
    application_provider *selected_movement = application_provider_for(app, player->actor, QA_ROLE_MOVEMENT, "");
    application_provider *selected_arsenal = application_provider_for(app, player->actor, QA_ROLE_ARSENAL, "");
    application_provider *selected_character = application_provider_for(app, player->actor, QA_ROLE_CHARACTER, "");
    if (!selected_movement || !selected_arsenal || !selected_character ||
        configuration.movement != selected_movement->owner || configuration.arsenal != selected_arsenal->owner ||
        configuration.character != selected_character->owner || configuration.input.state.kind != player->movement)
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified prediction lost its real selected movement, arsenal or character");
    const qa_launch_instance *movement_descriptor = selected_movement->launch;
    const qa_launch_instance *arsenal_descriptor = selected_arsenal->launch;
    const qa_launch_instance *character_descriptor = selected_character->launch;
    json j = {0};
    qa_unified_document *document = NULL;
    const qa_movement_input *input = &configuration.input;
    double command_time = input->state.kind == QA_MOVEMENT_Q3 ? input->state.data.q3.command_time_ms :
        (double)source->frame.time_ns / 1e6;
    bool ok = text(&j, "{\"schema\":\"qts-unified-prediction\",\"version\":1,\"actor\":", e) &&
        actor(&j, registry, player->actor, e) && scalar(&j, "sequence", (double)acknowledged_input, e) &&
        scalar(&j, "commandTimeMilliseconds", command_time, e) && key(&j, "profile", e) && profile(&j, app, &configuration, e) &&
        key(&j, "state", e) && movement(&j, registry, &input->state, e) &&
        key(&j, "arsenal", e) && arsenal(&j, app, selected_arsenal, player->actor, e) &&
        key(&j, "animation", e) && animation(&j, app, selected_character, player->actor, e) &&
        key(&j, "standingBounds", e) && application_unified_json_bounds(&j, input->standing.bounds, e) &&
        scalar(&j, "standingViewHeight", input->standing.view_height, e) &&
        text(&j, ",\"nativePostures\":{\"crouchedBounds\":", e) && application_unified_json_bounds(&j, input->crouched.bounds, e) &&
        scalar(&j, "crouchedViewHeight", input->crouched.view_height, e) && key(&j, "deadBounds", e) &&
        application_unified_json_bounds(&j, input->dead.bounds, e) && scalar(&j, "deadViewHeight", input->dead.view_height, e) &&
        key(&j, "invulnerabilityBounds", e) && application_unified_json_bounds(&j, input->invulnerability_bounds, e) && text(&j, "}", e) &&
        key(&j, "bounds", e) && application_unified_json_bounds(&j, input->current_bounds, e) &&
        vector(&j, "viewAngles", configuration.view_angles, e) && scalar(&j, "viewHeight", configuration.view_height, e) &&
        vector(&j, "viewOffset", qa_v3(0, 0, configuration.view_height), e) &&
        key(&j, "environment", e) && environment(&j, &configuration, e) &&
        text(&j, ",\"contact\":{\"ground\":", e) && ground(&j, registry, configuration.ground, e) &&
        scalar(&j, "waterLevel", configuration.water_level, e) && scalar(&j, "waterType", configuration.water_type, e) &&
        text(&j, "}", e) && key(&j, "collisions", e) && collisions(app, source, revision, &j, e);
    if (ok && input->state.kind == QA_MOVEMENT_Q2_RERELEASE)
        ok = vector(&j, "rereleaseOrigin", configuration.q2r_pml_origin, e);
    if (ok) ok = text(&j, "}", e);
    if (ok) ok = qa_unified_document_create(QA_UNIFIED_PREDICTION_DOCUMENT,
        (qa_bytes){j.bytes.data, j.bytes.size}, &document, e);
    if (ok) {
        application_provider *m = application_provider_for(app, player->actor, QA_ROLE_MOVEMENT, "");
        application_provider *a = application_provider_for(app, player->actor, QA_ROLE_ARSENAL, "");
        application_provider *c = application_provider_for(app, player->actor, QA_ROLE_CHARACTER, "");
        ok = application_unified_source_current(app, source) && application_unified_player_current(app, client, player) &&
            qa_actors_revision(registry) == revision && m == selected_movement && a == selected_arsenal && c == selected_character &&
            m->launch == movement_descriptor && a->launch == arsenal_descriptor && c->launch == character_descriptor &&
            m->constructed && m->attached && !m->close_pending && a->constructed && a->attached && !a->close_pending &&
            c->constructed && c->attached && !c->close_pending &&
            application_control_prediction_numeric_current(app, player->actor, &configuration.prediction_numeric, e);
        if (!ok) application_fail(e, QA_ERROR_ARGUMENT, "Unified prediction changed its Source, actor inventory or selected provider receipt");
    }
    application_unified_json_dispose(&j);
    if (!ok) { qa_unified_document_destroy(document); return false; }
    *out = document;
    return true;
}
