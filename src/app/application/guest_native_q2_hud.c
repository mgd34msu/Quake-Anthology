#include "guest_native_q2_private.h"
#include "qa/hud_q2.h"
#include "qa/tools.h"
#include "control_frame.h"
#include "native_q2_inventory_scanner.h"
#include "native_q2_inventory_source.h"
#include "qa/application_native_q2_presentation.h"
#include <limits.h>

struct application_native_q2 *application_native_q2_hud_source(
    struct application_native_q2 *engine, uint32_t *slot, qa_error *error)
{
    uint32_t seat = UINT32_MAX;
    for (size_t i = 1; i < 257; ++i)
        if (engine->clients[i].reserved) {
            if (seat != UINT32_MAX) {
                application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 cgame requires one admitted seat");
                return NULL;
            }
            seat = engine->clients[i].seat;
        }
    qa_application *app = engine->provider->application;
    qa_actor_id actor;
    if (seat == UINT32_MAX || !qa_application_player_actor(app, seat, &actor)) {
        application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 HUD has no live local player");
        return NULL;
    }
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_CHARACTER, NULL);
    struct application_native_q2 *source = provider && provider->kind == APPLICATION_PROVIDER_NATIVE
        ? provider->state.native.q2_engine : NULL;
    if (!source || source->profile != QA_NATIVE_Q2_GAME_API2023 || !source->map_ready ||
        !source->initialized || source->shutting_down) {
        application_fail(error, QA_ERROR_UNSUPPORTED, "External Q2 cgame requires an admitted KEX public player-state producer");
        return NULL;
    }
    if (application_native_q2_client_begun(source, actor)) {
        *slot = application_native_q2_client_slot(source, actor); return source;
    }
    application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 HUD source client projection is unavailable");
    return NULL;
}

static bool debug_import(struct application_native_q2 *engine,
    const qa_native_host_q2_application_call *call, qa_error *error)
{
    qa_application *app = engine->provider->application;
    qa_unified_frame_lease *storage = application_control_storage_acquire(app, error);
    if (!storage) return false;
    const qa_debug_line *lines = NULL;
    size_t count = 0;
    uint32_t lifetime = 0;
    bool handled = false;
    qa_clock_state clock;
    bool okay = qa_debug_native_q2(call->instance, call->import,
        qa_unified_frame_lease_arena(storage), &lines, &count, &lifetime, &handled, error);
    if (okay && handled && qa_session_clock(app->session, engine->provider->owner, &clock))
        okay = qa_application_debug_emit(app, &(qa_application_debug_event){
            .provider = engine->provider->owner, .time_ns = clock.frame.time_ns,
            .lines = lines, .count = count, .lifetime_ns = (uint64_t)lifetime * UINT64_C(1000000)}, error);
    qa_unified_frame_lease_release(storage);
    return okay && handled;
}

static bool world_text_import(struct application_native_q2 *engine,
    const qa_native_host_q2_application_call *call, qa_error *error)
{
    const qa_native_value *args = call->import->arguments;
    bool fixed = call->import->slot == 60;
    size_t offset = fixed ? 1 : 0;
    qa_q2_map_event event = {.kind = QA_Q2_MAP_WORLD_TEXT,
        .value = args[3 + offset].as.f32 * 8,
        .duration = args[4 + offset].as.f32,
        .flags = (fixed ? 0u : 2u) | (args[5 + offset].as.u8 ? 1u : 0u),
        .visible = true};
    uint8_t vector[12], color[4], text[255];
    size_t used = 0;
    if (!qa_native_read(call->instance, args[0].as.address, vector, sizeof(vector), error))
        return false;
    event.origin = qa_v3(qa_load_f32le(vector), qa_load_f32le(vector + 4), qa_load_f32le(vector + 8));
    if (fixed) {
        if (!qa_native_read(call->instance, args[1].as.address, vector, sizeof(vector), error))
            return false;
        event.direction = qa_v3(qa_load_f32le(vector), qa_load_f32le(vector + 4), qa_load_f32le(vector + 8));
    }
    if (!qa_native_read(call->instance, args[2 + offset].as.address, color, sizeof(color), error))
        return false;
    event.color = qa_v3(color[0] / 255.f, color[1] / 255.f, color[2] / 255.f);
    event.alpha = color[3] / 255.f;
    qa_native_address address = args[1 + offset].as.address;
    for (size_t i = 0; i < 127; ++i) {
        uint8_t byte;
        if (address > UINT64_MAX - i || !qa_native_read(call->instance, address + i, &byte, 1, error))
            return false;
        if (!byte) break;
        if (byte < 128) text[used++] = byte;
        else {
            text[used++] = (uint8_t)(0xc0u | byte >> 6);
            text[used++] = (uint8_t)(0x80u | (byte & 63u));
        }
    }
    if (!used) return true;
    if (!qa_strings_intern(qa_session_strings(engine->provider->application->session),
            (qa_bytes){text, used}, &event.text, error)) return false;
    return application_emit_q2_map(engine->provider, &event, error);
}

bool application_native_q2_import(void *opaque, const qa_native_host_q2_application_call *call,
    qa_native_value *result, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (call->import->profile == QA_NATIVE_Q2_GAME_API2023 &&
        call->import->slot >= 54 && call->import->slot <= 63 &&
        call->import->slot != 59 && call->import->slot != 60)
        return debug_import(engine, call, error);
    if (call->import->profile == QA_NATIVE_Q2_GAME_API2023 &&
        (call->import->slot == 59 || call->import->slot == 60))
        return world_text_import(engine, call, error);
    qa_native_q2_import_kind dispatch=qa_native_q2_import_dispatch(call->import->profile,call->import->slot);
    if ((dispatch == QA_NATIVE_Q2_IMPORT_CL_GetClientName) || (dispatch == QA_NATIVE_Q2_IMPORT_CL_GetClientPic) ||
        (dispatch == QA_NATIVE_Q2_IMPORT_CL_GetClientDogtag)) {
        uint32_t slot;
        struct application_native_q2 *source = application_native_q2_hud_source(engine, &slot, error);
        if (!source) return false;
        int32_t index = call->import->arguments[0].as.i32;
        if (index < 0 || index >= 256)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 HUD client index is invalid");
        const qa_hud_q2_player_ids *names=source->hud_names.players+(uint32_t)index;
        qa_string_id name=names->name;
        if (dispatch!=QA_NATIVE_Q2_IMPORT_CL_GetClientName) {
            bool present=qa_strings_text(qa_session_strings(source->provider->application->session),
                source->hud_names.values[source->hud_names.layout.player_skins+(uint32_t)index]).size!=0;
            name=present?(dispatch==QA_NATIVE_Q2_IMPORT_CL_GetClientDogtag?names->native_tag:source->player_media[(uint32_t)index].icon):QA_STRING_NONE;
        }
        return qa_native_host_retain_string(call->host,name,&result->as.address,error);
    }
    if ((dispatch == QA_NATIVE_Q2_IMPORT_CL_FrameValid) || (dispatch == QA_NATIVE_Q2_IMPORT_CL_ClientTime) ||
        (dispatch == QA_NATIVE_Q2_IMPORT_CL_ServerFrame) || (dispatch == QA_NATIVE_Q2_IMPORT_CL_ServerProtocol) ||
        (dispatch == QA_NATIVE_Q2_IMPORT_CL_GetWarnAmmoCount)) {
        uint32_t slot;
        struct application_native_q2 *source = application_native_q2_hud_source(engine, &slot, error);
        if (!source) return false;
        if ((dispatch == QA_NATIVE_Q2_IMPORT_CL_FrameValid)) result->as.u8 = source->map_ready && source->clients[slot].begun;
        else if ((dispatch == QA_NATIVE_Q2_IMPORT_CL_ClientTime)) result->as.u64 = source->frame.time_ns / UINT64_C(1000000);
        else if ((dispatch == QA_NATIVE_Q2_IMPORT_CL_ServerFrame)) result->as.i32 = (int32_t)(uint32_t)source->frame.number;
        else if ((dispatch == QA_NATIVE_Q2_IMPORT_CL_ServerProtocol)) result->as.i32 = 2023;
        else {
            int32_t weapon = call->import->arguments[0].as.i32;
            if (weapon < 0 || weapon >= 32)
                return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 weapon wheel index is invalid");
            result->as.i32 = source->hud_names.weapons.wheel[weapon].warning;
        }
        return true;
    }
    return engine->application
        ? engine->application(engine->application_context, call, result, error)
        : application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 application import is unbound");
}

static bool hud_initialize(struct application_native_q2 *engine, qa_error *error)
{
    if (engine->initialized) return true;
    if (!qa_native_host_initialize(engine->provider->state.native.host, 0, 0, false, error)) return false;
    engine->initialized = true;
    return qa_native_call(qa_native_host_instance(engine->provider->state.native.host),
        "TouchPics", NULL, 0, NULL, error);
}

bool application_native_q2_monster_flash_offset(application_provider *provider,
    uint16_t flash, qa_vec3 *out, qa_error *error)
{
    struct application_native_q2 *engine = provider->state.native.q2_engine;
    if (!application_native_q2_activate(engine, error)) return false;
    ++engine->calls;
    bool ok = hud_initialize(engine, error) &&
        qa_native_host_q2_monster_flash_offset(provider->state.native.host, flash, out, error);
    --engine->calls;
    return ok;
}

static bool hud_begin(application_provider *provider, uint32_t seat,
    struct application_native_q2 **source_out, uint32_t *slot, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || engine->profile != QA_NATIVE_Q2_CGAME_API2023 || engine->shutting_down ||
        engine->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 HUD requires its idle cgame owner");
    struct application_native_q2 *source = application_native_q2_hud_source(engine, slot, error);
    if (!source) return false;
    if (source->calls || source->source_invocation ||
        !qa_native_host_destroy_ready(source->provider->state.native.host))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 HUD source is executing game code");
    bool admitted = false;
    for (size_t i = 1; i < 257; ++i)
        if (engine->clients[i].reserved && engine->clients[i].seat == seat) admitted = true;
    if (!admitted) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 HUD cannot draw another seat");
    if (!application_native_q2_activate(engine, error)) return false;
    ++engine->calls; ++source->calls;
    bool ok = hud_initialize(engine, error);
    bool rebound = engine->hud_source_owner != source->provider->owner;
    uint32_t link = rebound ? 1 : qa_q2_config_changes_begin(&source->config_changes, engine->hud_config_revision);
    while (ok && link && link <= engine->hud_names.layout.max_configs) {
        uint32_t i = link - 1;
        qa_string_id value = source->hud_names.values[i], prior = engine->hud_names.values[i];
        qa_strings *strings = qa_session_strings(engine->provider->application->session);
        if (value != prior && (qa_strings_text(strings, value).size || qa_strings_text(strings, prior).size)) {
            const char *text = value ? qa_strings_cstr(strings, value) : "";
            ok = qa_native_host_q2_parse_config(provider->state.native.host, (int32_t)i, text, error);
            if (ok) ok=qa_hud_q2_names_set(&engine->hud_names,i,value,NULL,error);
        }
        link = rebound ? link + 1 : source->config_changes.slots[i].newer;
    }
    if (ok) {
        engine->hud_source_owner = source->provider->owner;
        engine->hud_config_revision = source->config_changes.revision;
    }
    if (!ok) { --source->calls; --engine->calls; return false; }
    *source_out = source;
    return true;
}

bool application_native_q2_hud_text(application_provider *provider, uint32_t seat,
    qa_native_q2_hud_text_kind kind, const char *text, bool flag, qa_error *error)
{
    struct application_native_q2 *engine = provider->state.native.q2_engine;
    if (kind >= QA_NATIVE_Q2_HUD_CLEAR_NOTIFY && !engine->initialized) return true;
    struct application_native_q2 *source;
    uint32_t slot;
    if (!hud_begin(provider, seat, &source, &slot, error)) return false;
    bool ok = qa_native_host_q2_hud_text(provider->state.native.host, seat, kind, text, flag, error);
    --source->calls; --engine->calls;
    return ok;
}

static void hud_observation_prepare(struct application_native_q2 *engine,
    const struct application_native_q2 *source, uint32_t slot)
{
    if (engine->hud_observation.source == source->provider->owner &&
        qa_actor_id_equal(engine->hud_observation.actor, source->clients[slot].actor) &&
        engine->hud_observation.frame == source->frame.number &&
        engine->hud_observation.config_revision == source->config_changes.revision) return;
    memset(&engine->hud_observation, 0, sizeof(engine->hud_observation));
    engine->hud_observation.actor = source->clients[slot].actor;
    engine->hud_observation.source = source->provider->owner;
    engine->hud_observation.frame = source->frame.number;
    engine->hud_observation.time_ns = source->frame.time_ns;
    engine->hud_observation.config_revision = source->config_changes.revision;
}

bool application_native_q2_wheel_read(application_provider *provider, uint32_t seat,
    bool powerups, qa_application_native_q2_wheel *out, qa_error *error)
{
    struct application_native_q2 *engine = provider->state.native.q2_engine, *source;
    uint32_t slot;
    if (!hud_begin(provider, seat, &source, &slot, error)) return false;
    hud_observation_prepare(engine, source, slot);
    const qa_hud_q2_weapons *definitions = &source->hud_names.weapons;
    uint32_t ammo_mask = 0, powerup_mask = 0;
    *out = (qa_application_native_q2_wheel){.owner = source->provider->owner, .definitions = definitions};
    bool ok = true;
    for (uint32_t i = 0; ok && i < QA_Q2_MAX_WHEEL_ITEMS; ++i) {
        const qa_hud_q2_wheel_definition *weapon = definitions->wheel + i,
            *powerup = definitions->powerups + i;
        if (weapon->present && (!powerups || weapon->powerup)) {
            if (weapon->ammo >= 0) ammo_mask |= UINT32_C(1) << weapon->ammo;
            ok = application_native_q2_inventory_item(source, (uint32_t)weapon->item, out->weapons + i, error);
        }
        if (ok && powerups && powerup->present) {
            powerup_mask |= UINT32_C(1) << i;
            if (powerup->ammo >= 0) ammo_mask |= UINT32_C(1) << powerup->ammo;
            ok = application_native_q2_inventory_item(source, (uint32_t)powerup->item, out->powerups + i, error);
        }
    }
    uint32_t new_ammo = ammo_mask & ~engine->hud_observation.wheel_ammo_mask,
        new_powerups = powerup_mask & ~engine->hud_observation.wheel_powerup_mask;
    if (ok && (!engine->hud_observation.wheel_ready || new_ammo || new_powerups)) {
        uint8_t storage[296];qa_bytes player;
        ok = qa_native_host_q2_player_state(source->provider->state.native.host, slot, storage, &player, error) &&
            qa_native_host_q2_wheel_read(provider->state.native.host, player,
                !engine->hud_observation.wheel_ready, new_ammo, new_powerups,
                &engine->hud_observation.wheel, error);
        if (ok) {
            engine->hud_observation.wheel_ready = true;
            engine->hud_observation.wheel_ammo_mask |= new_ammo;
            engine->hud_observation.wheel_powerup_mask |= new_powerups;
        }
    }
    if (ok) {
        out->state = engine->hud_observation.wheel;
        if (out->state.active >= 0 && out->state.active < QA_Q2_MAX_WHEEL_ITEMS)
            out->active = out->weapons[out->state.active];
    }
    --source->calls;--engine->calls;
    return ok;
}

bool application_native_q2_draw_hud(application_provider *provider, uint32_t seat,
    uint32_t milliseconds, qa_error *error)
{
    (void)milliseconds;
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    struct application_native_q2 *source;
    uint32_t slot;
    if (!hud_begin(provider, seat, &source, &slot, error)) return false;
    bool ok = true;
    uint8_t player_storage[296];
    qa_bytes player = {0}; qa_native_host_q2_hud_view view;
    bool shared_inventory = false;
    uint8_t data[1536] = {0};
    if (ok) ok = qa_native_host_q2_player_state(source->provider->state.native.host, slot, player_storage, &player, error);
    if (ok && !engine->platform.hud_view)
        ok = application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q2 HUD viewport owner is absent");
    if (ok) ok = engine->platform.hud_view(engine->platform.context, seat, &view, error);
    if (ok && source->inventory_scanner) {
        application_native_q2_inventory_readout mixed = {0};
        ok = application_native_q2_inventory_mixed_read(source->provider, source->clients[slot].actor,
            &mixed, error);
        if (ok) shared_inventory = mixed.present;
        application_native_q2_inventory_readout_free(&mixed);
    }
    if (ok) {
        memcpy(data, source->clients[slot].layout, 1024);
        for (size_t i = 0; i < 256; ++i)
            qa_store_u16le(data + 1024 + i * 2, (uint16_t)source->clients[slot].inventory[i]);
        hud_observation_prepare(engine, source, slot);
        bool observe = !engine->hud_observation.present;
        ok = qa_native_host_q2_draw_hud(provider->state.native.host, seat, &view,
            (int32_t)slot - 1, (qa_bytes){data, sizeof(data)}, player, shared_inventory,
            observe ? &engine->hud_observation.state : NULL, error);
        if (ok && observe) {
            engine->hud_observation.present = true;
        }
    }
    --source->calls; --engine->calls;
    if (ok) engine->map_ready = provider->map_bound = true;
    return ok;
}
