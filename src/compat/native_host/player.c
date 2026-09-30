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

bool qa_native_host_q2_player_state(qa_native_host *host, uint32_t slot,
                                     qa_buffer *out, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !out || !slot ||
        host->destroying || !host->instance)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Q2 player observation requires a live source client");
    qa_native_address entity;
    if (!qa_native_entity_address(host->instance, slot, &entity, error)) return false;
    size_t offset = host->profile == QA_NATIVE_Q2_GAME_API2023 ? NATIVE_Q2_RR_CLIENT :
        host->pointer_bytes == 4 ? 84u : 88u;
    uint8_t pointer[8];
    if (!native_host_read(host, entity + offset, pointer, host->pointer_bytes, error)) return false;
    qa_native_address client = host->pointer_bytes == 4 ? qa_load_u32le(pointer) : qa_load_u64le(pointer);
    if (!client) return native_host_fail(error, QA_ERROR_NOT_FOUND, slot, "Q2 source edict has no public player state");
    size_t bytes = host->profile == QA_NATIVE_Q2_GAME_API2023 ? 296u : 184u;
    qa_buffer copy = {.data = malloc(bytes), .size = bytes};
    if (!copy.data) return native_host_fail(error, QA_ERROR_MEMORY, slot, "Retaining original Q2 public player state");
    if (!native_host_read(host, client, copy.data, bytes, error)) { qa_buffer_free(&copy); return false; }
    *out = copy;
    return true;
}
