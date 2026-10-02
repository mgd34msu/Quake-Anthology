#include "unified_q2_events.h"
#include "unified_events.h"
#include "unified_output_json.h"
#include "guest_native_q2_private.h"
#include "map_players_private.h"
#include "qa/network_q2_messages.h"
#include "qa/application_network_q2.h"
#include "qa/native_host_q2_wire.h"
#include "../../gameplay/q2/monsters/muzzle_data.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct q2_projection {
    application_provider *provider;
    struct application_native_q2 *engine;
    const qa_application_protocol_event *message;
    const qa_application_q2_protocol_delivery *delivery;
    const qa_q2_server_record *record;
    uint64_t time_ns;
    size_t ordinal, record_ordinal;
} q2_projection;

static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool number(application_unified_json *j, const char *key, double n, qa_error *e)
{ return text(j, key, e) && application_unified_json_number(j, n, e); }
static bool string(application_unified_json *j, const char *key, const char *s, qa_error *e)
{ return text(j, key, e) && application_unified_json_string(j, s, e); }
static bool actor(application_unified_json *j, const char *key, qa_actor_id a, qa_error *e)
{
    return text(j, key, e) && (a.registry ? application_unified_json_actor(j, a, e) : text(j, "null", e));
}
static bool vector(application_unified_json *j, const char *key, qa_vec3 v, qa_error *e)
{ return text(j, key, e) && application_unified_json_vector(j, v, e); }
static qa_vec3 vec(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }
static bool boolean(application_unified_json *j, const char *key, bool value, qa_error *e)
{ return text(j, key, e) && text(j, value ? "true" : "false", e); }

static uint32_t player_slot(const q2_projection *p, qa_actor_id target)
{
    for (uint32_t slot = 1; slot <= 256; ++slot)
        if (target.registry && qa_actor_id_equal(p->engine->clients[slot].actor, target)) return slot;
    return 0;
}

static bool reference_actor(const q2_projection *p, uint32_t slot, size_t relative_offset, qa_actor_id *out)
{
    if (!p->record || !p->record->raw.data || !p->message->payload.data) return false;
    if (p->record->event.kind != QA_Q2_SVC_SOUND && p->record->event.kind != QA_Q2_SVC_MUZZLEFLASH &&
        p->record->event.kind != QA_Q2_SVC_TEMP_ENTITY) return false;
    uintptr_t raw = (uintptr_t)p->record->raw.data, payload = (uintptr_t)p->message->payload.data;
    if (raw < payload || raw - payload > p->message->payload.size ||
        p->record->raw.size > p->message->payload.size - (size_t)(raw - payload)) return false;
    size_t first = (size_t)(raw - payload);
    if (p->record->raw.size < 2 || relative_offset > p->record->raw.size - 2) return false;
    size_t offset = first + relative_offset;
    for (size_t i = 0; i < p->message->reference_count; ++i) {
        const qa_application_protocol_reference *reference = p->message->references + i;
        if (reference->offset != offset ||
            p->message->payload.size < 2 || reference->offset > p->message->payload.size - 2) continue;
        uint32_t number = qa_load_u16le(p->message->payload.data + reference->offset);
        if (reference->packed_sound) number >>= 3;
        if (number == slot) { *out = reference->actor; return true; }
    }
    return false;
}

/* Read the admitted public GAME prefix while the actual import is executing.
 * Idle snapshot APIs cannot be used at this append-time boundary. */
static bool source_entity(const q2_projection *p, uint32_t slot, size_t relative_offset, qa_actor_id *actor_out,
    qa_vec3 *origin, qa_vec3 *angles, float *scale, bool *present, qa_error *e)
{
    *present = false; *actor_out = (qa_actor_id){0};
    bool captured = reference_actor(p, slot, relative_offset, actor_out);
    qa_native_instance *instance = qa_native_host_instance(p->provider->state.native.host);
    qa_native_entity_table before;
    qa_native_slot_binding binding;
    if (!instance || !qa_native_entity_table_get(instance, &before, e)) return false;
    if (captured) {
        const qa_actor_record *record = qa_actors_get(qa_session_actors(p->provider->application->session), *actor_out);
        if (!record) return true;
        if (record->owner == p->provider->owner && record->has_source) slot = record->source_slot;
        else {
            uint32_t physical = player_slot(p, *actor_out);
            if (!physical) return true;
            slot = physical;
        }
    }
    if (slot >= before.capacity) return true;
    if (!qa_native_slot(instance, slot, &binding, e)) return false;
    if (binding.kind == QA_NATIVE_SLOT_FREE) return true;
    if (captured && !qa_actor_id_equal(binding.actor, *actor_out)) return true;
    if (binding.slot != slot || binding.source_slot != slot || binding.owner != p->provider->owner ||
        !qa_actors_get(qa_session_actors(p->provider->application->session), binding.actor))
        return application_fail(e, QA_ERROR_FORMAT, "Q2 service entity lost its full Source binding");
    qa_native_host_q2_entity actual;
    if (!qa_native_host_q2_wire_entity_import(p->provider->state.native.host, slot, &actual, e)) return false;
    *origin = vec(actual.state.origin); *angles = vec(actual.state.angles);
    if (scale) *scale = p->engine->profile == QA_NATIVE_Q2_GAME_API3 ? 1.0f : actual.state.scale;
    if (!qa_vec_finite(*origin) || !qa_vec_finite(*angles))
        return application_fail(e, QA_ERROR_FORMAT, "Q2 service entity has nonfinite Source coordinates");
    if (!qa_actor_id_equal(binding.actor, actual.binding.actor) || binding.owner != actual.binding.owner ||
        binding.source_slot != actual.binding.source_slot || binding.kind != actual.binding.kind)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q2 service entity changed during Source observation");
    *actor_out = binding.actor; *present = true;
    return true;
}

static bool source_actor(const q2_projection *p, uint32_t slot, size_t relative_offset, qa_actor_id *out, qa_error *e)
{
    qa_vec3 origin, angles; bool present;
    return source_entity(p, slot, relative_offset, out, &origin, &angles, NULL, &present, e);
}

static const qa_application_protocol_resource_reference *resource_receipt(const q2_projection *p,
    qa_native_host_resource_kind kind, uint32_t index)
{
    for (size_t i = 0; i < p->message->resource_count; ++i) {
        const qa_application_protocol_resource_reference *receipt = p->message->resources + i;
        if (receipt->record_ordinal == p->record_ordinal && receipt->kind == kind && receipt->source_index == index)
            return receipt;
    }
    return NULL;
}

static const char *resource_path(const q2_projection *p, qa_native_host_resource_kind kind,
    uint32_t index, qa_error *e)
{
    const qa_application_protocol_resource_reference *receipt = resource_receipt(p, kind, index);
    if (receipt) return receipt->name;
    uint32_t base = p->engine->resource_base[kind];
    if (index >= p->engine->resource_limit[kind] || base + index >= p->engine->configstring_count ||
        !p->engine->configstrings[base + index]) {
        application_fail(e, QA_ERROR_FORMAT, "Q2 service resource has no actual Source configstring"); return NULL;
    }
    return p->engine->configstrings[base + index];
}

static const char *print_level(uint8_t level)
{ return level == 3 ? "chat" : level == 2 ? "high" : level == 1 ? "medium" : "low"; }

static bool simulation_sound(q2_projection *p, application_unified_json *j,
    const qa_q2_kex_sound *sound, qa_actor_id source, qa_vec3 origin, const char *path, qa_error *e)
{
    char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY]; bool found;
    const qa_application_protocol_resource_reference *receipt = resource_receipt(p, QA_NATIVE_HOST_SOUND, sound->index);
    if (receipt) {
        found = receipt->resource_key[0] != 0;
        memcpy(id, receipt->resource_key, sizeof(id));
    } else if (!application_unified_event_resource_lookup(p->provider->application, p->provider->owner,
        path, id, &found, e)) return false;
    if (!found) return true;
    return text(j, "{\"kind\":\"sound\"", e) &&
        string(j, ",\"resource\":", id, e) && actor(j, ",\"actor\":", source, e) &&
        vector(j, ",\"origin\":", origin, e) && number(j, ",\"channel\":", sound->channel, e) &&
        number(j, ",\"volume\":", sound->volume, e) && number(j, ",\"attenuation\":", sound->attenuation, e) &&
        text(j, "}", e);
}

static const qa_q2_temp_field *temp_field(const qa_q2_temp_entity *t, qa_q2_temp_field_name name)
{
    for (size_t i = 0; i < t->field_count; ++i) if (t->fields[i].name == name) return t->fields + i;
    return NULL;
}
static qa_vec3 temp_vector(const qa_q2_temp_entity *t, qa_q2_temp_field_name name)
{
    const qa_q2_temp_field *f = temp_field(t, name);
    return f && f->kind == QA_Q2_TEMP_VECTOR ? vec(f->value.vector) : qa_v3(0, 0, 0);
}
static int32_t temp_integer(const qa_q2_temp_entity *t, qa_q2_temp_field_name name)
{
    const qa_q2_temp_field *f = temp_field(t, name);
    return f && f->kind == QA_Q2_TEMP_INTEGER ? f->value.integer : 0;
}

static const char *temp_effect(uint8_t type)
{
    switch (type) {
    case QA_Q2_TE_GUNSHOT: return "gunshot";
    case QA_Q2_TE_BLOOD: return "blood";
    case QA_Q2_TE_BLASTER: return "blaster";
    case QA_Q2_TE_SHOTGUN: return "shotgun";
    case QA_Q2_TE_SPARKS: return "sparks";
    case QA_Q2_TE_SCREEN_SPARKS: return "screen-sparks";
    case QA_Q2_TE_SHIELD_SPARKS: return "shield-sparks";
    case QA_Q2_TE_BULLET_SPARKS: return "bullet-sparks";
    case QA_Q2_TE_GREENBLOOD: return "greenblood";
    case QA_Q2_TE_BLASTER2: return "blaster2";
    case QA_Q2_TE_FLECHETTE: return "flechette";
    case QA_Q2_TE_MOREBLOOD: return "moreblood";
    case QA_Q2_TE_ELECTRIC_SPARKS: return "electric-sparks";
    case QA_Q2_TE_SPLASH: return "splash";
    case QA_Q2_TE_LASER_SPARKS: return "laser-sparks";
    case QA_Q2_TE_WELDING_SPARKS: return "welding-sparks";
    case QA_Q2_TE_TUNNEL_SPARKS: return "tunnel-sparks";
    case QA_Q2_TE_EXPLOSION1: return "explosion1";
    case QA_Q2_TE_EXPLOSION2: return "explosion2";
    case QA_Q2_TE_ROCKET_EXPLOSION: return "rocket-explosion";
    case QA_Q2_TE_GRENADE_EXPLOSION: return "grenade-explosion";
    case QA_Q2_TE_ROCKET_EXPLOSION_WATER: return "rocket-explosion-water";
    case QA_Q2_TE_GRENADE_EXPLOSION_WATER: return "grenade-explosion-water";
    case QA_Q2_TE_BFG_EXPLOSION: return "bfg-explosion";
    case QA_Q2_TE_BFG_BIGEXPLOSION: return "bfg-bigexplosion";
    case QA_Q2_TE_BOSSTPORT: return "boss-teleport";
    case QA_Q2_TE_TELEPORT_EFFECT: return "other-teleport";
    default: return NULL;
    }
}

static bool residual_temporary(q2_projection *p, application_unified_json *j,
    const qa_q2_temp_entity *t, qa_error *e)
{
    static const char *const names[] = {"entity1", "entity2", "count", "color", "time",
        "position1", "position2", "direction", "offset"};
    if (!text(j, "{\"kind\":\"q2-temp-entity\",\"event\":{", e) ||
        !string(j, "\"profile\":", p->engine->profile == QA_NATIVE_Q2_GAME_API3 ? "q2-34" : "q2-kex-2023", e) ||
        !number(j, ",\"type\":", t->type, e) || !text(j, ",\"fields\":[", e)) return false;
    size_t offset = 2;
    for (size_t i = 0; i < t->field_count; ++i) {
        const qa_q2_temp_field *f = t->fields + i;
        if ((unsigned)f->name >= sizeof(names) / sizeof(*names) ||
            (unsigned)f->kind > QA_Q2_TEMP_VECTOR)
            return application_fail(e, QA_ERROR_FORMAT, "Q2 temporary entity has an invalid decoded field");
        if ((i && !text(j, ",", e)) || !text(j, "{", e) ||
            !string(j, "\"name\":", names[f->name], e) ||
            !string(j, ",\"kind\":", f->kind == QA_Q2_TEMP_VECTOR ? "vector" : "integer", e)) return false;
        if (f->kind == QA_Q2_TEMP_VECTOR) {
            if (!vector(j, ",\"value\":", vec(f->value.vector), e)) return false;
        } else {
            if (!number(j, ",\"value\":", f->value.integer, e)) return false;
            if (f->name == QA_Q2_TEMP_ENTITY1 || f->name == QA_Q2_TEMP_ENTITY2) {
                qa_actor_id a = {0};
                if (f->value.integer >= 0 && t->type != QA_Q2_TE_STEAM && t->type != QA_Q2_TE_WIDOWBEAMOUT &&
                    !source_actor(p, (uint32_t)f->value.integer, offset, &a, e)) return false;
                if (!actor(j, ",\"actor\":", a, e)) return false;
            }
        }
        if (!text(j, "}", e)) return false;
        if (f->kind == QA_Q2_TEMP_VECTOR)
            offset += f->name == QA_Q2_TEMP_DIRECTION ? 1u : p->engine->profile == QA_NATIVE_Q2_GAME_API3 ? 6u : 12u;
        else offset += f->name == QA_Q2_TEMP_ENTITY1 || f->name == QA_Q2_TEMP_ENTITY2 ? 2u :
            f->name == QA_Q2_TEMP_TIME ? 4u : 1u;
    }
    return text(j, "]}}", e);
}

static bool temporary(q2_projection *p, application_unified_json *j, const qa_q2_temp_entity *t, qa_error *e)
{
    const char *effect = temp_effect(t->type);
    if (effect) return text(j, "{\"kind\":\"q2\",\"event\":{\"kind\":\"effect\"", e) &&
        string(j, ",\"effect\":", effect, e) && vector(j, ",\"origin\":", temp_vector(t, QA_Q2_TEMP_POSITION1), e) &&
        vector(j, ",\"direction\":", temp_vector(t, QA_Q2_TEMP_DIRECTION), e) &&
        number(j, ",\"count\":", temp_integer(t, QA_Q2_TEMP_COUNT), e) &&
        number(j, ",\"color\":", temp_integer(t, QA_Q2_TEMP_COLOR), e) && text(j, "}}", e);
    switch (t->type) {
    case QA_Q2_TE_RAILTRAIL: effect = "rail"; break;
    case QA_Q2_TE_BUBBLETRAIL: effect = "bubble-trail"; break;
    case QA_Q2_TE_BFG_LASER: effect = "bfg-laser"; break;
    case QA_Q2_TE_BFG_ZAP: effect = "bfg-zap"; break;
    default: return residual_temporary(p, j, t, e);
    }
    return text(j, "{\"kind\":\"q2-weapon\",\"event\":{\"kind\":\"beam\"", e) &&
        string(j, ",\"effect\":", effect, e) && text(j, ",\"actor\":null", e) &&
        vector(j, ",\"start\":", temp_vector(t, QA_Q2_TEMP_POSITION1), e) &&
        vector(j, ",\"end\":", temp_vector(t, QA_Q2_TEMP_POSITION2), e) && text(j, ",\"duration\":0.1}}", e);
}

static void fog_apply(qa_q2_wire_fog *previous, const qa_q2_wire_fog *value)
{
    uint16_t bits = value->bits;
    if (bits & 1u) { previous->density = value->density; previous->sky_factor = value->sky_factor; }
    for (unsigned i = 0; i < 3; ++i) {
        if (bits & (2u << i)) previous->color[i] = value->color[i];
        if (bits & (256u << i)) previous->height_start_color[i] = value->height_start_color[i];
        if (bits & (4096u << i)) previous->height_end_color[i] = value->height_end_color[i];
    }
    if (bits & 32u) previous->height_falloff = value->height_falloff;
    if (bits & 64u) previous->height_density = value->height_density;
    if (bits & 2048u) previous->height_start_distance = value->height_start_distance;
    if (bits & 32768u) previous->height_end_distance = value->height_end_distance;
}

static qa_vec3 fog_color(const uint8_t color[3])
{ return qa_v3(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f); }

static bool fog(q2_projection *p, application_unified_json *j, uint32_t slot,
    qa_actor_id target, const qa_q2_wire_fog *value, qa_error *e)
{
    if (!slot) return true;
    application_native_q2_client *client = p->engine->clients + slot;
    if (!qa_actor_id_equal(client->protocol_fog_actor, target)) {
        client->protocol_fog = (qa_q2_wire_fog){0};
        client->protocol_fog_actor = target;
    }
    fog_apply(&client->protocol_fog, value);
    const qa_q2_wire_fog *state = &client->protocol_fog;
    return text(j, "{\"kind\":\"q2-rerelease\",\"event\":{\"kind\":\"fog\"", e) &&
        actor(j, ",\"actor\":", target, e) &&
        text(j, ",\"value\":{\"fog\":{", e) && number(j, "\"density\":", state->density, e) &&
        number(j, ",\"skyFactor\":", state->sky_factor / 255.0, e) &&
        vector(j, ",\"color\":", fog_color(state->color), e) && text(j, "},\"heightFog\":{", e) &&
        vector(j, "\"startColor\":", fog_color(state->height_start_color), e) &&
        number(j, ",\"startDistance\":", state->height_start_distance, e) &&
        vector(j, ",\"endColor\":", fog_color(state->height_end_color), e) &&
        number(j, ",\"endDistance\":", state->height_end_distance, e) &&
        number(j, ",\"falloff\":", state->height_falloff, e) &&
        number(j, ",\"density\":", state->height_density, e) && text(j, "}}", e) &&
        number(j, ",\"transitionMilliseconds\":", value->bits & 16u ? value->time : 0, e) && text(j, "}}", e);
}

static bool muzzle(q2_projection *p, const qa_q2_server_event *event,
    application_unified_json *presentation, application_unified_json *simulation, qa_error *e)
{
    qa_actor_id a; qa_vec3 origin, angles; float scale = 1; bool present;
    if (!source_entity(p, event->data.muzzle.entity, 1, &a, &origin, &angles, &scale, &present, e)) return false;
    if (!a.registry || (event->data.muzzle.monster && !present)) return true;
    if (event->data.muzzle.monster) {
        bool classic = p->engine->profile == QA_NATIVE_Q2_GAME_API3;
        const qa_vec3 *offsets = classic ? q2m_classic_muzzle_offsets : q2m_rerelease_muzzle_offsets;
        size_t count = classic ? sizeof(q2m_classic_muzzle_offsets) / sizeof(*offsets) :
            sizeof(q2m_rerelease_muzzle_offsets) / sizeof(*offsets);
        if (event->data.muzzle.flash >= count)
            return application_fail(e, QA_ERROR_FORMAT, "Q2 monster muzzle exceeds its authentic Source offset table");
        qa_vec3 forward, right; qa_builtin_angle_vectors(angles, &forward, &right, NULL);
        qa_vec3 offset = qa_vec_scale(offsets[event->data.muzzle.flash], scale == 0 ? 1 : scale);
        origin = qa_vec_add(origin, qa_vec_add(qa_vec_scale(forward, offset.x), qa_vec_scale(right, offset.y)));
        origin.z += offset.z;
        if (!text(presentation, "{\"kind\":\"q2\",\"event\":{\"kind\":\"monster-muzzleflash\"", e) ||
            !actor(presentation, ",\"actor\":", a, e) || !number(presentation, ",\"flash\":", event->data.muzzle.flash, e) ||
            !vector(presentation, ",\"origin\":", origin, e) || !vector(presentation, ",\"direction\":", forward, e) ||
            !vector(presentation, ",\"angles\":", angles, e) || !number(presentation, ",\"scale\":", scale, e) ||
            !text(presentation, "}}", e)) return false;
    } else if (!text(presentation, "{\"kind\":\"q2-weapon\",\"event\":{\"kind\":\"muzzleflash\"", e) ||
        !actor(presentation, ",\"actor\":", a, e) || !number(presentation, ",\"flash\":", event->data.muzzle.flash, e) ||
        !boolean(presentation, ",\"silenced\":", event->data.muzzle.silenced, e) || !text(presentation, "}}", e)) return false;
    return text(simulation, "{\"kind\":\"message\",\"event\":{\"kind\":\"q2-muzzle-flash\"", e) &&
        number(simulation, ",\"entityNumber\":", event->data.muzzle.entity, e) &&
        number(simulation, ",\"flash\":", event->data.muzzle.flash, e) &&
        boolean(simulation, ",\"monster\":", event->data.muzzle.monster, e) && text(simulation, "}}", e);
}

static bool damage(q2_projection *p, const qa_q2_server_event *event, qa_actor_id target, qa_error *e)
{
    for (size_t i = 0; i < event->data.damage.count; ++i) {
        const qa_q2_kex_damage *d = event->data.damage.indicators + i;
        application_unified_json j = {0};
        bool ok = text(&j, "{\"kind\":\"q2-rerelease\",\"event\":{\"kind\":\"directional-damage\"", e) &&
            actor(&j, ",\"actor\":", target, e) && number(&j, ",\"damage\":", d->damage, e) &&
            boolean(&j, ",\"health\":", d->health, e) && boolean(&j, ",\"armor\":", d->armor, e) &&
            boolean(&j, ",\"shield\":", d->shield, e) && vector(&j, ",\"direction\":", vec(d->direction), e) && text(&j, "}}", e);
        if (ok) ok = application_unified_event_emit(p->provider->application, p->provider->owner,
            (qa_bytes){j.bytes.data, j.bytes.size}, (qa_bytes){0}, target, (qa_actor_id){0},
            p->time_ns, 0, false, false, e);
        application_unified_json_dispose(&j);
        if (!ok) return false;
    }
    return true;
}

static bool emit_to(q2_projection *p, const qa_q2_server_record *record, qa_actor_id target, qa_error *e)
{
    const qa_q2_server_event *event = &record->event;
    application_unified_json presentation = {0}, simulation = {0};
    uint32_t slot = player_slot(p, target), source = slot;
    bool has_source = slot != 0, link = false, ok = true;
    switch (event->kind) {
    case QA_Q2_SVC_PRINT: case QA_Q2_SVC_CENTERPRINT: {
        bool center = event->kind == QA_Q2_SVC_CENTERPRINT || event->data.print.level == 4 || event->data.print.level == 5;
        ok = text(&presentation, center ? "{\"kind\":\"q2\",\"event\":{\"kind\":\"centerprint\"" :
            "{\"kind\":\"q2-player\",\"event\":{\"kind\":\"print\"", e) &&
            actor(&presentation, center ? ",\"actor\":" : ",\"target\":", target, e) &&
            string(&presentation, ",\"text\":", event->data.print.text, e);
        if (ok && !center) ok = string(&presentation, ",\"level\":", print_level(event->data.print.level), e);
        if (ok && event->kind == QA_Q2_SVC_PRINT && center)
            ok = boolean(&presentation, ",\"instant\":", event->data.print.level == 4, e);
        if (ok) ok = text(&presentation, "}}", e) && text(&simulation, center ?
            "{\"kind\":\"message\",\"event\":{\"kind\":\"center-print\"" :
            "{\"kind\":\"message\",\"event\":{\"kind\":\"print\"", e) &&
            string(&simulation, ",\"text\":", event->data.print.text, e);
        if (ok && !center) ok = number(&simulation, ",\"level\":", event->data.print.level, e);
        if (ok) ok = text(&simulation, "}}", e);
        link = center; break;
    }
    case QA_Q2_SVC_COMMAND:
        ok = text(&presentation, "{\"kind\":\"q2-player\",\"event\":{\"kind\":\"stufftext\"", e) &&
            actor(&presentation, ",\"actor\":", target, e) && string(&presentation, ",\"text\":", event->data.print.text, e) &&
            text(&presentation, "}}", e) && text(&simulation, "{\"kind\":\"message\",\"event\":{\"kind\":\"command-text\"", e) &&
            string(&simulation, ",\"text\":", event->data.print.text, e) && text(&simulation, "}}", e);
        link = true; break;
    case QA_Q2_SVC_CONFIGSTRING: {
        uint32_t skins = p->engine->resource_base[QA_NATIVE_HOST_IMAGE] +
            p->engine->resource_limit[QA_NATIVE_HOST_IMAGE] + 512;
        uint32_t index = event->data.config.index;
        ok = text(&simulation, "{\"kind\":\"message\",\"event\":{\"kind\":\"config-string\"", e) &&
            number(&simulation, ",\"index\":", index, e) && string(&simulation, ",\"value\":", event->data.config.value, e) &&
            text(&simulation, "}}", e);
        if (ok && index >= skins && index < skins + 256) {
            source = index - skins + 1; qa_actor_id a;
            ok = source_actor(p, source, SIZE_MAX, &a, e);
            if (ok && a.registry) {
                const char *value = event->data.config.value, *split = strchr(value, '\\');
                size_t count = split ? (size_t)(split - value) : strlen(value);
                char *name = malloc(count + 1);
                if (!name) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining Q2 player name projection");
                else {
                    memcpy(name, value, count); name[count] = 0;
                    ok = text(&presentation, "{\"kind\":\"q2-player\",\"event\":{\"kind\":\"userinfo\"", e) &&
                        actor(&presentation, ",\"actor\":", a, e) && number(&presentation, ",\"slot\":", source - 1, e) &&
                        string(&presentation, ",\"name\":", name, e) && string(&presentation, ",\"skin\":", split ? split + 1 : "", e) &&
                        text(&presentation, "}}", e);
                    free(name); has_source = true;
                }
            }
        }
        break;
    }
    case QA_Q2_SVC_LAYOUT:
        if (slot) memcpy(p->engine->clients[slot].layout, event->data.print.text, strlen(event->data.print.text) + 1);
        ok = text(&simulation, "{\"kind\":\"message\",\"event\":{\"kind\":\"q2-layout\"", e) &&
            string(&simulation, ",\"program\":", event->data.print.text, e) && text(&simulation, "}}", e); break;
    case QA_Q2_SVC_INVENTORY:
        if (slot) memcpy(p->engine->clients[slot].inventory, event->data.inventory.counts,
            sizeof(p->engine->clients[slot].inventory));
        ok = text(&simulation, "{\"kind\":\"message\",\"event\":{\"kind\":\"q2-inventory\",\"counts\":[", e);
        for (size_t i = 0; ok && i < event->data.inventory.count; ++i)
            ok = (!i || text(&simulation, ",", e)) && application_unified_json_number(&simulation, event->data.inventory.counts[i], e);
        if (ok) ok = text(&simulation, "]}}", e);
        break;
    case QA_Q2_SVC_TEMP_ENTITY:
        ok = temporary(p, &presentation, &event->data.temporary, e); has_source = false; break;
    case QA_Q2_SVC_FOG:
        ok = fog(p, &presentation, slot, target, &event->data.fog, e); break;
    case QA_Q2_SVC_MUZZLEFLASH:
        source = event->data.muzzle.entity; has_source = true;
        ok = muzzle(p, event, &presentation, &simulation, e); break;
    case QA_Q2_SVC_SOUND: {
        const qa_q2_kex_sound *sound = &event->data.sound;
        qa_actor_id a = {0}; qa_vec3 origin = {0}, angles; bool present;
        const char *path = resource_path(p, QA_NATIVE_HOST_SOUND, sound->index, e);
        ok = path != NULL;
        if (ok && (sound->flags & 8u)) {
            uint8_t flags = sound->flags;
            size_t offset = 2u + ((flags & 32u) ? 2u : 1u) + ((flags & 1u) != 0) +
                ((flags & 2u) != 0) + ((flags & 16u) != 0);
            ok = source_entity(p, sound->entity, offset, &a, &origin, &angles, NULL, &present, e);
        }
        if (sound->has_position) origin = vec(sound->position);
        source = sound->entity; has_source = (sound->flags & 8u) != 0;
        if (ok) ok = text(&presentation, "{\"kind\":\"q2\",\"event\":{\"kind\":\"sound\"", e) &&
            actor(&presentation, ",\"actor\":", a, e) && vector(&presentation, ",\"origin\":", origin, e) &&
            string(&presentation, ",\"path\":", path, e) && number(&presentation, ",\"channel\":", sound->channel, e) &&
            number(&presentation, ",\"volume\":", sound->volume, e) && number(&presentation, ",\"attenuation\":", sound->attenuation, e) &&
            boolean(&presentation, ",\"reliable\":", p->message->reliable, e) &&
            text(&presentation, ",\"loop\":\"once\"}}", e) &&
            simulation_sound(p, &simulation, sound, a, origin, path, e);
        break;
    }
    case QA_Q2_SVC_ACHIEVEMENT:
        ok = text(&presentation, "{\"kind\":\"q2-rerelease\",\"event\":{\"kind\":\"achievement\"", e) &&
            string(&presentation, ",\"id\":", event->data.print.text, e) && text(&presentation, "}}", e);
        has_source = false; break;
    case QA_Q2_SVC_POI: {
        const qa_q2_kex_poi *poi = &event->data.poi;
        bool remove = poi->time == UINT16_MAX;
        ok = text(&presentation, remove ? "{\"kind\":\"q2-rerelease\",\"event\":{\"kind\":\"remove-poi\"" :
            "{\"kind\":\"q2-rerelease\",\"event\":{\"kind\":\"keyed-poi\"", e) &&
            actor(&presentation, ",\"actor\":", target, e) && number(&presentation, ",\"key\":", poi->key, e);
        if (ok && !remove) {
            const char *path = resource_path(p, QA_NATIVE_HOST_IMAGE, poi->image, e);
            ok = path && vector(&presentation, ",\"position\":", vec(poi->position), e) &&
                string(&presentation, ",\"image\":", path, e) && number(&presentation, ",\"duration\":", poi->time, e) &&
                number(&presentation, ",\"color\":", poi->color, e) && number(&presentation, ",\"flags\":", poi->flags, e);
        }
        if (ok) ok = text(&presentation, "}}", e);
        has_source = false; break;
    }
    case QA_Q2_SVC_DAMAGE:
        return damage(p, event, target, e);
    case QA_Q2_SVC_HELP_PATH:
        ok = text(&presentation, "{\"kind\":\"q2-rerelease\",\"event\":{\"kind\":\"help-path\"", e) &&
            actor(&presentation, ",\"actor\":", target, e) && boolean(&presentation, ",\"first\":", event->data.help_path.start, e) &&
            vector(&presentation, ",\"position\":", vec(event->data.help_path.position), e) &&
            vector(&presentation, ",\"direction\":", vec(event->data.help_path.direction), e) && text(&presentation, "}}", e); break;
    case QA_Q2_SVC_LOCALIZED_PRINT:
        ok = text(&presentation, "{\"kind\":\"q2-rerelease\",\"event\":{\"kind\":\"localized-print\"", e) &&
            actor(&presentation, ",\"actor\":", target, e) && string(&presentation, ",\"level\":", print_level(event->data.localized.flags & 7u), e) &&
            string(&presentation, ",\"text\":", event->data.localized.base, e) && text(&presentation, ",\"args\":[", e);
        for (size_t i = 0; ok && i < event->data.localized.arg_count; ++i)
            ok = (!i || text(&presentation, ",", e)) && application_unified_json_string(&presentation, event->data.localized.args[i], e);
        if (ok) ok = text(&presentation, "]}}", e);
        break;
    case QA_Q2_SVC_DISCONNECT:
        ok = text(&simulation, "{\"kind\":\"message\",\"event\":{\"kind\":\"disconnect\",\"reason\":\"Disconnected by the source game.\"}}", e); break;
    default: break;
    }
    if (ok && (presentation.bytes.size || simulation.bytes.size))
        ok = application_unified_event_emit(p->provider->application, p->provider->owner,
            (qa_bytes){presentation.bytes.data, presentation.bytes.size},
            (qa_bytes){simulation.bytes.data, simulation.bytes.size}, target, target,
            p->time_ns, (int32_t)source, has_source, link, e);
    application_unified_json_dispose(&presentation); application_unified_json_dispose(&simulation);
    return ok;
}

static bool receive(void *opaque, const qa_q2_server_record *record, qa_error *e)
{
    q2_projection *p = opaque;
    p->record = record;
    p->record_ordinal = p->ordinal++;
    if (record->event.kind == QA_Q2_SVC_SEAT) return true;
    if (p->delivery && p->delivery->original) {
        const qa_application_q2_audience *audience = &p->delivery->audience;
        if (!audience->captured) return true;
        for (size_t i = 0; i < audience->count; ++i) {
            const qa_application_q2_recipient *recipient = audience->recipients + i;
            qa_actor_id target = recipient->actor;
            if (record->seat && (!recipient->has_connection ||
                record->seat != recipient->remote_index + 1u)) continue;
            if (!emit_to(p, record, target, e)) return false;
        }
        return true;
    }
    if (p->message->recipient.registry) return emit_to(p, record, p->message->recipient, e);
    for (uint32_t slot = 1; slot <= 256; ++slot) {
        const application_native_q2_client *client = p->engine->clients + slot;
        if (client->connected && !client->disconnect_started && client->actor.registry &&
            !emit_to(p, record, client->actor, e)) return false;
    }
    return true;
}

static bool validate(void *opaque, const qa_q2_server_record *record, qa_error *e)
{
    (void)opaque;
    if (record->event.kind == QA_Q2_SVC_LAYOUT && strlen(record->event.data.print.text) >= 1024)
        return application_fail(e, QA_ERROR_FORMAT, "Native Q2 HUD layout exceeds the actual Source client record");
    if (record->event.kind == QA_Q2_SVC_INVENTORY && record->event.data.inventory.count != 256)
        return application_fail(e, QA_ERROR_FORMAT, "Native Q2 inventory differs from the actual Source client extent");
    return true;
}

bool application_unified_q2_protocol_event(application_provider *provider,
    const qa_application_protocol_event *message,
    const qa_application_q2_protocol_delivery *delivery, qa_error *e)
{
    if (!provider || !provider->product || provider->product->family != QA_GAME_Q2 ||
        provider->kind != APPLICATION_PROVIDER_NATIVE || !provider->state.native.q2_engine) return true;
    struct application_native_q2 *engine = provider->state.native.q2_engine;
    if (engine->profile != QA_NATIVE_Q2_GAME_API3 && engine->profile != QA_NATIVE_Q2_GAME_API2023) return true;
    qa_clock_state clock;
    bool has_clock = qa_session_clock(provider->application->session, provider->owner, &clock);
    if (!has_clock && !provider->constructed && engine->provider == provider &&
        engine->prepared && engine->calls && engine->host_constructing &&
        (!delivery || !delivery->audience.captured)) {
        bool connected = false;
        for (size_t i = 1; i < 257; ++i) connected |= engine->clients[i].connected;
        if (!connected) return true;
    }
    if (!message || !provider->state.native.host || engine->provider != provider ||
        !has_clock ||
        (delivery && delivery->original && (delivery->profile != engine->profile ||
            (delivery->audience.captured && delivery->audience.source != provider->owner))))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q2 protocol projection lost its actual GAME Source");
    q2_projection p = {.provider = provider, .engine = engine, .message = message, .delivery = delivery,
        .time_ns = delivery && delivery->audience.captured ? delivery->audience.source_time_ns : clock.frame.time_ns};
    qa_net_protocol_id protocol = {.kind = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_NET_Q2_34 : QA_NET_Q2KEX_2023};
    qa_q2_message_options options = {.config_strings = engine->configstring_count, .inventory_slots = 256,
        .native_api2023 = engine->profile == QA_NATIVE_Q2_GAME_API2023};
    qa_q2_messages *decoder = NULL;
    bool ok = qa_q2_messages_create(protocol, &options, &decoder, e) &&
        qa_q2_messages_read(decoder, message->payload, validate, NULL, e);
    if (ok) {
        qa_q2_messages_reset(decoder);
        ok = qa_q2_messages_read(decoder, message->payload, receive, &p, e);
    }
    qa_q2_messages_destroy(decoder);
    return ok;
}
