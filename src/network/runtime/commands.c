#include "internal.h"
#include "qa/network_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void qa_network_history_clear(qa_network_peer *peer) {
    for (size_t seat = 0; seat < peer->seat_count; ++seat) {
        qa_network_seat *state = &peer->seats[seat];
        for (size_t i = 0; i < QA_NETWORK_COMMAND_BACKUP; ++i)
            qa_buffer_free(&state->history[i].arsenal);
        qa_net_seat_id id = state->id;
        memset(state, 0, sizeof(*state)); state->id = id;
    }
}
static qa_network_seat *seat_get(qa_network_peer *peer, qa_net_seat_id id, qa_error *error) {
    for (size_t i = 0; i < peer->seat_count; ++i)
        if (peer->seats[i].id.owner == id.owner && peer->seats[i].id.index == id.index) return &peer->seats[i];
    qa_network_fail(error, "Connection does not own command seat"); return NULL;
}
bool qa_network_accepted_sequence(const qa_network_runtime *runtime, qa_net_client_id id,
    qa_net_seat_id seat, bool *present, uint64_t *sequence, qa_error *error)
{
    if (!runtime || !present || !sequence || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error, "Accepted-command lookup requires an idle runtime and outputs");
    const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
    if (!client || id.slot >= runtime->options.clients)
        return qa_network_fail(error, "Accepted-command lookup lacks its admitted connection");
    const qa_network_peer *peer = runtime->peers + id.slot;
    if (!peer->occupied || !qa_net_client_id_equal(peer->id, id))
        return qa_network_fail(error, "Accepted-command lookup lacks its actual peer generation");
    for (size_t i = 0; i < peer->seat_count; ++i) {
        const qa_network_seat *state = peer->seats + i;
        if (state->id.owner != seat.owner || state->id.index != seat.index) continue;
        if (state->applying) return qa_network_fail(error, "Accepted-command seat callback is active");
        *present = state->has_accepted; *sequence = state->accepted; return true;
    }
    return qa_network_fail(error, "Accepted-command lookup lacks its admitted source seat");
}
static bool authority(qa_network_runtime *runtime, const qa_network_command *command,
                       qa_network_peer **peer, qa_network_seat **seat, qa_error *error) {
    if (!runtime || !command || !command->actor.registry || !command->actor.generation ||
        (unsigned)command->movement.kind > QA_MOVEMENT_Q3 ||
        !isfinite(command->movement.forward_move) || !isfinite(command->movement.side_move) ||
        !isfinite(command->movement.up_move) || !isfinite(command->movement.angles.x) ||
        !isfinite(command->movement.angles.y) || !isfinite(command->movement.angles.z) ||
        !isfinite(command->movement.acknowledged_server_seconds) ||
        (command->has_arsenal && (!command->arsenal.provider.data || !command->arsenal.provider.size ||
         (command->arsenal.weapon.size && !command->arsenal.weapon.data))))
        return qa_network_fail(error, "Invalid network command");
    *peer = qa_network_peer_get(runtime, command->client, error);
    if (!*peer || command->epoch != (*peer)->epoch)
        return qa_network_fail(error, "Command belongs to another world epoch");
    *seat = seat_get(*peer, command->seat, error);
    if (!*seat) return false;
    if ((*seat)->applying) return qa_network_fail(error, "Recursive seat command/prediction callback");
    const qa_net_client *client = qa_net_connections_get(runtime->connections, command->client);
    if (client->phase != QA_NET_ACTIVE) return qa_network_fail(error, "Connection has not completed signon");
    bool previous = runtime->callback; runtime->callback = true;
    (*seat)->applying = true;
    bool ok = runtime->options.hooks.controlled(runtime->options.hooks.context, command->client,
        command->seat, command->actor, command->movement.kind,
        command->has_arsenal ? command->arsenal.provider : (qa_bytes){0}, error);
    (*seat)->applying = false; runtime->callback = previous; return ok;
}
bool qa_network_accept(qa_network_runtime *runtime, const qa_network_command *command, qa_error *error) {
    qa_network_peer *peer; qa_network_seat *seat;
    if (!authority(runtime, command, &peer, &seat, error)) return false;
    if (seat->has_accepted && command->movement.sequence <= seat->accepted) return true;
    bool previous = runtime->callback; runtime->callback = true;
    seat->applying = true;
    bool ok = runtime->options.hooks.command(runtime->options.hooks.context, command, error);
    seat->applying = false; runtime->callback = previous;
    if (ok) { seat->accepted = command->movement.sequence; seat->has_accepted = true; }
    return ok;
}
bool qa_network_submit(qa_network_runtime *runtime, const qa_network_command *command, qa_error *error) {
    qa_network_peer *peer; qa_network_seat *seat;
    if (!authority(runtime, command, &peer, &seat, error)) return false;
    if ((!seat->has_submitted && command->movement.sequence != 1) ||
        (seat->has_submitted && (seat->submitted == UINT64_MAX || command->movement.sequence != seat->submitted + 1)))
        return qa_network_fail(error, "Local command sequence must start at one and remain contiguous");
    qa_network_history_entry *entry = &seat->history[command->movement.sequence % QA_NETWORK_COMMAND_BACKUP];
    if (entry->valid && (!seat->has_snapshot || entry->command.movement.sequence > seat->acknowledged))
        return qa_network_fail(error, "Unacknowledged prediction history is full");
    qa_network_history_entry pending = {.command = *command, .valid = true};
    if (command->has_arsenal) {
        if (command->arsenal.weapon.size > SIZE_MAX - command->arsenal.provider.size)
            return qa_network_fail(error, "Arsenal command exceeds storage");
        pending.arsenal.size = command->arsenal.provider.size + command->arsenal.weapon.size;
        pending.arsenal.data = malloc(pending.arsenal.size);
        if (!pending.arsenal.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining command arsenal"); return false; }
        memcpy(pending.arsenal.data, command->arsenal.provider.data, command->arsenal.provider.size);
        if (command->arsenal.weapon.size) memcpy(pending.arsenal.data + command->arsenal.provider.size,
            command->arsenal.weapon.data, command->arsenal.weapon.size);
        pending.command.arsenal.provider.data = pending.arsenal.data;
        pending.command.arsenal.weapon.data = pending.arsenal.data + command->arsenal.provider.size;
    }
    bool previous = runtime->callback; runtime->callback = true;
    seat->applying = true;
    bool ok = peer->ops.command(peer->state, command, error);
    seat->applying = false; runtime->callback = previous;
    if (!ok) { qa_buffer_free(&pending.arsenal); return false; }
    qa_buffer_free(&entry->arsenal); *entry = pending;
    seat->submitted = command->movement.sequence; seat->has_submitted = true; return true;
}
bool qa_network_snapshot_apply(qa_network_runtime *runtime, qa_net_client_id id,
                                const qa_network_snapshot *snapshot, qa_error *error) {
    qa_network_peer *peer = qa_network_peer_get(runtime, id, error);
    if (!peer || !snapshot || snapshot->epoch != peer->epoch || !snapshot->owner_checkpoint ||
        !runtime->options.hooks.restore || !runtime->options.hooks.replay)
        return qa_network_fail(error, "Missing admitted snapshot/prediction owners");
    qa_network_seat *seat = seat_get(peer, snapshot->seat, error);
    if (!seat) return false;
    if (seat->applying || (unsigned)snapshot->movement > QA_MOVEMENT_Q3)
        return qa_network_fail(error, "Invalid or recursive snapshot callback");
    if (seat->prediction_fault) return qa_network_fail(error, "Prediction fault requires travel or reconnect recovery");
    if (seat->has_snapshot && snapshot->sequence <= seat->snapshot) return true;
    if ((seat->has_submitted && snapshot->acknowledged_command > seat->submitted) ||
        (!seat->has_submitted && snapshot->acknowledged_command) ||
        (seat->has_snapshot && snapshot->acknowledged_command < seat->acknowledged))
        return qa_network_fail(error, "Snapshot acknowledges an unsent or regressed command");
    uint64_t count = seat->has_submitted ? seat->submitted - snapshot->acknowledged_command : 0;
    if (count >= QA_NETWORK_COMMAND_BACKUP)
        return qa_network_fail(error, "Snapshot exceeds retained prediction history");
    for (uint64_t i = 1; i <= count; ++i) {
        uint64_t sequence = snapshot->acknowledged_command + i;
        qa_network_history_entry *entry = &seat->history[sequence % QA_NETWORK_COMMAND_BACKUP];
        if (!entry->valid || entry->command.movement.sequence != sequence ||
            entry->command.actor.registry != snapshot->actor.registry ||
            entry->command.actor.slot != snapshot->actor.slot ||
            entry->command.actor.generation != snapshot->actor.generation)
            return qa_network_fail(error, "Prediction command actor/history differs from snapshot");
    }
    bool previous = runtime->callback; runtime->callback = true;
    seat->applying = true;
    bool ok = runtime->options.hooks.controlled(runtime->options.hooks.context, id, snapshot->seat,
        snapshot->actor, snapshot->movement, (qa_bytes){0}, error);
    if (!ok) { seat->applying = false; runtime->callback = previous; return false; }
    ok = runtime->options.hooks.restore(runtime->options.hooks.context, snapshot, error);
    for (uint64_t i = 1; ok && i <= count; ++i) {
        qa_network_history_entry *entry = &seat->history[(snapshot->acknowledged_command + i) % QA_NETWORK_COMMAND_BACKUP];
        ok = runtime->options.hooks.replay(runtime->options.hooks.context, &entry->command, error);
    }
    seat->applying = false; runtime->callback = previous;
    if (!ok) { seat->prediction_fault = true; return false; }
    seat->snapshot = snapshot->sequence; seat->acknowledged = snapshot->acknowledged_command; seat->has_snapshot = true;
    for (size_t i = 0; i < QA_NETWORK_COMMAND_BACKUP; ++i) {
        qa_network_history_entry *entry = &seat->history[i];
        if (entry->valid && entry->command.movement.sequence <= seat->acknowledged) {
            qa_buffer_free(&entry->arsenal); memset(entry, 0, sizeof(*entry));
        }
    }
    return true;
}
