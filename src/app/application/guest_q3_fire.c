#include "guest_q3_private.h"
#include "guest_q3_fire.h"

/* Actual source playerState_t offsets, shared by the admitted Q3 ABI rows.
 * BG_AddPredictableEventToPlayerstate writes the ring before committing its
 * sequence. Watching that commit preserves each fire even when a Pmove call
 * produces more events than the two entries retained in the source ring. */
enum { FIRE_SEQUENCE = 108, FIRE_CLIENT = 140, FIRE_SPAWN_COUNT = 264 };

static bool same_data(const qa_q3_host_game_data *a, const qa_q3_host_game_data *b)
{
    return a->clients_address == b->clients_address && a->client_stride == b->client_stride &&
        a->client_count == b->client_count && a->entities_address == b->entities_address &&
        a->entity_stride == b->entity_stride;
}
static bool actor_current(q3g_role *role, uint32_t slot, qa_actor_id actor)
{
    uint32_t physical;
    const q3g_client *client = &role->engine->clients[slot];
    return (client->allocated || client->reserved) && !client->pending_retirement &&
        qa_actor_id_equal(client->actor, actor) &&
        qa_actors_get(qa_session_actors(role->engine->provider->application->session), actor) &&
        qa_q3_host_actor_slot(role->host, actor, &physical, NULL) && physical == slot;
}
static bool source_player(q3g_fire_scope *scope, qa_qvm *vm, uint32_t slot,
    qa_q3_player *out, qa_error *error)
{
    uint32_t address = (uint32_t)(scope->data.clients_address +
        (uint64_t)slot * scope->data.client_stride);
    int32_t pointer;
    memcpy(&pointer, &address, sizeof(pointer));
    return qa_qvm_read_player(vm, pointer, true, out, error);
}
static void baseline(q3g_fire_continuation *state, qa_actor_id actor,
    const qa_q3_player *player)
{
    *state = (q3g_fire_continuation){.actor = actor, .initialized = true,
        .sequence = (uint32_t)player->eventSequence, .spawn_count = player->persistant[4],
        .external_event = player->externalEvent, .external_time = player->externalEventTime};
}
static bool publish(void *context, qa_qvm *vm, const qa_qvm_committed_write *write,
    qa_error *error)
{
    q3g_fire_scope *scope = context;
    q3g_role *role = scope->role;
    qa_q3_host_game_data actual;
    if (role != role->engine->game || role->vm != vm || role->retired ||
        !qa_q3_host_game_data_read(role->host, &actual) || !same_data(&scope->data, &actual))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 fire observation changed its actual GAME storage");
    for (uint32_t slot = 0; slot < scope->data.client_count; ++slot) {
        uint32_t base = (uint32_t)(scope->data.clients_address +
            (uint64_t)slot * scope->data.client_stride);
        bool touched = false, increment = false, external_change = false;
        uint32_t previous = 0;
        for (size_t i = 0; i < write->count; ++i) {
            const qa_qvm_committed_range *range = &write->ranges[i];
            uint64_t end = (uint64_t)range->offset + range->after.size;
            if (range->offset < (uint64_t)base + scope->data.client_stride && end > base)
                touched = true;
            uint32_t sequence = base + FIRE_SEQUENCE;
            if (range->offset <= sequence && end >= (uint64_t)sequence + 4) {
                size_t offset = sequence - range->offset;
                previous = qa_load_u32le(range->before.data + offset);
                increment = qa_load_u32le(range->after.data + offset) == previous + 1u;
            }
            const uint32_t external_fields[2] = {base + 128, base + 136};
            for (size_t field = 0; field < 2; ++field) {
                uint32_t address = external_fields[field];
                if (range->offset <= address && end >= (uint64_t)address + 4) {
                    size_t offset = address - range->offset;
                    if (qa_load_u32le(range->before.data + offset) !=
                        qa_load_u32le(range->after.data + offset)) external_change = true;
                }
            }
        }
        if (!touched) continue;
        q3g_client *client = &role->engine->clients[slot];
        if (!actor_current(role, slot, client->actor)) continue;
        qa_q3_player player;
        if (!source_player(scope, vm, slot, &player, error)) return false;
        /* A spectator's copied PS keeps the viewed clientNum. It cannot
         * manufacture a fire belonging to the physical viewing actor. */
        if (player.clientNum != (int32_t)slot) continue;
        q3g_fire_continuation *state = &client->fire;
        if (!state->initialized || !qa_actor_id_equal(state->actor, client->actor))
            baseline(state, client->actor, &player);
        if (state->spawn_count != player.persistant[4]) {
            state->stamp = (qa_q3_fire_stamp){0};
            state->spawn_count = player.persistant[4];
        }
        if (increment && state->sequence == previous &&
            ((uint32_t)player.events[previous & 1u] & ~UINT32_C(0x300)) == 23u)
            state->stamp = (qa_q3_fire_stamp){.present = true,
                .time_ms = role->engine->milliseconds};
        state->sequence = (uint32_t)player.eventSequence;
        if (external_change &&
            ((uint32_t)player.externalEvent & ~UINT32_C(0x300)) == 23u)
            state->stamp = (qa_q3_fire_stamp){.present = true,
                .time_ms = player.externalEventTime};
        state->external_event = player.externalEvent;
        state->external_time = player.externalEventTime;
    }
    return true;
}
bool q3g_fire_begin(q3g_role *role, q3g_fire_scope *scope, qa_error *error)
{
    if (!scope) return application_fail(error, QA_ERROR_ARGUMENT, "Missing original Q3 fire entry scope");
    *scope = (q3g_fire_scope){0};
    if (!role || role->kind != QA_QVM_GAME || !role->vm || qa_qvm_active(role->vm)) return true;
    qa_q3_host_game_data data;
    if (!qa_q3_host_game_data_read(role->host, &data) || !data.clients_address) return true;
    size_t bytes = qa_qvm_player_bytes(role->abi);
    if (role != role->engine->game || !data.client_count || data.client_count > 64 ||
        data.client_stride < bytes || data.clients_address > UINT32_MAX ||
        (uint64_t)(data.client_count - 1) * data.client_stride + data.clients_address + bytes >
            qa_qvm_memory_size(role->vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 fire entry has invalid actual client storage");
    *scope = (q3g_fire_scope){.role = role, .data = data};
    qa_qvm_write_range ranges[128];
    for (uint32_t slot = 0; slot < data.client_count; ++slot) {
        uint32_t base = (uint32_t)(data.clients_address + (uint64_t)slot * data.client_stride);
        ranges[slot * 2] = (qa_qvm_write_range){base + FIRE_SEQUENCE, FIRE_CLIENT + 4 - FIRE_SEQUENCE};
        ranges[slot * 2 + 1] = (qa_qvm_write_range){base + FIRE_SPAWN_COUNT, 4};
        q3g_client *client = &role->engine->clients[slot];
        if (!actor_current(role, slot, client->actor)) continue;
        qa_q3_player player;
        if (!source_player(scope, role->vm, slot, &player, error)) return false;
        if (player.clientNum == (int32_t)slot &&
            (!client->fire.initialized || !qa_actor_id_equal(client->fire.actor, client->actor)))
            baseline(&client->fire, client->actor, &player);
    }
    return qa_qvm_observe_writes(role->vm, ranges, data.client_count * 2u,
        publish, NULL, scope, &scope->binding, error);
}
bool q3g_fire_end(q3g_fire_scope *scope, qa_error *error)
{
    if (!scope || !scope->binding) return true;
    bool ok = qa_qvm_unobserve_writes(scope->role->vm, scope->binding, error);
    if (ok) *scope = (q3g_fire_scope){0};
    return ok;
}
bool q3g_fire_valid(const q3g_fire_continuation *state, qa_actor_id actor)
{
    if (!state) return false;
    if (state->initialized) return actor.registry && qa_actor_id_equal(state->actor, actor) &&
        (state->stamp.present || !state->stamp.time_ms);
    return !state->actor.registry && !state->actor.generation && !state->actor.slot &&
        !state->stamp.present && !state->stamp.time_ms && !state->sequence &&
        !state->spawn_count && !state->external_event && !state->external_time;
}
bool q3g_fire_fields(qa_source_save_io *io, q3g_fire_continuation *state)
{
    return qa_source_save_actor(io, &state->actor) &&
        qa_source_save_bool(io, &state->initialized) &&
        qa_source_save_bool(io, &state->stamp.present) &&
        qa_source_save_i32(io, &state->stamp.time_ms) && qa_source_save_u32(io, &state->sequence) &&
        qa_source_save_i32(io, &state->spawn_count) && qa_source_save_i32(io, &state->external_event) &&
        qa_source_save_i32(io, &state->external_time);
}
bool application_q3_guest_fire_read(application_provider *provider, qa_actor_id actor,
    qa_q3_fire_stamp *out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    uint32_t slot;
    if (!out || !engine || engine->calls || engine->restore_pending ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !application_q3_guest_actor_client(provider, actor, &slot) || !engine->game->vm ||
        !actor_current(engine->game, slot, actor) ||
        !q3g_fire_valid(&engine->clients[slot].fire, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q3 fire stamp requires its idle actual physical actor");
    *out = engine->clients[slot].fire.stamp;
    return true;
}
