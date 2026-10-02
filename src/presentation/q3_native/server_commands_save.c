#include "server_commands_internal.h"
#include "../q3/internal.h"
#include <math.h>

static bool text(qa_source_save_io *io, char *value, size_t capacity)
{ return qa_source_save_bytes(io, value, capacity) && memchr(value, 0, capacity) != NULL; }
static bool reader_bound(const q3n_server_command_options *options, qa_error *e)
{
    if(options->compiled_source) {
        q3n_compiled_source_view source;
        return !options->client && !options->reader && !options->remote_client && !options->remote_source &&
            q3n_compiled_source_checkpoint_read(options->compiled_source,&source,e) && source.basis.application==options->application &&
            source.basis.content==options->content && source.basis.assets==options->assets && source.basis.product==options->product &&
            source.basis.publication==options->publication_generation && source.basis.map_revision==options->map_revision;
    }
    if (options->remote_client) {
        qa_native_q3_remote_client_basis basis; q3n_remote_source_view source;
        return !options->client && !options->reader && options->remote_source &&
            qa_native_q3_remote_client_basis_read(options->remote_client, &basis, e) &&
            q3n_remote_source_read(options->remote_source, &source, e) &&
            basis.application == options->application && basis.content == options->content &&
            basis.product == options->product && basis.client.receiver == options->recipient.receiver &&
            basis.client.service_owner == options->recipient.service_owner &&
            basis.client.frontend_lifetime == options->recipient.frontend_lifetime &&
            basis.client.cvars == options->recipient.cvars && basis.client.console == options->recipient.console &&
            basis.client.seat == options->recipient.seat && basis.client.source_client == options->recipient.source_client &&
            qa_actor_id_equal(basis.client.source_actor, options->recipient.source_actor) &&
            qa_net_client_id_equal(source.basis.connection, basis.connection) &&
            source.basis.client.service_owner == basis.client.service_owner;
    }
    qa_native_q3_wire_basis basis;
    const qa_application_q3_client_context *client = &options->recipient;
    const qa_native_q3_client_services *services = qa_native_q3_client_services_read(options->client);
    return services && services->wire_reader == options->reader &&
        qa_native_q3_wire_reader_idle(options->reader) &&
        qa_native_q3_wire_reader_basis(options->reader, &basis, e) &&
        basis.application == options->application && basis.session == client->session &&
        basis.source_cvars == client->source_cvars && basis.source_owner == client->source_owner &&
        basis.receiver == client->receiver && basis.product == options->product &&
        basis.seat == client->seat && basis.physical_client == client->source_client &&
        qa_actor_id_equal(basis.actor, client->source_actor) &&
        basis.publication_generation == options->publication_generation && basis.map_revision == options->map_revision;
}
static bool reached(const q3n_server_commands *o, qa_error *e)
{
    if(o->options.compiled_source) {
        q3n_compiled_source_view source;
        return !o->initialized || o->closed || (q3n_compiled_source_checkpoint_read(o->options.compiled_source,&source,e) &&
            source.basis.reached_command==o->state.server_command_sequence);
    }
    if (o->options.remote_source) {
        q3n_remote_source_view source;
        return !o->initialized || o->closed ||
            (q3n_remote_source_read(o->options.remote_source, &source, e) &&
                source.reached_command == o->state.server_command_sequence);
    }
    qa_native_q3_wire_publication publication;
    return !o->initialized || o->closed ||
        (qa_native_q3_wire_reader_publication(o->options.reader, &publication, e) && publication.has_gamestate &&
         publication.reached_command_sequence == o->state.server_command_sequence);
}
static bool identity(qa_source_save_io *io, const q3n_server_command_options *options)
{
    if(options->compiled_source) {
        bool scene_only=options->compiled_scene_only;
        return q3n_compiled_source_fields(io,options->compiled_source) &&
            qa_source_save_bool(io,&scene_only) && scene_only==options->compiled_scene_only;
    }
    if (options->remote_source) {
        q3n_remote_source_view source;
        if (!q3n_remote_source_read(options->remote_source, &source, io->error)) return false;
        const qa_native_q3_remote_client_basis *b = &source.basis;
        uint64_t connection = b->connection.owner, generation = b->connection.generation;
        uint64_t epoch = b->epoch, restart = b->restart_generation;
        uint64_t publication = b->publication_generation, configuration = b->configuration_generation;
        uint64_t receiver = b->client.receiver, service = b->client.service_owner;
        uint32_t slot = b->connection.slot, seat = b->client.seat, client = b->physical_client;
        int32_t message = b->initial_message, command = b->initial_command;
        qa_actor_id actor = b->client.source_actor;
        return qa_source_save_u64(io, &connection) && connection == b->connection.owner &&
            qa_source_save_u64(io, &generation) && generation == b->connection.generation &&
            qa_source_save_u32(io, &slot) && slot == b->connection.slot &&
            qa_source_save_u64(io, &epoch) && epoch == b->epoch &&
            qa_source_save_u64(io, &restart) && restart == b->restart_generation &&
            qa_source_save_u64(io, &publication) && publication == b->publication_generation &&
            qa_source_save_u64(io, &configuration) && configuration == b->configuration_generation &&
            qa_source_save_u64(io, &receiver) && receiver == b->client.receiver &&
            qa_source_save_u64(io, &service) && service == b->client.service_owner &&
            qa_source_save_u32(io, &seat) && seat == b->client.seat &&
            qa_source_save_u32(io, &client) && client == b->physical_client &&
            qa_source_save_actor(io, &actor) && qa_actor_id_equal(actor, b->client.source_actor) &&
            qa_source_save_i32(io, &message) && message == b->initial_message &&
            qa_source_save_i32(io, &command) && command == b->initial_command && q3n_remote_source_current(&source);
    }
    const qa_application_q3_client_context *actual = &options->recipient;
    uint64_t receiver = actual->receiver, source = actual->source_owner, service = actual->service_owner;
    uint64_t publication = options->publication_generation, map = options->map_revision;
    uint32_t seat = actual->seat, client = actual->source_client;
    qa_actor_id actor = actual->source_actor;
    return qa_source_save_u64(io, &receiver) && receiver == actual->receiver &&
        qa_source_save_u64(io, &source) && source == actual->source_owner &&
        qa_source_save_u64(io, &service) && service == actual->service_owner &&
        qa_source_save_u64(io, &publication) && publication == options->publication_generation &&
        qa_source_save_u64(io, &map) && map == options->map_revision &&
        qa_source_save_u32(io, &seat) && seat == actual->seat &&
        qa_source_save_u32(io, &client) && client == actual->source_client &&
        qa_source_save_actor(io, &actor) && qa_actor_id_equal(actor, actual->source_actor);
}
static bool score(qa_source_save_io *io, q3n_command_score *s)
{
    return qa_source_save_i32(io, &s->client) && s->client >= 0 && s->client < 64 &&
        qa_source_save_i32(io, &s->score) && qa_source_save_i32(io, &s->ping) &&
        qa_source_save_i32(io, &s->time) && qa_source_save_i32(io, &s->score_flags) &&
        qa_source_save_i32(io, &s->accuracy) && qa_source_save_i32(io, &s->impressive) &&
        qa_source_save_i32(io, &s->excellent) && qa_source_save_i32(io, &s->gauntlet) &&
        qa_source_save_i32(io, &s->defend) && qa_source_save_i32(io, &s->assist) &&
        qa_source_save_i32(io, &s->perfect) && qa_source_save_i32(io, &s->captures) &&
        qa_source_save_i32(io, &s->team) && s->team >= 0 && s->team <= 3;
}
static bool fields(qa_source_save_io *io, q3n_server_commands *o)
{
    uint8_t magic[4] = {'Q', '3', 'S', 'C'};
    uint32_t expected_version=o->options.compiled_source?2u:1u;
    uint32_t version = expected_version, product = o->options.product;
    q3n_command_state *s = &o->state;
    const char *expected = o->options.compiled_source?"Q3SU":o->options.remote_source ? "Q3SR" : "Q3SC";
    memcpy(magic, expected, 4);
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, expected, 4) ||
        !qa_source_save_u32(io, &version) || version != expected_version ||
        !qa_source_save_u32(io, &product) || product != (uint32_t)o->options.product ||
        !identity(io, &o->options) || !qa_source_save_bool(io, &o->initialized) || !qa_source_save_bool(io, &o->closed) ||
        !qa_source_save_i32(io, &s->server_command_sequence) || s->server_command_sequence < 0 ||
        !qa_source_save_i32(io, &s->game_type) || s->game_type < 0 || s->game_type > 7 ||
        !qa_source_save_i32(io, &s->dm_flags) || !qa_source_save_i32(io, &s->team_flags) ||
        !qa_source_save_i32(io, &s->fraglimit) || !qa_source_save_i32(io, &s->capturelimit) ||
        !qa_source_save_i32(io, &s->timelimit) || !qa_source_save_i32(io, &s->max_clients) ||
        !qa_source_save_i32(io, &s->level_start_time) || !text(io, s->mapname, sizeof(s->mapname)) ||
        !text(io, s->red_team, sizeof(s->red_team)) || !text(io, s->blue_team, sizeof(s->blue_team)) ||
        !qa_source_save_i32(io, &s->scores1) || !qa_source_save_i32(io, &s->scores2) ||
        !qa_source_save_i32(io, &s->warmup) || !qa_source_save_i32(io, &s->warmup_count) ||
        !qa_source_save_i32(io, &s->team_scores[0]) || !qa_source_save_i32(io, &s->team_scores[1]) ||
        !qa_source_save_i32(io, &s->num_scores) || s->num_scores > 64) return false;
    for (unsigned i = 0; i < 64; ++i) if (!score(io, &s->scores[i])) return false;
    if (!qa_source_save_i32(io, &s->num_sorted_team_players) || s->num_sorted_team_players > 8) return false;
    for (unsigned i = 0; i < 8; ++i)
        if (!qa_source_save_i32(io, &s->sorted_team_players[i]) || s->sorted_team_players[i] < 0 || s->sorted_team_players[i] >= 64) return false;
    if (!qa_source_save_i32(io, &s->vote_time) || !qa_source_save_i32(io, &s->vote_yes) ||
        !qa_source_save_i32(io, &s->vote_no) || !text(io, s->vote_string, sizeof(s->vote_string)) ||
        !qa_source_save_bool(io, &s->vote_modified)) return false;
    for (unsigned i = 0; i < 2; ++i)
        if (!qa_source_save_i32(io, &s->team_vote_time[i]) || !qa_source_save_i32(io, &s->team_vote_yes[i]) ||
            !qa_source_save_i32(io, &s->team_vote_no[i]) || !text(io, s->team_vote_string[i], sizeof(s->team_vote_string[i])) ||
            !qa_source_save_bool(io, &s->team_vote_modified[i])) return false;
    if (!qa_source_save_i32(io, &s->red_flag) || !qa_source_save_i32(io, &s->blue_flag) ||
        !qa_source_save_i32(io, &s->flag_status) || !qa_source_save_bool(io, &s->intermission_started) ||
        !qa_source_save_bool(io, &s->map_restart) || !qa_source_save_bool(io, &s->level_shot) ||
        !text(io, s->spectator_list, sizeof(s->spectator_list)) ||
        !qa_source_save_i32(io, &s->spectator_length) || s->spectator_length < 0 || s->spectator_length > 1023 ||
        !qa_source_save_f32(io, &s->spectator_width) || !isfinite(s->spectator_width) ||
        !qa_source_save_i32(io, &s->team_chat_position) || s->team_chat_position < 0 ||
        !qa_source_save_i32(io, &s->team_chat_last_position) || s->team_chat_last_position < 0 ||
        s->team_chat_last_position > s->team_chat_position) return false;
    for (unsigned i = 0; i < 8; ++i)
        if (!text(io, s->team_chat[i], sizeof(s->team_chat[i])) || !qa_source_save_i32(io, &s->team_chat_times[i])) return false;
    if (!qa_source_save_i32(io, &s->current_voice_client) || s->current_voice_client < 0 || s->current_voice_client >= 64 ||
        !qa_source_save_i32(io, &s->accept_order_time) || !qa_source_save_i32(io, &s->accept_task) ||
        s->accept_task < 0 || s->accept_task > 7 || !qa_source_save_i32(io, &s->accept_leader) ||
        s->accept_leader < 0 || s->accept_leader >= 64 || !text(io, s->accept_voice, sizeof(s->accept_voice))) return false;
    return q3n_voice_fields(io, o) && reached(o, io->error);
}
static bool captured(const q3n_server_commands *o, qa_error *e)
{
    const qa_q3_presentation_assets *assets = o ? o->options.assets : NULL;
    return (q3n_server_commands_idle(o) || (o && q3n_server_commands_rebind_checkpoint_current(o,o->rebind))) &&
        assets && assets->capturing && assets->busy == 1 && !assets->codec_busy &&
        reader_bound(&o->options, e) ? true :
        q3nc_fail(e, QA_ERROR_ARGUMENT, "Command codec needs the actual idle owner and backend registry capture lease");
}
bool q3n_server_commands_checkpoint(const q3n_server_commands *borrowed, qa_buffer *out, qa_error *e)
{
    if (!out || out->data || out->size || !captured(borrowed, e)) return false;
    q3n_server_commands *owner = (q3n_server_commands *)borrowed;
    q3n_server_commands *copy = malloc(sizeof(*copy));
    if (!copy) return q3nc_fail(e, QA_ERROR_MEMORY, "Copying native command continuation for checkpoint");
    *copy = *owner; owner->busy = true;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, owner->options.recipient.session, e) && fields(&io, copy) && qa_source_save_finish(&io, out);
    if (!ok && e && e->code == QA_OK) q3nc_fail(e, QA_ERROR_FORMAT, "Native command checkpoint contains inconsistent source fields");
    qa_source_save_dispose(&io); free(copy); owner->busy = false; return ok;
}
bool q3n_server_commands_restore(q3n_server_commands *owner, qa_bytes input, qa_error *e)
{
    if (!captured(owner, e)) return false;
    q3n_server_commands *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return q3nc_fail(e, QA_ERROR_MEMORY, "Allocating native command import candidate");
    candidate->options = owner->options; owner->busy = true;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, owner->options.recipient.session, input, e) &&
        fields(&io, candidate) && qa_source_save_finish(&io, NULL);
    if (ok) { owner->state = candidate->state; owner->voice = candidate->voice;
        owner->initialized = candidate->initialized; owner->closed = candidate->closed; }
    else if (e && e->code == QA_OK) q3nc_fail(e, QA_ERROR_FORMAT, "Native command import has invalid provenance or media references");
    qa_source_save_dispose(&io); free(candidate); owner->busy = false; return ok;
}
