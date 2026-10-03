#include "internal.h"
#include "bots_catalog.h"
#include "guest_q3_private.h"
#include "guest_q3_restart.h"
#include "map_players_private.h"
#include "guest_qc_internal.h"
#include "network_q1_signon.h"
#include "network_q1_source.h"
#include "native_q1_wire.h"
#include "native_q3_wire.h"
#include "native_q3_wire_state.h"
#include "native_q3_console.h"
#include "native_q3_clients.h"
#include "native_q3_remote_role.h"
#include "qa/application_network.h"
#include "qa/network_q3_prediction_scene.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_round.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/game_q3_configstrings.h"
#include "qa/physics.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static qa_bytes selected_arsenal(qa_application *application, qa_actor_id actor)
{
    application_provider *provider = application_provider_for(application, actor, QA_ROLE_ARSENAL, "");
    const char *instance = provider && provider->launch ? provider->launch->selection.instance : NULL;
    return instance ? (qa_bytes){(const uint8_t *)instance, strlen(instance)} : (qa_bytes){0};
}

bool qa_application_network_player_next(const qa_application *application, size_t *cursor,
    qa_application_network_player *out)
{
    if (!application || !application->players || !cursor || !out) return false;
    const struct application_player_roster *roster = application->players;
    while (*cursor < roster->count) {
        const application_player_record *record = &roster->records[(*cursor)++];
        if (!record->remote) continue;
        *out = (qa_application_network_player){.client = record->remote_client,
            .seat = record->remote_seat, .actor = record->actor,
            .application_seat = record->seat, .client_slot = record->client_slot,
            .source_slot = record->source_slot, .retiring = record->retiring,
            .deferred = record->deferred, .source_begin_pending = record->source_begin_pending};
        return true;
    }
    return false;
}

static bool q1_native(qa_application *app)
{
    application_provider *primary = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    return primary && primary->kind == APPLICATION_PROVIDER_Q1;
}
static bool q1_native_owner(qa_actor_owner owner, qa_error *error)
{
    return owner || application_fail(error, QA_ERROR_ARGUMENT,
        "Q1 host observation requires its installed source owner");
}
static struct application_qc_state *q1_source(qa_application *app, qa_actor_id player,
    uint32_t *source_slot, qa_error *error)
{
    struct application_qc_state *engine = application_network_q1_qc_source(app, 0, error);
    return engine && application_network_q1_qc_client(engine, player, source_slot, error) ? engine : NULL;
}
static struct application_qc_state *q1_host(qa_application *app, qa_actor_owner owner, qa_error *error)
{
    if (!owner) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q1 host observation requires its installed source owner");
        return NULL;
    }
    return application_network_q1_qc_source(app, owner, error);
}
bool qa_application_network_q1_source(qa_application *app, qa_actor_id player,
    qa_actor_owner *owner, uint32_t *slot, qa_net_protocol_id *protocol, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_source_player(app, player, owner, slot, protocol, error);
    if (!owner || !slot || !protocol) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source observation output");
    struct application_qc_state *engine = q1_source(app, player, slot, error);
    if (!engine) return false;
    *owner = engine->provider->owner; *protocol = engine->protocol; return true;
}
bool qa_application_network_q1_extents(qa_application *app, qa_actor_owner owner,
    uint32_t *clients, uint32_t *entities, qa_error *error)
{
    if (q1_native(app)) return q1_native_owner(owner, error) &&
        application_native_q1_wire_extents(app, owner, clients, entities, error);
    if (!clients || !entities) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source extent outputs");
    struct application_qc_state *engine = q1_host(app, owner, error);
    if (!engine) return false;
    *clients = engine->max_clients; *entities = qa_qc_entity_count(engine->provider->state.qc.instance); return true;
}
static bool q1_wire_scalar(struct application_qc_state *engine, int32_t reference,
    const char *name, uint32_t maximum, uint32_t *out, qa_error *error)
{
    float value;
    if (!application_qc_float(engine, reference, name, &value, error)) return false;
    double integer = trunc((double)value);
    if (!isfinite(value) || integer < 0 || integer > maximum)
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source field exceeds its admitted original wire range");
    *out = (uint32_t)value; return true;
}
qa_cvars *qa_application_network_q1_cvars(qa_application *app, qa_actor_owner owner, qa_error *error)
{
    if (q1_native(app)) return q1_native_owner(owner, error) ?
        application_native_q1_wire_cvars(app, owner, error) : NULL;
    struct application_qc_state *engine = q1_host(app, owner, error);
    return engine ? engine->cvars : NULL;
}
static bool q1_wire_vector(struct application_qc_state *engine, int32_t reference,
    const char *name, float out[3], qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_VECTOR, error); qa_vec3 value;
    if (!field || !qa_qc_entity_vector(engine->provider->state.qc.instance, reference, field->offset, &value, error)) return false;
    if (!qa_vec_finite(value)) return application_fail(error, QA_ERROR_FORMAT, "Nonfinite Q1 source network vector");
    out[0] = value.x; out[1] = value.y; out[2] = value.z; return true;
}
static bool q1_entity_reference(struct application_qc_state *engine, qa_actor_id entity,
    int32_t *out, qa_error *error)
{
    return application_network_q1_qc_entity(engine, entity, NULL, out, error);
}
bool qa_application_network_q1_entity(qa_application *app, qa_actor_id player, qa_actor_id entity,
    qa_q1_entity *out, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_entity(app, player, entity, out, error);
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 entity observation output");
    if (!engine) return false;
    int32_t reference; uint32_t physical;
    if (!application_network_q1_qc_entity(engine, entity, &physical, &reference, error)) return false;
    qa_q1_entity value; qa_q1_entity_init(&value); value.number = physical;
    float movetype;
    if (!q1_wire_scalar(engine, reference, "modelindex", 255, &value.model, error) ||
        !q1_wire_scalar(engine, reference, "frame", 255, &value.frame, error) ||
        !q1_wire_scalar(engine, reference, "colormap", 255, &value.colormap, error) ||
        !q1_wire_scalar(engine, reference, "skin", 255, &value.skin, error) ||
        !q1_wire_scalar(engine, reference, "effects", 255, &value.effects, error) ||
        !q1_wire_vector(engine, reference, "origin", value.origin, error) ||
        !q1_wire_vector(engine, reference, "angles", value.angles, error) ||
        !application_qc_float(engine, reference, "movetype", &movetype, error) || !isfinite(movetype)) return false;
    value.step = movetype == 4;
    if (!qa_actors_get(qa_session_actors(app->session), entity))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 source entity retired during observation");
    *out = value; return true;
}
bool qa_application_network_q1_entity_next(qa_application *app, qa_actor_id player,
    uint32_t *cursor, bool *present, qa_actor_id *actor, qa_q1_entity *out, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_entity_next(app, player, cursor, present, actor, out, error);
    if (!cursor || !present || !actor || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source edict inventory output");
    *present = false;
    uint32_t client; struct application_qc_state *engine = q1_source(app, player, &client, error);
    if (!engine) return false;
    uint32_t count = qa_qc_entity_count(engine->provider->state.qc.instance);
    if (!*cursor) *cursor = 1;
    while (*cursor < count) {
        uint32_t slot = (*cursor)++; qa_qc_slot_binding binding;
        if (!qa_qc_slot(engine->provider->state.qc.instance, slot, &binding))
            return application_fail(error, QA_ERROR_FORMAT, "Q1 source edict inventory exceeds its real allocation");
        if (binding.kind == QA_QC_SLOT_FREE || binding.kind == QA_QC_SLOT_WORLD) continue;
        if (!qa_application_network_q1_entity(app, player, binding.actor, out, error)) return false;
        *actor = binding.actor; *present = true; return true;
    }
    return true;
}
bool qa_application_network_q1_precache(qa_application *app, qa_actor_owner owner,
    bool models, const char *names[255], size_t *count, qa_error *error)
{
    if (q1_native(app)) return q1_native_owner(owner, error) &&
        application_native_q1_wire_precache(app, owner, models, names, count, error);
    struct application_qc_state *engine = q1_host(app, owner, error);
    if (!names || !count) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 precache observation output");
    if (!engine) return false;
    const char *retained[255] = {0}; size_t extent = 0;
    qa_qc_resource_kind kind = models ? QA_QC_RESOURCE_MODEL : QA_QC_RESOURCE_SOUND;
    for (size_t i = 0; i < engine->resource_count; ++i) {
        const application_qc_resource *resource = &engine->resources[i]; if (resource->kind != kind) continue;
        uint32_t index = resource->value.index;
        if (!index || index > 255 || !resource->name || !*resource->name || retained[index - 1])
            return application_fail(error, QA_ERROR_FORMAT, "Q1 source precache ordering differs from admitted wire indices");
        retained[index - 1] = resource->name; if (index > extent) extent = index;
    }
    for (size_t i = 0; i < extent; ++i) if (!retained[i])
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source precache has an unrepresented index");
    memcpy(names, retained, sizeof(retained)); *count = extent; return true;
}

bool qa_application_network_q1_eye(qa_application *app, qa_actor_id player,
    qa_vec3 *out, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_eye(app, player, out, error);
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source eye observation output");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    int32_t reference; float origin[3], offset[3];
    if (!q1_entity_reference(engine, player, &reference, error) ||
        !q1_wire_vector(engine, reference, "origin", origin, error) ||
        !q1_wire_vector(engine, reference, "view_ofs", offset, error)) return false;
    qa_vec3 eye = qa_v3(origin[0] + offset[0], origin[1] + offset[1], origin[2] + offset[2]);
    if (!qa_vec_finite(eye))
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source eye exceeds its finite spatial range");
    *out = eye; return true;
}

static bool q1_wire_string(struct application_qc_state *engine, int32_t reference,
    const char *name, const char **out, qa_error *error)
{
    const qa_qc_definition *field = application_qc_field(engine, name, QA_QC_STRING, error);
    int32_t id;
    return field && qa_qc_entity_int(engine->provider->state.qc.instance, reference, field->offset, &id, error) &&
        qa_qc_string(engine->provider->state.qc.instance, id, out, error);
}
bool qa_application_network_q1_bounds(qa_application *app, qa_actor_id player,
    qa_actor_id entity, qa_bounds *out, bool *has_model, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_bounds(app, player, entity, out, has_model, error);
    if (!out || !has_model) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source visibility observation output");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    int32_t reference; float minimum[3], maximum[3]; uint32_t index; const char *name;
    if (!q1_entity_reference(engine, entity, &reference, error) ||
        !q1_wire_vector(engine, reference, "absmin", minimum, error) ||
        !q1_wire_vector(engine, reference, "absmax", maximum, error) ||
        !q1_wire_scalar(engine, reference, "modelindex", 255, &index, error) ||
        !q1_wire_string(engine, reference, "model", &name, error)) return false;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (minimum[axis] > maximum[axis])
            return application_fail(error, QA_ERROR_FORMAT, "Q1 source absolute bounds are inverted");
    *out = (qa_bounds){qa_v3(minimum[0], minimum[1], minimum[2]),
        qa_v3(maximum[0], maximum[1], maximum[2])};
    *has_model = index && *name; return true;
}
static bool q1_wire_global(struct application_qc_state *engine, const char *name, float *out, qa_error *error)
{
    const qa_qc_definition *field = qa_qc_program_find_global(engine->provider->state.qc.program, name);
    if (!field || field->type != QA_QC_FLOAT)
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source global is missing or has a different type");
    if (!qa_qc_global_float(engine->provider->state.qc.instance, field->offset, out, error)) return false;
    return isfinite(*out) || application_fail(error, QA_ERROR_FORMAT, "Nonfinite Q1 source global");
}
static bool q1_wire_integer(float value, int32_t minimum, int32_t maximum, int32_t *out, qa_error *error)
{
    double integer = trunc((double)value);
    if (!isfinite(value) || integer < minimum || integer > maximum)
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source integer exceeds its original wire range");
    *out = (int32_t)value; return true;
}
/* Original QC floats carry bit masks through the same modulo-32-bit integer
 * conversion as the source protocol. Avoid undefined out-of-range C casts. */
static bool q1_wire_bits(float value, uint32_t *out, qa_error *error)
{
    if (!isfinite(value)) {
        application_fail(error, QA_ERROR_FORMAT, "Nonfinite Q1 source bit mask");
        return false;
    }
    double word = fmod(trunc((double)value), 4294967296.0);
    if (word < 0) word += 4294967296.0;
    *out = (uint32_t)word; return true;
}
static bool q1_standard_quake(qa_application *app, struct application_qc_state *engine, bool *out, qa_error *error)
{
    (void)app;
    const qa_product *product = engine->provider->product;
    if (!product || !product->campaign)
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source product has no original weapon dialect");
    *out = strcmp(product->campaign, "hipnotic") && strcmp(product->campaign, "rogue"); return true;
}
bool qa_application_network_q1_world_read(qa_application *app, qa_actor_owner owner,
    qa_application_network_q1_world *out, qa_error *error)
{
    if (q1_native(app)) return q1_native_owner(owner, error) &&
        application_native_q1_wire_world(app, owner, out, error);
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 world observation output");
    struct application_qc_state *engine = q1_host(app, owner, error);
    if (!engine) return false;
    qa_application_network_q1_world value = {.protocol = engine->protocol, .max_clients = engine->max_clients};
    const qa_cvar_view *deathmatch = qa_cvars_find(engine->cvars, "deathmatch");
    const qa_qc_definition *mapname = qa_qc_program_find_global(engine->provider->state.qc.program, "mapname");
    int32_t id; float number;
    if (!deathmatch || !isfinite(deathmatch->number) || !mapname || mapname->type != QA_QC_STRING)
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source world declaration is incomplete");
    if (!q1_standard_quake(app, engine, &value.standard_quake, error) ||
        !qa_qc_global_int(engine->provider->state.qc.instance, mapname->offset, &id, error) ||
        !qa_qc_string(engine->provider->state.qc.instance, id, &value.map, error) ||
        !q1_wire_string(engine, 0, "message", &value.level, error)) return false;
    value.seconds = (float)((double)engine->source_time_ns / 1e9);
    if (!*value.map) return application_fail(error, QA_ERROR_FORMAT, "Invalid Q1 source world identity");
    if (!*value.level) value.level = value.map;
    value.deathmatch = deathmatch->number != 0;
    const char *names[] = {"total_secrets", "total_monsters", "found_secrets", "killed_monsters"};
    int32_t *stats[] = {&value.total_secrets, &value.total_monsters, &value.found_secrets, &value.killed_monsters};
    for (size_t i = 0; i < 4; ++i)
        if (!q1_wire_global(engine, names[i], &number, error) ||
            !q1_wire_integer(number, INT32_MIN, INT32_MAX, stats[i], error)) return false;
    for (size_t i = 0; i < 64; ++i) value.lightstyles[i] = engine->lightstyles[i] ? engine->lightstyles[i] : "";
    *out = value; return true;
}
bool qa_application_network_q1_clientdata(qa_application *app, qa_actor_id player,
    qa_q1_clientdata *out, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_clientdata(app, player, out, error);
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 clientdata observation output");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    int32_t reference; qa_q1_clientdata value = {0}; float vec[3], scalar; uint32_t flags, extra;
    if (!qa_qc_actor_reference(engine->provider->state.qc.instance, player, false, &reference, error) ||
        !q1_wire_vector(engine, reference, "view_ofs", vec, error)) return false;
    value.viewheight = vec[2];
    if (!application_qc_float(engine, reference, "idealpitch", &value.idealpitch, error) ||
        !q1_wire_vector(engine, reference, "punchangle", value.punch, error) ||
        !q1_wire_vector(engine, reference, "velocity", value.velocity, error) ||
        !application_qc_float(engine, reference, "items", &scalar, error) || !q1_wire_bits(scalar, &value.items, error)) return false;
    const qa_qc_definition *items2 = qa_qc_program_find_field(engine->provider->state.qc.program, "items2");
    if (items2) {
        if (items2->type != QA_QC_FLOAT ||
            !qa_qc_entity_float(engine->provider->state.qc.instance, reference, items2->offset, &scalar, error))
            return application_fail(error, QA_ERROR_FORMAT, "Invalid Q1 source items2 field");
    } else if (!q1_wire_global(engine, "serverflags", &scalar, error)) return false;
    if (!q1_wire_bits(scalar, &extra, error)) return false;
    value.items |= extra << (items2 ? 23 : 28);
    if (!application_qc_float(engine, reference, "flags", &scalar, error) || !q1_wire_bits(scalar, &flags, error)) return false;
    value.onground = (flags & 512) != 0;
    if (!application_qc_float(engine, reference, "waterlevel", &scalar, error) || !isfinite(scalar))
        return application_fail(error, QA_ERROR_FORMAT, "Invalid Q1 source water level");
    value.inwater = scalar >= 2;
    const char *names[] = {"weaponframe", "armorvalue", "currentammo", "ammo_shells", "ammo_nails", "ammo_rockets", "ammo_cells"};
    uint32_t *fields[] = {&value.weapon_frame, &value.armor, &value.ammo, &value.shells, &value.nails, &value.rockets, &value.cells};
    for (size_t i = 0; i < 7; ++i) if (!q1_wire_scalar(engine, reference, names[i], 255, fields[i], error)) return false;
    if (!application_qc_float(engine, reference, "health", &scalar, error) ||
        !q1_wire_integer(scalar, INT16_MIN, INT16_MAX, &value.health, error) ||
        !application_qc_float(engine, reference, "weapon", &scalar, error) || !q1_wire_bits(scalar, &value.weapon, error)) return false;
    bool standard;
    if (!q1_standard_quake(app, engine, &standard, error)) return false;
    if ((standard && value.weapon > 255) || (!standard && value.weapon && (value.weapon & (value.weapon - 1))))
        return application_fail(error, QA_ERROR_FORMAT, "Q1 source active weapon differs from its product dialect");
    const char *weapon_model;
    if (!q1_wire_string(engine, reference, "weaponmodel", &weapon_model, error)) return false;
    if (*weapon_model) {
        for (size_t i = 0; i < engine->resource_count; ++i) {
            const application_qc_resource *r = &engine->resources[i];
            if (r->kind == QA_QC_RESOURCE_MODEL && !strcmp(r->name, weapon_model)) { value.weapon_model = r->value.index; break; }
        }
        if (!value.weapon_model || value.weapon_model > 255)
            return application_fail(error, QA_ERROR_FORMAT, "Q1 source weapon model is not in its ordered precache");
    }
    /* Apply the native writer's actual numeric/dialect guards before publishing
     * a source observation to any caller. The bytes are bounded scratch only. */
    uint8_t bytes[64]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_nq_write_clientdata(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15}, &value, standard)) return false;
    *out = value; return true;
}
bool qa_application_network_q1_status(qa_application *app, qa_actor_owner owner,
    qa_application_network_q1_status_player players[255], size_t *count, qa_error *error)
{
    if (q1_native(app)) return q1_native_owner(owner, error) &&
        application_native_q1_wire_status(app, owner, players, count, error);
    if (!players || !count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source client status output");
    struct application_qc_state *engine = q1_host(app, owner, error);
    if (!engine) return false;
    qa_application_network_q1_status_player values[255]; size_t extent = 0;
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        const application_qc_client *client = engine->clients + i;
        if (!client->connected) continue;
        int32_t reference; float frags; uint32_t bits;
        qa_application_network_q1_status_player value = {.actor = client->actor, .source_slot = i,
            .colors = client->colors, .spawned = client->spawned};
        if (!q1_entity_reference(engine, client->actor, &reference, error) ||
            !q1_wire_string(engine, reference, "netname", &value.name, error) ||
            !application_qc_float(engine, reference, "frags", &frags, error) ||
            !q1_wire_bits(frags, &bits, error)) return false;
        value.frags = bits <= INT32_MAX ? (int32_t)bits : (int32_t)((int64_t)bits - INT64_C(4294967296));
        value.source_frags = frags;
        if ((value.colors >> 4) > 13 || (value.colors & 15u) > 13)
            return application_fail(error, QA_ERROR_FORMAT, "Q1 source client colors exceed the original palette");
        values[extent++] = value;
    }
    memcpy(players, values, extent * sizeof(*values)); *count = extent; return true;
}

bool qa_application_network_q1_chat_recipients(qa_application *app, qa_actor_id sender,
    bool team_only, const char **name, qa_actor_id recipients[255], size_t *count, qa_error *error)
{
    if (q1_native(app)) {
        application_native_q1_wire_source source = {0};
        if (!application_native_q1_wire_begin(app, 0, &source, error)) return false;
        bool okay = application_native_q1_wire_chat(&source, sender, team_only, name, recipients, count, error);
        application_native_q1_wire_end(&source);
        return okay;
    }
    if (!name || !recipients || !count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source chat outputs");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, sender, &slot, error);
    if (!engine) return false;
    int32_t reference; const char *sender_name; float sender_team = 0;
    const qa_cvar_view *teamplay = qa_cvars_find(engine->cvars, "teamplay");
    bool filtered = team_only && teamplay && teamplay->number != 0;
    if (!q1_entity_reference(engine, sender, &reference, error) ||
        !q1_wire_string(engine, reference, "netname", &sender_name, error) ||
        (filtered && !application_qc_float(engine, reference, "team", &sender_team, error))) return false;
    qa_actor_id values[255]; size_t extent = 0;
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        const application_qc_client *client = engine->clients + i;
        if (!client->connected) continue;
        if (!q1_entity_reference(engine, client->actor, &reference, error)) return false;
        if (filtered) {
            float team;
            if (!application_qc_float(engine, reference, "team", &team, error)) return false;
            if (team != sender_team) continue;
        }
        values[extent++] = client->actor;
    }
    memcpy(recipients, values, extent * sizeof(*values)); *name = sender_name; *count = extent;
    return true;
}

bool qa_application_network_q1_kill(qa_application *app, qa_actor_id player, qa_error *error)
{
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    if (app->operation != APPLICATION_IDLE || app->state != QA_APPLICATION_RUNNING ||
        !application_qc_input_idle(engine->provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 ClientKill requires its idle running source owner");
    const application_qc_client *client = engine->clients + slot;
    if (!client->spawned || client->spectator) return true;
    int32_t reference; float health;
    if (!q1_entity_reference(engine, player, &reference, error) ||
        !application_qc_float(engine, reference, "health", &health, error)) return false;
    if (health <= 0) return true;
    qa_qc_game_global globals[] = {
        {"self", {QA_QC_GAME_ACTOR, {.actor = player}}},
        {"other", {QA_QC_GAME_ACTOR, {.actor = {0}}}},
        {"time", {QA_QC_GAME_FLOAT, {.number = (float)((double)engine->source_time_ns / 1e9)}}}
    };
    bool ok = qa_qc_game_call(engine->provider->state.qc.game, "ClientKill", NULL, 0,
        globals, sizeof(globals) / sizeof(*globals), NULL, error);
    if (!ok) application_fault(app, error);
    return ok;
}

bool qa_application_network_q1_pause(qa_application *app, qa_actor_id player,
    qa_buffer *text, bool *changed, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_pause(app, player, text, changed, error);
    if (!text || text->data || text->size || !changed)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 pause requires empty announcement output");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    if (app->operation != APPLICATION_IDLE || app->state != QA_APPLICATION_RUNNING ||
        !application_qc_input_idle(engine->provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 pause requires its idle running source owner");
    const qa_cvar_view *policy = qa_cvars_find(engine->cvars, "pausable");
    const char *denial = policy && policy->number == 0 ? "Pause not allowed.\n" :
        engine->profile == QA_QC_QUAKEWORLD && engine->clients[slot].spectator ? "Spectators can not pause.\n" : NULL;
    bool paused = !qa_application_q1_paused(app);
    const char *name = NULL, *suffix = paused ? " paused the game\n" : " unpaused the game\n";
    if (!denial) {
        const application_player_record *record = NULL;
        for (size_t i = 0; app->players && i < app->players->count; ++i)
            if (qa_actor_id_equal(app->players->records[i].actor, player)) {
                record = app->players->records + i; break;
            }
        if (!record || record->retiring)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 pause lacks its actual source roster admission");
        name = record->name ? record->name : "unconnected";
    }
    size_t length = denial ? strlen(denial) : strlen(name);
    if (!denial && length > SIZE_MAX - strlen(suffix) - 1)
        return application_fail(error, QA_ERROR_MEMORY, "Q1 pause announcement extent overflows");
    if (!denial) length += strlen(suffix);
    if (length > 7998)
        return application_fail(error, QA_ERROR_FORMAT, "Q1 pause announcement exceeds original reliable message extent");
    qa_buffer result = {.data = malloc(length + 1), .size = length};
    if (!result.data) return application_fail(error, QA_ERROR_MEMORY, "Allocating Q1 pause announcement");
    if (denial) memcpy(result.data, denial, length + 1);
    else {
        size_t prefix = strlen(name);
        memcpy(result.data, name, prefix); memcpy(result.data + prefix, suffix, length - prefix + 1);
        if (!application_q1_pause_set(app, engine->provider, paused, error)) {
            qa_buffer_free(&result); return false;
        }
    }
    *text = result; *changed = denial == NULL; return true;
}

bool qa_application_network_q1_name(qa_application *app, qa_actor_id player,
    const char *name, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_name(app, player, name, error);
    if (!name) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 client name");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    if (!application_qc_input_idle(engine->provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 name requires an idle source input owner");
    application_player_record *record = NULL;
    for (size_t i = 0; app->players && i < app->players->count; ++i)
        if (qa_actor_id_equal(app->players->records[i].actor, player)) { record = app->players->records + i; break; }
    if (!record || record->retiring)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 name lacks its actual source roster admission");
    size_t length = strlen(name); if (length > 15) length = 15;
    char *copy = malloc(length + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining Q1 client name");
    memcpy(copy, name, length); copy[length] = 0;
    int32_t reference, string; qa_qc_instance *vm = engine->provider->state.qc.instance;
    const qa_qc_definition *field = application_qc_field(engine, "netname", QA_QC_STRING, error);
    bool ok = field && q1_entity_reference(engine, player, &reference, error) &&
        qa_qc_string_allocate(vm, copy, &string, error) &&
        qa_qc_set_entity_int(vm, reference, field->offset, string, error);
    if (!ok) { free(copy); return false; }
    free(record->name); record->name = copy; return true;
}

bool qa_application_network_q1_colors(qa_application *app, qa_actor_id player,
    int32_t top, int32_t bottom, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_colors(app, player, top, bottom, error);
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    return engine && application_qc_client_colors(engine->provider, player, top, bottom, error);
}

bool qa_application_network_q1_consume_feedback(qa_application *app, qa_actor_id player,
    qa_application_network_q1_feedback *out, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_feedback(app, player, out, error);
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 client feedback output");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    if (engine->profile != QA_QC_NETQUAKE || !engine->clients[slot].spawned ||
        !application_qc_input_idle(engine->provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 feedback requires its idle spawned NetQuake source client");
    qa_application_network_q1_feedback value = {0}; int32_t reference;
    float armor, blood, fixangle;
    if (!q1_entity_reference(engine, player, &reference, error) ||
        !application_qc_float(engine, reference, "dmg_save", &armor, error) ||
        !application_qc_float(engine, reference, "dmg_take", &blood, error) ||
        !application_qc_float(engine, reference, "fixangle", &fixangle, error)) return false;
    if (!isfinite(fixangle)) return application_fail(error, QA_ERROR_FORMAT, "Nonfinite Q1 source view reset");
    value.damage = armor != 0 || blood != 0; value.set_angle = fixangle != 0;
    if (value.damage) {
        const qa_qc_definition *field = application_qc_field(engine, "dmg_inflictor", QA_QC_ENTITY, error);
        int32_t inflictor; uint32_t saved, taken; float origin[3], minimum[3], maximum[3];
        if (!field || !q1_wire_bits(armor, &saved, error) ||
            !q1_wire_bits(blood, &taken, error) ||
            !qa_qc_entity_int(engine->provider->state.qc.instance, reference, field->offset, &inflictor, error) ||
            !q1_wire_vector(engine, inflictor, "origin", origin, error) ||
            !q1_wire_vector(engine, inflictor, "mins", minimum, error) ||
            !q1_wire_vector(engine, inflictor, "maxs", maximum, error)) return false;
        value.armor = (uint8_t)saved; value.blood = (uint8_t)taken;
        for (unsigned axis = 0; axis < 3; ++axis)
            value.origin[axis] = (double)origin[axis] + ((double)minimum[axis] + maximum[axis]) * 0.5;
    }
    if (value.set_angle && !q1_wire_vector(engine, reference, "angles", value.angles, error)) return false;
    uint8_t bytes[32]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_nq_message message = {.op = QA_NQ_SETANGLE};
    qa_net_protocol_id protocol = {.kind = QA_NET_NQ15}; qa_nq_options options = {.standard_quake = true};
    if (value.damage && !qa_nq_write_damage(&writer, value.armor, value.blood, value.origin)) return false;
    memcpy(message.data.angles, value.angles, sizeof(value.angles));
    if (value.set_angle && !qa_nq_write(&writer, protocol, options, &message, NULL, 0)) return false;
    /* Source fields are consumed only after both complete messages qualify.
     * A source setter failure remains an actual failed application operation. */
    if ((value.damage && (!application_qc_set_float(engine, reference, "dmg_save", 0, error) ||
        !application_qc_set_float(engine, reference, "dmg_take", 0, error))) ||
        (value.set_angle && !application_qc_set_float(engine, reference, "fixangle", 0, error))) return false;
    *out = value; return true;
}

bool qa_application_network_q1_baseline(qa_application *app, qa_actor_id player,
    const qa_q1_entity *entity, qa_q1_entity *out, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_baseline(app, player, entity, out, error);
    if (!entity || !out || !entity->number || entity->number > UINT16_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid Q1 source baseline observation");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    qa_q1_entity value = *entity; value.effects = 0; value.step = false;
    value.colormap = 0;
    if (entity->number <= engine->max_clients) {
        value.colormap = entity->number; value.model = 0;
        for (size_t i = 0; i < engine->resource_count; ++i) {
            const application_qc_resource *r = &engine->resources[i];
            if (r->kind == QA_QC_RESOURCE_MODEL && !strcmp(r->name, "progs/player.mdl")) { value.model = r->value.index; break; }
        }
        if (!value.model || value.model > 255)
            return application_fail(error, QA_ERROR_FORMAT, "Q1 source player baseline lacks its original precached model");
    }
    uint8_t bytes[64]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_nq_message message = {.op = QA_NQ_BASELINE, .data.entity = value};
    if (!qa_nq_write(&writer, (qa_net_protocol_id){.kind = QA_NET_NQ15}, (qa_nq_options){.standard_quake = true}, &message, NULL, 0)) return false;
    *out = value; return true;
}
bool qa_application_network_q1_client_baseline(qa_application *app, qa_actor_id player,
    uint32_t source_slot, qa_q1_entity *out, qa_error *error)
{
    if (q1_native(app)) return application_native_q1_wire_client_baseline(app, player, source_slot, out, error);
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing reserved Q1 client baseline output");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    qa_qc_instance *vm = engine->provider->state.qc.instance; qa_qc_slot_binding binding;
    if (!source_slot || source_slot > engine->max_clients || !qa_qc_slot(vm, source_slot, &binding) ||
        (binding.kind != QA_QC_SLOT_FREE && binding.kind != QA_QC_SLOT_BORROWED))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 baseline is not a reserved physical client row");
    qa_q1_entity value; qa_q1_entity_init(&value); value.number = source_slot;
    int32_t reference;
    if (binding.kind == QA_QC_SLOT_BORROWED) {
        uint32_t model; const char *name;
        if (!q1_entity_reference(engine, binding.actor, &reference, error) ||
            !q1_wire_scalar(engine, reference, "modelindex", 255, &model, error) ||
            !q1_wire_string(engine, reference, "model", &name, error)) return false;
        if (model && *name && !qa_application_network_q1_entity(app, player, binding.actor, &value, error)) return false;
    } else if (!qa_qc_slot_reference(vm, source_slot, &reference, error)) return false;
    if (!q1_wire_vector(engine, reference, "origin", value.origin, error) ||
        !q1_wire_vector(engine, reference, "angles", value.angles, error)) return false;
    return qa_application_network_q1_baseline(app, player, &value, out, error);
}
bool qa_application_network_q1_signon_count(qa_application *app, qa_actor_owner owner,
    size_t *out, qa_error *error)
{
    if (q1_native(app)) {
        if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source signon count");
        application_native_q1_wire_source source = {0};
        if (!q1_native_owner(owner, error) || !application_native_q1_wire_begin(app, owner, &source, error)) return false;
        *out = application_q1_signon_count(app, source.provider->owner);
        application_native_q1_wire_end(&source); return true;
    }
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source signon count");
    struct application_qc_state *engine = q1_host(app, owner, error);
    if (!engine) return false;
    *out = application_q1_signon_count(app, engine->provider->owner); return true;
}
bool qa_application_network_q1_signon_at(qa_application *app, qa_actor_owner owner, size_t index,
    qa_application_protocol_event *out, qa_error *error)
{
    if (q1_native(app)) {
        application_native_q1_wire_source source = {0};
        if (!q1_native_owner(owner, error) || !application_native_q1_wire_begin(app, owner, &source, error)) return false;
        bool okay = application_q1_signon_at(app, source.provider->owner, index, out, error);
        application_native_q1_wire_end(&source); return okay;
    }
    struct application_qc_state *engine = q1_host(app, owner, error);
    return engine && application_q1_signon_at(app, engine->provider->owner, index, out, error);
}

bool qa_application_network_controlled(qa_application *application, qa_net_client_id client,
    qa_net_seat_id seat, qa_actor_id actor, qa_movement_kind kind, qa_bytes arsenal, qa_error *error)
{
    qa_actor_id admitted;
    qa_application_control_view control;
    if (!application || !qa_application_remote_player_actor(application, client, seat, &admitted) ||
        !qa_actor_id_equal(actor, admitted) || !qa_application_control_read(application, actor, &control) ||
        control.state.kind != kind)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote command does not own the current controlled actor");
    qa_bytes selected = selected_arsenal(application, actor);
    if (arsenal.size && (arsenal.size != selected.size || !arsenal.data ||
        memcmp(arsenal.data, selected.data, selected.size)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote command selected a foreign arsenal provider");
    return true;
}

bool qa_application_network_command(qa_application *application, const qa_network_command *command,
                                     qa_error *error)
{
    if (!command || !qa_application_network_controlled(application, command->client, command->seat,
        command->actor, command->movement.kind,
        command->has_arsenal ? command->arsenal.provider : (qa_bytes){0}, error)) return false;
    /* Typed arsenal selection is an application owner contract. Until that
     * producer exists, reject intent rather than discarding it after ACK. */
    if (command->has_arsenal && (command->arsenal.weapon.size || command->arsenal.use_holdable))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Application arsenal intent producer is unavailable");
    return qa_application_control_move(application, command->actor, &command->movement, error);
}

bool qa_application_network_resolve(qa_application *application, const qa_net_client *client,
    uint32_t slot, uint32_t generation, qa_net_seat_id *seat,
    qa_unified_controlled_actor *out, qa_error *error)
{
    if (!application || !client || !seat || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing network actor resolution owner");
    for (size_t i = 0; i < client->seat_count; ++i) {
        qa_actor_id actor;
        qa_application_control_view control;
        if (!qa_application_remote_player_actor(application, client->id, client->seats[i].seat, &actor) ||
            actor.slot != slot || actor.generation != generation ||
            !qa_application_control_read(application, actor, &control)) continue;
        *seat = client->seats[i].seat;
        *out = (qa_unified_controlled_actor){.actor = actor, .movement = control.state.kind,
            .arsenal = selected_arsenal(application, actor)};
        return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Packet actor is not owned by an admitted connection seat");
}

bool qa_application_network_detach(qa_application *application, const qa_net_client *client, qa_error *error)
{
    if (!application || !client)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing disconnected roster owner");
    for (size_t i = 0; i < client->seat_count; ++i)
        if (!qa_application_remote_player_detach(application, client->id, client->seats[i].seat, error)) return false;
    return true;
}

bool qa_application_network_command_owner_bound(const qa_application *application)
{
    static const char *const names[] = {"serverlist", "serverquery", "serverfavorite", "servermaster",
        "addip", "removeip", "heartbeat", "maprotation", "nextmap", "download", "downloadstatus", "downloadcancel", "downloadsuspend"};
    qa_console *console = qa_application_console((qa_application *)application);
    if (!console) return false;
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        uint64_t owner = 0;
        if (!qa_console_registration_owner(console, names[i], 0, &owner) || owner != QA_NETWORK_COMMAND_OWNER) return false;
    }
    return true;
}

static bool q3_source_bindings(struct application_q3_guest *engine, qa_error *error)
{
    application_provider *provider = engine->provider;
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(provider->application));
    if (!provider->launch || !choices)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 wire lacks its selected source composition");
    for (size_t i = 0; i < choices->binding_count; ++i) {
        const qa_launch_binding *binding = &choices->bindings[i];
        if (binding->role == QA_ROLE_HUD || binding->role == QA_ROLE_MENU ||
            binding->role == QA_ROLE_AUDIO || binding->role == QA_ROLE_MUSIC) continue;
        if (strcmp(binding->instance, provider->launch->selection.instance))
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 wire cannot represent additional scoped gameplay providers");
    }
    return true;
}

static application_provider *q3_native_host(qa_application *app,
    qa_actor_owner owner, qa_q3_round_source *world, qa_error *error)
{
    application_provider *provider = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    uint8_t snapshot_bit;
    if (!app || app->destroy_requested || app->state != QA_APPLICATION_RUNNING ||
        !provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->owner ||
        (owner && provider->owner != owner) || !provider->constructed || !provider->attached ||
        provider->close_pending || !provider->state.q3 || !provider->native_q3_wire ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !qa_world_idle(app->world) || !application_native_q3_wire_idle(provider) ||
        !qa_q3_round_read(provider->state.q3, world, error) ||
        !application_native_q3_wire_snapshot_bit(provider, &snapshot_bit, error)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 hosting requires its actual idle primary GAME owner");
        return NULL;
    }
    return provider;
}

bool qa_application_network_q3_owner(qa_application *application,
    qa_actor_owner *owner, qa_q3_product *product, qa_error *error)
{
    if (!application || !owner || !product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing primary Q3 source observation output");
    application_provider *provider = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (provider && provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_round_source world;
        provider = q3_native_host(application, 0, &world, error);
        if (!provider) return false;
        *owner = provider->owner; *product = world.product;
        return true;
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !provider->owner || !provider->constructed || !provider->attached ||
        provider->close_pending || !engine->game || !engine->game->host || engine->game->retired)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 wire requires its actual retained primary GAME owner");
    if (!q3_source_bindings(engine, error)) return false;
    for (unsigned role = 0; role < QA_ROLE_COUNT; ++role) {
        if (role == QA_ROLE_HUD || role == QA_ROLE_MENU || role == QA_ROLE_AUDIO || role == QA_ROLE_MUSIC) continue;
        application_provider *selected = application_world_provider(application, (qa_launch_role)role, "");
        if (selected && selected != provider)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 wire cannot represent mixed primary gameplay owners");
    }
    *owner = provider->owner; *product = engine->product; return true;
}

static application_provider *q3_host_source(qa_application *app,
    qa_actor_owner owner, qa_error *error)
{
    qa_actor_owner actual; qa_q3_product product;
    if (!qa_application_network_q3_owner(app, &actual, &product, error)) return NULL;
    if (!owner || owner != actual) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 host observation names another primary source owner"); return NULL;
    }
    return application_world_provider(app, QA_ROLE_ENTITIES, "");
}

qa_cvars *qa_application_network_q3_host_cvars(qa_application *app,
    qa_actor_owner owner, qa_error *error)
{
    application_provider *provider = q3_host_source(app, owner, error);
    qa_cvars *cvars = NULL;
    if (!provider) return NULL;
    if (provider->kind == APPLICATION_PROVIDER_Q3)
        cvars = application_native_q3_console_registry(provider);
    else {
        struct application_q3_guest *engine = q3g_engine(provider);
        (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    }
    if (!cvars) application_fail(error, QA_ERROR_ARGUMENT, "Q3 primary source cvar owner is unavailable");
    return cvars;
}
qa_vfs *qa_application_network_q3_content(qa_application *app,
    qa_actor_owner owner, qa_error *error)
{
    application_provider *provider = q3_host_source(app, owner, error);
    if (!provider || !provider->launch || !provider->launch->content) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 primary source has no actual mounted content owner"); return NULL;
    }
    return provider->launch->content;
}
bool qa_application_network_q3_content_product(qa_application *app, qa_actor_owner owner,
    qa_product_id *out, qa_error *error)
{
    application_provider *provider = q3_host_source(app, owner, error);
    if (!provider || !provider->launch || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 primary source lacks its admitted launch product identity");
    *out = provider->launch->selection.product; return true;
}
bool qa_application_network_q3_host_capacity(qa_application *app,
    qa_actor_owner owner, uint32_t *out, qa_error *error)
{
    application_provider *provider = q3_host_source(app, owner, error);
    if (!provider || !out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 constructor capacity output");
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_round_source world;
        if (!qa_q3_round_read(provider->state.q3, &world, error)) return false;
        *out = world.max_clients;
    } else {
        struct application_q3_guest *engine = q3g_engine(provider);
        if (!engine->loaded_compatibility || engine->loaded_max_clients < 1 || engine->loaded_max_clients > 64)
            return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 GAME did not retain its actual initialized client capacity");
        *out = (uint32_t)engine->loaded_max_clients;
    }
    return *out >= 1 && *out <= 64;
}

bool qa_application_network_q3_host_slots(qa_application *app, qa_actor_owner owner,
    qa_application_network_q3_host_slot slots[64], qa_error *error)
{
    if (!slots) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 host slot observation");
    application_provider *provider = q3_host_source(app, owner, error);
    if (!provider) return false;
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_round_source world;
        if (!qa_q3_round_read(provider->state.q3, &world, error)) return false;
        memset(slots, 0, 64 * sizeof(*slots));
        for (uint32_t i = 0; i < world.max_clients; ++i) {
            qa_q3_source_binding binding; qa_q3_native_client client;
            application_native_q3_wire_client_view wire;
            bool admitted;
            if (!qa_q3_source_binding_read(provider->state.q3, i, &binding, error) ||
                !qa_q3_client_slot_read(provider->state.q3, i, &client, error) ||
                !application_native_q3_wire_client_admission_read(provider, i, &wire, &admitted, error)) return false;
            slots[i] = (qa_application_network_q3_host_slot){
                .occupied = binding.actor.registry != 0 || client.connected != QA_Q3_CLIENT_DISCONNECTED,
                .bot = admitted && wire.bot};
        }
        return true;
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    for (size_t i = 0; i < 64; ++i) {
        const q3g_client *client = &engine->clients[i];
        slots[i] = (qa_application_network_q3_host_slot){
            .occupied = client->allocated || client->connected || client->pending_retirement,
            .bot = client->allocated && client->bot};
    }
    return true;
}

bool qa_application_network_q3_drop_bot(qa_application *app, qa_actor_owner owner,
    uint32_t slot, qa_error *error)
{
    application_provider *provider = q3_host_source(app, owner, error);
    uint32_t capacity;
    if (!provider || !qa_application_network_q3_host_capacity(app, owner, &capacity, error)) return false;
    if (slot >= capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 bot replacement exceeds its actual source capacity");
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        application_native_q3_wire_client_view wire;
        bool admitted;
        if (!application_native_q3_wire_client_admission_read(provider, slot, &wire, &admitted, error)) return false;
        if (!admitted || !wire.bot)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 bot replacement has no genuine source bot admission");
        return application_bots_catalog_remove_begin(app, slot, error) &&
            application_native_q3_wire_drop(provider, slot, "only bots on server", error) &&
            application_native_q3_clients_drain(app, error);
    }
    const q3g_client *client = &q3g_engine(provider)->clients[slot];
    if (!client->allocated || !client->bot || client->pending_retirement)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 bot replacement has no genuine original source bot admission");
    return application_bots_catalog_remove_begin(app, slot, error) &&
        application_q3_guest_client_disconnect(provider, slot, error);
}

bool qa_application_network_q3_host_baselines(qa_application *app, qa_actor_owner owner,
    qa_q3_gamestate *out, qa_error *error)
{
    application_provider *provider = q3_host_source(app, owner, error);
    return provider && application_q3_wire_host_baselines(provider, out, error);
}

static struct application_q3_guest *source(qa_application *application, qa_actor_id actor,
                                           uint32_t *slot, qa_error *error)
{
    application_provider *provider = application ?
        application_provider_for(application, actor, QA_ROLE_CHARACTER, "") : NULL;
    struct application_q3_guest *engine = provider ? q3g_engine(provider) : NULL;
    if (!engine || !engine->game || !engine->game->host ||
        !qa_q3_host_actor_slot(engine->game->host, actor, slot, error)) {
        application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 snapshot requires the selected qualified game host");
        return NULL;
    }
    /* Every selected source gameplay role must be represented by this game.
     * A native protocol cannot silently serialize only one mixed component. */
    for (unsigned role = 0; role < QA_ROLE_COUNT; ++role) {
        if (role == QA_ROLE_HUD || role == QA_ROLE_MENU || role == QA_ROLE_AUDIO || role == QA_ROLE_MUSIC) continue;
        application_provider *selected = application_provider_for(application, actor, (qa_launch_role)role, "");
        if (selected && selected != provider) {
            application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 wire cannot represent mixed selected gameplay owners");
            return NULL;
        }
    }
    if (!q3_source_bindings(engine, error)) return NULL;
    return engine;
}

bool qa_application_network_q3_source(qa_application *application, qa_actor_id actor,
    uint32_t *slot, qa_q3_product *product, qa_error *error)
{
    if (!slot || !product) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 source observation output");
    application_provider *primary = application ?
        application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_round_source world;
        qa_q3_native_client client;
        application_native_q3_wire_client_view admitted;
        bool present;
        uint32_t physical;
        if (application->destroy_requested || !primary->constructed || !primary->attached ||
            primary->close_pending || !primary->state.q3 || !primary->native_q3_wire ||
            !qa_session_safe(application->session) || qa_session_faulted(application->session) ||
            !qa_world_idle(application->world) ||
            !qa_q3_round_read(primary->state.q3, &world, error) ||
            !qa_q3_native_client_slot(primary->state.q3, actor, &physical, error) ||
            !qa_q3_client_slot_read(primary->state.q3, physical, &client, error) ||
            client.connected == QA_Q3_CLIENT_DISCONNECTED ||
            !application_native_q3_wire_client_admission_read(primary, physical, &admitted, &present, error) ||
            !present || !qa_actor_id_equal(admitted.actor, actor) || !admitted.userinfo)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Native Q3 source observation requires its actual admitted physical GAME client");
        *slot = physical; *product = world.product;
        return true;
    }
    struct application_q3_guest *engine = source(application, actor, slot, error);
    if (!engine) return false;
    *product = engine->product; return true;
}

bool qa_application_network_q3_client_bound(qa_application *app, qa_actor_owner owner,
    qa_actor_id actor, uint32_t slot)
{
    application_provider *provider = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!app || !owner || !provider || provider->owner != owner || !provider->constructed ||
        !provider->attached || provider->close_pending || app->destroy_requested ||
        !qa_actors_get(qa_session_actors(app->session), actor)) return false;
    uint32_t actual;
    if (provider->kind == APPLICATION_PROVIDER_Q3) {
        const char *userinfo;
        return provider->state.q3 && provider->native_q3_wire &&
            qa_q3_native_client_slot(provider->state.q3, actor, &actual, NULL) && actual == slot &&
            application_native_q3_wire_userinfo_read(provider, slot, &userinfo, NULL);
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    return engine && slot < 64 && engine->game && engine->game->host && !engine->game->retired &&
        engine->clients[slot].connected && qa_actor_id_equal(engine->clients[slot].actor, actor) &&
        qa_q3_host_actor_slot(engine->game->host, actor, &actual, NULL) && actual == slot;
}

static bool q3_wire_time(const struct application_q3_guest *engine, int32_t *out, qa_error *error)
{
    qa_clock_state clock;
    if (!engine || !out || !qa_session_clock(engine->provider->application->session, engine->provider->owner, &clock) ||
        clock.frame.provider != engine->provider->owner || clock.frame.kind != QA_CLOCK_Q3)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 wire time lacks its actual source server clock");
    uint32_t bits = (uint32_t)(clock.frame.time_ns / UINT64_C(1000000));
    memcpy(out, &bits, sizeof(bits));
    return true;
}
static application_provider *q3_native_actor(qa_application *app, qa_actor_id actor,
    uint32_t *slot, qa_error *error)
{
    qa_q3_product product;
    application_provider *provider = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !qa_application_network_q3_source(app, actor, slot, &product, error)) return NULL;
    return provider;
}
bool qa_application_network_q3_command(qa_application *app,
    const qa_network_q3_source_command *command, qa_error *error)
{
    if (!command || !command->sequence ||
        !qa_application_network_controlled(app, command->client, command->seat,
            command->actor, command->movement, (qa_bytes){0}, error)) return false;
    uint32_t slot; qa_q3_product product;
    if (!qa_application_network_q3_source(app, command->actor, &slot, &product, error)) return false;
    return qa_application_control_q3_command(app, command->actor, command->sequence,
        &command->command, error);
}
bool qa_application_network_nq_command(qa_application *app,
    const qa_network_nq_source_command *command, qa_error *error)
{
    if (!command || !command->sequence ||
        !qa_application_network_controlled(app, command->client, command->seat,
            command->actor, command->movement, (qa_bytes){0}, error)) return false;
    qa_actor_owner owner; uint32_t slot; qa_net_protocol_id protocol;
    if (!qa_application_network_q1_source(app, command->actor, &owner, &slot, &protocol, error)) return false;
    if (owner != command->source_owner || slot != command->source_slot ||
        protocol.kind != QA_NET_NQ15 || protocol.flags || protocol.revision)
        return application_fail(error, QA_ERROR_ARGUMENT, "NetQuake command lost its actual source client binding");
    return qa_application_control_nq_command(app, command->actor, command->sequence,
        &command->command, error);
}
bool qa_application_network_q3_enter(qa_application *app, qa_net_client_id client,
    qa_net_seat_id seat, const qa_q3_usercmd *command, qa_error *error)
{
    qa_actor_id actor;
    if (!app || !command || !qa_application_remote_player_actor(app, client, seat, &actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 enter command has no admitted canonical remote actor");
    application_provider *primary = application_world_provider(app, QA_ROLE_ENTITIES, "");
    uint32_t slot;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        application_provider *provider = q3_native_actor(app, actor, &slot, error);
        if (!provider || !application_native_q3_wire_command_seed(provider, slot, command, error)) return false;
    } else {
        struct application_q3_guest *engine = source(app, actor, &slot, error);
        if (!engine || !application_q3_guest_client_enter_command(engine->provider,
            slot, actor, command, error)) return false;
    }
    return qa_application_remote_player_begin(app, client, seat, error);
}
static bool q3_native_world(application_provider *provider, int32_t server_id,
    int32_t restarted_server_id, int32_t feed, qa_q3_server_world *out, qa_error *error)
{
    if (!out || server_id <= 0 || restarted_server_id <= 0 || restarted_server_id > server_id)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 world requires its actual wire identity and output");
    int32_t milliseconds;
    qa_cvars *cvars = application_native_q3_console_registry(provider);
    const qa_cvar_view *pure = cvars ? qa_cvars_find(cvars, "sv_pure") : NULL;
    const qa_cvar_view *flood = cvars ? qa_cvars_find(cvars, "sv_floodProtect") : NULL;
    if (!pure || !flood)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q3 engine source policies are not registered");
    if (!application_q3_wire_time(provider, &milliseconds, error)) return false;
    *out = (qa_q3_server_world){.generation = qa_application_configuration_generation(provider->application),
        .server_id = server_id, .restarted_server_id = restarted_server_id,
        .checksum_feed = feed, .time = milliseconds,
        .pure = pure->integer != 0, .flood_protect = flood->integer != 0};
    return true;
}
bool qa_application_network_q3_world(qa_application *application, qa_actor_id actor,
    int32_t server_id, int32_t restarted_server_id, int32_t feed,
    qa_q3_server_world *out, qa_error *error)
{
    uint32_t slot;
    application_provider *primary = application ? application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        application_provider *provider = q3_native_actor(application, actor, &slot, error);
        return provider && q3_native_world(provider, server_id, restarted_server_id, feed, out, error);
    }
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !out || server_id <= 0 || restarted_server_id <= 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 wire world requires its retained source identity");
    int32_t milliseconds;
    if (!q3_wire_time(engine, &milliseconds, error)) return false;
    qa_cvars *cvars = NULL;
    (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    if (!cvars) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source cvar owner is unavailable");
    const qa_cvar_view *pure = qa_cvars_find(cvars, "sv_pure");
    const qa_cvar_view *flood = qa_cvars_find(cvars, "sv_floodProtect");
    *out = (qa_q3_server_world){.generation = qa_application_configuration_generation(application),
        .server_id = server_id, .restarted_server_id = restarted_server_id,
        .checksum_feed = feed, .time = milliseconds,
        .pure = pure && pure->integer != 0, .flood_protect = !flood || flood->integer != 0};
    return true;
}
qa_cvars *qa_application_network_q3_cvars(qa_application *application, qa_actor_id actor)
{
    uint32_t slot; qa_cvars *cvars = NULL;
    application_provider *primary = application ? application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        application_provider *provider = q3_native_actor(application, actor, &slot, NULL);
        return provider ? application_native_q3_console_registry(provider) : NULL;
    }
    struct application_q3_guest *engine = source(application, actor, &slot, NULL);
    if (engine) (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    return cvars;
}
static bool q3_status(struct application_q3_guest *engine,
    qa_application_network_q3_status_player players[64], size_t *count, qa_error *error)
{
    if (!engine || !players || !count) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 status observation output");
    *count = 0;
    for (uint32_t i = 0; i < 64; ++i) {
        const q3g_client *client = &engine->clients[i];
        if (!client->connected || client->pending_retirement) continue;
        qa_q3_player player;
        if (!qa_q3_host_source_player(engine->game->host, i, &player, error)) return false;
        qa_application_network_q3_status_player value = {.slot = i,
            .score = player.persistant[0], .ping = player.ping};
        if (!qa_q3_info_value(client->userinfo ? client->userinfo : "", "name",
                value.name, sizeof(value.name), error)) return false;
        players[(*count)++] = value;
    }
    return true;
}
bool qa_application_network_q3_host_status(qa_application *app, qa_actor_owner owner,
    qa_application_network_q3_status_player players[64], size_t *count, qa_error *error)
{
    application_provider *provider = q3_host_source(app, owner, error);
    if (!provider || !players || !count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 host status observation");
    if (provider->kind != APPLICATION_PROVIDER_Q3)
        return q3_status(q3g_engine(provider), players, count, error);
    qa_q3_round_source world;
    if (!qa_q3_round_read(provider->state.q3, &world, error)) return false;
    *count = 0;
    for (uint32_t slot = 0; slot < world.max_clients; ++slot) {
        application_native_q3_wire_client_view client;
        bool present, dropping; const char *reason;
        if (!application_native_q3_wire_drop_read(provider, slot, &reason, &dropping, error)) return false;
        if (dropping) continue;
        if (!application_native_q3_wire_client_read(provider, slot, &client, &present, error)) return false;
        if (!present) continue;
        qa_q3_player player; qa_q3_native_client source_client;
        if (!qa_q3_wire_player_read(provider->state.q3, slot, &player, error) ||
            !qa_q3_client_slot_read(provider->state.q3, slot, &source_client, error)) return false;
        qa_application_network_q3_status_player value = {.slot = slot,
            .score = player.persistant[0], .ping = player.ping};
        memcpy(value.name, source_client.netname, sizeof(source_client.netname));
        players[(*count)++] = value;
    }
    return true;
}
static bool q3_packages_valid(qa_application *app, qa_actor_owner owner, int32_t feed,
    const qa_application_network_q3_package_view *packages, qa_error *error)
{
    return (packages && packages->owner == owner && packages->content && packages->references &&
        packages->content == qa_application_network_q3_content(app, owner, error) &&
        packages->source_generation == qa_application_configuration_generation(app) &&
        packages->read_generation == qa_vfs_read_generation(packages->content) &&
        packages->read_count == qa_vfs_read_count(packages->content) &&
        qa_q3_pak_checksum_feed(packages->references) == (uint32_t)feed) ||
        application_fail(error, QA_ERROR_FORMAT, "Q3 pure wire lacks its genuine primary package owner and complete opened-resource cut");
}
bool qa_application_network_q3_signon(qa_application *application, qa_actor_id actor,
    int32_t server_id, int32_t feed, const qa_application_network_q3_package_view *packages,
    qa_q3_gamestate *out, qa_q3_server_world *world, qa_error *error)
{
    uint32_t slot;
    application_provider *primary = application ? application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        application_provider *provider = q3_native_actor(application, actor, &slot, error);
        if (!provider || !out || !world)
            return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q3 signon source or output");
        if (!qa_application_network_q3_round_prepare(application, provider->owner,
            server_id, server_id, feed, packages, world, error)) return false;
        if (!q3_native_actor(application, actor, &slot, error)) return false;
        qa_q3_gamestate_init(out);
        out->client_number = (int32_t)slot; out->checksum_feed = feed;
        for (uint32_t index = 0; index < QA_Q3_NATIVE_CONFIGSTRINGS; ++index) {
            const char *text;
            if (!qa_q3_configstring_read(provider->state.q3, index, &text, error) ||
                !qa_q3_configstring_set(out, index, text, error)) return false;
        }
        return application_q3_wire_host_baselines(provider, out, error);
    }
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !out || !world || !qa_application_network_q3_world(application, actor, server_id, server_id, feed, world, error)) return false;
    if (world->pure && !q3_packages_valid(application, engine->provider->owner, feed, packages, error)) return false;
    char identity[32]; snprintf(identity, sizeof(identity), "%d", server_id);
    qa_cvars *cvars = qa_application_network_q3_cvars(application, actor);
    if (!cvars || !qa_cvars_register(cvars, "sv_serverid", identity, QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY,
        engine->provider->owner, "Original Q3 server identity", error) ||
        !qa_cvars_set(cvars, "sv_serverid", identity, true, error)) return false;
    qa_buffer system = {0}, server = {0};
    bool ok = qa_cvars_info(cvars, QA_CVAR_SYSTEMINFO, QA_Q3_BIG_INFO_CHARS, &system, error) &&
        qa_cvars_info(cvars, QA_CVAR_SERVERINFO, 1024, &server, error);
    if (ok) {
        *out = engine->gamestate; out->client_number = (int32_t)slot; out->checksum_feed = feed;
        ok = qa_q3_configstring_set(out, 0, (const char *)server.data, error) &&
            qa_q3_configstring_set(out, 1, (const char *)system.data, error);
        if (ok) ok = application_q3_wire_host_baselines(engine->provider, out, error);
    }
    qa_buffer_free(&system); qa_buffer_free(&server); return ok;
}
bool qa_application_network_q3_userinfo(qa_application *application, qa_actor_id actor,
    const char *text, qa_error *error)
{
    uint32_t slot;
    application_provider *primary = application ? application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        application_provider *provider = q3_native_actor(application, actor, &slot, error);
        return provider && application_native_q3_wire_userinfo(provider, slot, text, error) &&
            application_native_q3_client_userinfo_changed(provider, actor, error);
    }
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    return engine && application_q3_guest_client_userinfo(engine->provider, slot, text, error);
}

bool qa_application_network_q3_userinfo_read(qa_application *application, qa_actor_id actor,
    const char **out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing retained Q3 userinfo output");
    uint32_t slot;
    application_provider *primary = application ? application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        application_provider *provider = q3_native_actor(application, actor, &slot, error);
        return provider && application_native_q3_wire_userinfo_read(provider, slot, out, error);
    }
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    return engine && application_q3_guest_round_userinfo(engine->provider, slot, out, error);
}

static struct application_q3_guest *round_source(qa_application *application, qa_actor_owner owner, qa_error *error)
{
    application_provider *provider = application ? application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    struct application_q3_guest *engine = provider ? q3g_engine(provider) : NULL;
    if (!owner || !engine || provider->owner != owner || !provider->constructed || !provider->attached ||
        application->operation != APPLICATION_IDLE || application->state != QA_APPLICATION_RUNNING ||
        !qa_session_safe(application->session) || !qa_world_idle(application->world) ||
        engine->calls || engine->draining_clients || !engine->game || !engine->game->host) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 round wire world requires its exact idle retained primary GAME owner");
        return NULL;
    }
    if(!qa_q3_host_round_ready(engine->game->host,error)) return NULL;
    return engine;
}

bool qa_application_network_q3_round_world(qa_application *application, qa_actor_owner owner,
    int32_t server_id, int32_t restarted_server_id, int32_t feed,
    const qa_application_network_q3_package_view *packages, qa_q3_server_world *out, qa_error *error)
{
    if (!out || server_id <= 0 || restarted_server_id <= 0 || restarted_server_id > server_id)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 round world requires its admitted wire identity and output");
    application_provider *primary = application ? application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_round_source source_world;
        application_provider *provider = owner ? q3_native_host(application, owner, &source_world, error) : NULL;
        if (!provider || application->operation != APPLICATION_IDLE)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 round world requires its exact idle source owner");
        if (!q3_native_world(provider, server_id, restarted_server_id, feed, out, error)) return false;
        if (out->pure && !q3_packages_valid(application, owner, feed, packages, error)) return false;
        return true;
    }
    struct application_q3_guest *engine = round_source(application, owner, error);
    if (!engine) return false;
    int32_t milliseconds; qa_cvars *cvars = NULL;
    if (!application_q3_guest_round_clock(engine->provider, &milliseconds, error) ||
        !q3_wire_time(engine, &milliseconds, error)) return false;
    (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    if (!cvars) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 retained GAME cvar owner is unavailable");
    const qa_cvar_view *pure = qa_cvars_find(cvars, "sv_pure");
    if (pure && pure->integer && !q3_packages_valid(application, owner, feed, packages, error)) return false;
    const qa_cvar_view *flood = qa_cvars_find(cvars, "sv_floodProtect");
    *out = (qa_q3_server_world){.generation = qa_application_configuration_generation(application),
        .server_id = server_id, .restarted_server_id = restarted_server_id, .checksum_feed = feed,
        .time = milliseconds, .pure = pure && pure->integer != 0, .flood_protect = !flood || flood->integer != 0};
    return true;
}

bool qa_application_network_q3_round_prepare(qa_application *application, qa_actor_owner owner,
    int32_t server_id, int32_t restarted_server_id, int32_t feed,
    const qa_application_network_q3_package_view *packages, qa_q3_server_world *out, qa_error *error)
{
    qa_q3_server_world world;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 round prepared world output");
    if (!qa_application_network_q3_round_world(application, owner, server_id, restarted_server_id, feed, packages, &world, error)) return false;
    application_provider *primary = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        qa_cvars *cvars = application_native_q3_console_registry(primary);
        qa_q3_round_source source_world;
        char identity[32]; snprintf(identity, sizeof(identity), "%d", server_id);
        if (!cvars || !qa_q3_round_read(primary->state.q3, &source_world, error) ||
            !qa_cvars_register(cvars, "fs_game", source_world.product == QA_Q3_TEAM_ARENA ? "missionpack" : "",
                QA_CVAR_SYSTEMINFO, primary->owner, "Q3 source content directory", error) ||
            !qa_cvars_set(cvars, "sv_serverid", identity, true, error)) return false;
        qa_buffer system = {0}, server = {0};
        bool ok = qa_cvars_info(cvars, QA_CVAR_SYSTEMINFO, QA_Q3_BIG_INFO_CHARS, &system, error) &&
            qa_cvars_info(cvars, QA_CVAR_SERVERINFO, 1024, &server, error);
        if (ok) ok = qa_q3_configstring_write(primary->state.q3, 1, (const char *)system.data, error) &&
            qa_q3_configstring_write(primary->state.q3, 0, (const char *)server.data, error);
        qa_buffer_free(&system); qa_buffer_free(&server);
        if (!ok) return false;
        *out = world;
        return true;
    }
    struct application_q3_guest *engine = round_source(application, owner, error);
    if (!engine) return false;
    application_provider *provider = engine->provider; qa_cvars *cvars = NULL;
    (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    char identity[32]; snprintf(identity, sizeof(identity), "%d", server_id);
    if (!qa_cvars_register(cvars, "sv_serverid", identity, QA_CVAR_SYSTEMINFO | QA_CVAR_READONLY,
        provider->owner, "Original Q3 server identity", error) || !qa_cvars_set(cvars, "sv_serverid", identity, true, error)) return false;
    qa_buffer system = {0}, server = {0};
    bool ok = qa_cvars_info(cvars, QA_CVAR_SYSTEMINFO, QA_Q3_BIG_INFO_CHARS, &system, error) &&
        qa_cvars_info(cvars, QA_CVAR_SERVERINFO, 1024, &server, error);
    if (ok) ok = application_q3_guest_round_configstring(provider, 0, (const char *)server.data, error) &&
        application_q3_guest_round_configstring(provider, 1, (const char *)system.data, error);
    qa_buffer_free(&system); qa_buffer_free(&server);
    if (!ok) return false;
    *out = world;
    return true;
}

const qa_q3_gamestate *qa_application_network_q3_gamestate(qa_application *application, qa_actor_id actor)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, NULL);
    return engine ? &engine->gamestate : NULL;
}

static q3g_role *external_cgame(qa_application *app, qa_actor_owner owner, uint32_t seat)
{
    for (size_t i = 0; app && i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider->owner != owner || !provider->attached) continue;
        struct application_q3_guest *engine = q3g_engine(provider);
        for (q3g_role *role = engine ? engine->roles : NULL; role; role = role->next)
            if (role->kind == QA_QVM_CGAME && role->seat == seat && role->ready &&
                !role->retired && !role->local_client && role->client_services.gamestate) return role;
    }
    return NULL;
}
static bool remote_launch_seat(qa_application *app, uint32_t *out)
{
    const qa_launch_choices *choices = app ? qa_launch_snapshot_choices(qa_application_launch(app)) : NULL;
    if (!choices || choices->seat_count != 1) return false;
    *out = choices->seats[0].id; return true;
}

bool qa_application_network_q3_client_actor(qa_application *app,
    const qa_application_network_q3_projection *projection, uint32_t source_number,
    qa_actor_id *out, bool *present, qa_error *error)
{
    if (!app || !projection || !out || !present || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing remote Q3 actor projection owner");
    *out = (qa_actor_id){0}; *present = false;
    if (source_number >= QA_Q3_ENTITY_NONE || source_number == QA_Q3_ENTITY_WORLD) return true;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session),
        projection->actors[source_number]);
    if (record && record->owner == projection->owner && record->definition == projection->definition &&
        !record->has_source) {
        *out = record->id; *present = true;
    }
    return true;
}

bool qa_application_network_q3_client_unproject(qa_application *app,
    qa_application_network_q3_projection *projection, qa_error *error)
{
    if (!app || !projection || !qa_session_safe(app->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 projection retirement requires a safe session");
    for (uint32_t i = 0; i < QA_Q3_ENTITY_NONE; ++i) {
        qa_actor_id actor = projection->actors[i];
        const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
        if (record) {
            if (record->owner != projection->owner || record->definition != projection->definition || record->has_source)
                return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 projection contains a foreign actor");
            if (!qa_session_release(app->session, actor, error)) return false;
        }
        projection->actors[i] = (qa_actor_id){0};
    }
    projection->owner = 0; projection->definition = 0; return true;
}

static qa_trajectory projection_trajectory(const qa_q3_trajectory *source)
{
    return (qa_trajectory){(qa_trajectory_type)source->type, source->time, source->duration,
        qa_v3(source->base[0], source->base[1], source->base[2]),
        qa_v3(source->delta[0], source->delta[1], source->delta[2])};
}
static bool projection_snapshot(const qa_q3_snapshot *snapshot,
    qa_body_state bodies[QA_Q3_ENTITY_NONE], bool present[QA_Q3_ENTITY_NONE],
    bool identities[QA_Q3_ENTITY_NONE],
    qa_error *error)
{
    if (!snapshot) return true;
    if (!snapshot->valid || snapshot->entity_count > SIZE_MAX / sizeof(*snapshot->entities) ||
        (snapshot->entity_count && !snapshot->entities) || snapshot->player.clientNum < 0 ||
        snapshot->player.clientNum >= 64 || !qa_vec_finite(qa_v3(snapshot->player.origin[0],
            snapshot->player.origin[1], snapshot->player.origin[2])) ||
        !qa_vec_finite(qa_v3(snapshot->player.velocity[0], snapshot->player.velocity[1], snapshot->player.velocity[2])) ||
        !qa_vec_finite(qa_v3(snapshot->player.viewangles[0], snapshot->player.viewangles[1], snapshot->player.viewangles[2])))
        return application_fail(error, QA_ERROR_FORMAT, "Invalid remote Q3 projection snapshot");
    for (size_t i = 0; i < snapshot->entity_count; ++i) {
        const qa_q3_entity *entity = &snapshot->entities[i];
        if (entity->number < 0 || entity->number >= QA_Q3_ENTITY_NONE)
            return application_fail(error, QA_ERROR_FORMAT, "Remote Q3 projection entity number is invalid");
        if (entity->number == QA_Q3_ENTITY_WORLD) continue;
        qa_body_state body = {0};
        qa_trajectory trajectory = projection_trajectory(&entity->pos);
        if (!qa_trajectory_position(&trajectory, snapshot->server_time, 800, &body.origin, error) ||
            !qa_trajectory_velocity(&trajectory, snapshot->server_time, 800, &body.velocity, error) ||
            !qa_vec_finite(body.origin) || !qa_vec_finite(body.velocity))
            return application_fail(error, QA_ERROR_FORMAT, "Invalid remote Q3 entity position trajectory");
        trajectory = projection_trajectory(&entity->apos);
        if (!qa_trajectory_position(&trajectory, snapshot->server_time, 800, &body.angles, error) ||
            !qa_vec_finite(body.angles)) return application_fail(error, QA_ERROR_FORMAT, "Invalid remote Q3 entity angular trajectory");
        bodies[entity->number] = body; present[entity->number] = true;
        identities[entity->number] = true;
    }
    bodies[snapshot->player.clientNum] = (qa_body_state){
        .origin = qa_v3(snapshot->player.origin[0], snapshot->player.origin[1], snapshot->player.origin[2]),
        .velocity = qa_v3(snapshot->player.velocity[0], snapshot->player.velocity[1], snapshot->player.velocity[2]),
        .angles = qa_v3(snapshot->player.viewangles[0], snapshot->player.viewangles[1], snapshot->player.viewangles[2])};
    present[snapshot->player.clientNum] = true;
    identities[snapshot->player.clientNum] = true;
    if (snapshot->player.groundEntityNum >= 0 && snapshot->player.groundEntityNum < QA_Q3_ENTITY_WORLD)
        identities[snapshot->player.groundEntityNum] = true;
    if (snapshot->player.jumppadEnt > 0 && snapshot->player.jumppadEnt < QA_Q3_ENTITY_WORLD)
        identities[snapshot->player.jumppadEnt] = true;
    return true;
}

bool qa_application_network_q3_client_project(qa_application *app, qa_actor_owner owner,
    qa_application_network_q3_projection *projection, const qa_q3_snapshot *current,
    const qa_q3_snapshot *next, const qa_q3_prediction_scene *scene,
    const qa_q3_prediction_scene_view *view, qa_error *error)
{
    uint32_t seat; qa_application_q3_client_context receiver;
    if (!app || !projection || !current || app->destroy_requested || app->operation != APPLICATION_IDLE ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !remote_launch_seat(app, &seat) ||
        !qa_application_q3_remote_context_read(app, owner, seat, &receiver, error) ||
        !qa_application_q3_remote_context_current(app, &receiver) ||
        (projection->owner && projection->owner != owner) ||
        !qa_q3_prediction_scene_current(scene, view) || current != view->snapshot ||
        (next && (next != view->next_snapshot || view->next_frame_teleport)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication requires its admitted idle cgame owner");
    qa_body_state bodies[QA_Q3_ENTITY_NONE] = {0};
    bool present[QA_Q3_ENTITY_NONE] = {0};
    bool identities[QA_Q3_ENTITY_NONE] = {0};
    /* Current state wins where both snapshots observe the same source number. */
    if (!projection_snapshot(next, bodies, present, identities, error) ||
        !projection_snapshot(current, bodies, present, identities, error)) return false;
    /* Solid and trigger lists can retain a current centity which neither
     * present snapshot publishes. Its actual row supplies the identity. */
    for (unsigned list = 0; list < 2; ++list) for (size_t i = 0;; ++i) {
        qa_q3_prediction_scene_entity_view cell; bool observed;
        bool ok = list ? qa_q3_prediction_scene_trigger_at(scene, view, i, &cell, &observed, error) :
            qa_q3_prediction_scene_solid_at(scene, view, i, &cell, &observed, error);
        if (!ok) return false;
        if (!observed) break;
        if (!qa_q3_prediction_scene_entity_current(scene, view, &cell))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 collision projection lost its actual retained row");
        const qa_q3_entity *row = cell.entity;
        if (row->number < 0 || row->number >= QA_Q3_ENTITY_NONE ||
            (cell.published && (uint32_t)row->number != cell.source_number))
            return application_fail(error, QA_ERROR_FORMAT, "Q3 collision projection has an invalid retained source identity");
        if (row->number == QA_Q3_ENTITY_WORLD) continue;
        /* A next-only cold cell can produce a literal collision hit zero.
         * It demands that identity, without publishing a current pose. */
        identities[row->number] = true;
        if (!cell.published) continue;
        if (present[row->number]) continue;
        qa_body_state body = {0}; qa_trajectory trajectory = projection_trajectory(&row->pos);
        if (!qa_trajectory_position(&trajectory, view->physics_time, 800, &body.origin, error) ||
            !qa_trajectory_velocity(&trajectory, view->physics_time, 800, &body.velocity, error)) return false;
        trajectory = projection_trajectory(&row->apos);
        if (!qa_trajectory_position(&trajectory, view->physics_time, 800, &body.angles, error)) return false;
        if (!qa_vec_finite(body.origin) || !qa_vec_finite(body.velocity) || !qa_vec_finite(body.angles))
            return application_fail(error, QA_ERROR_FORMAT, "Q3 collision projection has an invalid retained source pose");
        bodies[row->number] = body; present[row->number] = true;
    }
    qa_string_id definition;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), "qa.network.q3.remote-entity", &definition, error)) return false;
    if (projection->owner && projection->definition != definition)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication has a foreign definition");
    projection->owner = owner; projection->definition = definition;
    for (uint32_t i = 0; i < QA_Q3_ENTITY_NONE; ++i) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), projection->actors[i]);
        if (record && (record->owner != owner || record->has_source || record->definition != definition))
            return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication contains a foreign actor");
        if (!record) projection->actors[i] = (qa_actor_id){0};
    }
    for (uint32_t i = 0; i < QA_Q3_ENTITY_NONE; ++i) {
        if (!identities[i]) continue;
        if (!projection->actors[i].registry &&
            !qa_session_allocate(app->session, owner, definition, false, 0, &projection->actors[i], error)) return false;
        if (present[i] && !qa_world_body_write(app->world, projection->actors[i], &bodies[i], error)) return false;
    }
    return true;
}
bool qa_application_network_q3_client_source(qa_application *app, qa_actor_id actor,
    qa_actor_owner *owner, qa_q3_product *product, uint32_t *launch_seat, qa_error *error)
{
    qa_actor_id viewing; uint32_t seat;
    if (!app || !owner || !product || !launch_seat || !remote_launch_seat(app, &seat) ||
        !qa_application_player_actor(app, seat, &viewing) ||
        !qa_actor_id_equal(viewing, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 remote client requires its actual viewing seat actor");
    application_provider *hud = application_provider_for(app, actor, QA_ROLE_HUD, NULL);
    qa_application_q3_client_context receiver;
    if (hud && hud->kind == APPLICATION_PROVIDER_Q3) {
        if (!qa_application_q3_remote_context_read(app, hud->owner, seat, &receiver, error) ||
            !qa_application_q3_remote_context_current(app, &receiver) || !receiver.native_source ||
            !application_native_q3_remote_role_product(hud, seat, product, error)) return false;
        *owner = hud->owner; *launch_seat = seat; return true;
    }
    q3g_role *role = hud ? external_cgame(app, hud->owner, seat) : NULL;
    if (!role)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 remote client requires its selected external CGAME owner");
    *owner = hud->owner; *product = role->engine->product; *launch_seat = seat; return true;
}
bool qa_application_network_q3_client_native_system_info(qa_application *app, qa_actor_owner owner,
    uint32_t seat, const char *info, qa_error *error)
{
    qa_application_q3_client_context receiver;
    if (!app || !info || !qa_application_q3_remote_context_read(app, owner, seat, &receiver, error) ||
        !receiver.native_source || !qa_application_q3_remote_context_current(app, &receiver))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 SystemInfo lacks its actual native CLIENT receiver");
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider->owner == owner && provider->attached)
            return application_native_q3_remote_role_system_info(provider, seat, info, error);
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "Q3 SystemInfo lost its actual native provider");
}
bool qa_application_network_q3_client_command(qa_application *app, qa_actor_owner owner,
    uint32_t seat, const qa_q3_tokens *tokens, qa_error *error)
{
    qa_application_q3_client_context receiver;
    if (!app || !tokens || tokens->truncated ||
        !qa_application_q3_remote_context_read(app, owner, seat, &receiver, error) ||
        !qa_application_q3_remote_context_current(app, &receiver))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 server command lacks its actual remote CLIENT receiver");
    if (receiver.native_source) {
        for (size_t i = 0; i < app->provider_count; ++i) {
            application_provider *provider = app->providers[i];
            if (provider->owner == owner && provider->attached)
                return application_native_q3_remote_role_command(provider, seat, tokens, error);
        }
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 native arguments lost their admitted provider");
    }
    q3g_role *role = external_cgame(app, owner, seat);
    if (!role || !tokens || tokens->truncated)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 server command lacks the current cgame argument owner");
    size_t bytes = 0;
    for (size_t i = 0; i < tokens->count; ++i) bytes += strlen(qa_q3_token(tokens, i)) + 1;
    qa_command_tokens copy = {.count = tokens->count};
    copy.values = calloc(tokens->count ? tokens->count : 1, sizeof(*copy.values));
    copy.storage = malloc(bytes ? bytes : 1); copy.args_text = malloc(bytes + 1);
    if (!copy.values || !copy.storage || !copy.args_text) {
        qa_command_tokens_free(&copy); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q3 cgame command arguments");
    }
    size_t cursor = 0, args = 0;
    for (size_t i = 0; i < tokens->count; ++i) {
        const char *value = qa_q3_token(tokens, i); size_t length = strlen(value);
        copy.values[i] = copy.storage + cursor;
        memcpy(copy.storage + cursor, value, length + 1); cursor += length + 1;
        if (i) {
            if (i > 1) copy.args_text[args++] = ' ';
            memcpy(copy.args_text + args, value, length); args += length;
        }
    }
    copy.args_text[args] = 0;
    qa_command_tokens_free(&role->arguments); role->arguments = copy; return true;
}

bool qa_application_network_q3_snapshot(qa_application *application, qa_actor_id actor,
    int32_t message, int32_t commands, uint8_t flags,
    qa_application_network_q3_frame *out, qa_error *error)
{
    if (!out || message < 0 || commands < 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 snapshot observation");
    uint32_t slot;
    application_provider *primary = application ? application_world_provider(application, QA_ROLE_ENTITIES, "") : NULL;
    if (primary && primary->kind == APPLICATION_PROVIDER_Q3) {
        application_provider *provider = q3_native_actor(application, actor, &slot, error);
        uint8_t source_bit;
        if (!provider || !application_native_q3_wire_snapshot_bit(provider, &source_bit, error)) return false;
        flags = (uint8_t)((flags & (uint8_t)~4u) | source_bit);
        return application_q3_wire_host_snapshot(provider, slot, message, commands, flags, out, error);
    }
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    return engine && application_q3_wire_host_snapshot(engine->provider, slot,
        message, commands, flags, out, error);
}
