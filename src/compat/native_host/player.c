#include "internal.h"

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
