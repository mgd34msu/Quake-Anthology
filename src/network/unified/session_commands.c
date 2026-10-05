#include "session_internal.h"
#include "value_internal.h"
#include "qa/network_unified_control.h"

#include <stdlib.h>
#include <string.h>

static bool bytes_equal(qa_bytes a, qa_bytes b)
{
    return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}

bool qa_unified_session_source_command(qa_unified_session *s, const qa_unified_source_command *command, qa_error *e)
{
    if (!qa_unified_session_idle(s) || !s->bound_source || s->server || !s->admitted ||
        !s->epoch || s->timeout_pending || s->closing || s->disconnected || !command || !command->instance ||
        !command->publication || !command->arguments ||
        !command->argument_count || command->argument_count > 128)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Compiled Source command lacks its admitted CLIENT and received activation");
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(s->runtime), s->id);
    if (!client || client->phase != QA_NET_ACTIVE)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Compiled Source command precedes its actual prepared frame");
    qa_unified_control value={.kind=QA_UNIFIED_CONTROL_SOURCE_COMMAND,.epoch=s->epoch,
        .value.source_command={.instance=(char *)command->instance,
            .publication=command->publication,.map_revision=command->map_revision,
            .arguments={.values=(char **)command->arguments,.count=command->argument_count}}};
    qa_unified_document *document=NULL;
    bool okay=qa_unified_document_create_control(&value,&document,e) && qa_unified_session_control(s,document,e);
    qa_unified_document_destroy(document); return okay;
}

bool qa_unified_session_player_read(const qa_unified_session *s, qa_unified_session_player *out, qa_error *e)
{
    qa_unified_session_player player = {0};
    if (!s->hooks.player(s->hooks.context, s->id, &player, e)) return false;
    if (!player.actor.registry || !player.source_owner ||
        player.seat.owner != s->seat.owner || player.seat.index != s->seat.index ||
        (unsigned)player.movement > QA_MOVEMENT_Q3 || (player.arsenal.size && !player.arsenal.data))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production player lacks its retained physical Source seat");
    *out = player;
    return true;
}

static void remove_input(qa_unified_input_batch *b, size_t at)
{
    qa_buffer_free(&b->providers[at]); qa_buffer_free(&b->weapons[at]);
    size_t tail = --b->count - at;
    if (tail) {
        memmove(b->commands + at, b->commands + at + 1, tail * sizeof(b->commands[0]));
        memmove(b->providers + at, b->providers + at + 1, tail * sizeof(b->providers[0]));
        memmove(b->weapons + at, b->weapons + at + 1, tail * sizeof(b->weapons[0]));
    }
    b->commands[b->count] = (qa_unified_input){0};
    b->providers[b->count] = (qa_buffer){0}; b->weapons[b->count] = (qa_buffer){0};
}

void qa_unified_session_ack(qa_unified_session *s, int64_t acknowledged)
{
    s->acknowledged = acknowledged;
    if (acknowledged < 0) return;
    qa_unified_input_batch *b = &s->inputs;
    size_t retained = 0;
    for (size_t i = 0; i < b->count; ++i) {
        if (b->commands[i].sequence <= (uint64_t)acknowledged) {
            qa_buffer_free(&b->providers[i]); qa_buffer_free(&b->weapons[i]);
        } else {
            if (retained != i) {
                b->commands[retained] = b->commands[i];
                b->providers[retained] = b->providers[i];
                b->weapons[retained] = b->weapons[i];
            }
            ++retained;
        }
    }
    for (size_t i = retained; i < b->count; ++i) {
        b->commands[i] = (qa_unified_input){0};
        b->providers[i] = (qa_buffer){0}; b->weapons[i] = (qa_buffer){0};
    }
    b->count = retained;
}

static bool retain_input(qa_unified_session *s, const qa_unified_input *input, qa_error *e)
{
    if (!input || !s->bound_source || s->server || s->disconnected || s->closing || !s->admitted || !s->epoch)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production input lacks its admitted local player");
    const qa_net_client *client = qa_net_connections_get(qa_network_connections(s->runtime), s->id);
    if (!client || client->phase != QA_NET_ACTIVE)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production input precedes the actual first prepared frame");
    if (s->acknowledged >= 0 && input->sequence <= (uint64_t)s->acknowledged) return true;
    for (size_t i = 0; i < s->inputs.count; ++i)
        if (s->inputs.commands[i].sequence == input->sequence) return true;
    qa_unified_session_player player;
    if (!qa_unified_session_player_read(s, &player, e)) return false;
    if (input->command.kind != player.movement ||
        (input->has_arsenal && !bytes_equal(input->arsenal.provider, player.arsenal)))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production input changes the player's selected providers");
    qa_unified_input_batch decoded = {0};
    bool ok=qa_unified_inputs_copy(s->epoch,input,1,&decoded,e);
    if (!ok) { qa_unified_inputs_free(&decoded); return false; }
    if (s->inputs.count == 64) remove_input(&s->inputs, 0);
    size_t at = s->inputs.count++;
    s->inputs.epoch = s->epoch;
    s->inputs.commands[at] = decoded.commands[0];
    s->inputs.providers[at] = decoded.providers[0]; s->inputs.weapons[at] = decoded.weapons[0];
    decoded.providers[0] = (qa_buffer){0}; decoded.weapons[0] = (qa_buffer){0};
    qa_unified_inputs_free(&decoded);
    return true;
}

bool qa_unified_session_input(qa_unified_session *s, const qa_unified_input *input, qa_error *e)
{
    if (!qa_unified_session_idle(s))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production input producer is not idle");
    return retain_input(s, input, e);
}

bool qa_unified_session_queue_inputs(qa_unified_session *s, qa_error *e)
{
    s->inputs.epoch=s->epoch;
    return qa_unified_inputs_write(&s->inputs,s->limits.message_bytes,&s->frame_wire,e) &&
        qa_unified_channel_frame(s->channel,(qa_bytes){s->frame_wire.data,s->frame_wire.size},0,e);
}

static qa_unified_movement command_movement(const qa_movement_command *command)
{
    qa_unified_movement movement = {.kind = command->kind};
    qa_unified_vec3 angles = {command->angles.x, command->angles.y, command->angles.z};
    switch (command->kind) {
    case QA_MOVEMENT_NETQUAKE:
        movement.data.nq.acknowledged_seconds = command->acknowledged_server_seconds;
        movement.data.nq.angles = angles;
        movement.data.nq.forward = command->forward_move; movement.data.nq.side = command->side_move;
        movement.data.nq.up = command->up_move; movement.data.nq.buttons = command->buttons;
        movement.data.nq.impulse = command->impulse;
        break;
    case QA_MOVEMENT_QUAKEWORLD:
        movement.data.qw.milliseconds = command->milliseconds; movement.data.qw.angles = angles;
        movement.data.qw.forward = command->forward_move; movement.data.qw.side = command->side_move;
        movement.data.qw.up = command->up_move; movement.data.qw.buttons = command->buttons;
        movement.data.qw.impulse = command->impulse;
        break;
    case QA_MOVEMENT_Q2_CLASSIC:
        movement.data.q2.milliseconds = command->milliseconds;
        for (size_t i = 0; i < 3; ++i) movement.data.q2.angle_shorts[i] = command->angle_words[i];
        movement.data.q2.forward = command->forward_move; movement.data.q2.side = command->side_move;
        movement.data.q2.up = command->up_move; movement.data.q2.buttons = command->buttons;
        movement.data.q2.impulse = command->impulse; movement.data.q2.light_level = command->light_level;
        break;
    case QA_MOVEMENT_Q2_RERELEASE:
        movement.data.q2r.milliseconds = command->milliseconds; movement.data.q2r.angles = angles;
        movement.data.q2r.forward = command->forward_move; movement.data.q2r.side = command->side_move;
        movement.data.q2r.buttons = command->buttons; movement.data.q2r.server_frame = command->server_frame;
        break;
    case QA_MOVEMENT_Q3:
        movement.data.q3.server_time_ms = command->server_time_ms;
        for (size_t i = 0; i < 3; ++i) movement.data.q3.angle_words[i] = command->angle_words[i];
        movement.data.q3.forward = command->forward_move; movement.data.q3.right = command->side_move;
        movement.data.q3.up = command->up_move; movement.data.q3.buttons = command->buttons;
        movement.data.q3.weapon = command->weapon;
        break;
    }
    return movement;
}

bool qa_unified_session_command(void *state, const qa_network_command *command, qa_error *e)
{
    qa_unified_session *s = state;
    if (!command || s->entered || s->processing || !qa_net_client_id_equal(command->client, s->id) ||
        command->epoch != qa_network_epoch(s->runtime, s->id) ||
        command->seat.owner != s->seat.owner || command->seat.index != s->seat.index)
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production native command changes its retained client seat");
    qa_unified_session_player player;
    if (!qa_unified_session_player_read(s, &player, e)) return false;
    if (!qa_actor_id_equal(player.actor, command->actor))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production native command changes its actual canonical actor");
    qa_unified_input input = {.sequence = command->movement.sequence, .command = command_movement(&command->movement),
        .has_arsenal = command->has_arsenal, .arsenal = command->arsenal};
    return retain_input(s, &input, e);
}
