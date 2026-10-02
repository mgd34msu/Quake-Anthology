#include "remote_frame.h"
#include "frame.h"
#include "qa/source_save.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct q3n_remote_source {
    q3n_remote_source_options options;
    qa_native_q3_remote_client_basis basis;
    int32_t initial_message, initial_command, reached_command;
    int32_t config_commands[QA_Q3_CONFIGSTRINGS];
    bool busy;
};
static bool fail(qa_error *e, qa_status code, const char *message)
{ qa_error_set(e, code, 0, "%s", message); return false; }
static int32_t integer(const char *text)
{
    while (*text && (signed char)*text <= 32) ++text;
    bool negative = *text == '-';
    if (*text == '+' || *text == '-') ++text;
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10u + (uint32_t)(*text++ - '0');
    value = negative ? 0u - value : value;
    int32_t result; memcpy(&result, &value, sizeof(result)); return result;
}
static bool client_identity(const qa_application_q3_client_context *a,
    const qa_application_q3_client_context *b)
{
    return a->session == b->session && a->receiver == b->receiver &&
        a->source_owner == b->source_owner && qa_actor_id_equal(a->source_actor, b->source_actor) &&
        a->seat == b->seat && a->source_client == b->source_client &&
        a->service_owner == b->service_owner && a->frontend_lifetime == b->frontend_lifetime &&
        a->console == b->console && a->cvars == b->cvars && a->source_cvars == b->source_cvars &&
        a->client_time_cvars == b->client_time_cvars && a->client_time_owner == b->client_time_owner &&
        a->native_source == b->native_source;
}
static bool basis_identity(const qa_native_q3_remote_client_basis *a,
    const qa_native_q3_remote_client_basis *b)
{
    return a->application == b->application && a->session == b->session &&
        a->descriptor && b->descriptor && a->descriptor->storage == b->descriptor->storage &&
        a->content == b->content && a->content_product == b->content_product && a->product == b->product &&
        qa_net_client_id_equal(a->connection, b->connection) && a->epoch == b->epoch &&
        a->publication_generation == b->publication_generation &&
        a->configuration_generation == b->configuration_generation &&
        a->map == b->map && a->geometry == b->geometry && a->gamestate == b->gamestate &&
        a->physical_client == b->physical_client && a->initial_message == b->initial_message &&
        a->initial_command == b->initial_command && client_identity(&a->client, &b->client);
}
static bool publication_equal(const q3n_remote_publication *a, const q3n_remote_publication *b)
{
    return qa_net_client_id_equal(a->connection, b->connection) && a->epoch == b->epoch &&
        a->restart_generation == b->restart_generation && a->gamestate == b->gamestate &&
        qa_actor_id_equal(a->viewer, b->viewer) && a->initial_message == b->initial_message &&
        a->initial_command == b->initial_command && a->latest_message == b->latest_message &&
        a->latest_time == b->latest_time && a->presentation_time == b->presentation_time &&
        a->server_message == b->server_message && a->received_command == b->received_command &&
        a->executed_command == b->executed_command && a->initializing == b->initializing &&
        a->has_snapshot == b->has_snapshot && a->demo_playback == b->demo_playback;
}
static bool reached_valid(int32_t reached, const q3n_remote_publication *publication)
{
    /* Cycled demo commands are consumed by CGAME without advancing the
     * engine's literal acknowledgment. Both counters retain their owners. */
    return reached >= publication->initial_command &&
        reached >= publication->executed_command && reached <= publication->received_command &&
        (reached == publication->executed_command || (publication->demo_playback &&
            (int64_t)reached <= (int64_t)publication->received_command - 64));
}
static bool observe(const q3n_remote_source *s, qa_native_q3_remote_client_basis *basis,
    q3n_remote_publication *publication, qa_error *e)
{
    if (!s || !qa_native_q3_remote_client_basis_read(s->options.client, basis, e) ||
        !basis_identity(&s->basis, basis) ||
        !s->options.publication_read(s->options.context, publication, e) ||
        !qa_net_client_id_equal(publication->connection, basis->connection) || publication->epoch != basis->epoch ||
        publication->restart_generation != basis->restart_generation || publication->gamestate != basis->gamestate ||
        publication->initial_message != s->initial_message || publication->initial_command != s->initial_command ||
        publication->executed_command < s->initial_command ||
        publication->executed_command > publication->received_command ||
        !s->options.publication_current(s->options.context, publication))
        return fail(e, QA_ERROR_ARGUMENT, "Remote source lost its actual CLIENT, reached gamestate or Network receipt");
    return true;
}
bool q3n_remote_source_create(const q3n_remote_source_options *o, q3n_remote_source **out, qa_error *e)
{
    if (!o || !o->client || !o->context || !o->publication_read || !o->publication_current ||
        !o->command_read || !o->command_current || !o->idle || !out || *out)
        return fail(e, QA_ERROR_ARGUMENT, "Remote source construction requires its actual retained Network services");
    qa_native_q3_remote_client_basis basis; q3n_remote_publication publication;
    if (!qa_native_q3_remote_client_basis_read(o->client, &basis, e) ||
        !o->publication_read(o->context, &publication, e) ||
        !qa_net_client_id_equal(publication.connection, basis.connection) || publication.epoch != basis.epoch ||
        publication.restart_generation != basis.restart_generation || publication.gamestate != basis.gamestate ||
        publication.initial_message != basis.initial_message || publication.initial_command != basis.initial_command ||
        publication.executed_command != publication.initial_command ||
        !o->publication_current(o->context, &publication))
        return fail(e, QA_ERROR_ARGUMENT, "Remote source must begin at its genuine CG_Init gamestate receipt");
    q3n_remote_source *s = calloc(1, sizeof(*s));
    if (!s) return fail(e, QA_ERROR_MEMORY, "Retaining remote CGAME reached configstring history");
    s->options = *o; s->basis = basis;
    s->initial_message = publication.initial_message; s->initial_command = publication.initial_command;
    s->reached_command = publication.initial_command;
    for (uint32_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) s->config_commands[i] = publication.initial_command;
    *out = s; return true;
}
bool q3n_remote_source_destroy(q3n_remote_source *s, qa_error *e)
{
    if (!s) return true;
    if (s->busy || !qa_native_q3_remote_client_idle(s->options.client) || !s->options.idle(s->options.context))
        return fail(e, QA_ERROR_ARGUMENT, "Remote source commands and retained Network callbacks must unwind before retirement");
    free(s); return true;
}
qa_native_q3_remote_client_service *q3n_remote_source_client(const q3n_remote_source *s)
{ return s ? s->options.client : NULL; }
bool q3n_remote_source_idle(const q3n_remote_source *s)
{ return !s || !s->busy; }
bool q3n_remote_source_read(const q3n_remote_source *s, q3n_remote_source_view *out, qa_error *e)
{
    if (!out) return fail(e, QA_ERROR_ARGUMENT, "Missing remote source receipt output");
    q3n_remote_source_view view = {.owner = s};
    if (!observe(s, &view.basis, &view.publication, e) || !reached_valid(s->reached_command, &view.publication))
        return fail(e, QA_ERROR_ARGUMENT, "Remote CGAME has not adopted its actual reached command receipt");
    const char *info = qa_q3_configstring(view.publication.gamestate, 0); char value[8192];
    if (!qa_q3_info_value(info, "g_gametype", value, sizeof(value), e)) return false;
    view.game_type = integer(value);
    if (!qa_q3_info_value(info, "sv_maxclients", value, sizeof(value), e)) return false;
    view.max_clients = integer(value);
    if (!qa_q3_info_value(info, "dmflags", value, sizeof(value), e)) return false;
    view.dm_flags = integer(value);
    view.match_start_time = integer(qa_q3_configstring(view.publication.gamestate, 21));
    view.reached_command = s->reached_command;
    if (!s->options.publication_current(s->options.context, &view.publication))
        return fail(e, QA_ERROR_ARGUMENT, "Remote source changed during its pure reached serverinfo observation");
    *out = view; return true;
}
bool q3n_remote_source_current(const q3n_remote_source_view *view)
{
    if (!view || !view->owner) return false;
    q3n_remote_source_view actual;
    return q3n_remote_source_read(view->owner, &actual, NULL) && basis_identity(&view->basis, &actual.basis) &&
        view->basis.restart_generation == actual.basis.restart_generation &&
        publication_equal(&view->publication, &actual.publication) && view->reached_command == actual.reached_command &&
        view->game_type == actual.game_type && view->max_clients == actual.max_clients &&
        view->dm_flags == actual.dm_flags &&
        view->match_start_time == actual.match_start_time;
}
bool q3n_remote_source_configstring(const q3n_remote_source *s, uint32_t index,
    const char **text, uint64_t *revision, qa_error *e)
{
    q3n_remote_source_view view;
    if (!text || !revision || index >= QA_Q3_CONFIGSTRINGS || !q3n_remote_source_read(s, &view, e))
        return fail(e, QA_ERROR_ARGUMENT, "Remote configstring requires its real reached source and row");
    *text = qa_q3_configstring(view.publication.gamestate, index);
    *revision = (uint32_t)s->config_commands[index]; return true;
}
bool q3n_remote_command_current(const q3n_remote_source *s, const q3n_remote_command *command)
{
    qa_native_q3_remote_client_basis basis; q3n_remote_publication publication;
    return command && observe(s, &basis, &publication, NULL) &&
        command->sequence == s->reached_command && reached_valid(s->reached_command, &publication) &&
        publication_equal(&command->publication, &publication) &&
        s->options.command_current(s->options.context, command);
}
bool q3n_remote_source_reached(q3n_remote_source *s, const q3n_remote_command *command, qa_error *e)
{
    qa_native_q3_remote_client_basis basis; q3n_remote_publication publication;
    if (!s || !command || !observe(s, &basis, &publication, e) ||
        s->reached_command == INT32_MAX || command->sequence != s->reached_command + 1 ||
        !reached_valid(command->sequence, &publication) ||
        !publication_equal(&command->publication, &publication) ||
        (command->present && !command->tokens) ||
        !s->options.command_current(s->options.context, command))
        return fail(e, QA_ERROR_ARGUMENT, "Remote CGAME requires the next genuine entered Network command receipt");
    /* Claim CGAME's cursor before inspecting authored arguments. The actual
     * Network result proves consumption, including absent demo commands. */
    s->reached_command = command->sequence;
    if (command->present && !strcmp(qa_q3_token(command->tokens, 0), "cs")) {
        int32_t index = integer(qa_q3_token(command->tokens, 1));
        if (index < 0 || index >= QA_Q3_CONFIGSTRINGS)
            return fail(e, QA_ERROR_FORMAT, "Remote reached configstring index exceeds MAX_CONFIGSTRINGS");
        s->config_commands[index] = command->sequence;
    }
    return true;
}
bool q3n_remote_source_command(q3n_remote_source *s, int32_t sequence, q3n_remote_command *out, qa_error *e)
{
    q3n_remote_source_view before;
    if (!s || !out || s->busy || !q3n_remote_source_read(s, &before, e) ||
        s->reached_command == INT32_MAX || sequence != s->reached_command + 1)
        return fail(e, QA_ERROR_ARGUMENT, "Remote command read requires the next actual CGAME cursor");
    s->busy = true; q3n_remote_command command = {0};
    bool ok = s->options.command_read(s->options.context, sequence, &command, e) &&
        q3n_remote_source_reached(s, &command, e);
    s->busy = false;
    if (ok) *out = command;
    return ok;
}

static bool same_u64(qa_source_save_io *io, uint64_t expected)
{ uint64_t value = expected; return qa_source_save_u64(io, &value) && value == expected; }
static bool same_u32(qa_source_save_io *io, uint32_t expected)
{ uint32_t value = expected; return qa_source_save_u32(io, &value) && value == expected; }
static bool same_i32(qa_source_save_io *io, int32_t expected)
{ int32_t value = expected; return qa_source_save_i32(io, &value) && value == expected; }
static bool map_fields(qa_source_save_io *io, const qa_resource *map)
{
    const char *path = qa_resource_path(map);
    const qa_sha256_digest *expected = qa_resource_digest(map);
    if (!path || !expected) return false;
    qa_sha256_digest digest = *expected;
    size_t size = strlen(path), saved = size;
    if (!qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) ||
        !qa_sha256_equal(&digest, expected) || !qa_source_save_count(io, &saved, SIZE_MAX) || saved != size) return false;
    for (size_t i = 0; i < size; ++i) {
        uint8_t value = (uint8_t)path[i];
        if (!qa_source_save_u8(io, &value) || value != (uint8_t)path[i]) return false;
    }
    return true;
}
static bool identity_fields(qa_source_save_io *io, const q3n_remote_source_view *view)
{
    const qa_native_q3_remote_client_basis *b = &view->basis;
    return same_u32(io, (uint32_t)b->product) && same_u32(io, b->content_product) &&
        same_u64(io, b->connection.owner) && same_u64(io, b->connection.generation) &&
        same_u32(io, b->connection.slot) && same_u64(io, b->epoch) &&
        same_u64(io, b->restart_generation) && same_u64(io, b->publication_generation) &&
        same_u64(io, b->configuration_generation) && same_u64(io, b->client.receiver) &&
        same_u64(io, b->client.service_owner) && same_u32(io, b->client.seat) &&
        same_u32(io, b->client.source_client) &&
        same_u32(io, b->physical_client) && same_i32(io, b->initial_message) &&
        same_i32(io, b->initial_command) && same_i32(io, view->publication.executed_command) &&
        same_u32(io, view->publication.demo_playback ? 1u : 0u) && map_fields(io, b->map);
}
static bool codec(qa_source_save_io *io, q3n_remote_source *s, const q3n_remote_source_view *view)
{
    uint8_t magic[4] = {'Q','R','F','S'};
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QRFS", sizeof(magic)) ||
        !same_u32(io, 2) || !identity_fields(io, view) ||
        !qa_source_save_i32(io, &s->reached_command) || !reached_valid(s->reached_command, &view->publication))
        return false;
    for (uint32_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i)
        if (!qa_source_save_i32(io, &s->config_commands[i]) || s->config_commands[i] < s->initial_command ||
            s->config_commands[i] > s->reached_command) return false;
    return true;
}
bool q3n_remote_source_checkpoint(const q3n_remote_source *s, qa_buffer *out, qa_error *e)
{
    q3n_remote_source_view view;
    if (!s || !out || out->data || out->size || s->busy ||
        !qa_native_q3_remote_client_idle(s->options.client) || !s->options.idle(s->options.context) ||
        !q3n_remote_source_read(s, &view, e))
        return fail(e, QA_ERROR_ARGUMENT, "Remote source capture requires its idle actual reached continuation");
    q3n_remote_source copy = *s; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, view.basis.session, e) && codec(&io, &copy, &view) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool q3n_remote_source_restore(const q3n_remote_source_options *o, qa_bytes bytes,
    q3n_remote_source **out, qa_error *e)
{
    if (!o || !o->client || !o->context || !o->publication_read || !o->publication_current ||
        !o->command_read || !o->command_current || !o->idle || !out || *out ||
        !qa_native_q3_remote_client_idle(o->client) || !o->idle(o->context))
        return fail(e, QA_ERROR_ARGUMENT, "Remote source import requires its actual restored idle Network services");
    q3n_remote_source *s = calloc(1, sizeof(*s));
    if (!s) return fail(e, QA_ERROR_MEMORY, "Restoring remote reached configstring continuation");
    s->options = *o;
    q3n_remote_source_view view = {.owner = s};
    bool ok = qa_native_q3_remote_client_basis_read(o->client, &s->basis, e);
    if (ok) {
        s->initial_message = s->basis.initial_message; s->initial_command = s->basis.initial_command;
        ok = observe(s, &view.basis, &view.publication, e);
    }
    qa_source_save_io io = {0};
    if (ok) ok = qa_source_save_reader(&io, view.basis.session, bytes, e) && codec(&io, s, &view) &&
        io.offset == io.input.size;
    qa_source_save_dispose(&io);
    if (ok) ok = q3n_remote_source_read(s, &view, e);
    if (!ok) { free(s); return fail(e, QA_ERROR_FORMAT, "Invalid complete remote source continuation for the installed Network graph"); }
    *out = s; return true;
}

static bool frame_shape(const q3n_remote_frame *f)
{
    if (!f || !f->source.owner || !f->client || !f->context || !f->current || !f->entity ||
        !f->entity_event || !f->entity_trajectory || !f->entity_weapon || !f->trace_number ||
        (f->snapshots.stage != Q3N_REMOTE_INITIALIZATION && f->snapshots.stage != Q3N_REMOTE_SNAPSHOT_CALLBACK &&
            f->snapshots.stage != Q3N_REMOTE_PREDICTION_CALLBACK && f->snapshots.stage != Q3N_REMOTE_COMPLETED_FRAME &&
            f->snapshots.stage != Q3N_REMOTE_CONSOLE && f->snapshots.stage != Q3N_REMOTE_AWAITING_SNAPSHOT &&
            f->snapshots.stage != Q3N_REMOTE_LOADING_INFORMATION) ||
        (f->snapshots.stage == Q3N_REMOTE_SNAPSHOT_CALLBACK && !f->snapshots.callback_scope) ||
        (f->snapshots.stage != Q3N_REMOTE_SNAPSHOT_CALLBACK && f->snapshots.callback_scope) ||
        f->source.owner->options.client != f->client ||
        f->snapshots.command_sequence != f->source.reached_command ||
        (f->snapshots.next_snapshot && !f->snapshots.snapshot) ||
        (f->predicted_state == NULL) != (f->predicted_entity == NULL) ||
        (f->predicted_next_state == NULL) != (f->predicted_entity == NULL) ||
        (f->predicted_state && f->predicted_state == f->predicted_next_state) ||
        (f->predicted_player == NULL) != (f->predicted_entity == NULL) ||
        (f->transition_player == NULL) != (f->previous_player == NULL)) return false;
    if (f->snapshots.stage == Q3N_REMOTE_INITIALIZATION) {
        if (!f->initialization_scope || !f->source.publication.initializing ||
            f->source.reached_command != f->source.publication.initial_command ||
            f->snapshots.snapshot || f->snapshots.next_snapshot || f->prediction.owner || f->prediction.player ||
            f->transition_player || !f->predicted_player ||
            (f->snapshots.owner == NULL) != (f->snapshots.entities == NULL) ||
            (f->snapshots.owner ? !f->snapshots.revision : f->snapshots.revision != 0)) return false;
    } else if (f->initialization_scope || !f->snapshots.owner || !f->snapshots.revision || !f->snapshots.entities)
        return false;
    if (f->snapshots.stage == Q3N_REMOTE_PREDICTION_CALLBACK ?
        !f->transition_scope || !f->transition_player : f->transition_scope != 0) return false;
    if (f->snapshots.stage == Q3N_REMOTE_CONSOLE ? !f->console_scope : f->console_scope != 0) return false;
    if (f->snapshots.stage == Q3N_REMOTE_AWAITING_SNAPSHOT ?
        !f->awaiting_snapshot_scope : f->awaiting_snapshot_scope != 0) return false;
    if (f->snapshots.stage == Q3N_REMOTE_LOADING_INFORMATION ?
        !f->loading_information_scope || !f->loading_information_text || !*f->loading_information_text ||
            !f->loading_information_current :
        f->loading_information_scope != 0 || f->loading_information_text || f->loading_information_current) return false;
    if (f->snapshots.stage == Q3N_REMOTE_CONSOLE || f->snapshots.stage == Q3N_REMOTE_AWAITING_SNAPSHOT ||
        f->snapshots.stage == Q3N_REMOTE_LOADING_INFORMATION) {
        qa_native_q3_remote_client_basis actual;
        if (!f->source.basis.client.initialized || !f->predicted_player || f->transition_player ||
            f->prediction.owner || f->prediction.player ||
            !qa_native_q3_remote_client_basis_read(f->client, &actual, NULL) || !actual.client.initialized)
            return false;
        if (f->snapshots.stage == Q3N_REMOTE_AWAITING_SNAPSHOT && f->snapshots.snapshot &&
            !(f->snapshots.snapshot->flags & 2)) return false;
    }
    if (f->snapshots.snapshot && (!f->snapshots.snapshot->valid ||
        f->snapshots.snapshot->player.product != f->source.basis.product ||
        f->snapshots.snapshot->message_number > f->snapshots.processed_message ||
        f->snapshots.snapshot->player.clientNum < 0 || f->snapshots.snapshot->player.clientNum >= 64)) return false;
    if (f->snapshots.next_snapshot && (!f->snapshots.next_snapshot->valid ||
        f->snapshots.next_snapshot->player.product != f->source.basis.product ||
        f->snapshots.next_snapshot->message_number > f->snapshots.processed_message ||
        f->snapshots.next_snapshot->message_number <= f->snapshots.snapshot->message_number)) return false;
    if ((f->snapshots.stage == Q3N_REMOTE_PREDICTION_CALLBACK || f->snapshots.stage == Q3N_REMOTE_COMPLETED_FRAME) &&
        (!f->snapshots.snapshot || !f->prediction.owner || !f->prediction.player ||
            !f->predicted_state || !f->predicted_entity || !f->predicted_player)) return false;
    if (f->snapshots.stage == Q3N_REMOTE_COMPLETED_FRAME && f->transition_player) return false;
    if (f->transition_player && (f->transition_player->product != f->source.basis.product ||
        f->previous_player->product != f->source.basis.product || f->transition_player->clientNum < 0 ||
        f->transition_player->clientNum >= 64 || f->previous_player->clientNum < 0 ||
        f->previous_player->clientNum >= 64)) return false;
    if (f->prediction.player && (!f->prediction.owner || f->prediction.player->product != f->source.basis.product ||
        f->prediction.player->clientNum < 0 || f->prediction.player->clientNum >= 64 || !f->prediction_error_clear)) return false;
    if (f->predicted_player && (f->predicted_player == f->prediction.player ||
        f->predicted_player->product != f->source.basis.product || f->predicted_player->clientNum < 0 ||
        f->predicted_player->clientNum >= 64 ||
        (f->snapshots.snapshot && f->predicted_player == &f->snapshots.snapshot->player) ||
        (f->snapshots.next_snapshot && f->predicted_player == &f->snapshots.next_snapshot->player))) return false;
    return true;
}
bool q3n_remote_frame_current(const q3n_remote_frame *f)
{
    return frame_shape(f) && q3n_remote_source_current(&f->source) && f->current(f->context, f) &&
        (f->snapshots.stage != Q3N_REMOTE_LOADING_INFORMATION ||
            f->loading_information_current(f->context, &f->source, f->loading_information_text));
}
bool q3n_remote_frame_read(const q3n_remote_frame_options *o, q3n_remote_frame *out, qa_error *e)
{
    if (!o || !out || !o->source.owner) return fail(e, QA_ERROR_ARGUMENT, "Missing actual remote frame receipt");
    q3n_remote_frame f = {.client = o->source.owner->options.client, .source = o->source,
        .snapshots = o->snapshots, .prediction = o->prediction, .predicted_state = o->predicted_state,
        .predicted_next_state = o->predicted_next_state,
        .predicted_entity = o->predicted_entity, .predicted_player = o->predicted_player,
        .transition_player = o->transition_player,
        .previous_player = o->previous_player, .initialization_scope = o->initialization_scope,
        .transition_scope = o->transition_scope, .console_scope = o->console_scope,
        .awaiting_snapshot_scope = o->awaiting_snapshot_scope,
        .loading_information_scope = o->loading_information_scope,
        .loading_information_text = o->loading_information_text,
        .loading_information_current = o->loading_information_current,
        .context = o->context, .current = o->current, .entity = o->entity, .entity_event = o->entity_event,
        .entity_trajectory = o->entity_trajectory, .entity_weapon = o->entity_weapon,
        .prediction_error_clear = o->prediction_error_clear, .trace_number = o->trace_number};
    if (!q3n_remote_frame_current(&f)) return fail(e, QA_ERROR_ARGUMENT, "Remote frame lost its real snapshot/prediction or entered callback receipt");
    *out = f; return true;
}
bool q3n_remote_frame_entity(const q3n_remote_frame *f, uint32_t number, q3n_remote_entity *out, qa_error *e)
{
    if (!out || number >= QA_Q3_ENTITY_NONE || !q3n_remote_frame_current(f) || !f->snapshots.entities)
        return fail(e, QA_ERROR_ARGUMENT, "Remote entityAt requires its current actual cache receipt");
    q3n_remote_entity row = {0};
    if (!f->entity(f->context, f, number, &row, e)) return false;
    if (!row.current || !row.next || row.predicted || row.number != number ||
        row.presentation != &f->snapshots.entities[number] ||
        (row.published && (row.current->number != (int32_t)number || row.presentation->physical != number ||
            row.publication_message > f->snapshots.processed_message)) ||
        (!row.published && row.current_valid) || row.current_valid != row.presentation->valid ||
        !q3n_remote_frame_current(f))
        return fail(e, QA_ERROR_ARGUMENT, "Remote entity observation differs from its actual retained centity row");
    row.frame = f; *out = row; return true;
}
bool q3n_remote_frame_predicted(const q3n_remote_frame *f, q3n_remote_entity *out, qa_error *e)
{
    if (!out || !q3n_remote_frame_current(f) || !f->predicted_state || !f->predicted_entity ||
        f->predicted_state->number < 0 || f->predicted_state->number >= QA_Q3_ENTITY_NONE)
        return fail(e, QA_ERROR_ARGUMENT, "Predicted centity requires its actual separate presentation owner");
    *out = (q3n_remote_entity){.frame = f, .current = f->predicted_state, .next = f->predicted_next_state,
        .presentation = f->predicted_entity, .number = (uint32_t)f->predicted_state->number,
        .current_valid = f->predicted_entity->valid, .predicted = true}; return true;
}
bool q3n_remote_entity_current(const q3n_remote_entity *row)
{
    if (!row || !row->frame) return false;
    q3n_remote_entity actual;
    bool ok = row->predicted ? q3n_remote_frame_predicted(row->frame, &actual, NULL) :
        q3n_remote_frame_entity(row->frame, row->number, &actual, NULL);
    return ok && row->current == actual.current && row->next == actual.next &&
        row->presentation == actual.presentation && row->number == actual.number &&
        row->publication_message == actual.publication_message && row->published == actual.published &&
        row->current_valid == actual.current_valid && row->interpolate == actual.interpolate &&
        row->predicted == actual.predicted;
}
bool q3n_remote_frame_trace_number(const q3n_remote_frame *f, const qa_trace_result *hit, int32_t *out, qa_error *e)
{
    if (!hit || !out || !q3n_remote_frame_current(f))
        return fail(e, QA_ERROR_ARGUMENT, "Remote collision number requires its actual held trace receipt");
    int32_t number;
    if (!f->trace_number(f->context, f, hit, &number, e)) return false;
    if (number < 0 || number > QA_Q3_ENTITY_NONE || !q3n_remote_frame_current(f))
        return fail(e, QA_ERROR_ARGUMENT, "Remote collision producer returned an invalid or retired raw number witness");
    *out = number; return true;
}
bool q3n_remote_frame_entity_event(const q3n_remote_frame *f, uint32_t number,
    int32_t event, int32_t parameter, qa_error *e)
{
    q3n_remote_entity row;
    if (!q3n_remote_frame_entity(f, number, &row, e) || !row.published)
        return fail(e, QA_ERROR_ARGUMENT, "Remote player event requires its actual published private centity");
    return f->entity_event(f->context, f, number, event, parameter, e) &&
        (q3n_remote_frame_current(f) || fail(e, QA_ERROR_ARGUMENT, "Remote event store retired its actual cache receipt"));
}
bool q3n_remote_frame_entity_trajectory(const q3n_remote_entity *row,
    int32_t current_before, int32_t next_before, int32_t current_after, int32_t next_after, qa_error *e)
{
    if (!q3n_remote_entity_current(row) || (!row->predicted && !row->published) || !row->next ||
        row->current->pos.type != current_before || row->next->pos.type != next_before)
        return fail(e, QA_ERROR_ARGUMENT, "Remote trajectory store lost its actual private row or expected values");
    const q3n_remote_frame *f = row->frame;
    return f->entity_trajectory(f->context, row, current_before, next_before, current_after, next_after, e) &&
        ((q3n_remote_entity_current(row) && row->current->pos.type == current_after && row->next->pos.type == next_after) ||
            fail(e, QA_ERROR_ARGUMENT, "Remote trajectory store differs from its actual private cache result"));
}
bool q3n_remote_frame_entity_weapon(const q3n_remote_entity *row, int32_t before, int32_t after, qa_error *e)
{
    if (!q3n_remote_entity_current(row) || (!row->predicted && !row->published) || row->current->weapon != before)
        return fail(e, QA_ERROR_ARGUMENT, "Remote weapon store lost its actual private row or expected value");
    const q3n_remote_frame *f = row->frame;
    return f->entity_weapon(f->context, row, before, after, e) &&
        ((q3n_remote_entity_current(row) && row->current->weapon == after) ||
            fail(e, QA_ERROR_ARGUMENT, "Remote weapon store differs from its actual private cache result"));
}
bool q3n_remote_frame_prediction_error_clear(const q3n_remote_frame *f, qa_error *e)
{
    if (!q3n_remote_frame_current(f) || !f->prediction.owner || !f->prediction_error_clear)
        return fail(e, QA_ERROR_ARGUMENT, "Prediction error clear requires its actual admitted copied predictor");
    return f->prediction_error_clear(f->context, f, e) &&
        (q3n_remote_frame_current(f) || fail(e, QA_ERROR_ARGUMENT, "Prediction error clear retired its actual input/source receipt"));
}

bool q3n_frame_current(const q3n_frame *f)
{
    if (!f || !f->application) return false;
    if (f->remote) return !f->source.source_game && !f->has_local_player && !f->effects_source && !f->effect_event &&
        !f->reader && !f->client_service && f->application == f->remote->source.basis.application &&
        f->entities == f->remote->snapshots.entities && f->time == f->remote->snapshots.time &&
        f->seat == f->remote->source.basis.client.seat &&
        f->viewing_client == f->remote->source.basis.physical_client &&
        qa_actor_id_equal(f->viewing_actor, f->remote->source.publication.viewer) && q3n_remote_frame_current(f->remote);
    if (f->effects_source) return !f->source.source_game && !f->reader && !f->client_service &&
        (!f->effect_event || f->effects_source == &f->effect_event->source) &&
        (f->effect_event ? qa_application_effect_event_current(f->application, f->effect_event) :
            qa_application_selected_effects_current(f->application, f->effects_source));
    return !f->effect_event && qa_application_native_q3_presentation_current(f->application, &f->source);
}
qa_q3_product q3n_frame_product(const q3n_frame *f)
{ return f->remote ? f->remote->source.basis.product : f->effects_source ? f->effects_source->q3_product : f->source.product; }
int32_t q3n_frame_game_type(const q3n_frame *f)
{ return f->remote ? f->remote->source.game_type : f->source.game_type; }
int32_t q3n_frame_max_clients(const q3n_frame *f)
{ return f->remote ? f->remote->source.max_clients : (int32_t)f->source.max_clients; }
int32_t q3n_frame_match_start_time(const q3n_frame *f)
{ return f->remote ? f->remote->source.match_start_time : f->effects_source ? f->effects_source->q3_match_start_ms : f->source.match_start_time_ms; }
uint32_t q3n_frame_entity_capacity(const q3n_frame *f)
{ return f->remote ? QA_Q3_ENTITIES : f->source.entity_count; }
const qa_q3_player *q3n_frame_snapshot_player(const q3n_frame *f)
{ return !f ? NULL : f->remote ? (f->remote->snapshots.snapshot ? &f->remote->snapshots.snapshot->player : NULL) :
    f->has_local_player ? &f->local_player : NULL; }
const qa_q3_player *q3n_frame_predicted_player(const q3n_frame *f)
{ return !f ? NULL : f->remote ? f->remote->predicted_player : f->has_local_player ? &f->local_player : NULL; }
bool q3n_frame_configstring(const q3n_frame *f, uint32_t index, const char **text, uint64_t *revision, qa_error *e)
{
    if (!q3n_frame_current(f) || f->effects_source)
        return fail(e, QA_ERROR_ARGUMENT, "Configstrings require an actual local or remote reached client receipt");
    if (f->remote) return q3n_remote_source_configstring(f->remote->source.owner, index, text, revision, e);
    return qa_native_q3_wire_reader_configstring(f->reader, index, text, revision, e);
}
bool q3n_frame_entity(const q3n_frame *f, uint32_t number, qa_q3_entity *state,
    q3n_entity **cent, bool *present, qa_error *e)
{
    if (!state || !cent || !present || !q3n_frame_current(f) || f->effects_source ||
        number >= q3n_frame_entity_capacity(f))
        return fail(e, QA_ERROR_ARGUMENT, "Entity observation requires its actual local or remote frame domain");
    if (f->remote) {
        q3n_remote_entity row;
        if (!q3n_remote_frame_entity(f->remote, number, &row, e)) return false;
        *state = *row.current; *cent = row.presentation; *present = row.published; return true;
    }
    qa_application_native_q3_entity row;
    if (!qa_application_native_q3_presentation_entity(f->application, &f->source, number, &row, e)) return false;
    *state = row.state; *cent = f->entities ? &f->entities[number] : NULL; *present = row.present;
    return q3n_frame_current(f) || fail(e, QA_ERROR_ARGUMENT, "Local source retired during its entity observation");
}
