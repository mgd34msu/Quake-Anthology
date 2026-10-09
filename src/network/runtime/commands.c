#include "internal.h"
#include "qa/network_save.h"
#include "qa/network_unified_session.h"
#include "qa/network_q1_client_runtime.h"
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
static bool authority_identity(qa_network_runtime *runtime, qa_net_client_id id,
    qa_net_seat_id seat_id, qa_actor_id actor, uint64_t epoch, qa_ruleset_id movement,
    qa_bytes arsenal, qa_network_peer **peer, qa_network_seat **seat, qa_error *error)
{
    if (!runtime || !actor.registry || (unsigned)movement > QA_RULESET_Q3)
        return qa_network_fail(error, "Invalid network command owner");
    *peer = qa_network_peer_get(runtime, id, error);
    if (!*peer || epoch != (*peer)->epoch)
        return qa_network_fail(error, "Command belongs to another world epoch");
    *seat = seat_get(*peer, seat_id, error);
    if (!*seat) return false;
    if ((*seat)->applying) return qa_network_fail(error, "Recursive seat command/prediction callback");
    const qa_net_client *client = qa_net_connections_get(runtime->connections, id);
    if (client->phase != QA_NET_ACTIVE) return qa_network_fail(error, "Connection has not completed signon");
    bool previous = runtime->callback; runtime->callback = true;
    (*seat)->applying = true;
    bool ok = runtime->options.hooks.controlled(runtime->options.hooks.context, id,
        seat_id, actor, movement, arsenal, error);
    (*seat)->applying = false; runtime->callback = previous; return ok;
}
static bool authority(qa_network_runtime *runtime, const qa_network_command *command,
    qa_network_peer **peer, qa_network_seat **seat, qa_error *error)
{
    if (!command || !isfinite(command->movement.forward_move) || !isfinite(command->movement.side_move) ||
        !isfinite(command->movement.up_move) || !isfinite(command->movement.angles.x) ||
        !isfinite(command->movement.angles.y) || !isfinite(command->movement.angles.z) ||
        !isfinite(command->movement.acknowledged_server_seconds) ||
        (command->has_arsenal && (!command->arsenal.provider.data || !command->arsenal.provider.size ||
         (command->arsenal.weapon.size && !command->arsenal.weapon.data))))
        return qa_network_fail(error, "Invalid network command");
    return authority_identity(runtime, command->client, command->seat, command->actor, command->epoch,
        command->movement.kind, command->has_arsenal ? command->arsenal.provider : (qa_bytes){0}, peer, seat, error);
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
bool qa_network_accept_unified_input(qa_network_runtime *runtime,qa_net_client_id client,
    qa_net_seat_id seat_id,qa_actor_id actor,uint64_t epoch,const qa_unified_input *input,qa_error *error)
{
    if(!runtime || !input || !runtime->options.hooks.unified_input ||
        (unsigned)input->command.kind>QA_RULESET_Q3 ||
        (input->has_arsenal && (!input->arsenal.provider.data || !input->arsenal.provider.size ||
            (input->arsenal.weapon.size && !input->arsenal.weapon.data))))
        return qa_network_fail(error,"Anthology input requires its actual binary64 source consumer");
    const qa_net_client *connection=qa_net_connections_get(runtime->connections,client);
    if(!connection || connection->protocol.kind!=QA_NET_UNIFIED_1)
        return qa_network_fail(error,"Anthology input changes its admitted source protocol");
    qa_network_peer *peer; qa_network_seat *seat;
    if(!authority_identity(runtime,client,seat_id,actor,epoch,input->command.kind,
        input->has_arsenal?input->arsenal.provider:(qa_bytes){0},&peer,&seat,error)) return false;
    if(seat->has_accepted && input->sequence<=seat->accepted) return true;
    bool previous=runtime->callback; runtime->callback=true; seat->applying=true;
    bool ok=runtime->options.hooks.unified_input(runtime->options.hooks.context,client,seat_id,actor,epoch,input,error);
    seat->applying=false; runtime->callback=previous;
    if(ok) { seat->accepted=input->sequence; seat->has_accepted=true; }
    return ok;
}
bool qa_network_accept_commands(qa_network_runtime *runtime,
    const qa_network_command_group *group, qa_error *error)
{
    if (!runtime || !group || !group->commands || !group->count || group->count > 20 ||
        (unsigned)group->movement > QA_RULESET_Q3 || !runtime->options.hooks.commands)
        return qa_network_fail(error, "QuakeWorld group requires its complete command consumer");
    uint64_t sequence = group->commands[0].sequence;
    if (!sequence) return qa_network_fail(error, "QuakeWorld source packet sequence is zero");
    for (size_t i = 0; i < group->count; ++i) {
        const qa_movement_command *command = group->commands + i;
        if (command->kind != QA_RULESET_QUAKEWORLD || command->sequence != sequence ||
            command->milliseconds > UINT8_MAX || command->buttons > UINT8_MAX ||
            !isfinite(command->forward_move) || !isfinite(command->side_move) ||
            !isfinite(command->up_move) || !isfinite(command->angles.x) ||
            !isfinite(command->angles.y) || !isfinite(command->angles.z) ||
            !isfinite(command->acknowledged_server_seconds))
            return qa_network_fail(error, "QuakeWorld group changes its source sequence or command domain");
    }
    qa_network_peer *peer; qa_network_seat *seat;
    if (!authority_identity(runtime, group->client, group->seat, group->actor, group->epoch,
        group->movement, (qa_bytes){0}, &peer, &seat, error)) return false;
    if (seat->has_accepted && sequence <= seat->accepted) return true;
    bool previous = runtime->callback; runtime->callback = true; seat->applying = true;
    bool ok = runtime->options.hooks.commands(runtime->options.hooks.context, group, error);
    seat->applying = false; runtime->callback = previous;
    if (ok) { seat->accepted = sequence; seat->has_accepted = true; }
    return ok;
}
bool qa_network_accept_q3_source_command(qa_network_runtime *runtime,
    const qa_network_q3_source_command *command, qa_error *error)
{
    if (!runtime || !command || !command->sequence ||
        (unsigned)command->movement > QA_RULESET_Q3 || !runtime->options.hooks.q3_source_command)
        return qa_network_fail(error, "Q3 source command requires its complete raw command consumer");
    qa_network_peer *peer; qa_network_seat *seat;
    if (!authority_identity(runtime, command->client, command->seat, command->actor,
        command->epoch, command->movement, (qa_bytes){0}, &peer, &seat, error)) return false;
    if (seat->has_accepted && command->sequence <= seat->accepted) return true;
    bool previous = runtime->callback; runtime->callback = true; seat->applying = true;
    bool ok = runtime->options.hooks.q3_source_command(runtime->options.hooks.context, command, error);
    seat->applying = false; runtime->callback = previous;
    if (ok) { seat->accepted = command->sequence; seat->has_accepted = true; }
    return ok;
}
bool qa_network_accept_nq_source_command(qa_network_runtime *runtime,
    const qa_network_nq_source_command *command, qa_error *error)
{
    if (!runtime || !command || !command->sequence || !command->source_owner ||
        (unsigned)command->movement > QA_RULESET_Q3 || !runtime->options.hooks.nq_source_command ||
        !isfinite(command->command.time) || !isfinite(command->command.angles[0]) ||
        !isfinite(command->command.angles[1]) || !isfinite(command->command.angles[2]))
        return qa_network_fail(error, "NetQuake source command requires its genuine raw command consumer");
    const qa_net_client *client = qa_net_connections_get(runtime->connections, command->client);
    if (!client || client->protocol.kind > QA_NET_RMQ999 || !qa_q1_profile_valid(client->protocol, error))
        return qa_network_fail(error, "NetQuake source command changes its admitted source dialect");
    qa_network_peer *peer; qa_network_seat *seat;
    if (!authority_identity(runtime, command->client, command->seat, command->actor,
        command->epoch, command->movement, (qa_bytes){0}, &peer, &seat, error)) return false;
    if (seat->has_accepted && command->sequence <= seat->accepted) return true;
    bool previous = runtime->callback; runtime->callback = true; seat->applying = true;
    bool ok = runtime->options.hooks.nq_source_command(runtime->options.hooks.context, command, error);
    seat->applying = false; runtime->callback = previous;
    if (ok) { seat->accepted = command->sequence; seat->has_accepted = true; }
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
    if (seat->applying || (unsigned)snapshot->movement > QA_RULESET_Q3)
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
bool qa_network_q1_client_submit(qa_network_runtime *runtime,
    const qa_network_command *command, qa_error *error)
{
    if (!runtime || !command || !command->movement.sequence || !qa_network_callbacks_idle(runtime))
        return qa_network_fail(error,"Q1 CLIENT input requires its actual idle Source command");
    qa_network_peer *peer; qa_network_seat *seat;
    if (!authority(runtime,command,&peer,&seat,error)) return false;
    const qa_net_client *client=qa_net_connections_get(runtime->connections,command->client);
    if (!client || !qa_network_q1_client_peer(peer) || peer->seat_count!=1 ||
        command->movement.kind!=(qa_q1_is_qw(client->protocol)?QA_RULESET_QUAKEWORLD:QA_RULESET_NETQUAKE))
        return qa_network_fail(error,"Q1 CLIENT input differs from its admitted native command profile");
    runtime->callback=true; seat->applying=true;
    bool ok=peer->ops.command(peer->state,command,error);
    seat->applying=false; runtime->callback=false;
    return ok;
}
