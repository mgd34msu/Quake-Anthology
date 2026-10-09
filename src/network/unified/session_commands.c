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
        (unsigned)player.movement > QA_RULESET_Q3 || (player.arsenal.size && !player.arsenal.data))
        return qa_unified_session_fail(e, QA_ERROR_ARGUMENT, "Production player lacks its retained physical Source seat");
    *out = player;
    return true;
}

static void remove_input(qa_unified_input_batch *b, size_t at)
{
    qa_buffer provider = b->providers[at], weapon = b->weapons[at];
    size_t provider_capacity = b->provider_capacity[at], weapon_capacity = b->weapon_capacity[at];
    size_t tail = --b->count - at;
    if (tail) {
        memmove(b->commands + at, b->commands + at + 1, tail * sizeof(b->commands[0]));
        memmove(b->providers + at, b->providers + at + 1, tail * sizeof(b->providers[0]));
        memmove(b->weapons + at, b->weapons + at + 1, tail * sizeof(b->weapons[0]));
        memmove(b->provider_capacity + at, b->provider_capacity + at + 1, tail * sizeof(b->provider_capacity[0]));
        memmove(b->weapon_capacity + at, b->weapon_capacity + at + 1, tail * sizeof(b->weapon_capacity[0]));
    }
    b->commands[b->count] = (qa_unified_input){0};
    b->providers[b->count] = provider; b->weapons[b->count] = weapon;
    b->provider_capacity[b->count] = provider_capacity; b->weapon_capacity[b->count] = weapon_capacity;
}

void qa_unified_session_ack(qa_unified_session *s, int64_t acknowledged)
{
    s->acknowledged = acknowledged;
    if (acknowledged < 0) return;
    qa_unified_input_batch *b = &s->inputs;
    size_t retained = 0;
    for (size_t i = 0; i < b->count; ++i) {
        if (b->commands[i].sequence <= (uint64_t)acknowledged) {
            b->commands[i] = (qa_unified_input){0};
        } else {
            if (retained != i) {
                qa_buffer provider = b->providers[retained], weapon = b->weapons[retained];
                size_t provider_capacity = b->provider_capacity[retained], weapon_capacity = b->weapon_capacity[retained];
                b->commands[retained] = b->commands[i];
                b->providers[retained] = b->providers[i];
                b->weapons[retained] = b->weapons[i];
                b->provider_capacity[retained] = b->provider_capacity[i];
                b->weapon_capacity[retained] = b->weapon_capacity[i];
                b->providers[i] = provider; b->weapons[i] = weapon;
                b->provider_capacity[i] = provider_capacity; b->weapon_capacity[i] = weapon_capacity;
            }
            ++retained;
        }
    }
    for (size_t i = retained; i < b->count; ++i) {
        b->commands[i] = (qa_unified_input){0};
    }
    b->count = retained;
}

static bool selection_reserve(qa_buffer *buffer, size_t *capacity, size_t size, qa_error *e)
{
    if (size <= *capacity) return true;
    size_t next = *capacity ? *capacity : 16;
    while (next < size) next *= 2;
    uint8_t *data = realloc(buffer->data, next);
    if (!data) return qa_unified_session_fail(e, QA_ERROR_MEMORY, "Retaining actual Unified input selection");
    buffer->data = data; *capacity = next; return true;
}

static void selection_copy(qa_buffer *buffer, qa_bytes source)
{
    if (source.size && (buffer->size != source.size || memcmp(buffer->data, source.data, source.size)))
        memcpy(buffer->data, source.data, source.size);
    buffer->size = source.size;
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
    qa_unified_input_batch view = {.epoch = s->epoch, .count = 1};
    view.commands[0] = *input;
    if (input->has_arsenal) {
        view.providers[0] = (qa_buffer){(uint8_t *)input->arsenal.provider.data, input->arsenal.provider.size};
        view.weapons[0] = (qa_buffer){(uint8_t *)input->arsenal.weapon.data, input->arsenal.weapon.size};
    }
    size_t measured;
    if (!qa_unified_inputs_check(&view, &measured, e)) return false;
    size_t reusable = s->inputs.count == 64 ? 0 : s->inputs.count;
    bool ok = selection_reserve(s->inputs.providers + reusable, s->inputs.provider_capacity + reusable,
        view.providers[0].size, e) &&
        selection_reserve(s->inputs.weapons + reusable, s->inputs.weapon_capacity + reusable,
        view.weapons[0].size, e);
    if (reusable < s->inputs.count) {
        s->inputs.commands[reusable].arsenal.provider = (qa_bytes){s->inputs.providers[reusable].data, s->inputs.providers[reusable].size};
        s->inputs.commands[reusable].arsenal.weapon = (qa_bytes){s->inputs.weapons[reusable].data, s->inputs.weapons[reusable].size};
    }
    if (!ok) return false;
    if (s->inputs.count == 64) remove_input(&s->inputs, 0);
    size_t at = s->inputs.count++;
    s->inputs.epoch = s->epoch;
    s->inputs.commands[at] = view.commands[0];
    selection_copy(s->inputs.providers + at, (qa_bytes){view.providers[0].data, view.providers[0].size});
    selection_copy(s->inputs.weapons + at, (qa_bytes){view.weapons[0].data, view.weapons[0].size});
    s->inputs.commands[at].arsenal.provider = (qa_bytes){s->inputs.providers[at].data, s->inputs.providers[at].size};
    s->inputs.commands[at].arsenal.weapon = (qa_bytes){s->inputs.weapons[at].data, s->inputs.weapons[at].size};
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
    case QA_RULESET_NETQUAKE:
        movement.data.nq.acknowledged_seconds = command->acknowledged_server_seconds;
        movement.data.nq.angles = angles;
        movement.data.nq.forward = command->forward_move; movement.data.nq.side = command->side_move;
        movement.data.nq.up = command->up_move; movement.data.nq.buttons = command->buttons;
        movement.data.nq.impulse = command->impulse;
        break;
    case QA_RULESET_QUAKEWORLD:
        movement.data.qw.milliseconds = command->milliseconds; movement.data.qw.angles = angles;
        movement.data.qw.forward = command->forward_move; movement.data.qw.side = command->side_move;
        movement.data.qw.up = command->up_move; movement.data.qw.buttons = command->buttons;
        movement.data.qw.impulse = command->impulse;
        break;
    case QA_RULESET_Q2_CLASSIC:
        movement.data.q2.milliseconds = command->milliseconds;
        for (size_t i = 0; i < 3; ++i) movement.data.q2.angle_shorts[i] = command->angle_words[i];
        movement.data.q2.forward = command->forward_move; movement.data.q2.side = command->side_move;
        movement.data.q2.up = command->up_move; movement.data.q2.buttons = command->buttons;
        movement.data.q2.impulse = command->impulse; movement.data.q2.light_level = command->light_level;
        break;
    case QA_RULESET_Q2_RERELEASE:
        movement.data.q2r.milliseconds = command->milliseconds; movement.data.q2r.angles = angles;
        movement.data.q2r.forward = command->forward_move; movement.data.q2r.side = command->side_move;
        movement.data.q2r.buttons = command->buttons; movement.data.q2r.server_frame = command->server_frame;
        break;
    case QA_RULESET_Q3:
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
