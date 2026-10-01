/* id Software cg_servercmds.c and cg_main.c; GPL-2.0-or-later. */
#include "server_commands_internal.h"
#include "qa/game_q3_wire.h"
#include "qa/common_parse.h"
#include "weapon.h"

bool q3nc_fail(qa_error *e, qa_status status, const char *text)
{ qa_error_set(e, status, 0, "%s", text); return false; }
void q3nc_copy(char *out, size_t capacity, const char *text)
{
    size_t count = strlen(text);
    if (count >= capacity) count = capacity - 1;
    memcpy(out, text, count); out[count] = 0;
}
bool q3nc_same(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
static int32_t word(uint32_t value)
{ return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value); }
int32_t q3nc_add(int32_t a, int32_t b) { return word((uint32_t)a + (uint32_t)b); }
int32_t q3nc_sub(int32_t a, int32_t b) { return word((uint32_t)a - (uint32_t)b); }
int32_t q3nc_integer(const char *text)
{
    while (*text && (signed char)*text <= 32) ++text;
    bool negative = *text == '-';
    if (*text == '+' || *text == '-') ++text;
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10u + (uint32_t)(*text++ - '0');
    return word(negative ? 0u - value : value);
}
static bool identity(const qa_application_q3_client_context *a,
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
bool q3nc_current(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    qa_application_q3_client_context client;
    qa_native_q3_wire_basis wire;
    qa_native_q3_wire_publication publication;
    const qa_native_q3_client_services *services = o ? qa_native_q3_client_services_read(o->options.client) : NULL;
    if (!o || !f || o->closed || f->application != o->options.application ||
        f->source.product != o->options.product || f->source.content != o->options.content ||
        f->source.publication_generation != o->options.publication_generation ||
        f->source.map_revision != o->options.map_revision ||
        f->source.session != o->options.recipient.session ||
        f->source.source_owner != o->options.recipient.source_owner ||
        f->seat != o->options.recipient.seat || f->viewing_client != o->options.recipient.source_client ||
        !qa_actor_id_equal(f->viewing_actor, o->options.recipient.source_actor) ||
        f->assets != o->options.assets || f->presentation != o->options.presentation ||
        f->media != o->options.media || f->clients != o->options.clients || f->events != o->options.events ||
        !qa_application_native_q3_presentation_current(f->application, &f->source) ||
        !services || services->wire_reader != o->options.reader ||
        !qa_native_q3_client_context_read(o->options.client, &client, e) ||
        !identity(&client, &o->options.recipient) ||
        !qa_native_q3_wire_reader_basis(o->options.reader, &wire, e) ||
        wire.application != f->application || wire.session != f->source.session ||
        wire.source_game != f->source.source_game || wire.source_cvars != client.source_cvars ||
        wire.source_owner != client.source_owner || wire.receiver != client.receiver ||
        wire.product != f->source.product || wire.seat != f->seat ||
        wire.physical_client != f->viewing_client || !qa_actor_id_equal(wire.actor, f->viewing_actor) ||
        wire.publication_generation != f->source.publication_generation || wire.map_revision != f->source.map_revision ||
        !qa_native_q3_wire_reader_current(o->options.reader) ||
        (!o->initialized && o->busy &&
            (!qa_native_q3_wire_reader_publication(o->options.reader, &publication, e) ||
             !publication.has_gamestate || publication.reached_command_sequence != o->state.server_command_sequence)) ||
        !o->options.current(o->options.context, f, &client))
        return q3nc_fail(e, QA_ERROR_ARGUMENT, "Native CGAME command owner left its physical source, recipient or service generation");
    return true;
}
bool q3nc_cvar(q3n_server_commands *o, const char *symbol,
    qa_native_q3_client_cvar *out, qa_error *e)
{ return qa_native_q3_client_cvar_read(o->options.client, symbol, out, e); }
static bool integer_cvar(q3n_server_commands *o, const char *symbol, int32_t *out, qa_error *e)
{
    qa_native_q3_client_cvar value;
    if (!q3nc_cvar(o, symbol, &value, e)) return false;
    *out = value.integer; return true;
}
static bool set(q3n_server_commands *o, const q3n_frame *f, const char *name, const char *value, qa_error *e)
{
    return q3nc_current(o, f, e) &&
        qa_cvars_set(o->options.recipient.cvars, name, value, true, e) && q3nc_current(o, f, e);
}
static bool set_number(q3n_server_commands *o, const q3n_frame *f, const char *name, int32_t value, qa_error *e)
{ char text[16]; snprintf(text, sizeof(text), "%d", value); return set(o, f, name, text, e); }
bool q3nc_sound(q3n_server_commands *o, const q3n_frame *f, int32_t sound, int32_t channel, qa_error *e)
{
    return q3nc_current(o, f, e) && qa_q3_presentation_sound(o->options.presentation,
        sound, NULL, (int32_t)f->viewing_client, channel, true, e) && q3nc_current(o, f, e);
}
static bool sound_field(q3n_server_commands *o, const q3n_frame *f, q3n_sound field, int32_t channel, qa_error *e)
{
    const q3n_media_view *media = q3n_media_read(o->options.media);
    return media ? q3nc_sound(o, f, media->sounds[field], channel, e) :
        q3nc_fail(e, QA_ERROR_ARGUMENT, "Native command sound has no idle media owner");
}
bool q3nc_message(q3n_server_commands *o, const q3n_frame *f, q3n_command_message_kind kind,
    const char *text, int32_t sender, const char *command, qa_error *e)
{
    qa_application_q3_client_context recipient;
    if (!q3nc_current(o, f, e) || !qa_native_q3_client_context_read(o->options.client, &recipient, e)) return false;
    q3n_command_message message = {.kind = kind, .frame = f, .recipient = &recipient,
        .text = text, .voice_command = command, .sender_client = sender};
    if (sender >= 0 && (uint32_t)sender < f->source.max_clients) {
        qa_application_native_q3_client client;
        if (!qa_application_native_q3_presentation_client(f->application, &f->source,
            (uint32_t)sender, &client, e)) return false;
        message.sender_present = client.present;
        if (client.present) message.sender_actor = client.binding.actor;
    }
    return o->options.message(o->options.context, &message, e) && q3nc_current(o, f, e);
}
static bool center(q3n_server_commands *o, const q3n_frame *f, const char *text,
    int32_t y, int32_t width, qa_error *e)
{
    qa_application_q3_client_context recipient;
    return q3nc_current(o, f, e) && qa_native_q3_client_context_read(o->options.client, &recipient, e) &&
        o->options.center_print(o->options.context, f, &recipient,
        text, y, width, e) && q3nc_current(o, f, e);
}
bool q3n_server_commands_idle(const q3n_server_commands *o) { return o && !o->busy; }
const q3n_command_state *q3n_server_commands_state(const q3n_server_commands *o)
{ return q3n_server_commands_idle(o) ? &o->state : NULL; }
bool q3n_server_commands_create(const q3n_server_command_options *options,
    q3n_server_commands **out, qa_error *e)
{
    const qa_native_q3_client_services *services = options ? qa_native_q3_client_services_read(options->client) : NULL;
    if (!options || !out || *out || !options->application || !options->client || !options->reader ||
        !services || services->wire_reader != options->reader ||
        !options->recipient.native_source || !options->recipient.receiver ||
        !options->recipient.source_owner || !options->recipient.service_owner ||
        !options->content || !options->assets || !options->presentation || !options->clients ||
        !options->media || !options->events || !options->current || !options->read_command ||
        !options->receipt_current || !options->message || !options->center_print ||
        !options->client_settings || !options->loading || !options->initialize_stage ||
        !options->clear_particles || !options->memory_remaining ||
        (options->product != QA_Q3_ARENA && options->product != QA_Q3_TEAM_ARENA) ||
        (options->product == QA_Q3_TEAM_ARENA && (!options->score_selection || !options->response_head)))
        return q3nc_fail(e, QA_ERROR_ARGUMENT, "Native commands require actual retained CGAME services and frontend producers");
    q3n_server_commands *o = calloc(1, sizeof(*o));
    if (!o) return q3nc_fail(e, QA_ERROR_MEMORY, "Allocating native CGAME server commands");
    o->options = *options;
    for (unsigned i = 0; i < 8; ++i) o->voice.lists[i].gender = QA_MODEL_MALE;
    *out = o; return true;
}
void q3n_server_commands_destroy(q3n_server_commands *o) { if (q3n_server_commands_idle(o)) free(o); }
static bool begin(q3n_server_commands *o, const q3n_frame *f, bool initialized, qa_error *e)
{
    qa_native_q3_wire_publication publication;
    if (!q3n_server_commands_idle(o) || (initialized && !o->initialized) || !q3nc_current(o, f, e))
        return q3nc_fail(e, QA_ERROR_ARGUMENT, "Native command call requires its idle current constructor state");
    if (initialized && (!qa_native_q3_wire_reader_publication(o->options.reader, &publication, e) ||
        !publication.has_gamestate || publication.reached_command_sequence != o->state.server_command_sequence))
        return q3nc_fail(e, QA_ERROR_ARGUMENT, "Native command continuation differs from its actual reliable reader");
    o->busy = true; return true;
}
static bool end(q3n_server_commands *o, bool ok)
{ o->busy = false; if (!ok) o->closed = true; return ok; }
static bool config(q3n_server_commands *o, const q3n_frame *f, uint32_t index,
    char **out, uint64_t *revision, qa_error *e)
{
    const char *text;
    if (!q3nc_current(o, f, e) || !qa_native_q3_wire_reader_configstring(
        o->options.reader, index, &text, revision, e)) return false;
    size_t size = strlen(text);
    if (size >= QA_Q3_GAMESTATE_CHARS) return q3nc_fail(e, QA_ERROR_FORMAT, "CG_ConfigString exceeds its source gamestate buffer");
    *out = malloc(size + 1);
    if (!*out) return q3nc_fail(e, QA_ERROR_MEMORY, "Retaining native CGAME configstring callback text");
    memcpy(*out, text, size + 1); return true;
}
static bool config_current(q3n_server_commands *o, const q3n_frame *f,
    uint32_t index, uint64_t revision, qa_error *e)
{
    const char *text; uint64_t actual;
    return q3nc_current(o, f, e) && qa_native_q3_wire_reader_configstring(
        o->options.reader, index, &text, &actual, e) &&
        (actual == revision || q3nc_fail(e, QA_ERROR_ARGUMENT, "CGAME configstring changed during its authored callback"));
}
static bool config_number(q3n_server_commands *o, const q3n_frame *f, uint32_t index, int32_t *out, qa_error *e)
{
    char *text = NULL; uint64_t revision;
    if (!config(o, f, index, &text, &revision, e)) return false;
    *out = q3nc_integer(text); free(text); return true;
}
static bool info_value(const char *info, const char *key, char *out, size_t capacity, qa_error *e)
{ return qa_q3_info_value(info, key, out, capacity, e); }
static bool server_info(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    char *text = NULL, value[8192]; uint64_t revision;
    if (!config(o, f, 0, &text, &revision, e)) return false;
    q3n_command_state *s = &o->state;
    bool ok = info_value(text, "g_gametype", value, sizeof(value), e);
    if (ok) {
        s->game_type = q3nc_integer(value);
        ok = s->game_type >= 0 && s->game_type <= 7;
        if (!ok) q3nc_fail(e, QA_ERROR_FORMAT, "Invalid source CGAME server game type");
    }
    if (ok) ok = set_number(o, f, "g_gametype", s->game_type, e);
    const char *keys[] = {"dmflags", "teamflags", "fraglimit", "capturelimit", "timelimit", "sv_maxclients"};
    int32_t *fields[] = {&s->dm_flags, &s->team_flags, &s->fraglimit, &s->capturelimit, &s->timelimit, &s->max_clients};
    for (unsigned i = 0; ok && i < 6; ++i) {
        ok = info_value(text, keys[i], value, sizeof(value), e);
        if (ok) *fields[i] = q3nc_integer(value);
    }
    if (ok) ok = info_value(text, "mapname", value, sizeof(value), e);
    if (ok) { char map[8202]; snprintf(map, sizeof(map), "maps/%s.bsp", value); q3nc_copy(s->mapname, sizeof(s->mapname), map); }
    if (ok) ok = info_value(text, "g_redTeam", value, sizeof(value), e);
    if (ok) { q3nc_copy(s->red_team, sizeof(s->red_team), value); ok = set(o, f, "g_redTeam", s->red_team, e); }
    if (ok) ok = info_value(text, "g_blueTeam", value, sizeof(value), e);
    if (ok) { q3nc_copy(s->blue_team, sizeof(s->blue_team), value); ok = set(o, f, "g_blueTeam", s->blue_team, e); }
    if (ok) ok = config_current(o, f, 0, revision, e);
    free(text); return ok;
}
static bool flag_status(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    char *text = NULL; uint64_t revision;
    if (!config(o, f, 23, &text, &revision, e)) return false;
    bool ok = true;
    if (o->state.game_type == 4) {
        if (!*text) ok = q3nc_fail(e, QA_ERROR_FORMAT, "CTF flag status leaves source bytes uninitialized");
        else { o->state.red_flag = (unsigned char)text[0] - '0'; o->state.blue_flag = (unsigned char)text[1] - '0'; }
    } else if (o->options.product == QA_Q3_TEAM_ARENA && o->state.game_type == 5)
        o->state.flag_status = (unsigned char)text[0] - '0';
    free(text); return ok;
}
static bool initial_config(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    return config_number(o, f, 6, &o->state.scores1, e) && config_number(o, f, 7, &o->state.scores2, e) &&
        config_number(o, f, 21, &o->state.level_start_time, e) && flag_status(o, f, e) &&
        config_number(o, f, 5, &o->state.warmup, e);
}
static bool music(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    char *text = NULL, intro[64], loop[64]; uint64_t revision;
    if (!config(o, f, 2, &text, &revision, e)) return false;
    qa_common_cursor cursor; qa_common_parser parser = {0};
    bool ok = qa_common_cursor_init(&cursor, (qa_bytes){(const uint8_t *)text, strlen(text)}, QA_COMMON_TERMINATED, e) &&
        qa_common_parse(&parser, &cursor, true, e);
    if (ok) { q3nc_copy(intro, sizeof(intro), parser.token); ok = qa_common_parse(&parser, &cursor, true, e); }
    if (ok) {
        q3nc_copy(loop, sizeof(loop), parser.token);
        ok = qa_q3_presentation_music(o->options.presentation, intro, loop, e) && config_current(o, f, 2, revision, e);
    }
    free(text); return ok;
}
static bool shader_state(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    char *text = NULL; uint64_t revision;
    if (!config(o, f, 24, &text, &revision, e)) return false;
    bool ok = true; const char *position = text;
    while (*position && ok) {
        const char *equal = strchr(position, '='), *colon, *end;
        if (!equal || !(colon = strchr(equal + 1, ':')) || !(end = strchr(colon + 1, '@'))) break;
        size_t a = (size_t)(equal - position), b = (size_t)(colon - equal - 1), c = (size_t)(end - colon - 1);
        if (a >= 64 || b >= 64 || c >= 16) { ok = q3nc_fail(e, QA_ERROR_FORMAT, "Shader remap exceeds donor scratch storage"); break; }
        char original[64], replacement[64], time[16];
        memcpy(original, position, a); original[a] = 0;
        memcpy(replacement, equal + 1, b); replacement[b] = 0;
        memcpy(time, colon + 1, c); time[c] = 0;
        ok = qa_q3_presentation_remap(o->options.presentation, original, replacement, (float)atof(time), e) &&
            config_current(o, f, 24, revision, e);
        position = end + 1;
    }
    free(text); return ok;
}
static void spectators(q3n_server_commands *o)
{
    q3n_command_state *s = &o->state; s->spectator_list[0] = 0;
    for (uint32_t i = 0; i < 64; ++i) {
        const q3n_client_info *ci = q3n_clients_get(o->options.clients, i);
        if (ci && ci->info_valid && ci->team == 3) {
            size_t used = strlen(s->spectator_list), available = sizeof(s->spectator_list) - used;
            if (available > 1) snprintf(s->spectator_list + used, available, "%s     ", ci->name);
        }
    }
    int32_t length = (int32_t)strlen(s->spectator_list);
    if (s->spectator_length != length) { s->spectator_length = length; s->spectator_width = -1; }
}
static bool settings(q3n_server_commands *o, const q3n_frame *f, bool loading,
    q3n_client_settings *out, qa_error *e)
{
    return o->options.client_settings(o->options.context, f, loading, out, e) &&
        out->loading == loading && q3nc_current(o, f, e);
}
static bool loading(q3n_server_commands *o, const q3n_frame *f, const char *text, int32_t item, qa_error *e)
{ return o->options.loading(o->options.context, f, text, item, e) && q3nc_current(o, f, e); }
typedef struct load_context { q3n_server_commands *owner; const q3n_frame *frame; } load_context;
static bool media_loading(void *context, const char *text, int32_t item, qa_error *e)
{ load_context *load = context; return loading(load->owner, load->frame, text, item, e); }
static bool stage(q3n_server_commands *o, const q3n_frame *f, q3n_command_init_stage which,
    uint32_t *inline_models, qa_error *e)
{
    return o->options.initialize_stage(o->options.context, f, which, o->state.mapname, -1, inline_models, e) &&
        q3nc_current(o, f, e);
}
static bool register_client(q3n_server_commands *o, const q3n_frame *f,
    uint32_t number, uint32_t *inline_models, const q3n_client_settings *client_settings, qa_error *e)
{
    return o->options.initialize_stage(o->options.context, f, Q3N_INIT_CLIENT_LOADING,
        o->state.mapname, (int32_t)number, inline_models, e) && q3nc_current(o, f, e) &&
        q3n_clients_register_one(o->options.clients, f->application, &f->source, client_settings, number, e) &&
        q3nc_current(o, f, e);
}
bool q3n_server_commands_initialize(q3n_server_commands *o, const q3n_frame *f,
    int32_t sequence, qa_error *e)
{
    if (sequence < 0 || !begin(o, f, false, e)) return false;
    if (o->initialized) return end(o, q3nc_fail(e, QA_ERROR_ARGUMENT, "CGAME constructor has already completed"));
    qa_native_q3_wire_publication publication;
    if (!qa_native_q3_wire_reader_publication(o->options.reader, &publication, e)) return end(o, false);
    if (!publication.has_gamestate || publication.initial_command_sequence != sequence ||
        publication.reached_command_sequence != sequence)
        return end(o, q3nc_fail(e, QA_ERROR_ARGUMENT, "CGAME constructor requires its real unreached gamestate baseline"));
    o->state.server_command_sequence = sequence;
    uint32_t inline_models = 0;
    bool mission = o->options.product == QA_Q3_TEAM_ARENA;
    bool ok = q3n_media_loading_graphics(o->options.media, e) && q3nc_current(o, f, e) &&
        qa_native_q3_client_register(o->options.client, e) && q3nc_current(o, f, e) &&
        stage(o, f, Q3N_INIT_CONSOLE_COMMANDS, &inline_models, e);
    if (ok) {
        if (!f->weapons || !q3n_weapons_idle(f->weapons)) ok = q3nc_fail(e, QA_ERROR_ARGUMENT, "CG_Init weapon selection requires its actual native child");
        else { q3n_weapons_set_selected(f->weapons, 2, 0);
            o->state.red_flag = o->state.blue_flag = o->state.flag_status = -1; }
    }
    char *version = NULL; uint64_t revision;
    if (ok) ok = config(o, f, 20, &version, &revision, e);
    if (ok && strcmp(version, "baseq3-1")) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Client/Server game mismatch: baseq3-1/%s", version); ok = false;
    }
    free(version);
    if (ok) ok = config_number(o, f, 21, &o->state.level_start_time, e) && server_info(o, f, e) &&
        loading(o, f, "collision map", -1, e) && stage(o, f, Q3N_INIT_COLLISION_MAP, &inline_models, e);
    if (ok && mission) ok = stage(o, f, Q3N_INIT_STRING_TABLE, &inline_models, e);
    load_context callbacks = {o, f};
    q3n_media_load load = {.application = f->application, .source = &f->source,
        .reader = o->options.reader,
        .context = &callbacks, .loading = media_loading, .game_type = o->state.game_type, .inline_models = inline_models};
    int32_t build;
    if (ok) ok = integer_cvar(o, "cg_buildScript", &build, e);
    if (ok) { load.build_script = build != 0; ok = loading(o, f, "sounds", -1, e); }
    if (ok && mission) ok = q3n_voice_load(o, f, e);
    if (ok) ok = q3n_media_load_sounds(o->options.media, &load, e) && q3nc_current(o, f, e) &&
        loading(o, f, "graphics", -1, e) && q3n_media_load_graphics(o->options.media, &load, e) &&
        q3nc_current(o, f, e) && stage(o, f, Q3N_INIT_PARTICLES, &inline_models, e) &&
        loading(o, f, "clients", -1, e);
    q3n_client_settings client_settings;
    if (ok) ok = settings(o, f, true, &client_settings, e) &&
        register_client(o, f, f->viewing_client, &inline_models, &client_settings, e);
    for (uint32_t i = 0; ok && i < 64; ++i) {
        if (i == f->viewing_client) continue;
        char *text = NULL; uint64_t client_revision;
        ok = config(o, f, 544u + i, &text, &client_revision, e);
        if (ok && text[0]) ok = register_client(o, f, i, &inline_models, &client_settings, e);
        free(text);
    }
    if (ok) ok =
        q3n_clients_sync(o->options.clients, f->application, &f->source, &client_settings, e) && q3nc_current(o, f, e);
    if (ok) spectators(o);
    if (ok && mission) ok = stage(o, f, Q3N_INIT_MISSION_ASSETS, &inline_models, e) &&
        stage(o, f, Q3N_INIT_HUD_MENU, &inline_models, e);
    if (ok) {
        q3n_events_round(o->options.events);
        ok = initial_config(o, f, e) && music(o, f, e) && loading(o, f, "", -1, e);
    }
    if (ok && mission) ok = stage(o, f, Q3N_INIT_TEAM_CHAT, &inline_models, e);
    if (ok) ok = shader_state(o, f, e) && qa_q3_presentation_clear_loops(o->options.presentation, true, e) && q3nc_current(o, f, e);
    if (ok) o->initialized = true;
    return end(o, ok);
}
bool q3nc_team_chat(q3n_server_commands *o, const q3n_frame *f, const char *input, qa_error *e)
{
    int32_t height, duration;
    if (!integer_cvar(o, "cg_teamChatHeight", &height, e) || !integer_cvar(o, "cg_teamChatTime", &duration, e)) return false;
    if (height > 8) height = 8;
    q3n_command_state *s = &o->state;
    if (height <= 0 || duration <= 0) { s->team_chat_position = s->team_chat_last_position = 0; return true; }
    if (s->team_chat_position < 0) return q3nc_fail(e, QA_ERROR_FORMAT, "Team chat cursor outside donor row storage");
    size_t offset = 0, length = 0; int32_t visible = 0; size_t last_space = SIZE_MAX; char color = '7';
    char line[241] = {0};
    while (input[offset]) {
        if (visible > 79) {
            if (last_space != SIZE_MAX) { offset -= length - last_space; ++offset; length = last_space; }
            line[length] = 0;
            q3nc_copy(s->team_chat[s->team_chat_position % height], 241, line);
            s->team_chat_times[s->team_chat_position % height] = f->time;
            s->team_chat_position = q3nc_add(s->team_chat_position, 1);
            if (s->team_chat_position < 0) return q3nc_fail(e, QA_ERROR_FORMAT, "Team chat source cursor overflow");
            line[0] = '^'; line[1] = color; length = 2; visible = 0; last_space = SIZE_MAX;
        }
        bool colored = input[offset] == '^' && input[offset + 1] && input[offset + 1] != '^';
        size_t bytes = colored ? 2 : 1;
        if (bytes > 240 - length) return q3nc_fail(e, QA_ERROR_FORMAT, "Team chat exceeds source color-expanded row");
        if (colored) { line[length++] = input[offset++]; color = input[offset]; line[length++] = input[offset++]; continue; }
        if (input[offset] == ' ') last_space = length;
        line[length++] = input[offset++]; ++visible;
    }
    line[length] = 0;
    q3nc_copy(s->team_chat[s->team_chat_position % height], 241, line);
    s->team_chat_times[s->team_chat_position % height] = f->time;
    s->team_chat_position = q3nc_add(s->team_chat_position, 1);
    if (q3nc_sub(s->team_chat_position, s->team_chat_last_position) > height)
        s->team_chat_last_position = q3nc_add(s->team_chat_position, -height);
    return true;
}
static const char *arg(const qa_command_tokens *tokens, size_t index)
{ return index < tokens->count ? tokens->values[index] : ""; }
static int32_t arg_int(const qa_command_tokens *tokens, size_t index) { return q3nc_integer(arg(tokens, index)); }
static bool dynamic(q3n_server_commands *o, const q3n_frame *f, uint32_t number,
    const q3n_client_info *ci, const q3n_client_dynamic *value, qa_error *e)
{
    return q3n_clients_dynamic_write(o->options.clients, f->application, &f->source,
        number, ci->configstring_revision, ci->media_revision, value, e) && q3nc_current(o, f, e);
}
static bool scores(q3n_server_commands *o, const q3n_frame *f, const qa_command_tokens *args, qa_error *e)
{
    q3n_command_state *s = &o->state; s->num_scores = arg_int(args, 1);
    if (s->num_scores > 64) s->num_scores = 64;
    s->team_scores[0] = arg_int(args, 2); s->team_scores[1] = arg_int(args, 3);
    memset(s->scores, 0, sizeof(s->scores));
    for (int32_t i = 0; i < s->num_scores; ++i) {
        size_t base = (size_t)i * 14; int32_t client = arg_int(args, base + 4);
        if (client < 0 || client >= 64) client = 0;
        const q3n_client_info *ci = q3n_clients_get(o->options.clients, (uint32_t)client);
        if (!ci) return q3nc_fail(e, QA_ERROR_ARGUMENT, "Scores require the canonical CGAME client row");
        q3n_command_score *score = &s->scores[i];
        *score = (q3n_command_score){.client = client, .score = arg_int(args, base + 5),
            .ping = arg_int(args, base + 6), .time = arg_int(args, base + 7), .score_flags = arg_int(args, base + 8),
            .accuracy = arg_int(args, base + 10), .impressive = arg_int(args, base + 11),
            .excellent = arg_int(args, base + 12), .gauntlet = arg_int(args, base + 13), .defend = arg_int(args, base + 14),
            .assist = arg_int(args, base + 15), .perfect = arg_int(args, base + 16), .captures = arg_int(args, base + 17), .team = ci->team};
        q3n_client_dynamic value = ci->dynamic; value.score = score->score; value.powerups = arg_int(args, base + 9);
        if (!dynamic(o, f, (uint32_t)client, ci, &value, e)) return false;
    }
    return o->options.product != QA_Q3_TEAM_ARENA ||
        (o->options.score_selection(o->options.context, f, s, e) && q3nc_current(o, f, e));
}
static bool team_info(q3n_server_commands *o, const q3n_frame *f, const qa_command_tokens *args, qa_error *e)
{
    int32_t count = arg_int(args, 1);
    if (count > 8) return q3nc_fail(e, QA_ERROR_FORMAT, "Team overlay exceeds TEAM_MAXOVERLAY");
    o->state.num_sorted_team_players = count;
    for (int32_t i = 0; i < count; ++i) {
        size_t base = (size_t)i * 6; int32_t client = arg_int(args, base + 2);
        if (client < 0 || client >= 64) return q3nc_fail(e, QA_ERROR_FORMAT, "Team overlay client outside CGAME table");
        const q3n_client_info *ci = q3n_clients_get(o->options.clients, (uint32_t)client);
        if (!ci) return q3nc_fail(e, QA_ERROR_ARGUMENT, "Team overlay requires its canonical CGAME client row");
        o->state.sorted_team_players[i] = client;
        q3n_client_dynamic value = ci->dynamic;
        value.location = arg_int(args, base + 3); value.health = arg_int(args, base + 4);
        value.armor = arg_int(args, base + 5); value.cur_weapon = arg_int(args, base + 6); value.powerups = arg_int(args, base + 7);
        if (!dynamic(o, f, (uint32_t)client, ci, &value, e)) return false;
    }
    return true;
}
static bool config_modified(q3n_server_commands *o, const q3n_frame *f, int32_t index, qa_error *e)
{
    if (index < 0 || index >= 1024) return q3nc_fail(e, QA_ERROR_FORMAT, "CG_ConfigString: bad index");
    char *text = NULL; uint64_t revision;
    if (!config(o, f, (uint32_t)index, &text, &revision, e)) return false;
    q3n_command_state *s = &o->state; bool ok = true;
    switch (index) {
    case 0: ok = server_info(o, f, e); break;
    case 2: ok = music(o, f, e); break;
    case 5: {
        int32_t warmup = q3nc_integer(text); s->warmup_count = -1;
        if (warmup > 0 && s->warmup <= 0) ok = sound_field(o, f,
            o->options.product == QA_Q3_TEAM_ARENA && s->game_type >= 4 && s->game_type <= 7 ? Q3N_S_PREPARE_TEAM : Q3N_S_PREPARE, 7, e);
        if (ok) s->warmup = warmup; break;
    }
    case 6: s->scores1 = q3nc_integer(text); break;
    case 7: s->scores2 = q3nc_integer(text); break;
    case 8: s->vote_time = q3nc_integer(text); s->vote_modified = true; break;
    case 9: q3nc_copy(s->vote_string, sizeof(s->vote_string), text);
        if (o->options.product == QA_Q3_TEAM_ARENA) ok = sound_field(o, f, Q3N_S_VOTE_NOW, 7, e); break;
    case 10: s->vote_yes = q3nc_integer(text); s->vote_modified = true; break;
    case 11: s->vote_no = q3nc_integer(text); s->vote_modified = true; break;
    case 21: s->level_start_time = q3nc_integer(text); break;
    case 22: s->intermission_started = q3nc_integer(text) != 0; break;
    case 23: ok = flag_status(o, f, e); break;
    case 24: ok = shader_state(o, f, e); break;
    default:
        if (index >= 12 && index <= 19) {
            unsigned slot = (unsigned)index % 2;
            if (index < 14) { s->team_vote_time[slot] = q3nc_integer(text); s->team_vote_modified[slot] = true; }
            else if (index < 16) {
                if (strlen(text) >= 1024) ok = q3nc_fail(e, QA_ERROR_FORMAT, "Team vote exceeds donor string storage");
                if (ok) q3nc_copy(s->team_vote_string[slot], 1024, text);
                if (ok && o->options.product == QA_Q3_TEAM_ARENA) ok = sound_field(o, f, Q3N_S_VOTE_NOW, 7, e);
            } else if (index < 18) { s->team_vote_yes[slot] = q3nc_integer(text); s->team_vote_modified[slot] = true; }
            else { s->team_vote_no[slot] = q3nc_integer(text); s->team_vote_modified[slot] = true; }
        } else if (index >= 32 && index < 544) ok = q3n_media_configstring_changed(
            o->options.media, o->options.reader, (uint32_t)index, e);
        else if (index >= 544 && index < 608) {
            q3n_client_settings value;
            ok = settings(o, f, false, &value, e) && q3n_clients_register_one(o->options.clients,
                f->application, &f->source, &value, (uint32_t)index - 544u, e);
            if (ok) spectators(o);
        }
        break;
    }
    if (ok) ok = config_current(o, f, (uint32_t)index, revision, e);
    free(text); return ok;
}
static bool restart(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    int32_t showmiss;
    if (!integer_cvar(o, "cg_showmiss", &showmiss, e)) return false;
    if (showmiss && !q3nc_message(o, f, Q3N_COMMAND_PRINT, "CG_MapRestart\n", -1, NULL, e)) return false;
    q3n_events_round(o->options.events);
    if (!o->options.clear_particles(o->options.context, f, e) || !q3nc_current(o, f, e)) return false;
    o->state.intermission_started = false; o->state.vote_time = 0; o->state.map_restart = true;
    if (!music(o, f, e) || !qa_q3_presentation_clear_loops(o->options.presentation, true, e) || !q3nc_current(o, f, e)) return false;
    if (o->state.warmup == 0 && (!sound_field(o, f, Q3N_S_FIGHT, 7, e) || !center(o, f, "FIGHT!", 120, 64, e))) return false;
    if (o->options.product == QA_Q3_TEAM_ARENA) {
        int32_t active, record; qa_native_q3_client_cvar demo;
        if (!integer_cvar(o, "cg_singlePlayerActive", &active, e)) return false;
        if (active) {
            if (!set_number(o, f, "ui_matchStartTime", f->time, e) || !integer_cvar(o, "cg_recordSPDemo", &record, e) ||
                !q3nc_cvar(o, "cg_recordSPDemoName", &demo, e)) return false;
            if (record && demo.value[0]) {
                char text[320]; snprintf(text, sizeof(text), "set g_synchronousclients 1 ; record %s \n", demo.value);
                if (!qa_native_q3_client_console(o->options.client, text, e) || !q3nc_current(o, f, e)) return false;
            }
        }
    }
    return set(o, f, "cg_thirdPerson", "0", e);
}
static bool prefix(const char *text, const char *beginning)
{
    while (*beginning) {
        unsigned char a = (unsigned char)*text++, b = (unsigned char)*beginning++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (a != b) return false;
    }
    return true;
}
static bool deferred(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    q3n_client_settings value;
    return settings(o, f, false, &value, e) &&
        q3n_clients_load_deferred(o->options.clients, f->application, &f->source, &value, e) && q3nc_current(o, f, e);
}
static bool dispatch(q3n_server_commands *o, const q3n_frame *f, const qa_command_tokens *args, qa_error *e)
{
    const char *name = arg(args, 0), *text = arg(args, 1);
    if (!*name) return true;
    if (!strcmp(name, "cp")) return center(o, f, text, o->options.product == QA_Q3_ARENA ? 143 : 144, 16, e);
    if (!strcmp(name, "cs")) return config_modified(o, f, arg_int(args, 1), e);
    if (!strcmp(name, "print")) {
        if (!q3nc_message(o, f, Q3N_COMMAND_PRINT, text, -1, NULL, e)) return false;
        if (o->options.product == QA_Q3_TEAM_ARENA) {
            if (prefix(text, "vote failed") || prefix(text, "team vote failed")) return sound_field(o, f, Q3N_S_VOTE_FAILED, 7, e);
            if (prefix(text, "vote passed") || prefix(text, "team vote passed")) return sound_field(o, f, Q3N_S_VOTE_PASSED, 7, e);
        }
        return true;
    }
    if (!strcmp(name, "chat") || !strcmp(name, "tchat")) {
        bool team = !strcmp(name, "tchat"); int32_t team_only;
        if (!integer_cvar(o, "cg_teamChatsOnly", &team_only, e)) return false;
        if (!team && team_only) return true;
        if (!sound_field(o, f, Q3N_S_TALK, 6, e)) return false;
        char message[151], copied[150]; q3nc_copy(copied, sizeof(copied), text); size_t size = 0;
        for (const char *p = copied; *p; ++p) if ((unsigned char)*p != 0x19) message[size++] = *p;
        message[size] = 0;
        if (team && !q3nc_team_chat(o, f, message, e)) return false;
        message[size++] = '\n'; message[size] = 0;
        return q3nc_message(o, f, team ? Q3N_COMMAND_TEAM_CHAT : Q3N_COMMAND_CHAT, message, -1, NULL, e);
    }
    if (!strcmp(name, "vchat") || !strcmp(name, "vtchat") || !strcmp(name, "vtell")) {
        if (o->options.product != QA_Q3_TEAM_ARENA) return true;
        int32_t no_taunt; const char *id = arg(args, 4);
        if (!integer_cvar(o, "cg_noTaunt", &no_taunt, e)) return false;
        if (no_taunt && (!strcmp(id, "kill_insult") || !strcmp(id, "taunt") || !strcmp(id, "death_insult") ||
            !strcmp(id, "kill_gauntlet") || !strcmp(id, "praise"))) return true;
        return q3n_voice_local(o, f, !strcmp(name, "vchat") ? 0 : !strcmp(name, "vtchat") ? 1 : 2,
            arg_int(args, 1) != 0, arg_int(args, 2), arg_int(args, 3), id, e);
    }
    if (!strcmp(name, "scores")) return scores(o, f, args, e);
    if (!strcmp(name, "tinfo")) return team_info(o, f, args, e);
    if (!strcmp(name, "map_restart")) return restart(o, f, e);
    if (q3nc_same(name, "remapShader") && args->count == 4) {
        /* Q3_VM CG_Argv has one static buffer. All three remap parameters
         * therefore contain the final argument, including the later name. */
        name = arg(args, 3);
        if (!qa_q3_presentation_remap(o->options.presentation, name, name, (float)atof(name), e) || !q3nc_current(o, f, e)) return false;
    }
    if (!strcmp(name, "loaddefered")) return deferred(o, f, e);
    if (!strcmp(name, "clientLevelShot")) { o->state.level_shot = true; return true; }
    char unknown[1100]; snprintf(unknown, sizeof(unknown), "Unknown client game command: %s\n", name);
    return q3nc_message(o, f, Q3N_COMMAND_PRINT, unknown, -1, NULL, e);
}
static bool receipt(q3n_server_commands *o, const q3n_frame *f, const q3n_server_command_receipt *r, int32_t sequence, qa_error *e)
{
    return r->wire.reader == o->options.reader && r->wire.sequence == sequence &&
        r->wire.publication_generation == r->publication_generation && r->wire.map_revision == r->map_revision &&
        qa_actor_id_equal(r->wire.actor, o->options.recipient.source_actor) &&
        r->wire.present == r->present && r->wire.arguments == r->arguments &&
        qa_native_q3_wire_receipt_current(&r->wire) &&
        r->sequence == sequence && identity(&r->recipient, &o->options.recipient) &&
        r->recipient.source_milliseconds == f->source.source_time_ms &&
        r->recipient.source_frame.provider == f->source.source_frame.provider &&
        r->recipient.source_frame.kind == f->source.source_frame.kind &&
        r->recipient.source_frame.phase == f->source.source_frame.phase &&
        r->recipient.source_frame.number == f->source.source_frame.number &&
        r->recipient.source_frame.start_ns == f->source.source_frame.start_ns &&
        r->recipient.source_frame.time_ns == f->source.source_frame.time_ns &&
        r->recipient.source_frame.elapsed_ns == f->source.source_frame.elapsed_ns &&
        r->publication_generation == o->options.publication_generation && r->map_revision == o->options.map_revision &&
        (!r->present || r->arguments) && o->options.receipt_current(o->options.context, f, r) && q3nc_current(o, f, e) ? true :
        q3nc_fail(e, QA_ERROR_ARGUMENT, "Native CGAME command receipt has another reader or publication generation");
}
static bool copy_arguments(const qa_command_tokens *source, qa_command_tokens *out, qa_error *e)
{
    if (!source || source->count > 1024 || (source->count && !source->values))
        return q3nc_fail(e, QA_ERROR_FORMAT, "Native CGAME command exceeds source token storage");
    size_t size = 0;
    for (size_t i = 0; i < source->count; ++i) {
        if (!source->values[i]) return q3nc_fail(e, QA_ERROR_FORMAT, "Native CGAME command has no source argument");
        size_t length = strlen(source->values[i]); if (length > 1023) length = 1023;
        size += length + 1;
    }
    out->values = source->count ? calloc(source->count, sizeof(*out->values)) : NULL;
    out->storage = size ? malloc(size) : NULL;
    if ((source->count && !out->values) || (size && !out->storage)) {
        qa_command_tokens_free(out); return q3nc_fail(e, QA_ERROR_MEMORY, "Retaining native command arguments across callbacks");
    }
    out->count = source->count; char *next = out->storage;
    for (size_t i = 0; i < out->count; ++i) {
        out->values[i] = next; q3nc_copy(next, 1024, source->values[i]); next += strlen(next) + 1;
    }
    return true;
}
bool q3n_server_commands_execute(q3n_server_commands *o, const q3n_frame *f, int32_t latest, qa_error *e)
{
    if (latest < 0 || !begin(o, f, true, e)) return false;
    bool ok = true;
    while (ok && o->state.server_command_sequence < latest) {
        int32_t sequence = ++o->state.server_command_sequence;
        q3n_server_command_receipt actual = {0}; qa_command_tokens owned = {0};
        ok = o->options.read_command(o->options.context, f, sequence, &actual, e) && receipt(o, f, &actual, sequence, e);
        if (ok && actual.present) ok = copy_arguments(actual.arguments, &owned, e) && dispatch(o, f, &owned, e);
        if (ok) ok = receipt(o, f, &actual, sequence, e);
        qa_command_tokens_free(&owned);
    }
    return end(o, ok);
}
bool q3n_server_commands_voice(q3n_server_commands *o, const q3n_frame *f, int32_t mode,
    bool voice_only, int32_t client, int32_t color, const char *command, qa_error *e)
{
    if (!command || !begin(o, f, true, e)) return false;
    return end(o, q3n_voice_local(o, f, mode, voice_only, client, color, command, e));
}
bool q3n_server_commands_finish(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    if (!begin(o, f, true, e)) return false;
    return end(o, q3n_voice_finish(o, f, e));
}
bool q3n_server_commands_vote_drawn(q3n_server_commands *o, const q3n_frame *f,
    int32_t team, qa_error *e)
{
    if (team < -1 || team > 1 || !begin(o, f, true, e)) return false;
    if (team < 0) o->state.vote_modified = false;
    else o->state.team_vote_modified[team] = false;
    return end(o, true);
}
bool q3n_server_commands_warmup_drawn(q3n_server_commands *o, const q3n_frame *f,
    int32_t warmup, int32_t count, qa_error *e)
{
    if (!begin(o, f, true, e)) return false;
    o->state.warmup = warmup; o->state.warmup_count = count;
    return end(o, true);
}
bool q3n_server_commands_chat_drawn(q3n_server_commands *o, const q3n_frame *f,
    int32_t last_position, qa_error *e)
{
    if (!begin(o, f, true, e)) return false;
    if (last_position > o->state.team_chat_position || last_position < o->state.team_chat_last_position)
        return end(o, q3nc_fail(e, QA_ERROR_ARGUMENT, "Team chat drawing cursor is outside its retained publication"));
    o->state.team_chat_last_position = last_position;
    return end(o, true);
}
bool q3n_server_commands_map_restart_taken(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    if (!begin(o, f, true, e)) return false;
    o->state.map_restart = false;
    return end(o, true);
}
