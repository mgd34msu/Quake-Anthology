#include "internal.h"
#include "guest_q3_private.h"
#include "map_players_private.h"
#include "guest_qc_internal.h"
#include "network_q1_signon.h"
#include "qa/application_network.h"
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

static struct application_qc_state *q1_source(qa_application *app, qa_actor_id player,
    uint32_t *source_slot, qa_error *error)
{
    application_provider *provider = app ? application_provider_for(app, player, QA_ROLE_CHARACTER, "") : NULL;
    struct application_qc_state *engine = provider && provider->kind == APPLICATION_PROVIDER_QC ? provider->state.qc.engine : NULL;
    const qa_actor_record *actor = app ? qa_actors_get(qa_session_actors(app->session), player) : NULL;
    qa_qc_slot_binding binding;
    if (!engine || !engine->initialized || engine->loading || engine->projecting ||
        provider->state.qc.qualified || !qa_qc_idle(provider->state.qc.instance) || !actor ||
        actor->owner != provider->owner || !actor->has_source || !actor->source_slot ||
        !qa_qc_slot(provider->state.qc.instance, actor->source_slot, &binding) ||
        binding.kind != QA_QC_SLOT_BORROWED || !qa_actor_id_equal(binding.actor, player) ||
        !engine->max_clients || engine->max_clients > 255 ||
        (engine->profile == QA_QC_QUAKEWORLD && engine->max_clients > 32) ||
        actor->source_slot > engine->max_clients || !engine->clients ||
        !engine->clients[actor->source_slot].connected ||
        !qa_actor_id_equal(engine->clients[actor->source_slot].actor, player) ||
        (engine->profile != QA_QC_NETQUAKE && engine->profile != QA_QC_QUAKEWORLD) ||
        engine->protocol.flags || engine->protocol.revision ||
        engine->protocol.kind != (engine->profile == QA_QC_QUAKEWORLD ? QA_NET_QW28 : QA_NET_NQ15)) {
        application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q1 wire requires its selected classic QuakeC source player"); return NULL;
    }
    for (unsigned role = 0; role < QA_ROLE_COUNT; ++role) {
        if (role == QA_ROLE_HUD || role == QA_ROLE_MENU || role == QA_ROLE_AUDIO || role == QA_ROLE_MUSIC) continue;
        application_provider *selected = application_provider_for(app, player, (qa_launch_role)role, "");
        if (selected && selected != provider) {
            application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q1 wire cannot represent mixed selected gameplay owners"); return NULL;
        }
    }
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(app));
    for (size_t i = 0; choices && i < choices->binding_count; ++i) {
        const qa_launch_binding *b = &choices->bindings[i];
        if (b->role == QA_ROLE_HUD || b->role == QA_ROLE_MENU || b->role == QA_ROLE_AUDIO || b->role == QA_ROLE_MUSIC) continue;
        if (strcmp(b->instance, provider->launch->selection.instance)) {
            application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q1 wire cannot represent additional scoped gameplay providers"); return NULL;
        }
    }
    *source_slot = actor->source_slot; return engine;
}
bool qa_application_network_q1_source(qa_application *app, qa_actor_id player,
    qa_actor_owner *owner, uint32_t *slot, qa_net_protocol_id *protocol, qa_error *error)
{
    if (!owner || !slot || !protocol) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source observation output");
    struct application_qc_state *engine = q1_source(app, player, slot, error);
    if (!engine) return false;
    *owner = engine->provider->owner; *protocol = engine->protocol; return true;
}
bool qa_application_network_q1_extents(qa_application *app, qa_actor_id player,
    uint32_t *clients, uint32_t *entities, qa_error *error)
{
    if (!clients || !entities) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source extent outputs");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
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
qa_cvars *qa_application_network_q1_cvars(qa_application *app, qa_actor_id player, qa_error *error)
{
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
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
    qa_application *app = engine->provider->application;
    const qa_actor_record *actor = qa_actors_get(qa_session_actors(app->session), entity); qa_qc_slot_binding binding;
    if (!actor || actor->owner != engine->provider->owner || !actor->has_source || !actor->source_slot ||
        actor->source_slot > UINT16_MAX || !qa_qc_slot(engine->provider->state.qc.instance, actor->source_slot, &binding) ||
        !qa_actor_id_equal(binding.actor, entity) ||
        (binding.kind != QA_QC_SLOT_OWNED && (binding.kind != QA_QC_SLOT_BORROWED ||
         actor->source_slot > engine->max_clients || !engine->clients[actor->source_slot].connected ||
         !qa_actor_id_equal(engine->clients[actor->source_slot].actor, entity))))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 entity lacks the admitted source edict identity");
    return qa_qc_actor_reference(engine->provider->state.qc.instance, entity, false, out, error);
}
bool qa_application_network_q1_entity(qa_application *app, qa_actor_id player, qa_actor_id entity,
    qa_q1_entity *out, qa_error *error)
{
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 entity observation output");
    if (!engine) return false;
    int32_t reference;
    if (!q1_entity_reference(engine, entity, &reference, error)) return false;
    const qa_actor_record *actor = qa_actors_get(qa_session_actors(app->session), entity);
    qa_q1_entity value; qa_q1_entity_init(&value); value.number = actor->source_slot;
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
bool qa_application_network_q1_precache(qa_application *app, qa_actor_id player,
    bool models, const char *names[255], size_t *count, qa_error *error)
{
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
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
    if (!isfinite(value)) return application_fail(error, QA_ERROR_FORMAT, "Nonfinite Q1 source bit mask");
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
bool qa_application_network_q1_world_read(qa_application *app, qa_actor_id player,
    qa_application_network_q1_world *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 world observation output");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
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
bool qa_application_network_q1_status(qa_application *app, qa_actor_id player,
    qa_application_network_q1_status_player players[255], size_t *count, qa_error *error)
{
    if (!players || !count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source client status output");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
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

bool qa_application_network_q1_name(qa_application *app, qa_actor_id player,
    const char *name, qa_error *error)
{
    if (!name) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 client name");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    if (!application_qc_input_idle(engine->provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 name requires an idle source input owner");
    application_player_record *record = NULL;
    for (size_t i = 0; app->players && i < app->players->count; ++i)
        if (qa_actor_id_equal(app->players->records[i].actor, player)) { record = app->players->records + i; break; }
    if (!record || record->retiring || record->source_slot != slot || record->character != engine->provider)
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
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    return engine && application_qc_client_colors(engine->provider, player, top, bottom, error);
}

bool qa_application_network_q1_consume_feedback(qa_application *app, qa_actor_id player,
    qa_application_network_q1_feedback *out, qa_error *error)
{
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
bool qa_application_network_q1_signon_count(qa_application *app, qa_actor_id player,
    size_t *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q1 source signon count");
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
    if (!engine) return false;
    *out = application_q1_signon_count(app, engine->provider->owner); return true;
}
bool qa_application_network_q1_signon_at(qa_application *app, qa_actor_id player, size_t index,
    qa_application_protocol_event *out, qa_error *error)
{
    uint32_t slot; struct application_qc_state *engine = q1_source(app, player, &slot, error);
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
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(application));
    for (size_t i = 0; choices && i < choices->binding_count; ++i) {
        const qa_launch_binding *binding = &choices->bindings[i];
        if (binding->role == QA_ROLE_HUD || binding->role == QA_ROLE_MENU ||
            binding->role == QA_ROLE_AUDIO || binding->role == QA_ROLE_MUSIC) continue;
        if (strcmp(binding->instance, provider->launch->selection.instance)) {
            application_fail(error, QA_ERROR_UNSUPPORTED, "Original Q3 wire cannot represent additional scoped gameplay providers");
            return NULL;
        }
    }
    return engine;
}

bool qa_application_network_q3_source(qa_application *application, qa_actor_id actor,
    uint32_t *slot, qa_q3_product *product, qa_error *error)
{
    if (!slot || !product) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 source observation output");
    struct application_q3_guest *engine = source(application, actor, slot, error);
    if (!engine) return false;
    *product = engine->product; return true;
}
bool qa_application_network_q3_world(qa_application *application, qa_actor_id actor,
    int32_t server_id, int32_t restarted_server_id, int32_t feed,
    qa_q3_server_world *out, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !out || server_id <= 0 || restarted_server_id <= 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 wire world requires its retained source identity");
    qa_cvars *cvars = NULL;
    (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    if (!cvars) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source cvar owner is unavailable");
    const qa_cvar_view *pure = qa_cvars_find(cvars, "sv_pure");
    const qa_cvar_view *flood = qa_cvars_find(cvars, "sv_floodProtect");
    *out = (qa_q3_server_world){.generation = qa_application_configuration_generation(application),
        .server_id = server_id, .restarted_server_id = restarted_server_id,
        .checksum_feed = feed, .time = engine->milliseconds,
        .pure = pure && pure->integer != 0, .flood_protect = !flood || flood->integer != 0};
    return true;
}
qa_cvars *qa_application_network_q3_cvars(qa_application *application, qa_actor_id actor)
{
    uint32_t slot; qa_cvars *cvars = NULL;
    struct application_q3_guest *engine = source(application, actor, &slot, NULL);
    if (engine) (void)qa_q3_host_console(engine->game->host, &cvars, NULL);
    return cvars;
}
bool qa_application_network_q3_status(qa_application *application, qa_actor_id actor,
    qa_application_network_q3_status_player players[64], size_t *count, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !players || !count) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 status observation output");
    *count = 0;
    for (uint32_t i = 0; i < 64; ++i) {
        const q3g_client *client = &engine->clients[i];
        if (!client->connected || !client->begun || client->pending_retirement) continue;
        qa_q3_player player;
        if (!qa_q3_host_source_player(engine->game->host, i, &player, error)) return false;
        players[(*count)++] = (qa_application_network_q3_status_player){i, player.persistant[0], player.ping,
            client->userinfo ? client->userinfo : ""};
    }
    return true;
}
bool qa_application_network_q3_slots(qa_application *application, qa_actor_id actor,
    bool occupied[64], qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !occupied) return application_fail(error, QA_ERROR_ARGUMENT, "Missing Q3 source slot observation");
    for (size_t i = 0; i < 64; ++i)
        occupied[i] = engine->clients[i].allocated || engine->clients[i].connected || engine->clients[i].pending_retirement;
    return true;
}
bool qa_application_network_q3_signon(qa_application *application, qa_actor_id actor,
    int32_t server_id, int32_t feed, qa_q3_gamestate *out, qa_q3_server_world *world, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine || !out || !world || !qa_application_network_q3_world(application, actor, server_id, server_id, feed, world, error)) return false;
    if (world->pure)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 pure signon requires complete live package-reference metadata");
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
        qa_q3_host_game_data data;
        if (ok && (!qa_q3_host_game_data_read(engine->game->host, &data) || data.entity_count > QA_Q3_ENTITY_NONE))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 source signon entity count exceeds wire capacity");
        memset(out->baseline_present, 0, sizeof(out->baseline_present));
        for (uint32_t i = 1; ok && i < data.entity_count; ++i) {
            qa_qvm_entity_shared shared;
            ok = qa_q3_host_entity(engine->game->host, i, &out->baselines[i], &shared, error);
            if (ok) out->baseline_present[i] = shared.linked;
        }
    }
    qa_buffer_free(&system); qa_buffer_free(&server); return ok;
}
bool qa_application_network_q3_userinfo(qa_application *application, qa_actor_id actor,
    const char *text, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    return engine && application_q3_guest_client_userinfo(engine->provider, slot, text, error);
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

bool qa_application_network_q3_client_actor(qa_application *app,
    const qa_application_network_q3_projection *projection, uint32_t source_number,
    qa_actor_id *out, bool *present, qa_error *error)
{
    if (!app || !projection || !out || !present || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing remote Q3 actor projection owner");
    *out = (qa_actor_id){0}; *present = false;
    if (source_number >= QA_Q3_ENTITY_WORLD) return true;
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
    for (uint32_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) {
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
    qa_body_state bodies[QA_Q3_ENTITY_WORLD], bool present[QA_Q3_ENTITY_WORLD],
    qa_error *error)
{
    if (!snapshot) return true;
    if (!snapshot->valid || snapshot->entity_count > QA_Q3_ENTITY_WORLD ||
        (snapshot->entity_count && !snapshot->entities) || snapshot->player.clientNum < 0 ||
        snapshot->player.clientNum >= 64 || !qa_vec_finite(qa_v3(snapshot->player.origin[0],
            snapshot->player.origin[1], snapshot->player.origin[2])) ||
        !qa_vec_finite(qa_v3(snapshot->player.velocity[0], snapshot->player.velocity[1], snapshot->player.velocity[2])) ||
        !qa_vec_finite(qa_v3(snapshot->player.viewangles[0], snapshot->player.viewangles[1], snapshot->player.viewangles[2])))
        return application_fail(error, QA_ERROR_FORMAT, "Invalid remote Q3 projection snapshot");
    int32_t previous = -1;
    for (size_t i = 0; i < snapshot->entity_count; ++i) {
        const qa_q3_entity *entity = &snapshot->entities[i];
        if (entity->number <= previous || entity->number >= QA_Q3_ENTITY_WORLD)
            return application_fail(error, QA_ERROR_FORMAT, "Remote Q3 projection entity order or number is invalid");
        previous = entity->number;
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
    }
    bodies[snapshot->player.clientNum] = (qa_body_state){
        .origin = qa_v3(snapshot->player.origin[0], snapshot->player.origin[1], snapshot->player.origin[2]),
        .velocity = qa_v3(snapshot->player.velocity[0], snapshot->player.velocity[1], snapshot->player.velocity[2]),
        .angles = qa_v3(snapshot->player.viewangles[0], snapshot->player.viewangles[1], snapshot->player.viewangles[2])};
    present[snapshot->player.clientNum] = true;
    return true;
}

bool qa_application_network_q3_client_project(qa_application *app, qa_actor_owner owner,
    qa_application_network_q3_projection *projection, const qa_q3_snapshot *current,
    const qa_q3_snapshot *next, qa_error *error)
{
    if (!app || !projection || !current || app->destroy_requested || app->operation != APPLICATION_IDLE ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) || !external_cgame(app, owner, 0) ||
        (projection->owner && projection->owner != owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication requires its admitted idle cgame owner");
    qa_body_state bodies[QA_Q3_ENTITY_WORLD] = {0};
    bool present[QA_Q3_ENTITY_WORLD] = {0};
    /* Current state wins where both snapshots observe the same source number. */
    if (!projection_snapshot(next, bodies, present, error) ||
        !projection_snapshot(current, bodies, present, error)) return false;
    qa_string_id definition;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), "qa.network.q3.remote-entity", &definition, error)) return false;
    if (projection->owner && projection->definition != definition)
        return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication has a foreign definition");
    projection->owner = owner; projection->definition = definition;
    for (uint32_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), projection->actors[i]);
        if (record && (record->owner != owner || record->has_source || record->definition != definition))
            return application_fail(error, QA_ERROR_ARGUMENT, "Remote Q3 publication contains a foreign actor");
        if (record && !present[i] && !qa_session_release(app->session, record->id, error)) return false;
        if (!record || !present[i]) projection->actors[i] = (qa_actor_id){0};
    }
    for (uint32_t i = 0; i < QA_Q3_ENTITY_WORLD; ++i) {
        if (!present[i]) continue;
        if (!projection->actors[i].registry &&
            !qa_session_allocate(app->session, owner, definition, false, 0, &projection->actors[i], error)) return false;
        if (!qa_world_body_write(app->world, projection->actors[i], &bodies[i], error)) return false;
    }
    return true;
}
bool qa_application_network_q3_client_source(qa_application *app, qa_actor_id actor,
    qa_actor_owner *owner, qa_q3_product *product, qa_error *error)
{
    uint32_t slot;
    struct application_q3_guest *engine = source(app, actor, &slot, error);
    application_provider *hud = app ? application_provider_for(app, actor, QA_ROLE_HUD, NULL) : NULL;
    if (!engine || !owner || !product || !hud || q3g_engine(hud) != engine ||
        !external_cgame(app, hud->owner, 0))
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 remote client requires the selected matching external cgame owner");
    *owner = hud->owner; *product = engine->product; return true;
}
bool qa_application_network_q3_client_clear(qa_application *app, qa_actor_owner owner,
    uint32_t seat, qa_error *error)
{
    q3g_role *role = external_cgame(app, owner, seat);
    if (!role) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 remote cgame owner is retired");
    if (role->initialized) {
        q3g_role *replacement = NULL;
        return q3g_role_restart(role, &replacement, error);
    }
    qa_command_tokens_free(&role->arguments); return true;
}
bool qa_application_network_q3_client_command(qa_application *app, qa_actor_owner owner,
    uint32_t seat, const qa_q3_tokens *tokens, qa_error *error)
{
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

typedef struct visibility_owner { qa_collision_geometry *geometry; qa_error failure; } visibility_owner;
static bool point(void *opaque, const float origin[3], int32_t *area, int32_t *cluster, qa_error *error)
{
    visibility_owner *owner = opaque;
    qa_collision_leaf leaf;
    if (!qa_collision_point_leaf(owner->geometry, (qa_vec3){origin[0], origin[1], origin[2]}, &leaf, error)) return false;
    if (leaf.area < INT32_MIN || leaf.area > INT32_MAX || leaf.cluster < INT32_MIN || leaf.cluster > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 geometry visibility index is outside source range");
    *area = (int32_t)leaf.area; *cluster = (int32_t)leaf.cluster; return true;
}
static bool area_bits(void *opaque, int32_t area, uint8_t accumulator[32], size_t *bytes, qa_error *error)
{
    uint8_t bits[32]; visibility_owner *owner = opaque;
    if (!qa_collision_area_bits(owner->geometry, area, bits, sizeof(bits), bytes, error)) return false;
    for (size_t i = 0; i < *bytes; ++i) accumulator[i] |= bits[i];
    return true;
}
static bool connected(void *opaque, int32_t first, int32_t second)
{
    visibility_owner *owner = opaque; bool value = false;
    if (!qa_collision_areas_connected(owner->geometry, first, second, &value, &owner->failure)) return false;
    return value;
}
static bool cluster_visible(void *opaque, int32_t first, int32_t second)
{
    visibility_owner *owner = opaque; bool value = false;
    if (!qa_collision_cluster_visible(owner->geometry, first, second, false, &value, &owner->failure)) return false;
    return value;
}
typedef struct source_entity {
    qa_q3_entity state;
    qa_q3_host_visibility visibility;
} source_entity;

bool qa_application_network_q3_snapshot(qa_application *application, qa_actor_id actor,
    int32_t message, int32_t commands, uint8_t flags,
    qa_application_network_q3_frame *out, qa_error *error)
{
    if (!out || message < 0 || commands < 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "Invalid Q3 snapshot observation");
    uint32_t slot;
    struct application_q3_guest *engine = source(application, actor, &slot, error);
    if (!engine) return false;
    qa_q3_host_game_data data;
    qa_collision_geometry *geometry = qa_world_geometry(application->world);
    if (!geometry || !qa_q3_host_game_data_read(engine->game->host, &data) || data.entity_count > QA_Q3_ENTITIES)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 snapshot source records or geometry are unavailable");
    source_entity *records = calloc(data.entity_count ? data.entity_count : 1, sizeof(*records));
    qa_q3_visibility_entity *entities = calloc(data.entity_count ? data.entity_count : 1, sizeof(*entities));
    qa_application_network_q3_frame *candidate = calloc(1, sizeof(*candidate));
    if (!records || !entities || !candidate) {
        free(records); free(entities); free(candidate);
        return application_fail(error, QA_ERROR_MEMORY, "Allocating Q3 snapshot observation");
    }
    bool ok = qa_q3_host_source_player(engine->game->host, slot, &candidate->snapshot.player, error);
    for (uint32_t i = 0; ok && i < data.entity_count; ++i) {
        qa_qvm_entity_shared shared; bool present;
        ok = qa_q3_host_entity(engine->game->host, i, &records[i].state, &shared, error) &&
            qa_q3_host_visibility_read(engine->game->host, i, &records[i].visibility, &present, error);
        if (!ok) break;
        if (records[i].visibility.cluster_count > 16) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 snapshot source cluster list exceeds host capacity"); break;
        }
        entities[i] = (qa_q3_visibility_entity){.state = &records[i].state,
            .linked = shared.linked && present, .flags = (uint32_t)shared.server_flags,
            .single_client = shared.single_client, .area = records[i].visibility.area,
            .area2 = records[i].visibility.area2, .last_cluster = records[i].visibility.last_cluster,
            .clusters = records[i].visibility.clusters, .cluster_count = records[i].visibility.cluster_count};
    }
    visibility_owner owner = {.geometry = geometry};
    qa_q3_visibility_world world = {.context = &owner, .point = point, .area_bits = area_bits,
        .areas_connected = connected, .cluster_visible = cluster_visible};
    if (ok) ok = qa_q3_select_snapshot_entities(&candidate->snapshot.player, entities, data.entity_count,
                                                &world, false, &candidate->visible, error);
    if (ok && owner.failure.code) { if (error) *error = owner.failure; ok = false; }
    if (ok) {
        candidate->snapshot.valid = true; candidate->snapshot.message_number = message;
        candidate->snapshot.server_command_number = commands; candidate->snapshot.server_time = engine->milliseconds;
        candidate->snapshot.delta_number = -1; candidate->snapshot.flags = flags;
        candidate->snapshot.area_bytes = candidate->visible.area_bytes;
        memcpy(candidate->snapshot.area_mask, candidate->visible.area_mask, sizeof(candidate->snapshot.area_mask));
        candidate->snapshot.entity_count = candidate->visible.count;
        *out = *candidate; out->snapshot.entities = out->visible.entities;
    }
    free(records); free(entities); free(candidate); return ok;
}
