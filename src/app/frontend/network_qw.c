#include "qa/platform_services.h"
#include "network_qw_private.h"
#include "qa/application_network.h"
#include "qa/application_language.h"
#include "qa/launch_identity.h"
#include "qa/localization.h"
#include "qa/network_save.h"
#include "qa/q1_chat_commands.h"
#include "qa/text.h"
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const qa_net_protocol_id protocol = {.kind = QA_NET_QW28};
static uint32_t peer_ping(const qw_frontend_peer *);
static bool source_status(void *, const char **, qa_error *);
static bool source_drop(void *, qa_net_client_id, const char *, qa_error *);
static bool peer_reliable(qw_frontend_peer *peer, qa_bytes bytes, qa_error *error)
{
    if (peer->retiring) return true;
    qa_error submission = {0};
    if (qa_network_qw_server_reliable(peer->host->runtime, peer->client, bytes, &submission)) return true;
    if (submission.code == QA_ERROR_CAPACITY)
        return source_drop(peer, peer->client, "Reliable message overflow", error);
    if (error) *error = submission;
    return false;
}
static char *text_copy(const char *text, qa_error *error)
{
    size_t size = strlen(text) + 1; char *copy = malloc(size);
    if (!copy) { frontend_fail(error, QA_ERROR_MEMORY, "Retaining QuakeWorld source text"); return NULL; }
    memcpy(copy, text, size); return copy;
}
static uint32_t source_random(void *context)
{
    frontend_qw_host *host = context;
    host->random = host->random * UINT32_C(1664525) + UINT32_C(1013904223);
    return host->random;
}
static bool source_world(frontend_qw_host *host, qa_application_network_qw_world *out, qa_error *error)
{
    return qa_application_network_qw_world_read(host->frontend->application, out, error) &&
        ((out->source.owner == host->owner && host->generation ==
          qa_application_configuration_generation(host->frontend->application)) ||
         frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld source factory belongs to a replaced physical owner"));
}
static bool peer_actor(qw_frontend_peer *peer, qa_actor_id *out, qa_error *error)
{
    qa_application_network_qw_client source;
    if (!peer->occupied || peer->retiring || !qa_application_remote_player_actor(peer->host->frontend->application,
        peer->client, peer->seat, out) || !qa_application_network_qw_client_read(peer->host->frontend->application, *out, &source, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld peer lost its genuine canonical source admission");
    return (source.source_slot == (uint32_t)(peer - peer->host->peers) + 1 && source.spectator == peer->spectator) ||
        frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld peer changes its physical row or trusted source role");
}
static bool reliable(qw_frontend_peer *peer, const qa_qw_service *service, qa_error *error)
{
    if (service->kind == QA_QW_PRINT && service->data.text.level < peer->message_level) return true;
    uint8_t bytes[QW_MESSAGE]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    return qa_qw_service_write(&writer, protocol, service, NULL) &&
        peer_reliable(peer, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
}
static bool broadcast(frontend_qw_host *host, const qa_qw_service *service, qa_error *error)
{
    for (size_t i = 0; i < QW_CLIENTS; ++i) {
        qw_frontend_peer *peer = host->peers + i;
        if (peer->occupied && !peer->retiring && peer->begun && !reliable(peer, service, error)) return false;
    }
    return true;
}
static bool print_text(qw_frontend_peer *peer, const char *text, qa_error *error)
{
    while (*text) {
        char piece[1396]; size_t size = strlen(text);
        if (size > sizeof(piece) - 1) size = sizeof(piece) - 1;
        memcpy(piece, text, size); piece[size] = 0;
        if (!reliable(peer, &(qa_qw_service){.kind = QA_QW_PRINT, .data.text = {2, piece}}, error)) return false;
        text += size;
    }
    return true;
}
static bool dropped_users(frontend_qw_host *host, qa_error *error)
{
    for (size_t i = 0; i < QW_CLIENTS; ++i) for (size_t j = 0; j < QW_CLIENTS; ++j) {
        uint32_t bit = UINT32_C(1) << j;
        if (!(host->drop_recipients[i] & bit)) continue;
        qw_frontend_peer *peer = host->peers + j;
        if (peer->occupied && !peer->retiring && peer->begun && qa_net_client_id_equal(peer->client, host->drop_clients[i][j]) &&
            !reliable(peer, &(qa_qw_service){.kind = QA_QW_USERINFO, .data.userinfo = {(uint8_t)i, 0, ""}}, error)) return false;
        host->drop_recipients[i] &= ~bit; host->drop_clients[i][j] = (qa_net_client_id){0};
    }
    return true;
}
static bool source_drop(void *context, qa_net_client_id id, const char *reason, qa_error *error)
{
    qw_frontend_peer *peer = context;
    if (!qa_net_client_id_equal(id, peer->client))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld retirement changes its full client generation");
    peer->retiring = true; snprintf(peer->reason, sizeof(peer->reason), "%s", reason);
    return true;
}
static void signon_disconnect(void *context, const char *reason)
{ qw_frontend_peer *peer = context; (void)source_drop(peer, peer->client, reason, NULL); }
static void source_print(void *context, const char *text)
{ frontend_print(((qw_frontend_peer *)context)->host->frontend, text); }
static bool source_paused(void *context, qa_net_client_id id, bool *out, qa_error *error)
{
    qw_frontend_peer *peer = context;
    if (!out || !qa_net_client_id_equal(id, peer->client))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld pause observation changes client identity");
    *out = qa_application_q1_paused(peer->host->frontend->application); return true;
}
static bool server_data(void *context, qa_qw_serverdata *out, qa_error *error)
{
    qw_frontend_peer *peer = context; qa_application_network_qw_world world;
    if (!source_world(peer->host, &world, error)) return false;
    *out = (qa_qw_serverdata){.protocol = world.protocol, .server_count = peer->host->server_count,
        .game_directory = world.game_directory, .level = world.level,
        .player_slot = (uint8_t)(peer - peer->host->peers), .cd_track = world.cd_track,
        .spectator = peer->spectator, .movement = world.movement};
    return true;
}
static bool signon_names(void *context, bool models, const char *const **out, size_t *count, qa_error *error)
{
    qw_frontend_peer *peer = context; (void)error;
    *out = (const char *const *)(models ? peer->host->models : peer->host->sounds);
    *count = models ? peer->host->model_count : peer->host->sound_count; return true;
}
static bool signon_buffers(void *context, const qa_bytes **out, size_t *count, qa_error *error)
{
    qw_frontend_peer *peer = context; (void)error;
    *out = peer->host->signon_views; *count = peer->host->signon_count; return true;
}
static bool accepts_checksum(void *context, uint32_t checksum)
{ return checksum == ((qw_frontend_peer *)context)->host->checksum; }
static bool source_spawn(void *context, uint8_t start, qa_q1_emit_fn emit, void *output, qa_error *error)
{
    qw_frontend_peer *peer = context; frontend_qw_host *host = peer->host; qa_actor_id actor;
    qa_application_network_qw_client source;
    if (!peer_actor(peer, &actor, error) ||
        !qa_application_network_qw_client_read(host->frontend->application, actor, &source, error) ||
        (!source.begun && !qa_application_network_qw_prepare(host->frontend->application, actor, error))) return false;
    uint8_t bytes[QW_MESSAGE]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    qa_qw_service service = {.kind = QA_QW_PAUSE, .data.paused = qa_application_q1_paused(host->frontend->application)};
    if (!qa_qw_service_write(&writer, protocol, &service, NULL) ||
        !emit(output, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error)) return false;
    for (size_t i = start; i < QW_CLIENTS; ++i) {
        qw_frontend_peer *other = host->peers + i; if (!other->occupied || other->retiring) continue;
        service = (qa_qw_service){.kind = QA_QW_USERINFO, .data.userinfo = {(uint8_t)i,
            (int32_t)((uint32_t)other->client.generation * 32u + (uint32_t)i + 1u), other->userinfo}};
        qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        if (!qa_qw_service_write(&writer, protocol, &service, NULL) ||
            !emit(output, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error)) return false;
    }
    qa_application_network_qw_world world;
    if (!source_world(host, &world, error)) return false;
    for (size_t i = 0; i < 64; ++i) {
        service = (qa_qw_service){.kind = QA_QW_LIGHT_STYLE, .data.light_style = {(uint8_t)i, world.lightstyles[i]}};
        qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        if (!qa_qw_service_write(&writer, protocol, &service, NULL) ||
            !emit(output, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error)) return false;
    }
    if (!qa_application_network_qw_client_read(host->frontend->application, actor, &source, error)) return false;
    for (uint8_t i = 0; i < 16; ++i) if (source.stat_mask & (UINT16_C(1) << i)) {
        qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        if (!qa_qw_source_write_stat(&writer, i, source.stats[i]) ||
            !emit(output, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error)) return false;
    }
    return true;
}
static bool source_begin(void *context, qa_error *error)
{
    qw_frontend_peer *peer = context; qa_actor_id before, after;
    if (!peer_actor(peer, &before, error) ||
        !qa_application_remote_player_begin(peer->host->frontend->application, peer->client, peer->seat, error) ||
        !peer_actor(peer, &after, error) || !qa_actor_id_equal(before, after)) return false;
    peer->begun = true; return true;
}
static bool open_download(void *context, const char *name, bool *found, qa_qw_download *out, qa_error *error)
{
    qw_frontend_peer *peer = context; qa_application_network_qw_world world;
    if (!source_world(peer->host, &world, error)) return false;
    *found = false; *out = (qa_qw_download){0};
    const qa_cvar_view *allowed = qa_cvars_find(world.source.cvars, "allow_download");
    if (!allowed || allowed->number == 0 || !qa_qw_download_path_valid(name)) return true;
    size_t size = strlen(name); char *path = malloc(size + 1);
    if (!path) return frontend_fail(error, QA_ERROR_MEMORY, "Qualifying QuakeWorld mounted download path");
    for (size_t i = 0; i <= size; ++i) path[i] = name[i] >= 'A' && name[i] <= 'Z' ? (char)(name[i] + ('a' - 'A')) : name[i];
    static const char *const prefixes[] = {"skins/", "progs/", "sound/", "maps/"};
    static const char *const settings[] = {"allow_download_skins", "allow_download_models", "allow_download_sounds", "allow_download_maps"};
    bool permit = strchr(path, '/') && path[0] != '.' && !strstr(path, "..");
    for (size_t i = 0; permit && i < 4; ++i) if (!strncmp(path, prefixes[i], strlen(prefixes[i]))) {
        const qa_cvar_view *value = qa_cvars_find(world.source.cvars, settings[i]);
        permit = value && value->number != 0;
    }
    qa_qw_download_admission admission = {.maximum_bytes = INT32_MAX,
        .content = qa_application_network_qw_content(peer->host->frontend->application, error)};
    bool ok = admission.content && (!permit || qa_qw_file_download_open(&admission, path, found, out, error));
    free(path); return ok;
}
static bool source_input(void *context, qa_net_client_id id, const qa_qw_command *commands,
    size_t count, uint32_t sequence, qa_error *error)
{
    qw_frontend_peer *peer = context; qa_actor_id actor; qa_player_state selected;
    if (!qa_net_client_id_equal(id, peer->client) || !commands || !count || count > 20 ||
        !peer_actor(peer, &actor, error) || !qa_application_control_read(peer->host->frontend->application, actor, &selected)) return false;
    qa_movement_command raw[20];
    for (size_t i = 0; i < count; ++i) raw[i] = (qa_movement_command){.kind = QA_RULESET_QUAKEWORLD,
        .sequence = sequence, .milliseconds = commands[i].msec,
        .angles = qa_v3(commands[i].angles[0], commands[i].angles[1], commands[i].angles[2]),
        .forward_move = commands[i].forward, .side_move = commands[i].side, .up_move = commands[i].up,
        .buttons = commands[i].buttons, .impulse = commands[i].impulse};
    qa_network_command_group group = {.client = id, .seat = peer->seat, .actor = actor,
        .epoch = qa_network_epoch(peer->host->runtime, id), .movement = selected.state.kind,
        .commands = raw, .count = count};
    if (!qa_network_accept_commands(peer->host->runtime, &group, error)) return false;
    peer->input_sequence = sequence; peer->command = commands[count - 1];
    qa_application_network_qw_source source;
    if (!qa_application_network_qw_source_read(peer->host->frontend->application, &source, error)) return false;
    peer->command_time_ns = source.source_time_ns;
    return true;
}
static bool source_receipt(void *context, qa_net_client_id id, uint32_t acknowledged,
    uint32_t outgoing, uint64_t now, qa_error *error)
{
    qw_frontend_peer *peer = context;
    if (!qa_net_client_id_equal(id, peer->client))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld ping receipt changes its full client generation");
    size_t index = acknowledged & 63u;
    if (peer->pings[index].present && peer->pings[index].sequence == acknowledged) {
        if (now < peer->pings[index].sent_ns)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld receipt predates its retained source frame");
        peer->pings[index].ping_ms = (double)(now - peer->pings[index].sent_ns) / 1e6;
    }
    index = outgoing & 63u;
    peer->pings[index].sequence = outgoing; peer->pings[index].sent_ns = now;
    peer->pings[index].ping_ms = -1; peer->pings[index].present = true;
    peer->last_received_ns = now;
    return true;
}
static bool source_blocked(void *context, const qa_net_address *address)
{ return qa_server_admin_rejects(((qw_frontend_peer *)context)->host->admin, address); }
static bool defer_command(void *context, qa_net_client_id id, const char *text, qa_error *error)
{
    qw_frontend_peer *peer = context; frontend_qw_host *host = peer->host; qa_actor_id actor;
    if (!qa_net_client_id_equal(id, peer->client) || !text || !peer_actor(peer, &actor, error)) return false;
    if (host->action_count == QW_ACTIONS)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source action queue overflow");
    qw_source_action *action = calloc(1, sizeof(*action));
    if (!action) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining QuakeWorld source action admission");
    action->text = text_copy(text, error);
    if (!action->text) { free(action); return false; }
    action->client = id; action->seat = peer->seat; action->actor = actor;
    action->epoch = qa_network_epoch(host->runtime, id);
    action->received_ns = peer->last_received_ns;
    if (host->last_action) host->last_action->next = action; else host->first_action = action;
    host->last_action = action; ++host->action_count; return true;
}
static uint32_t source_rate(const char *text)
{
    long value = strtol(text, NULL, 10);
    return value < 500 ? 500 : value > 10000 ? 10000 : (uint32_t)value;
}
static int32_t source_integer(const char *text)
{
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = *text == '-'; if (*text == '+' || *text == '-') ++text;
    uint32_t limit = negative ? UINT32_C(2147483648) : INT32_MAX, value = 0;
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (uint32_t)(*text++ - '0');
        value = value > (limit - digit) / 10 ? limit : value * 10 + digit;
    }
    return negative ? value == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)value : (int32_t)value;
}
bool frontend_qw_command_realtime(const frontend_qw_host *host,qa_actor_owner owner,
    qa_actor_id actor,uint64_t *out,qa_error *error)
{
    if (!host || !host->frontend || !host->runtime || !out || !owner || host->owner!=owner ||
        !actor.registry || !host->action_active || !qa_actor_id_equal(actor,host->action_actor) ||
        host->generation!=qa_application_configuration_generation(host->frontend->application))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"QW chat lost its current full-actor action receipt");
    *out=host->action_time_ns; return true;
}
static bool source_command(void *context, qa_net_client_id id, const char *text, qa_error *error)
{
    qw_frontend_peer *peer = context; frontend_qw_host *host = peer->host; qa_actor_id actor;
    if (!host->action_active || !qa_net_client_id_equal(id, peer->client) || !peer_actor(peer, &actor, error)) return false;
    const char *cursor = text; char name[64], first[1024], second[1024]; bool present;
    if (!qa_q1_token(&cursor, true, name, sizeof(name), &present, error) || !present) return !error || error->code == QA_OK;
    if (!strcmp(name, "rate")) {
        if (!qa_q1_token(&cursor, true, first, sizeof(first), &present, error)) return false;
        if (present && !qa_network_qw_server_rate(host->runtime, id, peer->rate = source_rate(first), error)) return false;
        char line[96]; snprintf(line, sizeof(line), "%s %u\n", present ? "Net rate set to" : "Current rate is", peer->rate);
        return reliable(peer, &(qa_qw_service){.kind = QA_QW_PRINT, .data.text = {2, line}}, error);
    }
    if (!strcmp(name, "kill")) {
        bool killed;
        return qa_application_network_qw_kill(host->frontend->application, actor, &killed, error) &&
            (killed || reliable(peer, &(qa_qw_service){.kind = QA_QW_PRINT,
                .data.text = {2, "Can't suicide -- allready dead!\n"}}, error));
    }
    if (!strcmp(name, "pause")) {
        qa_buffer announcement = {0}; bool changed;
        if (!qa_application_network_qw_pause(host->frontend->application, actor, &announcement, &changed, error)) return false;
        qa_qw_service message = {.kind = QA_QW_PRINT, .data.text = {2, (const char *)announcement.data}};
        bool ok = changed ? broadcast(host, &message, error) : reliable(peer, &message, error);
        qa_buffer_free(&announcement); return ok;
    }
    if (!strcmp(name, "ptrack")) {
        bool target,extra;
        if (!qa_q1_token(&cursor,true,first,sizeof(first),&target,error) ||
            !qa_q1_token(&cursor,true,second,sizeof(second),&extra,error)) return false;
        return qa_application_network_qw_ptrack(host->frontend->application,actor,
            target && !extra,target?source_integer(first):0,error);
    }
    if (!strcmp(name, "setinfo")) {
        bool key, value;
        if (!qa_q1_token(&cursor, true, first, sizeof(first), &key, error) ||
            !qa_q1_token(&cursor, true, second, sizeof(second), &value, error)) return false;
        if (!key || !value || !*first || first[0] == '*' || strpbrk(first, "\\\"\n\r") || strpbrk(second, "\\\"\n\r")) return true;
        bool language=!strcmp(first,"language");
        if (language && !qa_localization_language_valid(second))
            return print_text(peer,"Invalid language\n",error);
        char info[4096]; if (strlen(peer->userinfo) >= sizeof(info)) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source userinfo exceeds retained extent");
        memcpy(info, peer->userinfo, strlen(peer->userinfo) + 1);
        if (!qa_q3_info_set(info, sizeof(info), first, second, error)) return false;
        char *copy = text_copy(info, error); if (!copy) return false;
        qa_application_language_ticket *locale=NULL;
        if (language && !qa_application_language_prepare(host->frontend->application,actor,second,&locale,error)) {
            free(copy); return false;
        }
        if (locale && !qa_application_language_ready_is(locale,host->frontend->application,actor,second)) {
            qa_application_language_abort(locale); free(copy);
            return frontend_fail(error,QA_ERROR_ARGUMENT,"QW language lost its actual prepared recipient");
        }
        if (!qa_application_network_qw_userinfo(host->frontend->application, actor, info, error)) {
            qa_application_language_abort(locale); free(copy); return false;
        }
        qa_application_language_commit(locale);
        free(peer->userinfo); peer->userinfo = copy;
        if (!strcmp(first, "rate") && *second && !qa_network_qw_server_rate(host->runtime, id, peer->rate = source_rate(second), error)) return false;
        if (!strcmp(first, "msg") && *second) peer->message_level = source_integer(second);
        return broadcast(host, &(qa_qw_service){.kind = QA_QW_SET_INFO,
            .data.info = {(uint8_t)(peer - host->peers), first, second}}, error);
    }
    if (!strcmp(name, "msg")) {
        if (!qa_q1_token(&cursor, true, first, sizeof(first), &present, error)) return false;
        if (present) peer->message_level = source_integer(first);
        char line[96]; snprintf(line, sizeof(line), "%s %d\n", present ? "Msg level set to" : "Current msg level is", peer->message_level);
        return reliable(peer, &(qa_qw_service){.kind = QA_QW_PRINT, .data.text = {2, line}}, error);
    }
    if (!strcmp(name, "pings")) {
        for (size_t i = 0; i < QW_CLIENTS; ++i) {
            qw_frontend_peer *other = host->peers + i; if (!other->occupied || other->retiring || !other->begun) continue;
            double sum = 0; size_t samples = 0;
            for (size_t j = 0; j < 64; ++j) if (other->pings[j].present && other->pings[j].ping_ms > 0) { sum += other->pings[j].ping_ms; ++samples; }
            double ping = samples ? trunc(sum / (double)samples) : 9999;
            if (!isfinite(ping) || ping >= 0x1p63)
                return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld ping exceeds native millisecond storage");
            uint16_t bits = (uint16_t)(int64_t)ping;
            int16_t value; memcpy(&value, &bits, sizeof(value));
            qa_network_qw_server_state state;
            if (!qa_network_qw_server_state_read(host->runtime, other->client, &state, error) ||
                !reliable(peer, &(qa_qw_service){.kind = QA_QW_PING, .data.score = {(uint8_t)i, value}}, error) ||
                !reliable(peer, &(qa_qw_service){.kind = QA_QW_PACKET_LOSS, .data.packet_loss = {(uint8_t)i, state.loss}}, error)) return false;
        }
        return true;
    }
    if (!strcmp(name, "ping")) {
        if (!print_text(peer, "Client ping times:\n", error)) return false;
        for (size_t i = 0; i < QW_CLIENTS; ++i) {
            qw_frontend_peer *other = host->peers + i;
            if (!other->occupied || other->retiring || !other->begun) continue;
            qa_qw_info info = {0};
            if (!qa_qw_info_parse(other->userinfo, &info, error)) return false;
            const char *client_name = qa_qw_info_get(&info, "name");
            char prefix[32]; snprintf(prefix, sizeof(prefix), "%u ", peer_ping(other));
            bool ok = print_text(peer, prefix, error) && print_text(peer, client_name ? client_name : "unnamed", error) &&
                print_text(peer, "\n", error);
            qa_qw_info_free(&info); if (!ok) return false;
        }
        return true;
    }
    if (!strcmp(name, "status")) {
        const char *status = NULL;
        return source_status(host, &status, error) && print_text(peer, status, error);
    }
    if (qa_q1_chat_command_read(QA_RULESET_QUAKEWORLD, name, false) != QA_Q1_CHAT_UNKNOWN)
        return qa_application_actor_command(host->frontend->application,actor,text,error);
    frontend_print(host->frontend, text); return true;
}
qa_network_qw_server_hooks frontend_qw_peer_hooks(qw_frontend_peer *peer)
{
    return (qa_network_qw_server_hooks){.context = peer, .print = source_print, .paused = source_paused,
        .input = source_input, .command = source_command, .defer_command = defer_command,
        .receipt = source_receipt, .blocked = source_blocked, .drop = source_drop,
        .signon = {.user = peer, .server_data = server_data, .names = signon_names, .buffers = signon_buffers,
            .accepts_checksum = accepts_checksum, .spawn = source_spawn, .begin = source_begin,
            .disconnect = signon_disconnect, .open_download = open_download}};
}

static bool add_signon(frontend_qw_host *host, qa_bytes bytes, qa_error *error)
{
    if (!bytes.size) return true;
    if (!bytes.data || bytes.size > QW_SIGNON)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source signon record exceeds its factory block");
    if (!host->signon_count || host->signon[host->signon_count - 1].size + bytes.size > QW_SIGNON) {
        size_t count = host->signon_count + 1;
        qa_buffer *next = realloc(host->signon, count * sizeof(*next));
        if (!next) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining QuakeWorld immutable signon blocks");
        host->signon = next; host->signon[host->signon_count] = (qa_buffer){0}; host->signon_count = count;
    }
    qa_buffer *last = host->signon + host->signon_count - 1;
    void *next = realloc(last->data, last->size + bytes.size);
    if (!next) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining QuakeWorld immutable source signon bytes");
    last->data = next; memcpy(last->data + last->size, bytes.data, bytes.size); last->size += bytes.size; return true;
}
static bool capture_factory(frontend_qw_host *host, qa_error *error)
{
    qa_application_network_qw_world world;
    if (!source_world(host, &world, error) || !qa_qw_map_checksum2(world.map_bytes, &host->checksum, error)) return false;
    const char *models[255], *sounds[255];
    if (!qa_application_network_qw_precache(host->frontend->application, true, models, &host->model_count, error) ||
        !qa_application_network_qw_precache(host->frontend->application, false, sounds, &host->sound_count, error)) return false;
    for (size_t i = 0; i < host->model_count; ++i) {
        host->models[i] = text_copy(models[i], error); if (!host->models[i]) return false;
        if (!strcmp(models[i], "progs/player.mdl")) host->player_model = (uint32_t)i + 1;
        if (!strcmp(models[i], "progs/spike.mdl")) host->nail_model = (uint32_t)i + 1;
        if (!strcmp(models[i], "progs/s_spike.mdl")) host->supernail_model = (uint32_t)i + 1;
    }
    for (size_t i = 0; i < host->sound_count; ++i) {
        host->sounds[i] = text_copy(sounds[i], error); if (!host->sounds[i]) return false;
    }
    uint32_t cursor = 0; bool present; qa_actor_id actor; qa_application_network_qw_entity entity;
    for (size_t i = 0; i < QW_CLIENTS; ++i)
        host->baselines[host->baseline_count++] = (qa_qw_source_entity){.number = (uint32_t)i + 1,
            .model = host->player_model, .colormap = (double)i + 1};
    for (;;) {
        if (!qa_application_network_qw_entity_next(host->frontend->application, &cursor, &present, &actor, &entity, error)) return false;
        if (!present) break;
        qa_qw_source_entity *baseline = host->baselines + host->baseline_count++;
        *baseline = (qa_qw_source_entity){.number = entity.number, .model = entity.model, .frame = entity.frame,
            .colormap = entity.colormap, .skin = entity.skin};
        memcpy(baseline->origin, entity.origin, sizeof(baseline->origin)); memcpy(baseline->angles, entity.angles, sizeof(baseline->angles));
    }
    for (size_t i = 0; i < host->baseline_count; ++i) {
        uint8_t bytes[64]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
        if (!qa_qw_source_write_baseline(&writer, host->baselines + i) ||
            !add_signon(host, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error)) return false;
    }
    size_t count;
    if (!qa_application_network_qw_signon_count(host->frontend->application, &count, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_application_protocol_event event;
        if (!qa_application_network_qw_signon_at(host->frontend->application, i, &event, error) || !add_signon(host, event.payload, error)) return false;
    }
    host->signon_views = calloc(host->signon_count, sizeof(*host->signon_views));
    if (host->signon_count && !host->signon_views) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining QuakeWorld immutable signon views");
    for (size_t i = 0; i < host->signon_count; ++i) host->signon_views[i] = (qa_bytes){host->signon[i].data, host->signon[i].size};
    for (size_t i = 0; i < 64; ++i) { host->styles[i] = text_copy(world.lightstyles[i], error); if (!host->styles[i]) return false; }
    host->published_time_ns = world.source.source_time_ns; return true;
}

bool frontend_qw_create(qa_frontend *frontend, qa_network_runtime *runtime, qa_server_admin *admin,
    const uint64_t *composition, frontend_qw_host **out, qa_error *error)
{
    if (!frontend || !runtime || !admin || !composition || !out || *out || !frontend->application ||
        frontend->options.network_protocol.kind != QA_NET_QW28 || frontend->options.network_protocol.flags ||
        frontend->options.network_protocol.revision || frontend->options.network_connect)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld factory requires its installed source and sole runtime");
    qa_application_network_qw_source source;
    if (!qa_application_network_qw_source_read(frontend->application, &source, error)) return false;
    frontend_qw_host *host = calloc(1, sizeof(*host));
    if (!host) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating QuakeWorld physical factory owner");
    host->frontend = frontend; host->runtime = runtime; host->admin = admin; host->composition = *composition;
    host->owner = source.owner; host->generation = qa_application_configuration_generation(frontend->application);
    uint64_t seed;
    if (!qa_platform_entropy_u64(&seed,error)) { frontend_qw_destroy(host); return false; }
    host->server_count = 1; host->random = (uint32_t)seed;
    const qa_cvar_view *maximum = qa_cvars_find(source.cvars, "maxclients");
    if (!maximum || !isfinite(maximum->number) || maximum->number < 1 || maximum->number > 32) {
        frontend_qw_destroy(host);
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld factory requires its real startup player limit");
    }
    host->active_limit = (uint32_t)maximum->number;
    host->previous_pause = qa_application_q1_paused(frontend->application);
    static const char *const downloads[] = {"allow_download", "allow_download_skins", "allow_download_models", "allow_download_sounds", "allow_download_maps"};
    bool ok = true;
    for (size_t i = 0; ok && i < sizeof(downloads) / sizeof(*downloads); ++i)
        if (!qa_cvars_find(source.cvars, downloads[i])) ok = qa_cvars_register(source.cvars, downloads[i], "1", 0, host->owner, "QuakeWorld source download policy", error);
    if (ok) ok = qa_qw_challenges_create(1024, source_random, host, &host->challenges, error) && capture_factory(host, error);
    if (!ok) { frontend_qw_destroy(host); return false; }
    *out = host; return true;
}
void frontend_qw_destroy(frontend_qw_host *host)
{
    if (!host) return;
    qa_qw_challenges_destroy(host->challenges);
    for (size_t i = 0; i < QW_PENDING; ++i) qa_buffer_free(&host->pending[i].reply);
    for (size_t i = 0; i < QW_CLIENTS; ++i) free(host->peers[i].userinfo);
    for (size_t i = 0; i < 255; ++i) { free(host->models[i]); free(host->sounds[i]); }
    for (size_t i = 0; i < 64; ++i) free(host->styles[i]);
    if (host->signon) for (size_t i = 0; i < host->signon_count; ++i) qa_buffer_free(host->signon + i);
    while (host->first_action) { qw_source_action *next = host->first_action->next; free(host->first_action->text); free(host->first_action); host->first_action = next; }
    free(host->signon); free(host->signon_views); qa_buffer_free(&host->status); free(host);
}
void frontend_qw_disconnected(frontend_qw_host *host, qa_net_client_id id)
{
    if (!host) return;
    for (size_t i = 0; i < QW_CLIENTS; ++i) if (host->peers[i].occupied && qa_net_client_id_equal(host->peers[i].client, id)) {
        for (size_t j = 0; j < QW_CLIENTS; ++j) {
            const qw_frontend_peer *recipient = host->peers + j;
            if (j == i || !recipient->occupied || recipient->retiring || !recipient->begun) continue;
            host->drop_recipients[i] |= UINT32_C(1) << j;
            host->drop_clients[i][j] = recipient->client;
        }
        free(host->peers[i].userinfo); host->peers[i] = (qw_frontend_peer){0};
    }
}
bool frontend_qw_idle(const frontend_qw_host *host)
{ return !host || !host->busy; }

static bool connect_source(void *context, const qa_qw_connect_request *request,
    uint64_t now, qa_q1_connect_result *out, qa_error *error)
{
    frontend_qw_host *host = context;
    if (!dropped_users(host, error)) return false;
    if (request->donor_wide) {
        *out = (qa_q1_connect_result){.decision = QA_Q1_CONNECT_REJECT, .reason = "This source requires classic QW28\n"}; return true;
    }
    for (size_t i = 0; i < QW_CLIENTS; ++i) {
        qw_frontend_peer *peer = host->peers + i;
        const qa_net_client *client = peer->occupied ? qa_net_connections_get(qa_network_connections(host->runtime), peer->client) : NULL;
        if (!peer->retiring && client && peer->qport == request->qport && qa_net_address_equal(&client->endpoint, &request->from, false)) {
            if (!peer->begun) { *out = (qa_q1_connect_result){.decision = QA_Q1_CONNECT_ACCEPT}; return true; }
            if (!qa_network_detach(host->runtime, peer->client, "Reconnecting", error) || !dropped_users(host, error)) return false;
            break;
        }
    }
    qa_application_network_qw_world world;
    if (!source_world(host, &world, error)) return false;
    qa_net_seat_id local_seat = {0}; uint32_t application_seat = 0;
    bool local_player = frontend_network_local_seat(host->frontend, &request->from, &local_seat, &application_seat);
    qa_actor_id actor; qa_application_network_qw_client source;
    size_t index = 0; bool spectator = request->spectator;
    if (local_player) {
        if (!qa_application_player_actor(host->frontend->application, application_seat, &actor) ||
            !qa_application_network_qw_client_read(host->frontend->application, actor, &source, error)) return false;
        index = source.source_slot - 1; spectator = source.spectator;
    }
    const qa_cvar_view *maximum = qa_cvars_find(world.source.cvars, "maxspectators");
    if (!local_player && request->spectator && (!maximum || !isfinite(maximum->number)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld admission requires its actual source player-limit policy");
    if (!local_player) {
        uint32_t limit = request->spectator ? maximum->number <= 0 ? 0 : maximum->number >= 32 ? 32 : (uint32_t)maximum->number : host->active_limit;
        bool occupied[QW_CLIENTS] = {0}; size_t count = 0; uint32_t cursor = 0; bool present;
        for (;;) {
            if (!qa_application_network_qw_client_next(host->frontend->application, &cursor, &present, &source, error)) return false;
            if (!present) break;
            occupied[source.source_slot - 1] = true;
        }
        for (size_t i = 0; i < QW_CLIENTS; ++i)
            if (host->peers[i].occupied && !host->peers[i].retiring && host->peers[i].spectator == request->spectator) ++count;
        while (index < QW_CLIENTS && (occupied[index] || host->peers[index].occupied)) ++index;
        if (count >= limit || index == QW_CLIENTS) {
            *out = (qa_q1_connect_result){.decision = QA_Q1_CONNECT_REJECT, .reason = "Server is full\n"}; return true;
        }
    }
    char info[4096];
    if (strlen(request->userinfo) >= sizeof(info)) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld admission userinfo exceeds its source extent");
    strcpy(info, request->userinfo);
    if (!qa_q3_info_set(info, sizeof(info), "*spectator", spectator ? "1" : "", error)) return false;
    qa_qw_info parsed = {0}; if (!qa_qw_info_parse(info, &parsed, error)) return false;
    const char *language=qa_qw_info_get(&parsed,"language");
    if (language && !qa_localization_language_valid(language)) {
        qa_qw_info_free(&parsed);
        *out=(qa_q1_connect_result){.decision=QA_Q1_CONNECT_REJECT,.reason="Invalid language\n"}; return true;
    }
    if (!language && !qa_q3_info_set(info,sizeof(info),"language",qa_localization_language(NULL),error)) {
        qa_qw_info_free(&parsed); return false;
    }
    const char *name = qa_qw_info_get(&parsed, "name"), *team = qa_qw_info_get(&parsed, "team"), *skin = qa_qw_info_get(&parsed, "skin"),
        *rate = qa_qw_info_get(&parsed, "rate"), *message_level_text = qa_qw_info_get(&parsed, "msg");
    qw_frontend_peer *peer = host->peers + index;
    *peer = (qw_frontend_peer){.host = host,
        .seat = local_player ? local_seat : (qa_net_seat_id){QA_NETWORK_COMMAND_OWNER, 256u + (uint32_t)index},
        .qport = request->qport, .rate = rate && *rate ? source_rate(rate) : 2500,
        .connected_ns = now, .command_time_ns = world.source.source_time_ns, .spectator = spectator,
        .message_level = message_level_text && *message_level_text ? source_integer(message_level_text) : 0};
    qa_event_receipts_reset(&peer->event_receipts, qa_application_events_local_first(host->frontend->application));
    peer->userinfo = text_copy(info, error);
    if (!peer->userinfo) { qa_qw_info_free(&parsed); return false; }
    qa_net_seat_binding seat = {peer->seat, 0};
    qa_net_connect connect = {.attachment = local_player ? QA_NET_LOCAL_SEAT : QA_NET_REMOTE, .endpoint = request->from, .protocol = protocol,
        .seats = &seat, .seat_count = 1, .composition = host->composition};
    qa_network_qw_server_policy policy = {peer->qport, peer->rate, QW_MESSAGE, 5};
    qa_network_qw_server_hooks hooks = frontend_qw_peer_hooks(peer);
    bool ok = qa_network_attach_qw_server(host->runtime, &connect, &policy, &hooks, now, &peer->client, error);
    if (ok) {
        peer->occupied = true;
        qa_application_remote_player_request player = {.client = peer->client, .seat = peer->seat,
            .application_seat = peer->seat.index, .source_slot = (uint32_t)index + 1,
            .name = name ? name : "unnamed", .team = team ? team : "", .skin = skin ? skin : "",
            .userinfo = info, .spectator = request->spectator, .defer_source_begin = true};
        ok = local_player ? qa_application_network_local_bind(host->frontend->application,
            qa_net_connections_get(qa_network_connections(host->runtime), peer->client), peer->seat, application_seat, error) &&
            qa_application_network_qw_userinfo(host->frontend->application, actor, info, error) :
            qa_application_remote_player_attach(host->frontend->application, &player, &actor, error);
        ok = ok && peer_actor(peer, &actor, error) &&
            qa_network_qw_server_baselines(host->runtime, peer->client, host->baselines, host->baseline_count, error);
        if (!ok) {
            qa_error failure = error ? *error : (qa_error){0};
            if (!qa_network_detach(host->runtime, peer->client, "QuakeWorld source reservation failed", error)) { qa_qw_info_free(&parsed); return false; }
            if (error) *error = failure;
        }
    } else { free(peer->userinfo); *peer = (qw_frontend_peer){0}; }
    qa_qw_info_free(&parsed);
    if (!ok) return false;
    qa_qw_service message = {.kind = QA_QW_USERINFO, .data.userinfo = {(uint8_t)index,
        (int32_t)((uint32_t)peer->client.generation * 32u + (uint32_t)index + 1u), peer->userinfo}};
    if (!broadcast(host, &message, error)) return false;
    *out = (qa_q1_connect_result){.decision = QA_Q1_CONNECT_ACCEPT}; return true;
}
static bool blocked(void *context, const qa_net_address *address)
{ return qa_server_admin_rejects(((frontend_qw_host *)context)->admin, address); }
static uint32_t peer_ping(const qw_frontend_peer *peer)
{
    double sum = 0; size_t samples = 0;
    for (size_t i = 0; i < 64; ++i) if (peer->pings[i].present && peer->pings[i].ping_ms > 0) {
        sum += peer->pings[i].ping_ms; ++samples;
    }
    double mean = samples ? trunc(sum / (double)samples) : 9999;
    return mean >= UINT32_MAX ? UINT32_MAX : (uint32_t)mean;
}
static bool source_status(void *context, const char **out, qa_error *error)
{
    frontend_qw_host *host = context; qa_application_network_qw_world world; qa_buffer info = {0};
    if (!source_world(host, &world, error)) return false;
    if (!world.source.serverinfo &&
        !qa_cvars_info(world.source.cvars, QA_CVAR_SERVERINFO, 4096, &info, error)) return false;
    qa_buffer status = {malloc(65531), 0};
    if (!status.data) { qa_buffer_free(&info); return frontend_fail(error, QA_ERROR_MEMORY, "Capturing QuakeWorld source status response"); }
    const char *serverinfo=world.source.serverinfo?world.source.serverinfo:info.data?(char *)info.data:"";
    int length = snprintf((char *)status.data, 65531, "%s\\map\\%s\n", serverinfo, world.map);
    qa_buffer_free(&info);
    bool ok = length >= 0 && length < 65531; size_t used = ok ? (size_t)length : 0;
    for (size_t i = 0; ok && i < QW_CLIENTS; ++i) {
        qw_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring || peer->spectator) continue;
        qa_actor_id actor; qa_application_network_qw_client source; qa_qw_info parsed = {0};
        if (!peer_actor(peer, &actor, error) || !qa_application_network_qw_client_read(host->frontend->application, actor, &source, error) ||
            !qa_qw_info_parse(peer->userinfo, &parsed, error)) { ok = false; break; }
        const char *name = qa_qw_info_get(&parsed, "name"), *skin = qa_qw_info_get(&parsed, "skin"),
            *top = qa_qw_info_get(&parsed, "topcolor"), *bottom = qa_qw_info_get(&parsed, "bottomcolor");
        length = snprintf((char *)status.data + used, 65531 - used, "%u %.0f %" PRIu64 " %u \"%s\" \"%s\" %s %s\n",
            (uint32_t)peer->client.generation * 32u + (uint32_t)i + 1u, trunc((double)source.frags),
            host->frontend->wall_time_ns >= peer->connected_ns ? (host->frontend->wall_time_ns - peer->connected_ns) / UINT64_C(60000000000) : 0,
            peer_ping(peer), name ? name : "unnamed", skin ? skin : "", top && *top ? top : "0", bottom && *bottom ? bottom : "0");
        qa_qw_info_free(&parsed);
        if (length < 0 || (size_t)length >= 65531 - used) ok = false; else used += (size_t)length;
    }
    if (!ok) { qa_buffer_free(&status); return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source status response exceeds packet extent"); }
    status.size = used; qa_buffer_free(&host->status); host->status = status; *out = (const char *)status.data; return true;
}
static bool source_log(void *context,int32_t sequence,const char **out,qa_error *error)
{
    frontend_qw_host *host=context; bool enabled=false,present=false;
    *out=NULL;
    if (!qa_application_network_qw_log_enabled(host->frontend->application,&enabled,error)) return false;
    if (!enabled) return true;
    qa_q1_qw_fraglog_view log;
    if (!qa_application_network_qw_log_read(host->frontend->application,&log,&present,error) || !present) return false;
    uint32_t previous=log.sequence-1;
    if (sequence==(int32_t)previous) return true;
    qa_bytes bytes=log.buffers[previous&1u];
    const uint8_t *nul=bytes.size?memchr(bytes.data,0,bytes.size):NULL;
    if (bytes.size && !nul) return frontend_fail(error,QA_ERROR_FORMAT,"QuakeWorld frag buffer lost its Source string terminator");
    size_t length=nul?(size_t)(nul-bytes.data):0;
    char prefix[64]; int size=snprintf(prefix,sizeof(prefix),"stdlog %u\n",previous);
    if (size<0 || (size_t)size>=sizeof(prefix) || length>65534-(size_t)size)
        return frontend_fail(error,QA_ERROR_FORMAT,"QuakeWorld frag response exceeds its native datagram");
    qa_buffer response={malloc((size_t)size+length+1),(size_t)size+length};
    if (!response.data) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining QuakeWorld frag response");
    memcpy(response.data,prefix,(size_t)size);
    if (length) memcpy(response.data+(size_t)size,bytes.data,length);
    response.data[response.size]=0;
    qa_buffer_free(&host->status); host->status=response; *out=(const char *)response.data; return true;
}
typedef struct qw_reply { frontend_qw_host *host; qw_pending_control *pending; } qw_reply;
static bool send_reply(void *context, qa_bytes bytes, qa_error *error)
{
    qw_reply *reply=context; qw_pending_control *pending=reply->pending;
    if (!bytes.size || bytes.size>65535)
        return frontend_fail(error,QA_ERROR_FORMAT,"QuakeWorld connectionless response exceeds its datagram");
    pending->reply.data=malloc(bytes.size);
    if (!pending->reply.data) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining QuakeWorld native reply");
    pending->reply.size=bytes.size; memcpy(pending->reply.data,bytes.data,bytes.size);
    if (!qa_network_send_address(reply->host->runtime,&pending->address,bytes,error)) return false;
    qa_buffer_free(&pending->reply); return true;
}
bool frontend_qw_receive(frontend_qw_host *host, const qa_net_datagram *packet, bool *recognized, qa_error *error)
{
    if (!host || !packet || !recognized) return frontend_fail(error, QA_ERROR_ARGUMENT, "Missing QuakeWorld connectionless owner");
    *recognized = packet->payload.size >= 4 && qa_load_u32le(packet->payload.data) == UINT32_MAX;
    if (!*recognized || blocked(host, &packet->from)) return true;
    if (packet->payload.size > QW_MESSAGE || host->pending_count == QW_PENDING) return true;
    qw_pending_control *pending = host->pending + host->pending_count++;
    pending->address = packet->from; pending->time_ns = packet->received_ns; pending->size = packet->payload.size;
    memcpy(pending->bytes, packet->payload.data, pending->size); return true;
}
bool frontend_qw_prepare(frontend_qw_host *host, qa_error *error)
{
    if (!host) return true;
    if (!frontend_qw_idle(host) || !qa_network_callbacks_idle(host->runtime))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld publication requires returned source/network callbacks");
    for (size_t i = 0; i < QW_CLIENTS; ++i) {
        qw_frontend_peer *peer = host->peers + i;
        if (peer->occupied && peer->retiring && !qa_network_detach(host->runtime, peer->client, peer->reason, error)) return false;
    }
    if (!dropped_users(host, error)) return false;
    uint64_t generation = qa_application_configuration_generation(host->frontend->application);
    if (generation != host->generation) {
        if (host->server_count == INT32_MAX)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source server count exhausted");
        qa_application_network_qw_source source;
        if (!qa_application_network_qw_source_read(host->frontend->application, &source, error)) return false;
        frontend_qw_host *factory = calloc(1, sizeof(*factory));
        if (!factory) return frontend_fail(error, QA_ERROR_MEMORY, "Capturing replacement QuakeWorld factory");
        factory->frontend = host->frontend; factory->owner = source.owner; factory->generation = generation;
        bool ok = capture_factory(factory, error);
        if (!ok) { frontend_qw_destroy(factory); return false; }
        host->composition = generation;
        for (size_t i = 0; i < 255; ++i) {
            free(host->models[i]); free(host->sounds[i]);
            host->models[i] = factory->models[i]; host->sounds[i] = factory->sounds[i];
            factory->models[i] = factory->sounds[i] = NULL;
        }
        for (size_t i = 0; i < 64; ++i) { free(host->styles[i]); host->styles[i] = factory->styles[i]; factory->styles[i] = NULL; }
        for (size_t i = 0; i < host->signon_count; ++i) qa_buffer_free(host->signon + i);
        free(host->signon); free(host->signon_views);
        host->signon = factory->signon; host->signon_views = factory->signon_views; host->signon_count = factory->signon_count;
        factory->signon = NULL; factory->signon_views = NULL; factory->signon_count = 0;
        host->model_count = factory->model_count; host->sound_count = factory->sound_count;
        host->baseline_count = factory->baseline_count; memcpy(host->baselines, factory->baselines, sizeof(host->baselines));
        host->checksum = factory->checksum; host->player_model = factory->player_model;
        host->nail_model = factory->nail_model; host->supernail_model = factory->supernail_model;
        host->published_time_ns = factory->published_time_ns; frontend_qw_destroy(factory);
        host->owner = source.owner; host->generation = generation; ++host->server_count;
        uint64_t first = qa_application_events_local_first(host->frontend->application);
        if (host->event_cursor < first) host->event_cursor = first;
        if (host->reliable_cursor < first) host->reliable_cursor = first;
        host->event_generation = host->reliable_generation = qa_application_protocol_events_generation(host->frontend->application);
        host->previous_pause = qa_application_q1_paused(host->frontend->application);
        while (host->first_action) {
            qw_source_action *next = host->first_action->next;
            free(host->first_action->text); free(host->first_action); host->first_action = next;
        }
        host->last_action = NULL; host->action_count = 0;
        for (size_t i = 0; i < QW_CLIENTS; ++i) {
            qw_frontend_peer *peer = host->peers + i;
            if (!peer->occupied || peer->retiring) continue;
            qa_actor_id actor; qa_application_network_qw_client client;
            if (!peer_actor(peer, &actor, error) ||
                !qa_application_network_qw_client_read(host->frontend->application, actor, &client, error) || client.begun)
                return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld carried source must defer Begin until genuine new signon");
            peer->begun = false; peer->input_sequence = 0; peer->stat_mask = 0;
            peer->command = (qa_qw_command){0}; peer->command_time_ns = source.source_time_ns;
            memset(peer->stats, 0, sizeof(peer->stats));
            if (!qa_network_restart(host->runtime, peer->client, &host->composition, error) ||
                !qa_network_qw_server_baselines(host->runtime, peer->client, host->baselines, host->baseline_count, error)) return false;
            qa_event_receipts_reset(&peer->event_receipts, first);
        }
    }
    qa_application_network_qw_world world;
    return source_world(host, &world, error);
}
bool frontend_qw_pump(frontend_qw_host *host, qa_error *error)
{
    if (!host) return true;
    if (!frontend_qw_prepare(host, error)) return false;
    bool log_present = false;
    if (!qa_application_network_qw_log_check(host->frontend->application,
        (double)host->frontend->wall_time_ns / 1000000000.0, &log_present, error)) return false;
    qa_application_network_qw_world world;
    if (!source_world(host, &world, error)) return false;
    const qa_cvar_view *password = qa_cvars_find(world.source.cvars, "password"),
        *spectator = qa_cvars_find(world.source.cvars, "spectator_password"),
        *high = qa_cvars_find(world.source.cvars, "sv_highchars"),
        *rcon = qa_cvars_find(qa_application_cvars(host->frontend->application), "rcon_password");
    if (!password || !spectator || !high || !rcon)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld authentication lacks its actual constructor policies");
    qa_qw_connection_host hooks = {.context = host, .password = password->value,
        .spectator_password = spectator->value, .rcon_password = rcon->value,
        .high_characters = high->number != 0, .blocked = blocked, .connect = connect_source,
        .status = source_status,.log=source_log};
    while (host->pending_count) {
        qw_pending_control *pending = host->pending; qw_reply reply = {host, pending};
        if (pending->reply.size) {
            if (!qa_network_send_address(host->runtime,&pending->address,
                (qa_bytes){pending->reply.data,pending->reply.size},error)) return false;
            qa_buffer_free(&pending->reply);
        } else if (!qa_qw_connectionless_receive(&hooks, host->challenges, (qa_bytes){pending->bytes, pending->size},
            &pending->address, pending->time_ns, send_reply, &reply, error)) return false;
        --host->pending_count;
        memmove(host->pending, host->pending + 1, host->pending_count * sizeof(*host->pending));
        host->pending[host->pending_count]=(qw_pending_control){0};
    }
    return true;
}

typedef struct qw_physical_frame {
    qa_application_network_qw_source source;
    qa_application_network_qw_client clients[QW_CLIENTS];
    size_t client_count, entity_count;
    qa_application_network_qw_entity entities[479];
    qa_actor_id actors[479];
} qw_physical_frame;
static bool capture_frame(frontend_qw_host *host, qw_physical_frame *frame, qa_error *error)
{
    memset(frame, 0, sizeof(*frame));
    if (!qa_application_network_qw_source_read(host->frontend->application, &frame->source, error)) return false;
    uint32_t cursor = 0; bool present;
    for (;;) {
        qa_application_network_qw_client client;
        if (!qa_application_network_qw_client_next(host->frontend->application, &cursor, &present, &client, error)) return false;
        if (!present) break;
        frame->clients[frame->client_count++] = client;
    }
    cursor = 0;
    for (;;) {
        qa_actor_id actor; qa_application_network_qw_entity entity;
        if (!qa_application_network_qw_entity_next(host->frontend->application, &cursor, &present, &actor, &entity, error)) return false;
        if (!present) break;
        frame->entities[frame->entity_count] = entity; frame->actors[frame->entity_count++] = actor;
    }
    qa_application_network_qw_source after;
    if (!qa_application_network_qw_source_read(host->frontend->application, &after, error)) return false;
    return (after.owner == frame->source.owner && after.source_time_ns == frame->source.source_time_ns &&
        after.completed_time_ns == frame->source.completed_time_ns && after.entity_count == frame->source.entity_count) ||
        frontend_fail(error, QA_ERROR_ARGUMENT, "QuakeWorld immutable physical frame changed during capture");
}
static bool flush_events(frontend_qw_host *host, qa_net_writer *datagram,
    qw_frontend_peer *only, qa_error *error)
{
    qa_application *app = host->frontend->application;
    uint64_t first = qa_application_events_local_first(app);
    uint64_t next = qa_application_events_next(app);
    uint64_t generation = qa_application_protocol_events_generation(app);
    if (only && host->event_generation != generation) host->event_generation = generation;
    if (!only && host->reliable_generation != generation) host->reliable_generation = generation;
    if (only && host->event_cursor < first) host->event_cursor = first;
    if (!only && host->reliable_cursor < first) host->reliable_cursor = first;
    uint64_t cursor = only ? host->event_cursor : host->reliable_cursor;
    for (uint64_t i = cursor; i < next; ++i) {
        qa_application_protocol_event event;
        if (!qa_application_protocol_event_at(app, i, &event)) continue;
        if (event.provider != host->owner || event.signon) continue;
        for (size_t j = 0; j < QW_CLIENTS; ++j) {
            qw_frontend_peer *peer = host->peers + j;
            if (!peer->occupied || peer->retiring || (only && only != peer) ||
                (!peer->begun && !event.recipient.registry) || (only && event.reliable) || (!only && !event.reliable)) continue;
            qa_actor_id actor; bool receives;
            if (!peer_actor(peer, &actor, error) || !qa_application_network_qw_receives(app, actor, &event, &receives, error)) return false;
            if (!receives) continue;
            if (event.reliable && !only) {
                qa_network_qw_server_state state;
                if (!qa_network_qw_server_state_read(host->runtime, peer->client, &state, error)) return false;
                (void)qa_event_receipts_retired(&peer->event_receipts, state.reliable_acknowledged);
                if (qa_event_receipts_full(&peer->event_receipts)) {
                    if (!source_drop(peer, peer->client, "Reliable event overflow", error)) return false;
                    continue;
                }
                if (!peer_reliable(peer, event.payload, error)) return false;
                if (peer->retiring) continue;
                if (!qa_network_qw_server_state_read(host->runtime, peer->client, &state, error)) return false;
                if (!qa_event_receipts_submit(&peer->event_receipts, i, i + 1, state.reliable_queued)) {
                    if (!source_drop(peer, peer->client, "Reliable event overflow", error)) return false;
                }
            } else if (!event.reliable && datagram && !qa_net_write_data(datagram, event.payload.data, event.payload.size)) return false;
        }
    }
    if (!only) for (size_t p = 0; p < QW_CLIENTS; ++p) {
        qw_frontend_peer *peer = host->peers + p;
        if (peer->occupied && !peer->retiring && peer->begun)
            (void)qa_event_receipts_submit(&peer->event_receipts, peer->event_receipts.through, next, 0);
    }
    return true;
}
uint64_t frontend_qw_events_retired(frontend_qw_host *host)
{
    if (!host) return UINT64_MAX;
    uint64_t retired = qa_application_events_next(host->frontend->application);
    for (size_t i = 0; i < QW_CLIENTS; ++i) {
        qw_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring) continue;
        if (!peer->event_receipts.through)
            qa_event_receipts_reset(&peer->event_receipts, qa_application_events_local_first(host->frontend->application));
        qa_network_qw_server_state state = {0};
        (void)qa_network_qw_server_state_read(host->runtime, peer->client, &state, NULL);
        uint64_t first = qa_event_receipts_retired(&peer->event_receipts, state.reliable_acknowledged);
        if (!peer->begun && !qa_event_receipts_count(&peer->event_receipts)) continue;
        if (first < retired) retired = first;
    }
    return retired;
}
static bool source_actions(frontend_qw_host *host, qa_error *error)
{
    while (host->first_action) {
        qw_source_action *action = host->first_action;
        host->first_action = action->next; if (!host->first_action) host->last_action = NULL; --host->action_count;
        qw_frontend_peer *peer = NULL;
        for (size_t i = 0; i < QW_CLIENTS; ++i) if (host->peers[i].occupied && !host->peers[i].retiring &&
            qa_net_client_id_equal(host->peers[i].client, action->client)) peer = host->peers + i;
        qa_actor_id actor; bool current = peer && peer->seat.owner == action->seat.owner && peer->seat.index == action->seat.index &&
            qa_network_epoch(host->runtime, action->client) == action->epoch &&
            qa_application_remote_player_actor(host->frontend->application, action->client, action->seat, &actor) && qa_actor_id_equal(actor, action->actor);
        bool ok = !current || (qa_application_network_qw_flush(host->frontend->application, error) &&
            flush_events(host, NULL, NULL, error));
        if (ok && current) {
            host->reliable_cursor = qa_application_events_next(host->frontend->application);
            host->action_active = true; host->action_time_ns = action->received_ns; host->action_actor = actor;
            ok = qa_network_qw_server_command(host->runtime, peer->client, action->text, error) &&
                qa_application_network_qw_flush(host->frontend->application, error) && flush_events(host, NULL, NULL, error);
            host->action_active = false; host->action_time_ns = 0; host->action_actor = (qa_actor_id){0};
            if (ok) host->reliable_cursor = qa_application_events_next(host->frontend->application);
        }
        free(action->text); free(action);
        if (!ok) return false;
    }
    return true;
}
static int16_t source_short(float value)
{
    uint16_t bits = (uint16_t)(uint32_t)qa_source_float_to_i32(value);
    int16_t result; memcpy(&result, &bits, sizeof(result)); return result;
}
static qa_qw_source_player source_player(const qa_application_network_qw_client *client,
    const qa_application_network_qw_client *viewer, uint32_t model, uint64_t source_time)
{
    qa_qw_source_player player = {.slot = (uint8_t)(client->source_slot - 1), .flags = QA_QW_PF_MSEC | QA_QW_PF_COMMAND,
        .frame = client->entity.frame, .model = client->entity.model, .skin = client->entity.skin,
        .effects = client->entity.effects, .weapon_frame = client->weapon_frame};
    memcpy(player.origin, client->entity.origin, sizeof(player.origin)); memcpy(player.velocity, client->velocity, sizeof(player.velocity));
    if (player.model != model) player.flags |= QA_QW_PF_MODEL;
    if (player.velocity[0] != 0) player.flags |= QA_QW_PF_VELOCITY1;
    if (player.velocity[1] != 0) player.flags |= QA_QW_PF_VELOCITY2;
    if (player.velocity[2] != 0) player.flags |= QA_QW_PF_VELOCITY3;
    if (player.effects != 0) player.flags |= QA_QW_PF_EFFECTS;
    if (player.skin != 0) player.flags |= QA_QW_PF_SKIN;
    if (client->health <= 0) player.flags |= QA_QW_PF_DEAD;
    if (client->minimum[2] != -24) player.flags |= QA_QW_PF_GIB;
    if (client->spectator) {
        player.flags &= QA_QW_PF_VELOCITY1 | QA_QW_PF_VELOCITY2 | QA_QW_PF_VELOCITY3;
    } else if (qa_actor_id_equal(client->actor, viewer->actor)) {
        player.flags &= (uint16_t)~(QA_QW_PF_MSEC | QA_QW_PF_COMMAND);
        if (player.weapon_frame != 0) player.flags |= QA_QW_PF_WEAPONFRAME;
    }
    if (qa_actor_id_equal(viewer->spectator_track,client->actor) && player.weapon_frame!=0)
        player.flags |= QA_QW_PF_WEAPONFRAME;
    uint64_t age = client->command_present && source_time >= client->command_time_ns ?
        (source_time - client->command_time_ns) / UINT64_C(1000000) : 0;
    player.msec = age >= 255 ? 255 : (uint8_t)age;
    if (client->command_present) {
        player.command.msec = (uint8_t)client->command.milliseconds;
        player.command.angles[0] = client->command.angles.x; player.command.angles[1] = client->command.angles.y;
        player.command.angles[2] = client->command.angles.z;
        player.command.forward = source_short(client->command.forward_move);
        player.command.side = source_short(client->command.side_move); player.command.up = source_short(client->command.up_move);
    }
    if (client->health <= 0) { player.command.angles[0] = 0; player.command.angles[1] = client->entity.angles[1]; }
    return player;
}
static bool publish_entities(qw_frontend_peer *peer, const qw_physical_frame *physical,
    const qa_application_network_qw_client *viewer, qa_bytes pvs, qa_net_writer *writer, qa_error *error)
{
    frontend_qw_host *host = peer->host; qa_actor_id actor = viewer->actor;
    for (size_t i = 0; i < physical->client_count; ++i) {
        const qa_application_network_qw_client *client = physical->clients + i;
        if (!client->begun || (client->spectator && !qa_actor_id_equal(client->actor, actor) &&
            !qa_actor_id_equal(viewer->spectator_track,client->actor))) continue;
        bool visible;
        if (!qa_application_network_qw_visible(host->frontend->application, actor, client->actor, pvs, &visible, error)) return false;
        if (visible) {
            qa_qw_source_player player = source_player(client, viewer, host->player_model, physical->source.source_time_ns);
            if (!qa_qw_source_write_player(writer, &player)) return false;
        }
    }
    qa_qw_source_frame frame = {0}; qa_qw_nail nails[32]; size_t nail_count = 0;
    for (size_t i = 0; i < physical->entity_count; ++i) {
        const qa_application_network_qw_entity *entity = physical->entities + i; bool visible;
        if (!qa_application_network_qw_visible(host->frontend->application, actor, physical->actors[i], pvs, &visible, error)) return false;
        if (!visible) continue;
        if ((host->nail_model && entity->model == host->nail_model) || (host->supernail_model && entity->model == host->supernail_model)) {
            if (nail_count < 32) {
                qa_qw_nail *nail = nails + nail_count++;
                memcpy(nail->origin, entity->origin, sizeof(nail->origin)); nail->pitch = entity->angles[0]; nail->yaw = entity->angles[1];
            }
        } else if (frame.count < QA_QW_MAX_PACKET_ENTITIES) {
            qa_qw_source_entity *state = frame.entities + frame.count++;
            *state = (qa_qw_source_entity){.number = entity->number, .model = entity->model, .frame = entity->frame,
                .colormap = entity->colormap, .skin = entity->skin, .effects = entity->effects};
            memcpy(state->origin, entity->origin, sizeof(state->origin)); memcpy(state->angles, entity->angles, sizeof(state->angles));
        }
    }
    if (nail_count && !qa_qw_source_write_nails(writer, nails, nail_count)) return false;
    return qa_network_qw_server_frame(host->runtime, peer->client,
        (qa_bytes){writer->data, qa_net_writer_size(writer)}, &frame, error);
}
static bool publish_peer(qw_frontend_peer *peer, const qw_physical_frame *physical, qa_error *error)
{
    frontend_qw_host *host = peer->host; qa_actor_id actor;
    if (!peer_actor(peer, &actor, error)) return false;
    const qa_application_network_qw_client *viewer = NULL;
    for (size_t i = 0; i < physical->client_count; ++i) if (qa_actor_id_equal(physical->clients[i].actor, actor)) viewer = physical->clients + i;
    if (!viewer || !viewer->begun) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld begun peer is absent from immutable physical frame");
    uint8_t services[65507]; qa_net_writer writer; qa_net_writer_init(&writer, services, sizeof(services), error);
    if (!flush_events(host, &writer, peer, error)) return false;
    for (uint8_t i = 0; i < 16; ++i) if ((viewer->stat_mask & (UINT16_C(1) << i)) &&
        (!(peer->stat_mask & (UINT16_C(1) << i)) || peer->stats[i] != viewer->stats[i])) {
        uint8_t bytes[6]; qa_net_writer stat; qa_net_writer_init(&stat, bytes, sizeof(bytes), error);
        if (!qa_qw_source_write_stat(&stat, i, viewer->stats[i]) ||
            !peer_reliable(peer, (qa_bytes){bytes, qa_net_writer_size(&stat)}, error)) return false;
        peer->stats[i] = viewer->stats[i]; peer->stat_mask = (uint16_t)(peer->stat_mask | (UINT32_C(1) << i));
    }
    qa_vec3 eye = qa_v3(viewer->entity.origin[0] + viewer->view_offset[0],
        viewer->entity.origin[1] + viewer->view_offset[1], viewer->entity.origin[2] + viewer->view_offset[2]);
    if (!qa_vec_finite(eye)) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source eye exceeds finite spatial range");
    qa_collision_geometry *geometry = qa_world_geometry(qa_application_world(host->frontend->application));
    size_t extent = qa_collision_q1_pvs_bytes(geometry);
    uint8_t *bytes = extent ? malloc(extent) : NULL;
    if (extent && !bytes) return frontend_fail(error, QA_ERROR_MEMORY, "Observing QuakeWorld source fat PVS");
    bool ok = qa_collision_q1_fat_pvs(geometry,
        qa_world_trace_scratch(qa_application_world(host->frontend->application),geometry),eye, bytes, extent, error) &&
        publish_entities(peer, physical, viewer, (qa_bytes){bytes, extent}, &writer, error);
    free(bytes);
    return ok;
}
bool frontend_qw_publish(frontend_qw_host *host, qa_error *error)
{
    if (!host) return true;
    if (!frontend_qw_prepare(host, error) || !source_actions(host, error) || !frontend_qw_prepare(host, error)) return false;
    qw_physical_frame *frame = malloc(sizeof(*frame));
    if (!frame) return frontend_fail(error, QA_ERROR_MEMORY, "Capturing immutable QuakeWorld physical frame");
    bool ok = capture_frame(host, frame, error);
    if (ok) ok = qa_application_network_qw_flush(host->frontend->application, error) && flush_events(host, NULL, NULL, error);
    qa_application_network_qw_world world;
    if (ok) ok = source_world(host, &world, error);
    for (size_t i = 0; ok && i < 64; ++i) if (strcmp(host->styles[i], world.lightstyles[i])) {
        char *copy = text_copy(world.lightstyles[i], error); if (!copy) { ok = false; break; }
        ok = broadcast(host, &(qa_qw_service){.kind = QA_QW_LIGHT_STYLE, .data.light_style = {(uint8_t)i, copy}}, error);
        if (ok) { free(host->styles[i]); host->styles[i] = copy; } else free(copy);
    }
    bool paused = qa_application_q1_paused(host->frontend->application);
    if (ok && host->previous_pause != paused) {
        ok = broadcast(host, &(qa_qw_service){.kind = QA_QW_PAUSE, .data.paused = paused}, error);
        if (ok) host->previous_pause = paused;
    }
    for (size_t i = 0; ok && i < QW_CLIENTS; ++i) {
        qw_frontend_peer *peer = host->peers + i;
        if (!peer->occupied || peer->retiring || !peer->begun) continue;
        for (size_t j = 0; ok && j < frame->client_count; ++j) if (frame->clients[j].source_slot == i + 1 && peer->frags != frame->clients[j].frags) {
            ok = broadcast(host, &(qa_qw_service){.kind = QA_QW_FRAGS,
                .data.score = {(uint8_t)i, source_short(frame->clients[j].frags)}}, error);
            if (ok) peer->frags = frame->clients[j].frags;
        }
    }
    for (size_t i = 0; ok && i < QW_CLIENTS; ++i) {
        qw_frontend_peer *peer = host->peers + i;
        if (peer->occupied && !peer->retiring && peer->begun) ok = publish_peer(peer, frame, error);
    }
    if (ok) {
        host->event_cursor = qa_application_events_next(host->frontend->application);
        host->reliable_cursor = host->event_cursor; host->published_time_ns = frame->source.source_time_ns;
        host->event_generation = host->reliable_generation = qa_application_protocol_events_generation(host->frontend->application);
    }
    free(frame); return ok;
}

bool frontend_qw_source_hooks(frontend_qw_host *host, const qa_net_client *client,
    qa_network_qw_server_policy *policy, qa_network_qw_server_hooks *hooks,
    qa_qw_download_admission *downloads, qa_error *error)
{
    if (!host || !client || !policy || !hooks || !downloads ||
        (client->attachment != QA_NET_REMOTE && client->attachment != QA_NET_LOCAL_SEAT) ||
        client->protocol.kind != QA_NET_QW28 || client->protocol.flags || client->protocol.revision || client->seat_count != 1 ||
        client->seats[0].remote_index || client->composition != host->composition)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld restored source changes its admitted protocol or composition");
    qw_frontend_peer *peer = NULL; size_t index = 0;
    for (size_t i = 0; i < QW_CLIENTS; ++i) if (host->peers[i].occupied && qa_net_client_id_equal(host->peers[i].client, client->id)) { peer = host->peers + i; index = i; }
    if (!peer || peer->host != host || client->seats[0].seat.owner != peer->seat.owner || client->seats[0].seat.index != peer->seat.index)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld restored source has no declared physical peer and seat");
    size_t cursor = 0; qa_application_network_player row; bool found = false;
    while (qa_application_network_player_next(host->frontend->application, &cursor, &row)) {
        if (!qa_net_client_id_equal(row.client, client->id) || row.seat.owner != peer->seat.owner || row.seat.index != peer->seat.index) continue;
        if (found || row.client_slot != index || row.application_seat != peer->seat.index || (!peer->retiring && row.retiring))
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld physical peer changes its genuine canonical roster row");
        if (!peer->retiring) {
            qa_application_network_qw_client source; const char *info;
            if (!qa_application_network_qw_client_read(host->frontend->application, row.actor, &source, error) ||
                source.source_slot != index + 1 || source.spectator != peer->spectator ||
                (client->attachment == QA_NET_REMOTE && source.begun != peer->begun) ||
                !qa_application_network_qw_userinfo_read(host->frontend->application, row.actor, &info, error) || strcmp(info, peer->userinfo))
                return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld restored canonical/source role, Begin or raw userinfo differs");
        }
        found = true;
    }
    if (!found) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld restored source lacks its genuine canonical player");
    *policy = (qa_network_qw_server_policy){peer->qport, peer->rate, QW_MESSAGE, 5};
    *hooks = frontend_qw_peer_hooks(peer);
    *downloads = (qa_qw_download_admission){.maximum_bytes = INT32_MAX,
        .content = qa_application_network_qw_content(host->frontend->application, error)};
    return downloads->content != NULL;
}
bool frontend_qw_qualified(const frontend_qw_host *host, bool complete, qa_error *error)
{
    if (!host || !host->frontend || !host->runtime || !host->admin || !frontend_qw_idle(host) || host->action_active || host->action_time_ns ||
        host->action_actor.registry || host->action_actor.generation || host->action_actor.slot ||
        host->generation != qa_application_configuration_generation(host->frontend->application) || !host->owner ||
        !host->active_limit || host->active_limit > 32 || host->server_count < 1 ||
        host->baseline_count < 32 || host->baseline_count > 511 || host->pending_count > QW_PENDING || host->action_count > QW_ACTIONS ||
        host->model_count > 255 || host->sound_count > 255 || (host->signon_count && (!host->signon || !host->signon_views)))
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld continuation lacks its actual source factory inventory");
    qa_application_network_qw_world world;
    if (!qa_application_network_qw_world_read(host->frontend->application, &world, error) || world.source.owner != host->owner ||
        host->published_time_ns > world.source.source_time_ns) return false;
    if (complete) {
        uint64_t generation = qa_application_protocol_events_generation(host->frontend->application);
        if (host->event_generation > generation || host->reliable_generation > generation)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source publication generations differ from restored actual event batch");
    }
    const char *models[255], *sounds[255]; size_t model_count, sound_count; uint32_t checksum;
    if (!qa_application_network_qw_precache(host->frontend->application, true, models, &model_count, error) ||
        !qa_application_network_qw_precache(host->frontend->application, false, sounds, &sound_count, error) ||
        model_count != host->model_count || sound_count != host->sound_count || !qa_qw_map_checksum2(world.map_bytes, &checksum, error) || checksum != host->checksum)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld continuation changes actual source precache or admitted map bytes");
    for (size_t i = 0; i < 255; ++i) {
        if ((i < model_count && (!host->models[i] || strcmp(host->models[i], models[i]))) || (i >= model_count && host->models[i]) ||
            (i < sound_count && (!host->sounds[i] || strcmp(host->sounds[i], sounds[i]))) || (i >= sound_count && host->sounds[i]))
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld immutable factory names differ from source indices");
    }
    uint32_t player = 0, nail = 0, supernail = 0;
    for (size_t i = 0; i < model_count; ++i) {
        if (!strcmp(models[i], "progs/player.mdl")) player = (uint32_t)i + 1;
        if (!strcmp(models[i], "progs/spike.mdl")) nail = (uint32_t)i + 1;
        if (!strcmp(models[i], "progs/s_spike.mdl")) supernail = (uint32_t)i + 1;
    }
    if (host->player_model != player || host->nail_model != nail || host->supernail_model != supernail)
        return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld compressed model indices differ from real source precache");
    for (size_t i = 0; i < 64; ++i) if (!host->styles[i]) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld lightstyle cache owner is absent");
    for (size_t i = 0; i < host->baseline_count; ++i) {
        const qa_qw_source_entity *v = host->baselines + i;
        if (!v->number || v->number > 511 || (i && host->baselines[i - 1].number >= v->number) || v->effects != 0 || v->solid)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld immutable baseline identity differs from its physical source factory");
        if (i < 32 && (v->number != i + 1 || v->model != host->player_model || v->colormap != (double)i + 1 || v->frame != 0 || v->skin != 0 ||
            v->origin[0] != 0 || v->origin[1] != 0 || v->origin[2] != 0 || v->angles[0] != 0 || v->angles[1] != 0 || v->angles[2] != 0))
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld reserved client baseline changes its true constructor fields");
    }
    size_t occupied = 0;
    for (size_t i = 0; i < QW_CLIENTS; ++i) {
        const qw_frontend_peer *peer = host->peers + i;
        if (!peer->occupied) {
            if (peer->host || peer->retiring || peer->begun || peer->userinfo)
                return frontend_fail(error, QA_ERROR_FORMAT, "Absent QuakeWorld peer retains actual source ownership");
            continue;
        }
        ++occupied;
        if (peer->host != host || !peer->client.generation || peer->client.owner != QA_NETWORK_COMMAND_OWNER ||
            peer->seat.owner != QA_NETWORK_COMMAND_OWNER || !peer->userinfo ||
            peer->rate < 500 || peer->rate > 10000 || peer->input_sequence > INT32_MAX)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld retained peer has invalid full source admission");
        if (complete) {
            const qa_net_client *client = qa_net_connections_get(qa_network_connections(host->runtime), peer->client);
            qa_network_qw_server_state state; qa_network_qw_server_policy policy; qa_network_qw_server_hooks hooks; qa_qw_download_admission downloads;
            if (!frontend_qw_source_hooks((frontend_qw_host *)host, client, &policy, &hooks, &downloads, error) ||
                !qa_network_qw_server_state_read(host->runtime, peer->client, &state, error) || state.input_sequence != peer->input_sequence ||
                state.qport != peer->qport || state.active != peer->begun || state.retiring != peer->retiring) return false;
            if (peer->begun && !peer->retiring) {
                qa_actor_id actor; qa_application_network_qw_client source; bool present; uint64_t accepted;
                if (!qa_application_remote_player_actor(host->frontend->application, peer->client, peer->seat, &actor) ||
                    !qa_application_network_qw_client_read(host->frontend->application, actor, &source, error) ||
                    !qa_network_accepted_sequence(host->runtime, peer->client, peer->seat, &present, &accepted, error) ||
                    present != (peer->input_sequence != 0) || (present && (accepted != peer->input_sequence ||
                        !source.command_present || source.command.sequence != accepted || source.command_time_ns != peer->command_time_ns))) return false;
            }
        }
    }
    if (complete) {
        uint32_t cursor = 0; const qa_net_client *client; size_t count = 0;
        while (qa_net_connections_next(qa_network_connections(host->runtime), &cursor, &client)) ++count;
        if (count != occupied) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld runtime has an undeclared physical peer");
    }
    size_t action_count = 0; const qw_source_action *last = NULL;
    for (const qw_source_action *action = host->first_action; action; action = action->next) {
        if (++action_count > QW_ACTIONS || !action->text || !action->client.generation || !action->actor.registry || !action->epoch)
            return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld retained source action lacks its authored admission");
        last = action;
    }
    if (action_count != host->action_count || last != host->last_action) return frontend_fail(error, QA_ERROR_FORMAT, "QuakeWorld source action allocator differs");
    return true;
}
