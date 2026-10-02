/* id Software cg_servercmds.c voice chat runtime; GPL-2.0-or-later. */
#include "server_commands_internal.h"

static bool warn(q3n_server_commands *o, const q3n_frame *f, qa_error *e,
    const char *kind, const char *filename)
{
    char message[1200]; snprintf(message, sizeof(message), "^1%s: %s\n", kind, filename);
    return q3nc_message(o, f, Q3N_COMMAND_PRINT, message, -1, NULL, e);
}
static bool file(q3n_server_commands *o, const q3n_frame *f, const char *filename,
    bool missing_warning, qa_resource **out, bool *found, qa_error *e)
{
    *out = NULL; *found = false;
    if (!q3nc_current(o, f, e)) return false;
    qa_error local = {0};
    char *normalized = qa_vfs_normalize_path(filename, &local);
    if (!normalized) {
        if (!missing_warning && (local.code == QA_ERROR_ARGUMENT || local.code == QA_ERROR_FORMAT)) return true;
        if (e) *e = local;
        return false;
    }
    bool ok = qa_vfs_acquire(o->options.content, normalized, out, NULL, &local);
    free(normalized);
    if (!ok) {
        if (local.code == QA_ERROR_NOT_FOUND)
            return !missing_warning || warn(o, f, e, "voice chat file not found", filename);
        if (e) *e = local;
        return false;
    }
    qa_bytes bytes = qa_resource_bytes(*out);
    if (bytes.size >= 16384) {
        char message[1200]; snprintf(message, sizeof(message),
            "^1voice chat file too large: %s is %zu, max allowed is 16384", filename, bytes.size);
        qa_resource_release(*out); *out = NULL;
        return q3nc_message(o, f, Q3N_COMMAND_PRINT, message, -1, NULL, e);
    }
    *found = true; return q3nc_current(o, f, e);
}
static bool token(q3n_server_commands *o, qa_common_cursor *cursor, qa_error *e)
{ return qa_common_parse(&o->voice.parser, cursor, true, e); }
static bool parse(q3n_server_commands *o, const q3n_frame *f, const char *filename,
    uint32_t list_index, qa_error *e)
{
    qa_native_q3_client_cvar build;
    if (!q3nc_cvar(o, "cg_buildScript", &build, e)) return false;
    qa_resource *resource = NULL; bool found;
    if (!file(o, f, filename, true, &resource, &found, e)) { qa_resource_release(resource); return false; }
    if (!found) return true;
    qa_common_cursor cursor;
    bool ok = qa_common_cursor_init(&cursor, qa_resource_bytes(resource), QA_COMMON_TERMINATED, e);
    q3n_voice_list *list = &o->voice.lists[list_index];
    q3nc_copy(list->name, sizeof(list->name), filename);
    for (unsigned i = 0; i < 64; ++i) list->chats[i].id[0] = 0;
    if (ok) ok = token(o, &cursor, e);
    const char *value = o->voice.parser.token;
    if (ok && !*value) { qa_resource_release(resource); return true; }
    if (ok) {
        if (q3nc_same(value, "female")) list->gender = QA_MODEL_FEMALE;
        else if (q3nc_same(value, "male")) list->gender = QA_MODEL_MALE;
        else if (q3nc_same(value, "neuter")) list->gender = QA_MODEL_NEUTER;
        else {
            ok = warn(o, f, e, "expected gender not found in voice chat file", filename);
            qa_resource_release(resource); return ok;
        }
        list->count = 0;
    }
    while (ok && list->count < 64) {
        ok = token(o, &cursor, e); if (!ok || !o->voice.parser.token[0]) break;
        q3n_voice_chat *chat = &list->chats[list->count];
        q3nc_copy(chat->id, sizeof(chat->id), o->voice.parser.token);
        ok = token(o, &cursor, e); if (!ok) break;
        if (strcmp(o->voice.parser.token, "{")) {
            char message[1300]; snprintf(message, sizeof(message), "^1expected { found %s in voice chat file: %s\n",
                o->voice.parser.token, filename);
            ok = q3nc_message(o, f, Q3N_COMMAND_PRINT, message, -1, NULL, e); break;
        }
        chat->count = 0;
        bool terminated = false;
        while (ok && chat->count < 64) {
            ok = token(o, &cursor, e); if (!ok) break;
            if (!o->voice.parser.token[0]) { terminated = true; break; }
            if (!strcmp(o->voice.parser.token, "}")) break;
            int32_t sound;
            ok = qa_q3_register_sound(o->options.assets, o->voice.parser.token, build.integer == 0, &sound, e) &&
                q3nc_current(o, f, e);
            if (!ok) break;
            chat->sounds[chat->count] = sound;
            ok = token(o, &cursor, e); if (!ok) break;
            if (!o->voice.parser.token[0]) { terminated = true; break; }
            q3nc_copy(chat->text[chat->count], sizeof(chat->text[chat->count]), o->voice.parser.token);
            if (sound) ++chat->count;
        }
        if (!ok || terminated) break;
        ++list->count;
    }
    qa_resource_release(resource); return ok && q3nc_current(o, f, e);
}
bool q3n_voice_load(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    if (o->options.product != QA_Q3_TEAM_ARENA) return true;
    if (o->voice.loaded) return q3nc_fail(e, QA_ERROR_ARGUMENT, "Voice registration constructor ran twice");
    int32_t before = o->options.memory_remaining(o->options.context);
    if (!q3nc_current(o, f, e)) return false;
    const char *const names[8] = {"female1", "female2", "female3", "male1", "male2", "male3", "male4", "male5"};
    for (uint32_t i = 0; i < 8; ++i) {
        char path[64]; snprintf(path, sizeof(path), "scripts/%s.voice", names[i]);
        if (!parse(o, f, path, i, e)) return false;
    }
    int32_t after = o->options.memory_remaining(o->options.context);
    if (!q3nc_current(o, f, e)) return false;
    char message[80];
    snprintf(message, sizeof(message), "voice chat memory size = %d\n", q3nc_sub(before, after));
    if (!q3nc_message(o, f, Q3N_COMMAND_PRINT, message, -1, NULL, e)) return false;
    o->voice.loaded = true; return true;
}
static bool head_file(q3n_server_commands *o, const q3n_frame *f, const char *filename, int32_t *index, qa_error *e)
{
    *index = -1; qa_resource *resource = NULL; bool found;
    if (!file(o, f, filename, false, &resource, &found, e)) { qa_resource_release(resource); return false; }
    if (!found) return true;
    qa_common_cursor cursor;
    bool ok = qa_common_cursor_init(&cursor, qa_resource_bytes(resource), QA_COMMON_TERMINATED, e) && token(o, &cursor, e);
    if (ok && o->voice.parser.token[0]) for (int32_t i = 0; i < 8; ++i)
        if (q3nc_same(o->voice.lists[i].name, o->voice.parser.token)) { *index = i; break; }
    qa_resource_release(resource); return ok && q3nc_current(o, f, e);
}
static q3n_voice_list *remember(q3n_server_commands *o, const char *head, int32_t list)
{
    for (unsigned i = 0; i < 64; ++i) if (!o->voice.heads[i].head[0]) {
        q3nc_copy(o->voice.heads[i].head, sizeof(o->voice.heads[i].head), head);
        o->voice.heads[i].list = list; break;
    }
    return &o->voice.lists[list];
}
static bool list_for_client(q3n_server_commands *o, const q3n_frame *f,
    const q3n_client_info *ci, q3n_voice_list **out, qa_error *e)
{
    const char *model = ci->head_model_name[0] == '*' ? ci->head_model_name + 1 : ci->head_model_name;
    char head[64];
    for (unsigned pass = 0; pass < 2; ++pass) {
        char complete[130];
        if (!pass) snprintf(complete, sizeof(complete), "%s/%s", model, ci->head_skin_name);
        else q3nc_copy(complete, sizeof(complete), model);
        q3nc_copy(head, sizeof(head), complete);
        for (unsigned i = 0; i < 64; ++i) if (q3nc_same(o->voice.heads[i].head, head)) {
            *out = &o->voice.lists[o->voice.heads[i].list]; return true;
        }
        for (unsigned i = 0; i < 64; ++i) if (!o->voice.heads[i].head[0]) {
            char path[80], final[64]; snprintf(path, sizeof(path), "scripts/%s.vc", head); q3nc_copy(final, sizeof(final), path);
            int32_t index;
            if (!head_file(o, f, final, &index, e)) return false;
            if (index >= 0) { *out = remember(o, head, index); return true; }
            break;
        }
    }
    int32_t gender = ci->animations.gender;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (int32_t i = 0; i < 8; ++i) if (o->voice.lists[i].name[0] && o->voice.lists[i].gender == gender) {
            *out = remember(o, head, i); return true;
        }
        if (gender == QA_MODEL_MALE) break;
        gender = QA_MODEL_MALE;
    }
    *out = remember(o, head, 0); return true;
}
static int32_t order(const char *id)
{
    if (q3nc_same(id, "getflag") || q3nc_same(id, "offense")) return 1;
    if (q3nc_same(id, "defend") || q3nc_same(id, "defendflag")) return 2;
    if (q3nc_same(id, "patrol")) return 3;
    if (q3nc_same(id, "followme")) return 4;
    if (q3nc_same(id, "returnflag")) return 5;
    if (q3nc_same(id, "followflagcarrier")) return 6;
    if (q3nc_same(id, "camp")) return 7;
    return -1;
}
static bool play(q3n_server_commands *o, const q3n_frame *f, q3n_buffered_voice *v, qa_error *e)
{
    if (o->state.intermission_started) return true;
    qa_native_q3_client_cvar chats, text;
    if (!q3nc_cvar(o, "cg_noVoiceChats", &chats, e)) return false;
    if (!chats.integer) {
        if (!q3nc_sound(o, f, v->sound, 3, e)) return false;
        const qa_q3_player *player = q3n_frame_snapshot_player(f);
        if (!player) return q3nc_fail(e, QA_ERROR_ARGUMENT, "Voice playback requires the actual current source player");
        if (v->client != player->clientNum) {
            int32_t task = order(v->command);
            if (task > 0) {
                o->state.accept_order_time = q3nc_add(f->time, 5000);
                q3nc_copy(o->state.accept_voice, sizeof(o->state.accept_voice), v->command);
                o->state.accept_task = task; o->state.accept_leader = v->client;
            }
            if (!o->options.response_head(o->options.context, f, &o->state, e) || !q3nc_current(o, f, e)) return false;
        }
    }
    if (!q3nc_cvar(o, "cg_noVoiceText", &text, e)) return false;
    if (!v->voice_only && !text.integer) {
        if (!q3nc_team_chat(o, f, v->message, e)) return false;
        char message[151]; snprintf(message, sizeof(message), "%s\n", v->message);
        if (!q3nc_message(o, f, Q3N_COMMAND_VOICE, message, v->client, v->command, e)) return false;
    }
    if (o->voice.out < 0 || o->voice.out >= 32) return q3nc_fail(e, QA_ERROR_FORMAT, "Voice buffer output outside donor storage");
    o->voice.buffer[o->voice.out].sound = 0; return true;
}
static bool buffer(q3n_server_commands *o, const q3n_frame *f, const q3n_buffered_voice *v, qa_error *e)
{
    if (o->state.intermission_started) return true;
    if (o->voice.in < 0 || o->voice.in >= 32) return q3nc_fail(e, QA_ERROR_FORMAT, "Voice buffer input outside donor storage");
    o->voice.buffer[o->voice.in] = *v; o->voice.in = (o->voice.in + 1) % 32;
    if (o->voice.in == o->voice.out) {
        if (o->voice.out < 0 || o->voice.out >= 32) return q3nc_fail(e, QA_ERROR_FORMAT, "Voice overflow outside donor storage");
        if (!play(o, f, &o->voice.buffer[o->voice.out], e)) return false;
        /* Donor overflow increments without modulo. Preserve the cursor and
         * reject its next invalid storage access rather than changing it. */
        ++o->voice.out;
    }
    return true;
}
bool q3n_voice_local(q3n_server_commands *o, const q3n_frame *f, int32_t mode,
    bool voice_only, int32_t number, int32_t color, const char *id, qa_error *e)
{
    if (o->options.product != QA_Q3_TEAM_ARENA || o->state.intermission_started) return true;
    if (!o->voice.loaded || !q3nc_current(o, f, e)) return q3nc_fail(e, QA_ERROR_ARGUMENT, "Voice commands require real constructor registration");
    if (number < 0 || number >= 64) number = 0;
    const q3n_client_info *ci = q3n_clients_get(o->options.clients, (uint32_t)number);
    if (!ci) return q3nc_fail(e, QA_ERROR_ARGUMENT, "Voice sender has no canonical CGAME row");
    o->state.current_voice_client = number;
    q3n_voice_list *list;
    if (!list_for_client(o, f, ci, &list, e)) return false;
    for (int32_t i = 0; i < list->count; ++i) {
        q3n_voice_chat *chat = &list->chats[i];
        if (!q3nc_same(chat->id, id)) continue;
        volatile float choice = q3n_events_random(o->options.events) * (float)chat->count;
        int32_t index = (int32_t)choice;
        if (index < 0 || index >= 64) return q3nc_fail(e, QA_ERROR_FORMAT, "Voice random choice outside donor sound storage");
        qa_native_q3_client_cvar team_only;
        if (!q3nc_cvar(o, "cg_teamChatsOnly", &team_only, e)) return false;
        if (mode != 1 && team_only.integer) return true;
        q3n_buffered_voice voice = {.client = number, .sound = chat->sounds[index], .voice_only = voice_only};
        q3nc_copy(voice.command, sizeof(voice.command), id);
        const char *before = mode == 2 ? "[" : mode == 1 ? "(" : "";
        const char *after = mode == 2 ? "]" : mode == 1 ? ")" : "";
        char formatted[300]; snprintf(formatted, sizeof(formatted), "%s%s%s: ^%c%s", before, ci->name,
            after, (unsigned char)color, chat->text[index]);
        q3nc_copy(voice.message, sizeof(voice.message), formatted);
        return buffer(o, f, &voice, e);
    }
    return true;
}
bool q3n_voice_finish(q3n_server_commands *o, const q3n_frame *f, qa_error *e)
{
    if (o->options.product != QA_Q3_TEAM_ARENA || o->voice.time >= f->time) return true;
    if (o->voice.out != o->voice.in) {
        if (o->voice.out < 0 || o->voice.out >= 32) return q3nc_fail(e, QA_ERROR_FORMAT, "Voice playback cursor outside donor storage");
        q3n_buffered_voice *voice = &o->voice.buffer[o->voice.out];
        if (voice->sound) {
            if (!play(o, f, voice, e)) return false;
            o->voice.out = (o->voice.out + 1) % 32; o->voice.time = q3nc_add(f->time, 1000);
        }
    }
    return true;
}
