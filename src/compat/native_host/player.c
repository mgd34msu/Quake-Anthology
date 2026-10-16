#include "internal.h"

static bool hud_records(qa_native_host *host, qa_error *error)
{
    return host->q2_hud_records || qa_native_allocate(host->instance, 1536 + 296,
        INT32_MIN + 9, &host->q2_hud_records, error);
}

bool qa_native_host_q2_wheel_read(qa_native_host *host, qa_bytes player,
    bool weapons, uint32_t ammo_mask, uint32_t powerup_mask,
    qa_native_host_q2_wheel_state *out, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_CGAME || !host->instance || host->destroying ||
        !out || !player.data || player.size != 296)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Q2 wheel requires its actual player state");
    if (!hud_records(host, error) ||
        !native_host_write(host, host->q2_hud_records + 1536, player.data, player.size, error)) return false;
    qa_native_value args[] = {{.type = QA_NATIVE_ADDRESS, .as.address = host->q2_hud_records + 1536},
        {.type = QA_NATIVE_I32}}, result;
    if (weapons) {
        if (!qa_native_call(host->instance, "GetActiveWeaponWheelWeapon", args, 1, &result, error)) return false;
        out->active = result.as.i32;
        if (!qa_native_call(host->instance, "GetOwnedWeaponWheelWeapons", args, 1, &result, error)) return false;
        out->owned = result.as.u32;
    }
    for (int32_t i = 0; i < QA_Q2_MAX_WHEEL_ITEMS; ++i) {
        args[1].as.i32 = i;
        if (ammo_mask & (UINT32_C(1) << i)) {
            if (!qa_native_call(host->instance, "GetWeaponWheelAmmoCount", args, 2, &result, error)) return false;
            out->ammo[i] = result.as.i16;
        }
        if (powerup_mask & (UINT32_C(1) << i)) {
            if (!qa_native_call(host->instance, "GetPowerupWheelCount", args, 2, &result, error)) return false;
            out->powerups[i] = result.as.i16;
        }
    }
    return true;
}

bool qa_native_host_q2_monster_flash_offset(qa_native_host *host, uint16_t flash,
    qa_vec3 *out, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_CGAME || !host->instance || host->destroying || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, flash, "Q2 muzzle offset requires a live cgame");
    if (!hud_records(host, error)) return false;
    qa_native_address address = host->q2_hud_records + 1536;
    qa_native_value args[] = {{.type = QA_NATIVE_U16, .as.u16 = flash},
        {.type = QA_NATIVE_ADDRESS, .as.address = address}};
    uint8_t bytes[12];
    if (!qa_native_call(host->instance, "GetMonsterFlashOffset", args, 2, NULL, error) ||
        !native_host_read(host, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
    return qa_vec_finite(*out) || native_host_fail(error, QA_ERROR_FORMAT, flash,
        "Q2 cgame returned a nonfinite muzzle offset");
}

bool qa_native_host_q2_parse_config(qa_native_host *host, int32_t index,
    const char *text, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_CGAME || !host->instance || host->destroying || !text)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Q2 config text requires a live cgame");
    if (!native_host_string_buffer_write(host, &host->q2_hud_text, text, error)) return false;
    qa_native_value args[] = {{.type = QA_NATIVE_I32, .as.i32 = index},
        {.type = QA_NATIVE_ADDRESS, .as.address = host->q2_hud_text.address}};
    return qa_native_call(host->instance, "ParseConfigString", args, 2, NULL, error);
}

bool qa_native_host_q2_hud_text(qa_native_host *host, uint32_t seat,
    qa_native_q2_hud_text_kind kind, const char *text, bool flag, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_CGAME || !host->instance || host->destroying ||
        !host->q2_seat_bound || host->q2_seat != seat || seat > INT32_MAX ||
        kind < QA_NATIVE_Q2_HUD_CENTER || kind > QA_NATIVE_Q2_HUD_CLEAR_CENTER ||
        (kind <= QA_NATIVE_Q2_HUD_NOTIFY && !text))
        return native_host_fail(error, QA_ERROR_ARGUMENT, seat, "Q2 HUD text requires its live cgame seat");
    qa_native_value args[3] = {{.type = QA_NATIVE_I32, .as.i32 = (int32_t)seat}};
    const char *entry = kind == QA_NATIVE_Q2_HUD_CLEAR_NOTIFY ? "ClearNotify" : "ClearCenterprint";
    size_t count = 1;
    if (kind <= QA_NATIVE_Q2_HUD_NOTIFY) {
        if (!native_host_string_buffer_write(host, &host->q2_hud_text, text, error)) return false;
        qa_native_value string = {.type = QA_NATIVE_ADDRESS, .as.address = host->q2_hud_text.address};
        if (kind == QA_NATIVE_Q2_HUD_CENTER) {
            args[1] = args[0]; args[0] = string; entry = "ParseCenterPrint";
        } else {
            args[1] = string; entry = "NotifyMessage";
        }
        args[2] = (qa_native_value){.type = QA_NATIVE_U8, .as.u8 = flag ? 1 : 0};
        count = 3;
    }
    return qa_native_call(host->instance, entry, args, count, NULL, error);
}

bool qa_native_host_q2_draw_hud(qa_native_host *host, uint32_t seat,
    const qa_native_host_q2_hud_view *view, int32_t player_number,
    qa_bytes data, qa_bytes player, bool shared_inventory,
    qa_native_host_q2_hud_state *observe, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_CGAME || !host->instance || host->destroying ||
        !host->q2_seat_bound || host->q2_seat != seat || seat > INT32_MAX ||
        !view || view->width <= 0 || view->height <= 0 || view->scale <= 0 ||
        player_number < 0 || player_number >= 256 || !data.data || data.size != 1536 ||
        !memchr(data.data, 0, 1024) || !player.data || player.size != 296)
        return native_host_fail(error, QA_ERROR_ARGUMENT, seat, "Native Q2 DrawHUD requires its admitted KEX seat and source records");
    uint8_t viewport[16], safe[16];
    qa_store_u32le(viewport, (uint32_t)view->x); qa_store_u32le(viewport + 4, (uint32_t)view->y);
    qa_store_u32le(viewport + 8, (uint32_t)view->width); qa_store_u32le(viewport + 12, (uint32_t)view->height);
    qa_store_u32le(safe, (uint32_t)view->safe_x); qa_store_u32le(safe + 4, (uint32_t)view->safe_y);
    qa_store_u32le(safe + 8, (uint32_t)view->safe_width); qa_store_u32le(safe + 12, (uint32_t)view->safe_height);
    if (!hud_records(host, error)) return false;
    qa_native_address records = host->q2_hud_records;
    bool ok =
        native_host_write(host, records, data.data, data.size, error) &&
        native_host_write(host, records + 1536, player.data, player.size, error);
    if (ok && observe) {
        qa_native_value arg = {.type = QA_NATIVE_ADDRESS, .as.address = records + 1536}, result;
        ok = qa_native_call(host->instance, "LayoutFlags", &arg, 1, &result, error);
        if (ok) observe->layout_flags = result.as.i16;
        if (ok) ok = qa_native_call(host->instance, "GetHitMarkerDamage", &arg, 1, &result, error);
        if (ok) observe->hit_marker_damage = result.as.i16;
    }
    if (ok && shared_inventory) {
        /* Query the actual state above, then suppress the duplicate stock panel. */
        uint8_t flags[2];
        qa_store_u16le(flags, (uint16_t)(qa_load_u16le(player.data + 166 + 13 * 2) & ~2u));
        ok = native_host_write(host, records + 1536 + 166 + 13 * 2, flags, sizeof(flags), error);
    }
    if (ok) {
        qa_native_value args[] = {
            {.type = QA_NATIVE_I32, .as.i32 = (int32_t)seat},
            {.type = QA_NATIVE_ADDRESS, .as.address = records},
            {.type = QA_NATIVE_BYTES, .as.bytes = {viewport, sizeof(viewport)}},
            {.type = QA_NATIVE_BYTES, .as.bytes = {safe, sizeof(safe)}},
            {.type = QA_NATIVE_I32, .as.i32 = view->scale},
            {.type = QA_NATIVE_I32, .as.i32 = player_number},
            {.type = QA_NATIVE_ADDRESS, .as.address = records + 1536}};
        ok = qa_native_call(host->instance, "DrawHUD", args, 7, NULL, error);
    }
    return ok;
}

static bool player_prefix(qa_native_host *host, uint32_t slot,
    qa_native_address *address, size_t *bytes, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !slot ||
        host->destroying || !host->instance)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 player observation requires a live source client");
    qa_native_address entity;
    if (!qa_native_entity_address(host->instance, slot, &entity, error)) return false;
    size_t offset = host->profile == QA_NATIVE_Q2_GAME_API2023 ? NATIVE_Q2_RR_CLIENT :
        host->pointer_bytes == 4 ? 84u : 88u;
    uint8_t pointer[8];
    if (!native_host_read(host, entity + offset, pointer, host->pointer_bytes, error)) return false;
    *address = host->pointer_bytes == 4 ? qa_load_u32le(pointer) : qa_load_u64le(pointer);
    if (!*address) return native_host_fail(error, QA_ERROR_NOT_FOUND, slot, "Q2 source edict has no public player state");
    *bytes = host->profile == QA_NATIVE_Q2_GAME_API2023 ? 296u : 184u;
    return true;
}

static void player_vector(const uint8_t *bytes, float out[3])
{
    for (size_t axis = 0; axis < 3; ++axis) out[axis] = qa_load_f32le(bytes + axis * 4);
}

static void classic_player(const uint8_t *bytes, qa_q2_player *state)
{
    state->pmove.type = qa_load_i32le(bytes);
    for (size_t i = 0; i < 3; ++i) {
        state->pmove.origin[i] = (int16_t)qa_load_u16le(bytes + 4 + i * 2);
        state->pmove.velocity[i] = (int16_t)qa_load_u16le(bytes + 10 + i * 2);
        state->pmove.origin_f[i] = (float)state->pmove.origin[i] * .125f;
        state->pmove.velocity_f[i] = (float)state->pmove.velocity[i] * .125f;
        state->pmove.delta_angles[i] = (int16_t)qa_load_u16le(bytes + 20 + i * 2);
    }
    state->pmove.flags = bytes[16]; state->pmove.time = bytes[17];
    state->pmove.gravity = (int16_t)qa_load_u16le(bytes + 18);
    player_vector(bytes + 28, state->viewangles); player_vector(bytes + 40, state->viewoffset);
    player_vector(bytes + 52, state->kick_angles); player_vector(bytes + 64, state->gunangles);
    player_vector(bytes + 76, state->gunoffset);
    state->gunindex = qa_load_u32le(bytes + 88); state->gunframe = qa_load_u32le(bytes + 92);
    for (size_t i = 0; i < 4; ++i) state->blend[i] = qa_load_f32le(bytes + 96 + i * 4);
    state->fov = qa_load_f32le(bytes + 112); state->rdflags = qa_load_u32le(bytes + 116);
    for (size_t i = 0; i < 32; ++i) state->stats[i] = (int16_t)qa_load_u16le(bytes + 120 + i * 2);
}

static void rerelease_player(const uint8_t *bytes, qa_q2_player *state)
{
    state->pmove.type = qa_load_i32le(bytes);
    player_vector(bytes + 4, state->pmove.origin_f); player_vector(bytes + 16, state->pmove.velocity_f);
    state->pmove.flags = qa_load_u16le(bytes + 28); state->pmove.time = qa_load_u16le(bytes + 30);
    state->pmove.gravity = (int16_t)qa_load_u16le(bytes + 32);
    player_vector(bytes + 36, state->pmove.delta_angles_f); state->pmove.float_delta_angles = true;
    state->pmove.viewheight = (int8_t)bytes[48];
    player_vector(bytes + 52, state->viewangles); player_vector(bytes + 64, state->viewoffset);
    player_vector(bytes + 76, state->kick_angles); player_vector(bytes + 88, state->gunangles);
    player_vector(bytes + 100, state->gunoffset);
    state->gunindex = qa_load_u32le(bytes + 112); state->gunskin = qa_load_u32le(bytes + 116);
    state->gunframe = qa_load_u32le(bytes + 120); state->gunrate = qa_load_u32le(bytes + 124);
    for (size_t i = 0; i < 4; ++i) {
        state->blend[i] = qa_load_f32le(bytes + 128 + i * 4);
        state->damage_blend[i] = qa_load_f32le(bytes + 144 + i * 4);
    }
    state->fov = qa_load_f32le(bytes + 160); state->rdflags = bytes[164];
    for (size_t i = 0; i < 64; ++i) state->stats[i] = (int16_t)qa_load_u16le(bytes + 166 + i * 2);
    state->team_id = bytes[294];
}

bool qa_native_host_q2_player(qa_native_host *host, uint32_t slot,
    qa_q2_player *out, qa_error *error)
{
    qa_native_address address; size_t extent;
    if (!out) return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 player observation requires an output");
    if (!player_prefix(host, slot, &address, &extent, error)) return false;
    uint8_t bytes[296];
    if (!native_host_read(host, address, bytes, extent, error)) return false;
    qa_q2_player value = {.clientnum = (int32_t)(slot - 1)};
    if (host->profile == QA_NATIVE_Q2_GAME_API3) classic_player(bytes, &value);
    else rerelease_player(bytes, &value);
    *out = value;
    return true;
}

bool qa_native_host_q2_player_state(qa_native_host *host, uint32_t slot,
    uint8_t storage[296], qa_bytes *out, qa_error *error)
{
    qa_native_address address; size_t bytes;
    if (!storage || !out) return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 player observation requires an output");
    if (!player_prefix(host, slot, &address, &bytes, error)) return false;
    if (!native_host_read(host, address, storage, bytes, error)) return false;
    *out = (qa_bytes){storage, bytes};
    return true;
}
