#include "internal.h"

bool qa_native_host_q2_retain_string(qa_native_host *host, const char *text,
    qa_native_address *out, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_CGAME || !host->instance || host->destroying || !text || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native Q2 string requires its live cgame host");
    return native_host_string_address(host, text, out, error);
}

bool qa_native_host_q2_draw_hud(qa_native_host *host, uint32_t seat,
    const qa_native_host_q2_hud_view *view, int32_t player_number,
    qa_bytes data, qa_bytes player, qa_error *error)
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
    if (!host->q2_hud_records &&
        !qa_native_allocate(host->instance, 1536 + 296, INT32_MIN + 9, &host->q2_hud_records, error)) return false;
    qa_native_address records = host->q2_hud_records;
    bool ok =
        native_host_write(host, records, data.data, data.size, error) &&
        native_host_write(host, records + 1536, player.data, player.size, error);
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
    qa_buffer *out, qa_error *error)
{
    qa_native_address address; size_t bytes;
    if (!out) return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 player observation requires an output");
    if (!player_prefix(host, slot, &address, &bytes, error)) return false;
    qa_buffer copy = {.data = malloc(bytes), .size = bytes};
    if (!copy.data) return native_host_fail(error, QA_ERROR_MEMORY, slot, "Retaining original Q2 public player state");
    if (!native_host_read(host, address, copy.data, bytes, error)) { qa_buffer_free(&copy); return false; }
    *out = copy;
    return true;
}
