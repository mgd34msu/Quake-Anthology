#include "unified_q2_events.h"
#include "unified_events.h"
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

static qa_vec3 vec(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }

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
    return qa_strings_cstr(qa_session_strings(p->engine->provider->application->session), p->engine->configstrings[base + index]);
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

static bool temporary(q2_projection *p, const qa_q2_temp_entity *t,
    qa_unified_q2_temporary *out, qa_unified_q2_temp_field fields[7], qa_error *e)
{
    *out = (qa_unified_q2_temporary){.type = t->type, .rerelease = p->engine->profile != QA_NATIVE_Q2_GAME_API3,
        .fields = fields, .field_count = t->field_count};
    for (size_t i = 0; i < t->field_count; ++i) {
        const qa_q2_temp_field *f = t->fields + i;
        fields[i] = (qa_unified_q2_temp_field){.name = f->name, .kind = f->kind};
        if (f->kind == QA_Q2_TEMP_VECTOR) fields[i].vector = vec(f->value.vector);
        else {
            fields[i].integer = f->value.integer;
            if ((f->name == QA_Q2_TEMP_ENTITY1 || f->name == QA_Q2_TEMP_ENTITY2) && f->value.integer >= 0 &&
                t->type != QA_Q2_TE_STEAM && t->type != QA_Q2_TE_WIDOWBEAMOUT &&
                !source_actor(p, (uint32_t)f->value.integer, f->offset + 1, &fields[i].actor, e)) return false;
        }

    }
    return true;
}
static bool muzzle(q2_projection *p, const qa_q2_server_event *event, qa_unified_q2_muzzle *out, bool *reached, qa_error *e)
{
    qa_actor_id a; qa_vec3 origin = {0}, angles = {0}; float scale = 1; bool present;
    *reached = false;
    if (!source_entity(p, event->data.muzzle.entity, 1, &a, &origin, &angles, &scale, &present, e)) return false;
    if (!a.registry || (event->data.muzzle.monster && !present)) return true;
    qa_vec3 direction = {0};
    if (event->data.muzzle.monster) {
        bool classic = p->engine->profile == QA_NATIVE_Q2_GAME_API3;
        const qa_vec3 *offsets = classic ? q2m_classic_muzzle_offsets : q2m_rerelease_muzzle_offsets;
        size_t count = classic ? sizeof(q2m_classic_muzzle_offsets) / sizeof(*offsets) : sizeof(q2m_rerelease_muzzle_offsets) / sizeof(*offsets);
        if (event->data.muzzle.flash >= count) return application_fail(e, QA_ERROR_FORMAT, "Q2 monster muzzle exceeds its authentic Source offset table");
        qa_vec3 right; qa_builtin_angle_vectors(angles, &direction, &right, NULL);
        qa_vec3 offset = qa_vec_scale(offsets[event->data.muzzle.flash], scale == 0 ? 1 : scale);
        origin = qa_vec_add(origin, qa_vec_add(qa_vec_scale(direction, offset.x), qa_vec_scale(right, offset.y))); origin.z += offset.z;
    }
    *out = (qa_unified_q2_muzzle){.actor = a, .entity = (uint16_t)event->data.muzzle.entity,
        .flash = (uint16_t)event->data.muzzle.flash, .monster = event->data.muzzle.monster,
        .silenced = event->data.muzzle.silenced, .has_pose = event->data.muzzle.monster,
        .origin = origin, .angles = angles, .direction = direction, .scale = scale};
    *reached = true; return true;
}
static bool damage(q2_projection *p, const qa_q2_server_event *event, qa_actor_id target, qa_error *e)
{
    for (size_t i = 0; i < event->data.damage.count; ++i) {
        const qa_q2_kex_damage *d = event->data.damage.indicators + i;
        qa_unified_presentation_payload value = {.kind = QA_UNIFIED_PRESENTATION_Q2_PROTOCOL,
            .value.q2_protocol = {.kind = QA_Q2_SVC_DAMAGE, .actor = target, .damage = d->damage,
                .health = d->health, .armor = d->armor, .shield = d->shield, .direction = vec(d->direction)}};
        if (!application_unified_event_emit(p->provider->application, p->provider->owner, &value, NULL,
            target, (qa_actor_id){0}, p->time_ns, 0, false, false, e)) return false;
    }
    return true;
}
static bool emit_to(q2_projection *p, const qa_q2_server_record *record, qa_actor_id target, qa_error *e)
{
    const qa_q2_server_event *event = &record->event;
    qa_unified_presentation_payload presentation = {.kind = QA_UNIFIED_PRESENTATION_Q2_PROTOCOL};
    qa_unified_q2_protocol_event *r = &presentation.value.q2_protocol;
    *r = (qa_unified_q2_protocol_event){.kind = event->kind, .actor = target};
    qa_unified_simulation_payload simulation = {.kind = QA_UNIFIED_SIMULATION_MESSAGE};
    const qa_unified_presentation_payload *pres = NULL; const qa_unified_simulation_payload *sim = NULL;
    uint32_t slot = player_slot(p, target), source = slot;
    bool has_source = slot != 0, link = false, ok = true;
    qa_unified_q2_temp_field fields[7] = {0}; qa_builtin_message_arg args[8] = {0};
    char sound_id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], *name = NULL;
    switch (event->kind) {
    case QA_Q2_SVC_PRINT: case QA_Q2_SVC_CENTERPRINT: {
        bool center = event->kind == QA_Q2_SVC_CENTERPRINT || event->data.print.level == 4 || event->data.print.level == 5;
        r->level = event->data.print.level; r->text = (char *)event->data.print.text;
        r->instant = event->kind == QA_Q2_SVC_PRINT && event->data.print.level == 4;
        simulation.value.message = (qa_unified_message_event){.kind = center ? QA_UNIFIED_MESSAGE_CENTER_PRINT : QA_UNIFIED_MESSAGE_PRINT,
            .text = r->text, .level = r->level};
        pres = &presentation; sim = &simulation; link = center; break;
    }
    case QA_Q2_SVC_COMMAND:
        r->text = (char *)event->data.print.text;
        simulation.value.message = (qa_unified_message_event){.kind = QA_UNIFIED_MESSAGE_COMMAND_TEXT, .text = r->text};
        pres = &presentation; sim = &simulation; link = true; break;
    case QA_Q2_SVC_CONFIGSTRING: {
        uint32_t skins = p->engine->resource_base[QA_NATIVE_HOST_IMAGE] + p->engine->resource_limit[QA_NATIVE_HOST_IMAGE] + 512;
        uint32_t index = event->data.config.index;
        simulation.value.message = (qa_unified_message_event){.kind = QA_UNIFIED_MESSAGE_CONFIG_STRING,
            .index = index, .text = (char *)event->data.config.value}; sim = &simulation;
        if (index >= skins && index < skins + 256) {
            source = index - skins + 1; qa_actor_id a;
            ok = source_actor(p, source, SIZE_MAX, &a, e);
            if (ok && a.registry) {
                const char *value = event->data.config.value, *split = strchr(value, '\\');
                size_t length = split ? (size_t)(split - value) : strlen(value);
                name = malloc(length + 1);
                if (!name) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining Q2 player name projection");
                else {
                    memcpy(name, value, length); name[length] = 0;
                    presentation.kind = QA_UNIFIED_PRESENTATION_Q2_PLAYER;
                    presentation.value.q2_player = (qa_q2_player_event){.kind = QA_Q2_PLAYER_USERINFO,
                        .actor = a, .slot = source - 1, .text = name, .skin = (char *)(split ? split + 1 : "")};
                    pres = &presentation; has_source = true;
                }
            }
        }
        break;
    }
    case QA_Q2_SVC_LAYOUT:
        if (slot) {
            application_native_q2_client *client = p->engine->clients + slot;
            if (strcmp(client->layout, event->data.print.text)) ++client->layout_revision;
            memcpy(client->layout, event->data.print.text, strlen(event->data.print.text) + 1);
        }
        simulation.value.message = (qa_unified_message_event){.kind = QA_UNIFIED_MESSAGE_Q2_LAYOUT, .text = (char *)event->data.print.text};
        sim = &simulation; break;
    case QA_Q2_SVC_INVENTORY:
        if (slot) {
            application_native_q2_client *client = p->engine->clients + slot;
            if (memcmp(client->inventory, event->data.inventory.counts, sizeof(client->inventory)))
                ++client->inventory_revision;
            memcpy(client->inventory, event->data.inventory.counts, sizeof(client->inventory));
        }
        simulation.value.message = (qa_unified_message_event){.kind = QA_UNIFIED_MESSAGE_Q2_INVENTORY,
            .counts = (int16_t *)event->data.inventory.counts, .count = event->data.inventory.count}; sim = &simulation; break;
    case QA_Q2_SVC_TEMP_ENTITY:
        presentation.kind = QA_UNIFIED_PRESENTATION_Q2_TEMPORARY;
        ok = temporary(p, &event->data.temporary, &presentation.value.q2_temporary, fields, e);
        pres = &presentation; has_source = false; break;
    case QA_Q2_SVC_FOG:
        if (slot) {
            application_native_q2_client *client = p->engine->clients + slot;
            if (!qa_actor_id_equal(client->protocol_fog_actor, target)) { client->protocol_fog = (qa_q2_wire_fog){0}; client->protocol_fog_actor = target; }
            fog_apply(&client->protocol_fog, &event->data.fog);
            const qa_q2_wire_fog *state = &client->protocol_fog;
            r->fog = (qa_q2_fog){.density = state->density, .sky_factor = state->sky_factor / 255.0f,
                .color = fog_color(state->color), .start_color = fog_color(state->height_start_color),
                .end_color = fog_color(state->height_end_color), .start_distance = (float)state->height_start_distance,
                .end_distance = (float)state->height_end_distance, .falloff = state->height_falloff, .height_density = state->height_density};
            r->transition_ms = event->data.fog.bits & 16u ? event->data.fog.time : 0; pres = &presentation;
        }
        break;
    case QA_Q2_SVC_MUZZLEFLASH: {
        bool reached; ok = muzzle(p, event, &r->muzzle, &reached, e);
        source = event->data.muzzle.entity; has_source = true;
        if (ok && reached) {
            pres = &presentation; sim = &simulation;
            simulation.value.message = (qa_unified_message_event){.kind = QA_UNIFIED_MESSAGE_Q2_MUZZLE_FLASH,
                .entity = r->muzzle.entity, .flash = r->muzzle.flash, .monster = r->muzzle.monster};
        }
        break;
    }
    case QA_Q2_SVC_SOUND: {
        const qa_q2_kex_sound *sound = &event->data.sound;
        qa_actor_id a = {0}; qa_vec3 origin = {0}, angles; bool present;
        const char *path = resource_path(p, QA_NATIVE_HOST_SOUND, sound->index, e); ok = path != NULL;
        if (ok && (sound->flags & 8u)) {
            uint8_t flags = sound->flags;
            size_t offset = 2u + ((flags & 32u) ? 2u : 1u) + ((flags & 1u) != 0) + ((flags & 2u) != 0) + ((flags & 16u) != 0);
            ok = source_entity(p, sound->entity, offset, &a, &origin, &angles, NULL, &present, e);
        }
        if (sound->has_position) origin = vec(sound->position);
        source = sound->entity; has_source = (sound->flags & 8u) != 0;
        r->actor = a; r->origin = origin; r->resource = (char *)path; r->channel = sound->channel;
        r->volume = sound->volume; r->attenuation = sound->attenuation; r->reliable = p->message->reliable;
        pres = &presentation;
        bool found = false;
        const qa_application_protocol_resource_reference *receipt = resource_receipt(p, QA_NATIVE_HOST_SOUND, sound->index);
        if (receipt) { found = receipt->resource_key[0] != 0; memcpy(sound_id, receipt->resource_key, sizeof(sound_id)); }
        else if (ok) ok = application_unified_event_resource_lookup(p->provider->application, p->provider->owner, path, sound_id, &found, e);
        if (ok && found) {
            simulation.kind = QA_UNIFIED_SIMULATION_SOUND;
            simulation.value.sound = (qa_unified_sound_event){.resource = sound_id, .actor = a, .origin = origin,
                .channel = sound->channel, .volume = sound->volume, .attenuation = sound->attenuation}; sim = &simulation;
        }
        break;
    }
    case QA_Q2_SVC_ACHIEVEMENT: r->text = (char *)event->data.print.text; pres = &presentation; has_source = false; break;
    case QA_Q2_SVC_POI: {
        const qa_q2_kex_poi *poi = &event->data.poi;
        r->poi = (qa_unified_q2_poi){.actor = target, .key = poi->key, .image = poi->image,
            .position = vec(poi->position), .duration = poi->time, .color = poi->color,
            .flags = poi->flags, .remove = poi->time == UINT16_MAX};
        if (!r->poi.remove) { r->resource = (char *)resource_path(p, QA_NATIVE_HOST_IMAGE, poi->image, e); ok = r->resource != NULL; }
        pres = &presentation; has_source = false; break;
    }
    case QA_Q2_SVC_DAMAGE: return damage(p, event, target, e);
    case QA_Q2_SVC_HELP_PATH:
        r->first = event->data.help_path.start; r->origin = vec(event->data.help_path.position);
        r->direction = vec(event->data.help_path.direction); pres = &presentation; break;
    case QA_Q2_SVC_LOCALIZED_PRINT:
        r->level = event->data.localized.flags & 7u; r->text = (char *)event->data.localized.base;
        r->arguments = args; r->argument_count = event->data.localized.arg_count;
        for (size_t i = 0; ok && i < r->argument_count; ++i) {
            args[i].kind = QA_BUILTIN_MESSAGE_STRING;
            ok = qa_strings_intern_cstr(qa_session_strings(p->provider->application->session),
                event->data.localized.args[i], &args[i].value.text, e);
        }
        pres = &presentation; break;
    case QA_Q2_SVC_DISCONNECT:
        simulation.value.message = (qa_unified_message_event){.kind = QA_UNIFIED_MESSAGE_DISCONNECT,
            .text = "Disconnected by the source game."}; sim = &simulation; break;
    default: break;
    }
    if (ok && (pres || sim)) ok = application_unified_event_emit(p->provider->application, p->provider->owner,
        pres, sim, target, target, p->time_ns, (int32_t)source, has_source, link, e);
    free(name); return ok;
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
