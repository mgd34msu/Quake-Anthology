#include "remote_q2_private.h"
#include "qa/persistence_content.h"
#include "qa/scene_world_save.h"
#include "qa/scene_model_save.h"
#include "qa/scene_resource_save.h"
#include "qa/material_library_save.h"
#include "qa/font_save.h"
#include "capture.h"
#include "remote_q2_effects.h"
#include "remote_q2_effects_bridge.h"
#include "remote_q2_footsteps.h"
#include "remote_q2_material_movies_bridge.h"
#include "qa/media_library_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool remote_q2_fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }
static bool linked(const frontend_remote_q2 *row)
{
    for (const frontend_remote_q2 *p = row && row->frontend ? row->frontend->remote_q2 : NULL; p; p = p->next)
        if (p == row) return true;
    return false;
}
bool remote_q2_domain_equal(const frontend_remote_q2_domain *a, const frontend_remote_q2_domain *b)
{
    return a && b && a->application == b->application && a->runtime == b->runtime &&
        qa_net_client_id_equal(a->client, b->client) && a->seat.owner == b->seat.owner &&
        a->seat.index == b->seat.index && a->epoch == b->epoch &&
        a->configuration_generation == b->configuration_generation && a->physical_seat == b->physical_seat &&
        a->protocol.kind == b->protocol.kind && a->protocol.revision == b->protocol.revision &&
        a->protocol.flags == b->protocol.flags && a->catalog == b->catalog && a->product == b->product &&
        a->console == b->console && a->cvars == b->cvars &&
        a->command_context.session == b->command_context.session &&
        a->command_context.owner == b->command_context.owner &&
        a->command_context.client == b->command_context.client &&
        a->command_context.seat == b->command_context.seat &&
        a->command_context.registry == b->command_context.registry &&
        a->command_context.generation == b->command_context.generation &&
        a->command_context.dialect == b->command_context.dialect &&
        a->command_context.origin == b->command_context.origin &&
        a->command_context.direct == b->command_context.direct &&
        a->command_context.console_text == b->command_context.console_text &&
        qa_actor_id_equal(a->command_context.actor, b->command_context.actor) &&
        a->command_context.script == b->command_context.script;
}
bool remote_q2_live(const frontend_remote_q2 *row, qa_error *error)
{
    if (!row || !linked(row) || !row->bound || row->retired || row->importing || row->image_policy ||
        row->frontend->capture || row->frontend->resource_inventory ||
        row->frontend->application != row->options.domain.application ||
        qa_network_epoch(row->options.domain.runtime, row->options.domain.client) != row->options.domain.epoch)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Remote Q2 receiver has no current authenticated CLIENT");
    return row->options.current(row->options.context, &row->options.domain, error);
}
bool frontend_remote_q2_wire_seat(const frontend_remote_q2 *row, uint32_t *out, qa_error *error)
{
    const qa_net_client *client = row && row->bound ? qa_net_connections_get(
        qa_network_connections(row->options.domain.runtime), row->options.domain.client) : NULL;
    if (!row || !linked(row) || row->retired || row->importing || !client || !out)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 wire seat has no actual attached connection");
    bool found = false;
    for (size_t i = 0; i < client->seat_count; ++i)
        if (client->seats[i].seat.owner == row->options.domain.seat.owner &&
            client->seats[i].seat.index == row->options.domain.seat.index) {
            if (found || client->seats[i].remote_index >= QA_Q2_MAX_SEATS)
                return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 wire seat binding is not unique and representable");
            *out = client->seats[i].remote_index; found = true;
        }
    return found || remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 canonical seat is absent from the actual connection");
}
remote_q2_layout remote_q2_layout_read(qa_net_protocol_id protocol)
{
    if (protocol.kind == QA_NET_Q2REPRO_1038 || protocol.kind == QA_NET_Q2KEX_2023 ||
        protocol.kind == QA_NET_Q2KEX_DEMO_2022)
        return (remote_q2_layout){62, 8254, 10302, 10814, 11326, 11582, 61, 8192, 2048, 512, 12448};
    return (remote_q2_layout){32, 288, 544, 800, 1056, 1312, 31, 256, 256, 256, 2080};
}
const char *frontend_remote_q2_config(const frontend_remote_q2 *row, uint16_t index)
{ return row && row->configs && index < row->layout.max_configs && row->configs[index] ? row->configs[index] : ""; }
bool remote_q2_config_set(frontend_remote_q2 *row, uint16_t index, const char *value, qa_error *error)
{
    if (!value || index >= row->layout.max_configs)
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 configstring leaves the actual protocol layout");
    size_t size = strlen(value) + 1; char *copy = malloc(size);
    if (!copy) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 configstring");
    memcpy(copy, value, size); free(row->configs[index]); row->configs[index] = copy; return true;
}
static bool content_clear(frontend_remote_q2 *row, qa_error *error)
{
    if (!remote_q2_media_clear(row, error)) return false;
    remote_q2_download_clear(row);
    qa_q2_frame_free(&row->frame); qa_q2_frame_free(&row->previous);
    for (size_t i = 0; row->configs && i < row->layout.max_configs; ++i) { free(row->configs[i]); row->configs[i] = NULL; }
    free(row->baselines); row->baselines = NULL; row->baseline_count = 0;
    free(row->overlay); row->overlay = NULL; memset(row->inventory, 0, sizeof(row->inventory));
    qa_vfs_destroy(row->content.mounts); qa_catalog_release(row->content.catalog);
    row->content = (frontend_remote_q2_content){0}; row->selected = row->content_admitted = false; row->height_set = false;
    row->sample_frame_seconds = 0;
    row->gun_set = false; row->gun_frame = row->gun_previous_frame = 0; row->gun_server_frame = 0;
    row->fog_start = row->fog_end = (qa_scene_fog){0};
    row->fog_started_ms = 0; row->fog_duration_ms = 0; row->fog_received = false;
    memset(row->sent, 0, sizeof(row->sent)); row->sent_set = row->input_set = false;
    memset(row->commands, 0, sizeof(row->commands)); row->last_command = row->acknowledged_command = 0;
    row->predicted = false; row->prediction_error = row->prediction_pml = qa_v3(0, 0, 0);
    row->prediction_step = 0; row->prediction_step_ns = 0;
    row->prediction_command = 0; row->prediction_frame = 0;
    row->prediction_ground = (qa_movement_ground){0}; row->prediction_plane = (qa_collision_plane){0};
    row->last_sent = row->acknowledged = 0; qa_input_command_clear(&row->input);
    if (row->bound && !row->retired && !row->importing && row->options.entities_changed &&
        qa_net_connections_get(qa_network_connections(row->options.domain.runtime), row->options.domain.client)) {
        ++row->busy;
        bool ok = row->options.current(row->options.context, &row->options.domain, error) &&
            row->options.entities_changed(row->options.context, &row->options.domain, error);
        --row->busy;
        if (!ok) return false;
    }
    return true;
}
bool frontend_remote_q2_create(qa_frontend *f, const frontend_remote_q2_options *options,
    frontend_remote_q2 **out, qa_error *error)
{
    const frontend_remote_q2_domain *d = options ? &options->domain : NULL;
    const qa_product *product = d ? qa_catalog_product(d->catalog, d->product) : NULL;
    if (!f || f->capture || f->resource_inventory || !options || !out || *out || !options->current || !options->download_allowed || !options->download_nonce || !options->records ||
        !options->disconnected || !d->application || f->application != d->application ||
        !d->runtime || !d->console || !d->cvars || !product || product->family != QA_GAME_Q2 ||
        d->physical_seat >= f->options.seats || !f->seats || !f->seats[d->physical_seat].input ||
        d->client.owner || d->client.generation || d->client.slot || d->epoch ||
        d->protocol.kind < QA_NET_Q2_34 || d->protocol.kind > QA_NET_Q2PRIVATE_4038)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 construction requires its pending real CLIENT registry and physical input");
    frontend_remote_q2 *row = calloc(1, sizeof(*row));
    if (!row) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining remote Q2 CLIENT");
    row->frontend = f; row->options = *options; row->layout = remote_q2_layout_read(d->protocol);
    row->configs = calloc(row->layout.max_configs, sizeof(*row->configs));
    if (!row->configs || !frontend_source_identity_allocate(f, &row->identity, error)) {
        free(row->configs); free(row); return false;
    }
    qa_catalog_retain(d->catalog); row->frame_ms = 100; row->fraction = 1;
    row->next = f->remote_q2; f->remote_q2 = row; *out = row; return true;
}
bool frontend_remote_q2_bind(frontend_remote_q2 *row, const frontend_remote_q2_domain *actual, qa_error *error)
{
    if (!row || !actual || !linked(row) || row->frontend->capture || row->frontend->resource_inventory || row->image_policy || row->bound || row->busy || row->retired ||
        !actual->client.owner || !actual->client.generation || !actual->epoch)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 bind requires its actual successful attach result");
    frontend_remote_q2_domain expected = row->options.domain;
    expected.client = actual->client; expected.seat = actual->seat; expected.epoch = actual->epoch;
    if (!remote_q2_domain_equal(&expected, actual) ||
        qa_network_epoch(actual->runtime, actual->client) != actual->epoch ||
        !row->options.current(row->options.context, actual, error)) return false;
    row->options.domain = *actual; row->bound = true; return true;
}
static bool hook_current(void *context, qa_net_client_id id, qa_error *error)
{
    frontend_remote_q2 *row = context;
    return row && qa_net_client_id_equal(id, row->options.domain.client) && remote_q2_live(row, error);
}
static bool select_owned(frontend_remote_q2 *row, const qa_q2_serverdata *data, qa_error *error)
{
    const char *key = row->layout.max_models == 8192 ? "q2-rerelease-baseq2" : "q2-classic-baseq2";
    const qa_product *base = qa_catalog_find(row->options.domain.catalog, key);
    qa_catalog *fresh = NULL; qa_product_id selected = 0; qa_vfs *mounts = NULL;
    if (!base || row->content_generation == UINT64_MAX)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 CLIENT lacks its actual configured base");
    if (!qa_catalog_discover_remote_q2(row->options.domain.catalog, base->id, data->gamedir,
        row->content_generation + 1, &fresh, &selected, error)) return false;
    const qa_product *fresh_base = qa_catalog_find(fresh, key);
    if (!fresh_base || !qa_catalog_open(fresh, selected, &mounts, error)) { qa_catalog_release(fresh); return false; }
    row->content = (frontend_remote_q2_content){fresh, selected, fresh_base->id, mounts,
        qa_catalog_product_write_root(fresh, selected), qa_catalog_product_write_root(fresh, fresh_base->id)};
    return true;
}
static bool serverdata_equal(const qa_q2_serverdata *a, const qa_q2_serverdata *b)
{
    if (a->servercount != b->servercount || a->attractloop != b->attractloop ||
        strcmp(a->gamedir, b->gamedir) || strcmp(a->levelname, b->levelname) ||
        a->clientnum != b->clientnum || a->client_count != b->client_count ||
        a->server_state != b->server_state || a->server_fps != b->server_fps ||
        a->wire_flags != b->wire_flags || a->protocol_revision != b->protocol_revision ||
        a->strafejump_hack != b->strafejump_hack || a->qw_mode != b->qw_mode || a->waterjump_hack != b->waterjump_hack)
        return false;
    for (size_t i = 0; i < a->client_count; ++i) if (a->clientnums[i] != b->clientnums[i]) return false;
    return true;
}
static bool content_publish(frontend_remote_q2 *row, qa_error *error)
{
    if (row->content_admitted) return true;
    ++row->busy;
    bool admitted = !row->options.content_admit || row->options.content_admit(row->options.context,
        row->loading_generation, &row->content, error);
    --row->busy;
    if (admitted) row->content_admitted = true;
    return admitted && remote_q2_live(row, error);
}
static bool hook_serverdata(void *context, qa_net_client_id id, uint64_t generation,
    const qa_q2_serverdata *data, qa_q2_preparation *result, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!hook_current(row, id, error) || !data || !generation || !result) return false;
    if (generation != row->loading_generation) {
        if (!content_clear(row, error)) return false;
        row->loading_generation = generation; row->data = *data;
    } else if (!serverdata_equal(&row->data, data))
        return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 retained serverdata changed within one loading operation");
    if (!row->selected) {
        ++row->busy;
        bool ok;
        if (row->options.select_content) {
            frontend_remote_q2_content borrowed = {0};
            ok = row->options.select_content(row->options.context, generation, data, &borrowed, result, error);
            if (ok && *result == QA_Q2_PREPARATION_READY) {
                const qa_product *selected = qa_catalog_product(borrowed.catalog, borrowed.selected);
                ok = selected && selected->family == QA_GAME_Q2 &&
                    qa_catalog_product_view_current(borrowed.catalog, borrowed.selected, borrowed.mounts) &&
                    borrowed.selected_write_root == qa_catalog_product_write_root(borrowed.catalog, borrowed.selected) &&
                    borrowed.base_write_root == qa_catalog_product_write_root(borrowed.catalog, borrowed.base);
                if (ok) {
                    row->content = borrowed; row->content.mounts = qa_vfs_clone(borrowed.mounts, error);
                    ok = row->content.mounts != NULL;
                    if (ok) qa_catalog_retain(row->content.catalog); else row->content = (frontend_remote_q2_content){0};
                }
            }
        } else { ok = select_owned(row, data, error); *result = QA_Q2_PREPARATION_READY; }
        --row->busy;
        if (!ok || !remote_q2_live(row, error)) return false;
        if (*result != QA_Q2_PREPARATION_READY) return true;
        row->selected = true; ++row->content_generation;
    }
    if (!content_publish(row, error)) return false;
    *result = QA_Q2_PREPARATION_READY; return true;
}
static bool hook_prepare(void *context, qa_net_client_id id, uint64_t generation,
    const qa_q2_game_state *state, qa_q2_preparation *result, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!hook_current(row, id, error) || !state || !result || !row->selected ||
        generation != row->loading_generation || state->data.servercount != row->data.servercount)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 media preparation lost its retained serverdata");
    if (!content_publish(row, error)) return false;
    if (row->media_ready) { *result = QA_Q2_PREPARATION_READY; return true; }
    for (size_t i = 0; i < state->config_count; ++i)
        if (!remote_q2_config_set(row, state->configs[i].index, state->configs[i].value, error)) return false;
    if (state->baselines.count > SIZE_MAX / sizeof(*row->baselines)) return false;
    qa_q2_entity *baselines = state->baselines.count ? malloc(state->baselines.count * sizeof(*baselines)) : NULL;
    if (state->baselines.count && !baselines) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 baselines");
    if (state->baselines.count) memcpy(baselines, state->baselines.data, state->baselines.count * sizeof(*baselines));
    free(row->baselines); row->baselines = baselines; row->baseline_count = state->baselines.count;
    ++row->busy;
    bool ok = remote_q2_download_prepare(row, result, error);
    if (ok && *result == QA_Q2_PREPARATION_READY) ok = remote_q2_media_prepare(row, error);
    --row->busy;
    return ok && remote_q2_live(row, error);
}
static bool hook_frame(void *context, qa_net_client_id id, const qa_q2_wire_frame *frame,
    const qa_q2_server_record *records, size_t count, uint64_t received_ns, qa_error *error)
{
    (void)records; (void)count; frontend_remote_q2 *row = context;
    if (!hook_current(row, id, error) || !frame || !frame->valid || !row->media_ready) return false;
    qa_q2_wire_frame held = {0};
    if (!qa_q2_frame_clone(frame, &held, error)) return false;
    qa_q2_frame_free(&row->previous); row->previous = row->frame; row->frame = held;
    row->received_ns = received_ns; row->fraction = 0;
    row->frame_ms = row->data.server_fps ? 1000.0f / (float)row->data.server_fps : 100;
    if (row->layout.max_models == 8192) {
        uint32_t seat;
        if (!frontend_remote_q2_wire_seat(row, &seat, error) || seat >= row->frame.player_count) return false;
        const qa_q2_player *player = &row->frame.players[seat].player;
        const qa_q2_player *previous = row->previous.valid && seat < row->previous.player_count ?
            &row->previous.players[seat].player : player;
        if (previous->gunindex != player->gunindex) {
            row->gun_frame = row->gun_previous_frame = player->gunframe;
            row->gun_server_frame = row->frame.server_frame;
        } else if (!row->gun_set || row->gun_frame != player->gunframe) {
            row->gun_frame = player->gunframe; row->gun_previous_frame = previous->gunframe;
            row->gun_server_frame = row->frame.server_frame;
        }
        row->gun_set = true;
    }
    ++row->busy;
    bool ok = !row->options.entities_changed || row->options.entities_changed(row->options.context,
        &row->options.domain, error);
    if (ok) remote_q2_prediction_receive(row);
    if (ok) ok = remote_q2_prediction_replay(row, error);
    if (ok) ok = remote_q2_effects_frame(row, error);
    --row->busy;
    return ok && remote_q2_live(row, error);
}
static bool hook_records(void *context, qa_net_client_id id, const qa_q2_server_record *records,
    size_t count, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!hook_current(row, id, error)) return false;
    ++row->busy; bool ok = remote_q2_records(row, records, count, error);
    if (ok) ok = row->options.records(row->options.context, &row->options.domain, records, count, error);
    --row->busy;
    return ok && remote_q2_prediction_replay(row, error) && remote_q2_live(row, error);
}
static bool hook_download(void *context, qa_net_client_id id, const qa_q2_server_event *event,
    bool *complete, qa_error *error)
{ return hook_current(context, id, error) && remote_q2_download_receive(context, event, complete, error); }
static bool hook_cancel(void *context, qa_net_client_id id, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!hook_current(row, id, error)) return false;
    if (!content_clear(row, error)) return false;
    row->loading_generation = 0; return true;
}
static bool hook_ack(void *context, qa_net_client_id id, uint32_t sequence, uint64_t ns, qa_error *error)
{
    (void)ns; frontend_remote_q2 *row = context;
    if (!hook_current(row, id, error)) return false;
    row->acknowledged = sequence;
    uint32_t nearest = UINT32_MAX;
    for (size_t i = 0; i < 64; ++i) {
        const remote_q2_sent_command *sent = row->sent + i;
        uint32_t distance = sequence - sent->packet_sequence;
        if (sent->valid && distance < 64 && distance < nearest) {
            row->acknowledged_command = sent->command_number; nearest = distance;
        }
    }
    return true;
}
static bool hook_sent(void *context, qa_net_client_id id, qa_net_seat_id seat, uint32_t sequence,
    uint64_t command_number, const qa_q2_usercmd *command, uint64_t ns, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!command || !command_number || !hook_current(row, id, error) || seat.owner != row->options.domain.seat.owner ||
        seat.index != row->options.domain.seat.index) return false;
    row->sent[sequence & 63] = (remote_q2_sent_command){.valid = true, .packet_sequence = sequence,
        .sent_ns = ns, .command_number = command_number, .command = *command};
    row->commands[command_number & 63] = row->sent[sequence & 63]; row->last_command = command_number;
    row->last_sent = sequence; row->sent_set = true;
    return remote_q2_prediction_replay(row, error);
}
static bool hook_command(void *context, const qa_network_command *source, qa_q2_usercmd *out, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!source || !out || !hook_current(row, source->client, error) ||
        source->epoch != row->options.domain.epoch || source->seat.owner != row->options.domain.seat.owner ||
        source->seat.index != row->options.domain.seat.index) return false;
    const qa_movement_command *command = &source->movement;
    *out = (qa_q2_usercmd){.server_frame = row->frame.valid ? row->frame.server_frame : -1,
        .msec = command->milliseconds > 255 ? 255 : (uint8_t)command->milliseconds,
        .buttons = (uint8_t)command->buttons, .impulse = command->impulse, .lightlevel = command->light_level,
        .forwardmove = command->forward_move, .sidemove = command->side_move, .upmove = command->up_move};
    uint32_t remote_index;
    if (!frontend_remote_q2_wire_seat(row, &remote_index, error)) return false;
    const qa_q2_player *player = row->frame.valid && remote_index < row->frame.player_count ?
        &row->frame.players[remote_index].player : NULL;
    if (row->layout.max_models == 8192) {
        float angles[3] = {command->angles.x, command->angles.y, command->angles.z};
        for (size_t i = 0; i < 3; ++i) {
            float delta = player ? player->pmove.delta_angles_f[i] : 0;
            out->angles[i] = (int16_t)(int32_t)fmodf(truncf((angles[i] - delta) * (65536.0f / 360)), 65536);
        }
    } else for (size_t i = 0; i < 3; ++i)
        out->angles[i] = (int16_t)(command->angle_words[i] - (player ? player->pmove.delta_angles[i] : 0));
    return true;
}
static bool hook_server_command(void *context, qa_net_client_id id, uint8_t seat, const char *text, qa_error *error)
{
    frontend_remote_q2 *row = context;
    uint32_t remote_index;
    if (!hook_current(row, id, error) || !text || !frontend_remote_q2_wire_seat(row, &remote_index, error) ||
        (seat && seat != remote_index + 1))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 stufftext names a different authentic CLIENT seat");
    ++row->busy; bool ok = qa_console_append(row->options.domain.console, &row->options.domain.command_context, text, error);
    --row->busy; return ok && remote_q2_live(row, error);
}
static bool hook_print(void *context, qa_net_client_id id, const char *text, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!hook_current(row, id, error) || !text) return false;
    ++row->busy; qa_console_emit(row->options.domain.console, &row->options.domain.command_context, text); --row->busy;
    return remote_q2_live(row, error);
}
static bool hook_drop(void *context, qa_net_client_id id, const char *reason, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || !qa_net_client_id_equal(id, row->options.domain.client) || row->retired ||
        row->frontend->capture || row->frontend->resource_inventory || row->image_policy) return false;
    row->retired = true; ++row->busy;
    bool ok = row->options.disconnected(row->options.context, &row->options.domain, reason, error);
    --row->busy; return ok;
}
bool frontend_remote_q2_hooks(frontend_remote_q2 *row, qa_network_q2_client_hooks *out, qa_error *error)
{
    if (!row || !out || !linked(row) || row->retired)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 session hooks require their actual retained Source");
    *out = (qa_network_q2_client_hooks){row, hook_current, hook_serverdata, hook_prepare, hook_frame,
        hook_records, hook_download, hook_cancel, hook_ack, hook_sent, hook_command,
        hook_server_command, hook_print, hook_drop}; return true;
}
bool frontend_remote_q2_metadata_read(const frontend_remote_q2 *row, frontend_remote_q2_view *out, qa_error *error)
{
    if (!row || !out || !linked(row) || row->importing)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 metadata requires its actual retained receiver");
    *out = (frontend_remote_q2_view){row, row->options.domain, row->identity, row->loading_generation,
        row->content_generation, row->received_ns, &row->data, row->content, row->map, &row->map_opening,
        &row->frame, &row->previous, row->images, row->materials, row->sounds, row->world, row->media_ready, row->retired, row->geometry};
    return true;
}
bool frontend_remote_q2_read(const frontend_remote_q2 *row, frontend_remote_q2_view *out, qa_error *error)
{ return remote_q2_live(row, error) && frontend_remote_q2_metadata_read(row, out, error); }
bool frontend_remote_q2_current(const frontend_remote_q2_view *view)
{
    qa_error error = {0}; const frontend_remote_q2 *row = view ? view->owner : NULL;
    return row && remote_q2_live(row, &error) && remote_q2_domain_equal(&view->domain, &row->options.domain) &&
        view->identity == row->identity && view->loading_generation == row->loading_generation &&
        view->content_generation == row->content_generation && view->received_ns == row->received_ns &&
        view->content.catalog == row->content.catalog && view->content.mounts == row->content.mounts &&
        view->map == row->map && view->world == row->world && view->geometry == row->geometry && view->frame == &row->frame &&
        view->media_ready == row->media_ready && view->retired == row->retired;
}
static bool media_idle(const frontend_remote_q2 *row)
{
    if (!remote_q2_material_movies_idle(row) || !frontend_remote_q2_effects_idle(row->effects) || (row->world && !qa_scene_world_idle(row->world)) ||
        (row->images && !qa_scene_resources_idle(row->images)) ||
        (row->materials && !qa_material_library_idle(row->materials)) ||
        (row->fonts && !qa_font_library_idle(row->fonts))) return false;
    for (remote_q2_model *model = row->models; model; model = model->next)
        if (model->scene && !qa_scene_model_idle(model->scene)) return false;
    return true;
}
bool remote_q2_capture_owned(const frontend_remote_q2 *row)
{
    if (!row || !linked(row) || row->busy || row->importing || row->image_policy || !frontend_remote_q2_effects_idle(row->effects) || !row->frontend->capture ||
        row->frontend->application != row->options.domain.application) return false;
    if (row->bound && !row->retired) {
        const frontend_remote_q2_domain *domain = &row->options.domain;
        const qa_net_client *client = qa_net_connections_get(qa_network_connections(domain->runtime), domain->client);
        uint32_t remote_index; qa_error error = {0};
        if (!client || qa_network_epoch(domain->runtime, domain->client) != domain->epoch ||
            client->protocol.kind != domain->protocol.kind || client->protocol.revision != domain->protocol.revision ||
            client->protocol.flags != domain->protocol.flags ||
            !frontend_remote_q2_wire_seat(row, &remote_index, &error)) return false;
    }
    const frontend_capture *capture = row->frontend->capture;
    bool images = !row->images, materials = !row->materials, fonts = !row->fonts, world = !row->world;
    for (size_t i = 0; frontend_capture_images_at(capture, i); ++i)
        if (frontend_capture_images_at(capture, i) == row->images) images = true;
    for (size_t i = 0; frontend_capture_library_at(capture, i); ++i)
        if (frontend_capture_library_at(capture, i) == row->materials) materials = true;
    for (size_t i = 0; frontend_capture_fonts_at(capture, i); ++i)
        if (frontend_capture_fonts_at(capture, i) == row->fonts) fonts = true;
    for (size_t i = 0; frontend_capture_world_at(capture, i); ++i)
        if (frontend_capture_world_at(capture, i) == row->world) world = true;
    if (!images || !materials || !fonts || !world) return false;
    for (const remote_q2_model *model = row->models; model; model = model->next) {
        bool found = !model->scene;
        for (size_t i = 0; frontend_capture_model_at(capture, i); ++i)
            if (frontend_capture_model_at(capture, i) == model->scene) found = true;
        if (!found) return false;
    }
    return true;
}
bool frontend_remote_q2_entity_received(const frontend_remote_q2 *row, uint32_t number)
{
    if (!row || !linked(row) || row->retired || row->importing || !row->frame.valid || !number) return false;
    for (size_t i = 0; i < row->frame.entity_count; ++i) if (row->frame.entities[i].number == number) return true;
    for (size_t i = 0; i < row->frame.player_count; ++i)
        if (row->frame.players[i].player.clientnum >= 0 &&
            (uint32_t)row->frame.players[i].player.clientnum + 1 == number) return true;
    return false;
}
bool frontend_remote_q2_entity_generation(const frontend_remote_q2 *row, uint32_t number, uint64_t *generation)
{
    if (generation) *generation = 0;
    if (!generation || !row || !row->bound || !row->media_ready || !row->content_generation ||
        !row->loading_generation || !frontend_remote_q2_entity_received(row, number)) return false;
    *generation = row->content_generation;
    return true;
}
bool frontend_remote_q2_entity_publication_read(const frontend_remote_q2 *row,
    qa_application_client_entity_publication *out)
{
    if (!row || !out || !linked(row) || !row->bound || row->retired || row->importing) return false;
    bool published = row->media_ready && row->frame.valid && row->content_generation && row->loading_generation;
    *out = (qa_application_client_entity_publication){.published = published,
        .map_generation = published ? row->content_generation : 0,
        .received_ns = published ? row->received_ns : 0,
        .source_frame = published ? row->frame.server_frame : 0};
    return true;
}
bool frontend_remote_q2_idle(const qa_frontend *f)
{
    if (!f) return false;
    for (const frontend_remote_q2 *row = f->remote_q2; row; row = row->next) {
        if (row->busy || row->importing || row->image_policy || !media_idle(row)) return false;
    }
    return true;
}
size_t frontend_remote_q2_count(const qa_frontend *f)
{
    size_t count = 0;
    for (const frontend_remote_q2 *row = f ? f->remote_q2 : NULL; row; row = row->next) ++count;
    return count;
}
frontend_remote_q2 *frontend_remote_q2_at(const qa_frontend *f, size_t ordinal)
{
    frontend_remote_q2 *row = f ? f->remote_q2 : NULL;
    while (row && ordinal--) row = row->next;
    return row;
}
size_t frontend_remote_q2_model_count(const frontend_remote_q2 *row)
{
    size_t count = 0;
    for (const remote_q2_model *model = row ? row->models : NULL; model; model = model->next) ++count;
    return count;
}
bool frontend_remote_q2_model_at(const frontend_remote_q2 *row, size_t ordinal,
    frontend_remote_q2_model_view *out, qa_error *error)
{
    if (!row || !out || !linked(row) || row->busy)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 model inventory requires returned retained cache producers");
    const remote_q2_model *model = row->models;
    while (model && ordinal--) model = model->next;
    if (!model) return remote_q2_fail(error, QA_ERROR_NOT_FOUND, "Q2 model cache ordinal is absent");
    *out = (frontend_remote_q2_model_view){model->path, model->resource, &model->opening, model->source, model->scene};
    return true;
}
qa_font_library *frontend_remote_q2_fonts(const frontend_remote_q2 *row)
{ return row && linked(row) ? row->fonts : NULL; }
bool frontend_remote_q2_destroy(frontend_remote_q2 **owned, qa_error *error)
{
    frontend_remote_q2 *row = owned ? *owned : NULL;
    if (!row) return true;
    if (!linked(row) || row->frontend->capture || row->frontend->resource_inventory || row->busy || row->image_policy || !media_idle(row) || (row->importing && !row->frontend->source_restoring))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 destruction requires returned Source callbacks");
    if (row->bound && !row->importing && qa_net_connections_get(
        qa_network_connections(row->options.domain.runtime), row->options.domain.client))
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 receiver still owns its attached transport callbacks");
    if (!content_clear(row, error)) return false;
    qa_catalog_release(row->options.domain.catalog); free(row->configs);
    frontend_remote_q2 **link = &row->frontend->remote_q2;
    while (*link != row) link = &(*link)->next;
    *link = row->next; free(row); *owned = NULL; return true;
}
bool frontend_remote_q2_destroy_all(qa_frontend *f, qa_error *error)
{
    if (!f || f->capture || f->resource_inventory) return false;
    for (frontend_remote_q2 *row = f->remote_q2; row; row = row->next)
        if (row->busy || row->image_policy || (row->importing && !f->source_restoring))
            return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 children are still entered");
    while (f->remote_q2) { frontend_remote_q2 *row = f->remote_q2; if (!frontend_remote_q2_destroy(&row, error)) return false; }
    return true;
}
bool frontend_remote_q2_content_visit(const qa_frontend *f, const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!f || !visitor || !visitor->catalog || !visitor->pool || !visitor->view) return false;
    if (!f->capture && !frontend_remote_q2_idle(f)) return false;
    for (const frontend_remote_q2 *row = f->remote_q2; row; row = row->next) {
        if (f->capture && !remote_q2_capture_owned(row)) return false;
        if (!visitor->catalog(visitor->context, row->options.domain.catalog, error)) return false;
        if (row->content.catalog && (!visitor->catalog(visitor->context, row->content.catalog, error) ||
            !visitor->pool(visitor->context, qa_vfs_resources(row->content.mounts), error) ||
            !visitor->view(visitor->context, row->content.mounts, error))) return false;
        if (!remote_q2_footsteps_visit(row, visitor, error)) return false;
        if (row->media) {
            qa_resource_pool *pool = qa_vfs_resources(row->content.mounts);
            if (!pool || qa_media_library_resource_owner(row->media) != row->images ||
                !visitor->pool(visitor->context, pool, error)) return false;
            for (size_t i = 0; i < qa_media_library_record_count(row->media); ++i) {
                const qa_cinematic_asset *asset = qa_media_library_record_at(row->media, i);
                const qa_resource *resource = asset ? qa_cinematic_asset_resource(asset) : NULL;
                if (!resource || qa_resource_pool_find(pool, qa_resource_id(resource)) != resource)
                    return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 movie cache left its actual retained content pool");
            }
        }
    }
    return true;
}
bool frontend_remote_q2_rebind_ready(const frontend_remote_q2 *row, qa_frontend *f,
    const frontend_remote_q2_options *options, qa_error *error)
{
    if (!row || !f || !options || f->capture || f->resource_inventory || row->busy || row->importing || row->image_policy || !linked(row) ||
        (f != row->frontend && (row->effects || row->media || row->shader_movies)) ||
        !remote_q2_domain_equal(&row->options.domain, &options->domain) ||
        f->application != options->domain.application || !options->current || !options->download_allowed ||
        !options->download_nonce || !options->records || !options->disconnected)
        return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 handoff requires its exact retained CLIENT binding");
    return options->current(options->context, &options->domain, error);
}
void frontend_remote_q2_rebind(frontend_remote_q2 *row, qa_frontend *f, const frontend_remote_q2_options *options)
{ if (row && f && options) { row->frontend = f; row->options = *options; } }
