/* Native client media follows content/q3/presentation/players.ts.
 * Source: id Software cg_players.c, GPL-2.0-or-later. */
#include "client_info_internal.h"
#include <stdarg.h>
#include <stdio.h>

static const char *const custom_sounds[] = {
    "*death1.wav", "*death2.wav", "*death3.wav", "*jump1.wav",
    "*pain25_1.wav", "*pain50_1.wav", "*pain75_1.wav", "*pain100_1.wav",
    "*falling1.wav", "*gasp.wav", "*drown.wav", "*fall1.wav", "*taunt.wav"
};
bool q3n_client_fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
void q3n_animation_dispose(q3n_animation_holder *holder)
{
    qa_resource_release(holder->resource);
    qa_vfs_acquisition_dispose(&holder->receipt);
    *holder = (q3n_animation_holder){0};
}
static bool compiled_current(const q3n_clients *o,const q3n_compiled_source_view *v,qa_error *e)
{
    return v && o->options.compiled_source==v->owner && !o->options.reader && !o->options.remote_source &&
        o->options.content==v->basis.content && o->options.assets==v->basis.assets &&
        o->options.product==v->basis.product && q3n_compiled_source_current(v) ? true :
        q3n_client_fail(e,QA_ERROR_ARGUMENT,"Compiled client media lost its real reached source owner");
}
static bool source_current(const q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, qa_error *error)
{
    qa_native_q3_wire_basis basis;
    if (owner->compiled_cut) return compiled_current(owner,owner->compiled_cut,error);
    if (owner->options.remote_source || owner->options.compiled_source)
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Local client media cannot borrow a remote source");
    if (!qa_application_native_q3_presentation_current(app, cut) ||
        !qa_native_q3_wire_reader_basis(owner->options.reader, &basis, error))
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 media source publication was superseded");
    return basis.application == app && basis.session == cut->session &&
        basis.source_game == cut->source_game && basis.source_owner == cut->source_owner &&
        basis.product == owner->options.product && cut->product == owner->options.product &&
        cut->content == owner->options.content &&
        basis.publication_generation == cut->publication_generation && basis.map_revision == cut->map_revision ? true :
        q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client reader has another physical source");
}
static bool remote_current(const q3n_clients *owner, const q3n_remote_source_view *cut, qa_error *error)
{
    return cut && owner->options.remote_source == cut->owner && !owner->options.reader && !owner->options.compiled_source &&
        cut->basis.product == owner->options.product && cut->basis.content == owner->options.content &&
        q3n_remote_source_current(cut) ? true :
        q3n_client_fail(error, QA_ERROR_ARGUMENT, "Remote client media lost its actual reached CLIENT source");
}
static bool source_string(const q3n_clients *owner, uint32_t index,
    const char **text, uint64_t *revision, qa_error *error)
{
    if (owner->options.compiled_source)
        return q3n_compiled_source_configstring(owner->options.compiled_source,index,text,revision,error);
    return owner->options.remote_source ?
        q3n_remote_source_configstring(owner->options.remote_source, index, text, revision, error) :
        qa_native_q3_wire_reader_configstring(owner->options.reader, index, text, revision, error);
}
static bool current(q3n_clients *owner, qa_error *error)
{
    if (!owner->cut && !owner->remote_cut && !owner->compiled_cut) return true;
    if (!(owner->remote_cut ? remote_current(owner, owner->remote_cut, error) :
        source_current(owner, owner->application, owner->cut, error))) return false;
    const char *text; uint64_t revision;
    if (!source_string(owner, 0, &text, &revision, error)) return false;
    if (revision != owner->serverinfo_revision)
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 reached serverinfo changed during media registration");
    if (owner->active_info) {
        if (!source_string(owner,
            544u + owner->active_client, &text, &revision, error)) return false;
        if (revision != owner->active_configstring_revision)
            return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client-info publication was superseded");
    }
    return true;
}
static bool print(q3n_clients *owner, qa_error *error, const char *format, ...)
{
    char message[1024]; va_list args; va_start(args, format);
    int length = vsnprintf(message, sizeof(message), format, args); va_end(args);
    if (length < 0 || (size_t)length >= sizeof(message))
        return q3n_client_fail(error, QA_ERROR_FORMAT, "CG_Printf exceeds its source buffer");
    if (owner->options.print) owner->options.print(owner->options.context, message);
    return current(owner, error);
}
static bool filename(q3n_clients *owner, char *out, size_t capacity, qa_error *error,
    const char *format, ...)
{
    va_list args; va_start(args, format);
    int length = vsnprintf(out, capacity, format, args); va_end(args);
    if (length < 0) return q3n_client_fail(error, QA_ERROR_FORMAT, "Formatting Q3 client media path");
    if ((size_t)length >= capacity)
        return print(owner, error, "Com_sprintf: overflow of %i in %zu\n", length, capacity);
    return current(owner, error);
}
static void copy_text(char *out, size_t capacity, const char *text)
{
    size_t length = strlen(text); if (length >= capacity) length = capacity - 1;
    memcpy(out, text, length); out[length] = 0;
}
static unsigned char fold(unsigned char byte)
{ return byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte; }
static bool same(const char *a, const char *b)
{
    while (*a && *b && fold((unsigned char)*a) == fold((unsigned char)*b)) { ++a; ++b; }
    return !*a && !*b;
}
static int32_t source_integer(const char *text)
{
    while (*text && (signed char)*text <= 32) ++text;
    bool negative = *text == '-'; if (*text == '-' || *text == '+') ++text;
    uint32_t value = 0;
    while (*text >= '0' && *text <= '9') value = value * 10u + (uint32_t)(*text++ - '0');
    if (negative) value = 0u - value;
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}
static int32_t integer_key(const char *info, const char *key)
{
    char value[1024]; qa_q3_client_info_value(info, key, value, sizeof(value));
    return source_integer(value);
}
static qa_vec3 color(int32_t bits)
{
    return bits < 1 || bits > 7 ? (qa_vec3){1, 1, 1} :
        (qa_vec3){(bits & 4) != 0, (bits & 2) != 0, (bits & 1) != 0};
}
static void model_skin(const char *value, char model[64], char skin[64])
{
    char path[64]; copy_text(path, sizeof(path), value);
    char *slash = strchr(path, '/');
    if (slash) { *slash++ = 0; copy_text(skin, 64, slash); }
    else copy_text(skin, 64, "default");
    copy_text(model, 64, path);
}
static const char *team_skin(const q3n_client_info *ci, int32_t game_type)
{ return game_type >= 3 ? ci->team == 2 ? "blue" : "red" : "default"; }
static bool exists(q3n_clients *owner, const char *path, bool *found, qa_error *error)
{
    *found = false;
    qa_resource *resource = NULL; qa_error local = {0};
    char *normalized = qa_vfs_normalize_path(path, &local);
    if (!normalized) {
        if (local.code == QA_ERROR_ARGUMENT || local.code == QA_ERROR_FORMAT) return current(owner, error);
        if (error) *error = local;
        return false;
    }
    bool ok = qa_vfs_acquire(owner->options.content, normalized, &resource, NULL, &local);
    free(normalized);
    if (!ok && local.code == QA_ERROR_NOT_FOUND) ok = true;
    if (!ok && error) *error = local;
    if (resource) *found = qa_resource_bytes(resource).size != 0;
    qa_resource_release(resource);
    return ok && current(owner, error);
}
static bool find_file(q3n_clients *owner, const q3n_client_info *ci, int32_t game_type,
    const char *team_name, const char *model, const char *skin, const char *base,
    const char *extension, bool head, char *path, size_t capacity, bool *found, qa_error *error)
{
    const char *team = team_skin(ci, game_type), *name = model;
    const char *folders[2] = {"", head ? "heads/" : "characters/"};
    size_t start = head && model[0] == '*' ? 1 : 0;
    if (start) ++name;
    *found = false;
    for (size_t folder = start; folder < 2; ++folder)
        for (size_t prefix = 0; prefix < (*team_name ? 2u : 1u); ++prefix) {
            const char *leader = prefix ? "" : team_name;
            bool ok = head ? filename(owner, path, capacity, error, "models/players/%s%s/%s/%s%s_%s.%s",
                folders[folder], name, skin, leader, base, team, extension) :
                filename(owner, path, capacity, error, "models/players/%s%s/%s%s_%s_%s.%s",
                folders[folder], name, leader, base, skin, team, extension);
            if (!ok || !exists(owner, path, found, error)) return false;
            if (*found) return true;
            if (!filename(owner, path, capacity, error, "models/players/%s%s/%s%s_%s.%s",
                folders[folder], name, leader, base, game_type >= 3 ? team : skin, extension) ||
                !exists(owner, path, found, error)) return false;
            if (*found) return true;
        }
    return true;
}
static bool skins(q3n_clients *owner, q3n_client_info *ci, int32_t game_type, const char *team,
    const char *model, const char *skin, const char *head, const char *head_skin,
    bool *loaded, qa_error *error)
{
    const char *parts[] = {"lower", "upper", "head"};
    const char *labels[] = {"Leg", "Torso", "Head"};
    for (size_t i = 0; i < 3; ++i) {
        char path[64]; bool found;
        if (!find_file(owner, ci, game_type, team, i == 2 ? head : model,
            i == 2 ? head_skin : skin, parts[i], "skin", i == 2, path, sizeof(path), &found, error)) return false;
        if (found && (!qa_q3_register_skin(owner->options.assets, path, &ci->skins[i], error) ||
            !current(owner, error))) return false;
        if (!ci->skins[i] && !print(owner, error, "%s skin load failure: %s\n", labels[i], path)) return false;
    }
    *loaded = ci->skins[0] && ci->skins[1] && ci->skins[2]; return true;
}
static int32_t signed_bits(uint32_t value)
{ return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value); }
static float source_float(const char *text)
{
    while (*text && (signed char)*text <= 32) ++text;
    bool negative = *text == '-'; if (*text == '-' || *text == '+') ++text;
    float value = 0;
    while (*text >= '0' && *text <= '9') {
        /* Distinct binary32 operations mirror the source MULF and ADDF. */
        float product = (float)((double)value * 10.0);
        value = (float)((double)product + (*text++ - '0'));
    }
    if (*text == '.') {
        ++text; float fraction = 0.1f;
        while (*text >= '0' && *text <= '9') {
            float product = (float)((double)(*text++ - '0') * fraction);
            value = (float)((double)value + product);
            fraction = (float)((double)fraction * 0.1f);
        }
    }
    return negative ? -value : value;
}
static bool animation_token(q3n_clients *owner, qa_common_cursor *cursor, qa_error *error)
{ return qa_common_parse(&owner->animation_parser, cursor, true, error); }
static bool parse_animation(q3n_clients *owner, qa_player_animation_config *config,
    qa_bytes bytes, const char *path, bool *loaded, qa_error *error)
{
    *loaded = false;
    if (!bytes.size) return true;
    if (bytes.size > 19998) return print(owner, error, "File %s too long\n", path);
    qa_common_cursor cursor;
    if (!qa_common_cursor_init(&cursor, bytes, QA_COMMON_TERMINATED, error)) return false;
    config->footsteps = QA_FOOTSTEP_NORMAL; config->gender = QA_MODEL_MALE;
    memset(config->head_offset, 0, sizeof(config->head_offset));
    config->fixed_legs = config->fixed_torso = false;
    for (;;) {
        qa_common_cursor_state previous = qa_common_cursor_capture(&cursor);
        if (!animation_token(owner, &cursor, error)) return false;
        const char *token = owner->animation_parser.token;
        if (same(token, "footsteps")) {
            if (!animation_token(owner, &cursor, error)) return false;
            const char *names[] = {"normal", "boot", "flesh", "mech", "energy"};
            bool known = same(token, "default");
            if (known) config->footsteps = QA_FOOTSTEP_NORMAL;
            for (uint32_t i = 0; i < 5; ++i) if (same(token, names[i])) {
                config->footsteps = (qa_model_footstep)i; known = true; break;
            }
            if (!known && !print(owner, error, "Bad footsteps parm in %s: %s\n", path, token)) return false;
        } else if (same(token, "headoffset")) {
            for (size_t i = 0; i < 3; ++i) {
                if (!animation_token(owner, &cursor, error)) return false;
                config->head_offset[i] = source_float(token);
            }
        } else if (same(token, "sex")) {
            if (!animation_token(owner, &cursor, error)) return false;
            unsigned char sex = fold((unsigned char)*token);
            config->gender = sex == 'f' ? QA_MODEL_FEMALE : sex == 'n' ? QA_MODEL_NEUTER : QA_MODEL_MALE;
        } else if (same(token, "fixedlegs")) config->fixed_legs = true;
        else if (same(token, "fixedtorso")) config->fixed_torso = true;
        else {
            if (*token >= '0' && *token <= '9') {
                if (!qa_common_cursor_restore(&cursor, previous, error)) return false;
                break;
            }
            if (!print(owner, error, "unknown token '%s' is %s\n", token, path)) return false;
            if (previous.offset == cursor.offset && previous.ended == cursor.ended)
                return q3n_client_fail(error, QA_ERROR_FORMAT, "CG animation prelude reached the source nonprogress cycle");
        }
    }
    int32_t skip = 0; size_t index = 0;
    for (; index < 31; ++index) {
        qa_player_animation *a = &config->animations[index];
        if (!animation_token(owner, &cursor, error)) return false;
        const char *token = owner->animation_parser.token;
        if (!*token) {
            if (index >= 25) {
                *a = config->animations[6]; a->reversed = a->flipflop = false;
                a->present = true; continue;
            }
            break;
        }
        a->first_frame = source_integer(token);
        if (index == 13) skip = signed_bits((uint32_t)a->first_frame - (uint32_t)config->animations[6].first_frame);
        if (index >= 13 && index < 25) a->first_frame = signed_bits((uint32_t)a->first_frame - (uint32_t)skip);
        if (!animation_token(owner, &cursor, error)) return false;
        if (!*token) break;
        a->num_frames = source_integer(token); a->reversed = a->flipflop = false;
        if (a->num_frames < 0) { a->num_frames = signed_bits(0u - (uint32_t)a->num_frames); a->reversed = true; }
        if (!animation_token(owner, &cursor, error)) return false;
        if (!*token) break;
        a->loop_frames = source_integer(token);
        if (!animation_token(owner, &cursor, error)) return false;
        if (!*token) break;
        float fps = source_float(token); if (fps == 0) fps = 1;
        float lerp = (float)(1000.0 / (double)fps);
        a->frame_lerp = a->initial_lerp = lerp >= -2147483648.0f && lerp < 2147483648.0f ? (int32_t)lerp : INT32_MIN;
        a->present = true;
    }
    if (index != 31) return print(owner, error, "Error parsing animation file: %s", path);
    config->animations[32] = config->animations[13]; config->animations[32].reversed = true;
    config->animations[33] = config->animations[14]; config->animations[33].reversed = true;
    config->animations[34] = (qa_player_animation){0, 16, 16, 66, 66, false, false, true};
    config->animations[35] = (qa_player_animation){16, 5, 0, 50, 50, false, false, true};
    config->animations[36] = (qa_player_animation){16, 5, 1, 66, 66, true, false, true};
    *loaded = true; return true;
}
static bool animation(q3n_clients *owner, q3n_client_info *ci, q3n_animation_holder *holder,
    const char *path, bool *loaded, qa_error *error)
{
    *loaded = false; qa_error local = {0}; q3n_animation_holder next = {0};
    char *normalized = qa_vfs_normalize_path(path, &local);
    if (!normalized) {
        if (local.code == QA_ERROR_ARGUMENT || local.code == QA_ERROR_FORMAT) return current(owner, error);
        if (error) *error = local;
        return false;
    }
    free(normalized);
    bool ok = qa_vfs_acquire_receipt(owner->options.content, path, &next.resource, &next.receipt, &local);
    if (!ok && local.code == QA_ERROR_NOT_FOUND) return current(owner, error);
    if (!ok) { if (error) *error = local; return false; }
    qa_bytes bytes = qa_resource_bytes(next.resource);
    ok = parse_animation(owner, &ci->animations, bytes, path, loaded, error) && current(owner, error);
    if (ok && *loaded) { q3n_animation_dispose(holder); *holder = next; next = (q3n_animation_holder){0}; }
    q3n_animation_dispose(&next); return ok;
}
static bool register_model(q3n_clients *owner, q3n_client_info *ci, q3n_animation_holder *holder,
    int32_t game_type, const char *model, const char *skin, const char *head_model,
    const char *head_skin, const char *team, bool *loaded, qa_error *error)
{
    *loaded = false; char path[128]; const char *head = *head_model ? head_model : model;
    for (size_t i = 0; i < 2; ++i) {
        const char *part = i ? "upper" : "lower";
        if (!filename(owner, path, sizeof(path), error, "models/players/%s/%s.md3", model, part) ||
            !qa_q3_register_model(owner->options.assets, path, &ci->models[i], error) || !current(owner, error)) return false;
        if (!ci->models[i]) {
            if (!filename(owner, path, sizeof(path), error, "models/players/characters/%s/%s.md3", model, part) ||
                !qa_q3_register_model(owner->options.assets, path, &ci->models[i], error) || !current(owner, error)) return false;
        }
        if (!ci->models[i]) return print(owner, error, "Failed to load model file %s\n", path);
    }
    bool ok = head[0] == '*' ? filename(owner, path, sizeof(path), error,
        "models/players/heads/%s/%s.md3", head_model + 1, head_model + 1) :
        filename(owner, path, sizeof(path), error, "models/players/%s/head.md3", head);
    if (!ok || !qa_q3_register_model(owner->options.assets, path, &ci->models[2], error) || !current(owner, error)) return false;
    if (!ci->models[2] && head[0] != '*') {
        if (!filename(owner, path, sizeof(path), error, "models/players/heads/%s/%s.md3", head_model, head_model) ||
            !qa_q3_register_model(owner->options.assets, path, &ci->models[2], error) || !current(owner, error)) return false;
    }
    if (!ci->models[2]) return print(owner, error, "Failed to load model file %s\n", path);
    bool ready;
    if (!skins(owner, ci, game_type, team, model, skin, head, head_skin, &ready, error)) return false;
    if (!ready) {
        if (!print(owner, error, "Failed to load skin file: %s : %s : %s, %s : %s\n", team, model, skin, head, head_skin)) return false;
        if (!*team) return true;
        char fallback[128];
        if (!filename(owner, fallback, sizeof(fallback), error, "%s/", ci->team == 2 ? "Pagans" : "Stroggs") ||
            !skins(owner, ci, game_type, fallback, model, skin, head, head_skin, &ready, error)) return false;
        if (!ready) return print(owner, error, "Failed to load skin file: %s : %s : %s, %s : %s\n", fallback, model, skin, head, head_skin);
    }
    if (!filename(owner, path, sizeof(path), error, "models/players/%s/animation.cfg", model) ||
        !animation(owner, ci, holder, path, &ready, error)) return false;
    if (!ready) {
        if (!filename(owner, path, sizeof(path), error, "models/players/characters/%s/animation.cfg", model) ||
            !animation(owner, ci, holder, path, &ready, error)) return false;
        if (!ready) return print(owner, error, "Failed to load animation file %s\n", path);
    }
    if (!find_file(owner, ci, game_type, team, head, head_skin, "icon", "skin", true,
        path, sizeof(path), &ready, error)) return false;
    if (!ready && !find_file(owner, ci, game_type, team, head, head_skin, "icon", "tga", true,
        path, sizeof(path), &ready, error)) return false;
    if (ready && (!qa_q3_register_shader(owner->options.assets, path, false, &ci->icon, error) || !current(owner, error))) return false;
    *loaded = ci->icon != 0; return true;
}
static bool load(q3n_clients *owner, q3n_client_info *ci, q3n_animation_holder *holder,
    int32_t game_type, const q3n_client_settings *settings, qa_error *error)
{
    bool mission = owner->options.product == QA_Q3_TEAM_ARENA;
    const char *team_model = mission ? "james" : "sarge", *team_head = mission ? "*james" : "sarge";
    char team[128] = {0};
    if (mission && game_type >= 3) {
        const char *name = ci->team == 2 ? settings->blue_team_name : settings->red_team_name;
        if (*name && !filename(owner, team, sizeof(team), error, "%s/", name)) return false;
    }
    bool loaded;
    if (!register_model(owner, ci, holder, game_type, ci->model_name, ci->skin_name,
        ci->head_model_name, ci->head_skin_name, team, &loaded, error)) return false;
    if (!loaded) {
        if (settings->build_script) return q3n_client_fail(error, QA_ERROR_NOT_FOUND, "CG_RegisterClientModelname failed");
        bool fallback;
        if (game_type >= 3) {
            if (!register_model(owner, ci, holder, game_type, team_model, ci->skin_name, team_head,
                ci->skin_name, ci->team == 2 ? "Pagans" : "Stroggs", &fallback, error)) return false;
        } else if (!register_model(owner, ci, holder, game_type, "sarge", "default", "sarge", "default", team, &fallback, error)) return false;
        if (!fallback) return q3n_client_fail(error, QA_ERROR_NOT_FOUND, "Default Q3 client model failed to register");
    }
    qa_model_tag tag;
    if (!qa_q3_presentation_tag(owner->options.assets, ci->models[1], "tag_flag", 0, 0, 1,
        &tag, &ci->new_anims, error) || !current(owner, error)) return false;
    const char *fallback_sound = mission && game_type >= 3 ? "james" : "sarge";
    memset(ci->sounds, 0, sizeof(ci->sounds));
    for (size_t i = 0; i < sizeof(custom_sounds) / sizeof(*custom_sounds); ++i) {
        char path[160];
        if (loaded && (!filename(owner, path, sizeof(path), error, "sound/player/%s/%s", ci->model_name, custom_sounds[i] + 1) ||
            !qa_q3_register_sound(owner->options.assets, path, false, &ci->sounds[i], error) || !current(owner, error))) return false;
        if (!ci->sounds[i] && (!filename(owner, path, sizeof(path), error, "sound/player/%s/%s", fallback_sound, custom_sounds[i] + 1) ||
            !qa_q3_register_sound(owner->options.assets, path, false, &ci->sounds[i], error) || !current(owner, error))) return false;
    }
    ci->deferred = false; return true;
}
static bool copy_media(q3n_clients *owner, uint32_t index, q3n_client_info *ci,
    q3n_animation_holder *holder, qa_error *error)
{
    const q3n_client_info *source = &owner->clients[index];
    const q3n_animation_holder *from = &owner->holders[index];
    q3n_animation_holder next = {0};
    /* Receipts retain the exact request, including links, rather than replaying
     * a deduplicated resource's first path. */
    const char *strings[] = {from->receipt.path, from->receipt.lookup_path, from->receipt.link_source, from->receipt.link_target};
    char **targets[] = {&next.receipt.path, &next.receipt.lookup_path, &next.receipt.link_source, &next.receipt.link_target};
    next.receipt.mount = from->receipt.mount; next.receipt.resource_id = from->receipt.resource_id;
    for (size_t i = 0; i < 4; ++i) if (strings[i]) {
        size_t length = strlen(strings[i]); *targets[i] = malloc(length + 1);
        if (!*targets[i]) { q3n_animation_dispose(&next); return q3n_client_fail(error, QA_ERROR_MEMORY, "Copying native Q3 animation receipt"); }
        memcpy(*targets[i], strings[i], length + 1);
    }
    next.resource = from->resource; qa_resource_retain(next.resource);
    q3n_animation_dispose(holder); *holder = next;
    bool fixed_legs = ci->animations.fixed_legs, fixed_torso = ci->animations.fixed_torso;
    ci->animations = source->animations;
    ci->animations.fixed_legs = fixed_legs; ci->animations.fixed_torso = fixed_torso;
    memcpy(ci->models, source->models, sizeof(ci->models)); memcpy(ci->skins, source->skins, sizeof(ci->skins));
    memcpy(ci->sounds, source->sounds, sizeof(ci->sounds)); ci->icon = source->icon; ci->new_anims = source->new_anims;
    return true;
}
static bool media_match(const q3n_client_info *a, const q3n_client_info *b, int32_t game_type)
{
    return b->info_valid && !b->deferred && same(a->model_name, b->model_name) && same(a->skin_name, b->skin_name) &&
        same(a->head_model_name, b->head_model_name) && same(a->head_skin_name, b->head_skin_name) &&
        same(a->blue_team, b->blue_team) && same(a->red_team, b->red_team) && (game_type < 3 || a->team == b->team);
}
static bool media_equal(const q3n_client_info *a, const q3n_client_info *b)
{
    if (a->info_valid != b->info_valid || a->icon != b->icon || a->new_anims != b->new_anims ||
        memcmp(a->models, b->models, sizeof(a->models)) || memcmp(a->skins, b->skins, sizeof(a->skins)) ||
        memcmp(a->sounds, b->sounds, sizeof(a->sounds)) || a->animations.footsteps != b->animations.footsteps ||
        a->animations.gender != b->animations.gender || a->animations.fixed_legs != b->animations.fixed_legs ||
        a->animations.fixed_torso != b->animations.fixed_torso ||
        memcmp(a->animations.head_offset, b->animations.head_offset, sizeof(a->animations.head_offset))) return false;
    for (size_t i = 0; i < QA_PLAYER_ANIMATION_COUNT; ++i) {
        const qa_player_animation *x = &a->animations.animations[i], *y = &b->animations.animations[i];
        if (x->first_frame != y->first_frame || x->num_frames != y->num_frames || x->loop_frames != y->loop_frames ||
            x->frame_lerp != y->frame_lerp || x->initial_lerp != y->initial_lerp || x->reversed != y->reversed ||
            x->flipflop != y->flipflop || x->present != y->present) return false;
    }
    return true;
}
static bool new_info(q3n_clients *owner, uint32_t index, const char *info, uint64_t revision,
    uint32_t max_clients, int32_t game_type, const q3n_client_settings *settings, qa_error *error)
{
    q3n_client_info ci = {.observed = true, .physical_client = index, .configstring_revision = revision};
    q3n_animation_holder holder = {0}; bool ok = true;
    if (*info) {
        qa_q3_client_info_value(info, "n", ci.name, sizeof(ci.name));
        ci.color1 = color(integer_key(info, "c1")); ci.color2 = color(integer_key(info, "c2"));
        ci.bot_skill = integer_key(info, "skill"); ci.handicap = integer_key(info, "hc");
        ci.wins = integer_key(info, "w"); ci.losses = integer_key(info, "l"); ci.team = integer_key(info, "t");
        if (ci.team < 0 || ci.team > 3) return q3n_client_fail(error, QA_ERROR_FORMAT, "Invalid Q3 client-info team");
        ci.team_task = integer_key(info, "tt"); ci.team_leader = integer_key(info, "tl") != 0;
        qa_q3_client_info_value(info, "g_redteam", ci.red_team, sizeof(ci.red_team));
        qa_q3_client_info_value(info, "g_blueteam", ci.blue_team, sizeof(ci.blue_team));
        char model[1024], head[1024];
        qa_q3_client_info_value(info, "model", model, sizeof(model)); qa_q3_client_info_value(info, "hmodel", head, sizeof(head));
        const char *forced = owner->options.product == QA_Q3_TEAM_ARENA ? "james" : "sarge";
        model_skin(settings->force_model ? game_type >= 3 ? forced : settings->model : model, ci.model_name, ci.skin_name);
        model_skin(settings->force_model ? game_type >= 3 ? forced : settings->head_model : head, ci.head_model_name, ci.head_skin_name);
        if (settings->force_model && game_type >= 3) {
            const char *slash = strchr(model, '/'); if (slash) copy_text(ci.skin_name, 64, slash + 1);
            slash = strchr(head, '/'); if (slash) copy_text(ci.head_skin_name, 64, slash + 1);
        }
        bool reused = false;
        for (uint32_t i = 0; i < max_clients; ++i) if (media_match(&ci, &owner->clients[i], game_type)) {
            ok = copy_media(owner, i, &ci, &holder, error); reused = true; break;
        }
        bool requested_load = !reused;
        bool force_defer = settings->memory_remaining < 4000000;
        if (ok && !reused && (force_defer || (settings->defer_players && !settings->build_script && !settings->loading))) {
            bool exact = false;
            for (uint32_t i = 0; i < max_clients; ++i) {
                const q3n_client_info *match = &owner->clients[i];
                if (match->info_valid && !match->deferred && same(ci.skin_name, match->skin_name) &&
                    same(ci.model_name, match->model_name) && (game_type < 3 || ci.team == match->team)) { exact = true; break; }
            }
            if (!exact) for (uint32_t i = 0; i < max_clients; ++i) {
                const q3n_client_info *match = &owner->clients[i];
                if (match->info_valid && (game_type < 3 || (!match->deferred && same(ci.skin_name, match->skin_name) && ci.team == match->team))) {
                    ok = copy_media(owner, i, &ci, &holder, error); ci.deferred = true; reused = true; break;
                }
            }
            if (ok && !reused && game_type < 3) ok = print(owner, error, "CG_SetDeferredClientInfo: no valid clients!\n");
        }
        if (ok && !reused) ok = load(owner, &ci, &holder, game_type, settings, error);
        if (ok && requested_load && force_defer) { ok = print(owner, error, "Memory is low.  Using deferred model.\n"); ci.deferred = false; }
        ci.info_valid = ok;
    }
    if (ok && !current(owner, error)) ok = false;
    bool changed = !media_equal(&ci, &owner->clients[index]);
    if (ok && changed && owner->next_media_revision == UINT64_MAX) ok = q3n_client_fail(error, QA_ERROR_MEMORY, "Q3 client media revision exhausted");
    if (ok) {
        ci.media_revision = changed ? ++owner->next_media_revision : owner->clients[index].media_revision;
        q3n_animation_dispose(&owner->holders[index]); owner->holders[index] = holder;
        holder = (q3n_animation_holder){0}; owner->clients[index] = ci;
    }
    q3n_animation_dispose(&holder); return ok;
}
static bool create(const q3n_client_options *options, unsigned domain, q3n_clients **out, qa_error *error)
{
    if (!options || !options->content || !options->assets || !out || *out ||
        (domain==2 ? (!options->compiled_source || options->reader || options->remote_source) :
            domain==1 ? (!options->remote_source || options->reader || options->compiled_source) :
            (!options->reader || options->remote_source || options->compiled_source)) ||
        (options->product != QA_Q3_ARENA && options->product != QA_Q3_TEAM_ARENA))
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Invalid native Q3 client media services");
    if (domain==2) {
        q3n_compiled_source_view v;
        if (!q3n_compiled_source_checkpoint_read(options->compiled_source,&v,error) ||
            v.basis.content!=options->content || v.basis.assets!=options->assets || v.basis.product!=options->product)
            return q3n_client_fail(error,QA_ERROR_ARGUMENT,"Compiled client media has another content owner");
    } else if (domain==1) {
        q3n_remote_source_view source;
        if (!q3n_remote_source_read(options->remote_source, &source, error) ||
            source.basis.content != options->content || source.basis.product != options->product)
            return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Remote client media has another content/product owner");
    }
    q3n_clients *owner = calloc(1, sizeof(*owner));
    if (!owner) return q3n_client_fail(error, QA_ERROR_MEMORY, "Allocating native Q3 client media");
    owner->options = *options;
    for (uint32_t i = 0; i < 64; ++i) owner->clients[i].physical_client = i;
    *out = owner; return true;
}
bool q3n_clients_create(const q3n_client_options *options, q3n_clients **out, qa_error *error)
{ return create(options, 0, out, error); }
bool q3n_clients_create_remote(const q3n_client_options *options, q3n_clients **out, qa_error *error)
{ return create(options, 1, out, error); }
bool q3n_clients_create_compiled(const q3n_client_options *options,q3n_clients **out,qa_error *error)
{ return create(options,2,out,error); }
bool q3n_clients_idle(const q3n_clients *owner) { return owner && !owner->busy; }
qa_q3_presentation_assets *q3n_clients_assets(const q3n_clients *owner)
{ return owner ? owner->options.assets : NULL; }
bool q3n_clients_runtime_bound(const q3n_clients *owner, const q3n_client_options *source, qa_error *error)
{
    return q3n_clients_idle(owner) && source &&
        owner->options.content == source->content && owner->options.assets == source->assets &&
        owner->options.product == source->product && owner->options.reader == source->reader &&
        owner->options.remote_source == source->remote_source && owner->options.compiled_source==source->compiled_source ? true :
        q3n_client_fail(error, QA_ERROR_ARGUMENT, "Runtime requires its returned constructor-owned client media");
}
bool q3n_clients_runtime_dispose(q3n_clients *owner, const q3n_client_options *source, qa_error *error)
{
    if (!q3n_clients_runtime_bound(owner, source, error) || !qa_q3_assets_idle(owner->options.assets))
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Runtime disposal requires returned media acquisition callbacks");
    /* A failed command may have invalidated its live source. Dispose only
     * this retained owner's cells; do not read GS or fabricate newer stamps. */
    owner->busy = true;
    for (uint32_t i = 0; i < 64; ++i) {
        bool observed = owner->clients[i].observed;
        uint64_t config_revision = owner->clients[i].configstring_revision;
        uint64_t media_revision = owner->clients[i].media_revision;
        q3n_animation_dispose(&owner->holders[i]);
        owner->clients[i] = (q3n_client_info){.observed = observed, .physical_client = i,
            .configstring_revision = config_revision, .media_revision = media_revision};
    }
    owner->busy = false;
    return true;
}
bool q3n_clients_remote_current(const q3n_clients *owner, const q3n_remote_source_view *source, qa_error *error)
{
    return q3n_clients_idle(owner) && remote_current(owner, source, error) ? true :
        q3n_client_fail(error, QA_ERROR_ARGUMENT, "Remote client observation requires its idle actual source owner");
}
void q3n_clients_destroy(q3n_clients *owner)
{
    if (!q3n_clients_idle(owner)) return;
    for (uint32_t i = 0; i < 64; ++i) q3n_animation_dispose(&owner->holders[i]);
    free(owner);
}
const q3n_client_info *q3n_clients_get(const q3n_clients *owner, uint32_t index)
{ return owner && !owner->busy && index < 64 ? &owner->clients[index] : NULL; }
static bool begin(q3n_clients *owner, qa_application *app, const qa_application_native_q3_presentation *cut,
    const q3n_remote_source_view *remote, const q3n_client_settings *settings, qa_error *error)
{
    if (!q3n_clients_idle(owner) || (!remote && !owner->compiled_cut && (!app || !cut)) || !settings ||
        !memchr(settings->model, 0, 64) || !memchr(settings->head_model, 0, 64) ||
        !memchr(settings->red_team_name, 0, 64) || !memchr(settings->blue_team_name, 0, 64))
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client media requires its current physical source");
    if (!(remote ? remote_current(owner, remote, error) : source_current(owner, app, cut, error))) return false;
    const char *serverinfo; uint64_t revision;
    if (!source_string(owner, 0, &serverinfo, &revision, error)) return false;
    char value[8192];
    if (!qa_q3_info_value(serverinfo, "g_gametype", value, sizeof(value), error)) return false;
    int32_t game_type = source_integer(value);
    if (!qa_q3_info_value(serverinfo, "sv_maxclients", value, sizeof(value), error)) return false;
    int32_t max_clients = source_integer(value);
    if (game_type < 0 || game_type > 7 || max_clients < 0 || max_clients > 64)
        return q3n_client_fail(error, QA_ERROR_FORMAT, "Invalid native Q3 reached client media serverinfo");
    owner->serverinfo_revision = revision; owner->game_type = game_type; owner->max_clients = (uint32_t)max_clients;
    owner->busy = true; owner->application = app; owner->cut = cut; owner->remote_cut = remote; return true;
}
static bool end(q3n_clients *owner, bool ok)
{
    owner->application = NULL; owner->cut = NULL; owner->remote_cut = NULL; owner->active_info = false;
    owner->serverinfo_revision = 0; owner->max_clients = 0; owner->game_type = 0;
    owner->busy = false; return ok;
}
static bool qualify_table(q3n_clients *owner, qa_error *error)
{
    for (uint32_t i = 0; i < 64; ++i) {
        const char *text; uint64_t revision;
        if (!source_string(owner,
            544u + i, &text, &revision, error)) return false;
        if (!owner->clients[i].observed || revision != owner->clients[i].configstring_revision)
            return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client table changed during media registration");
    }
    return current(owner, error);
}
static bool register_one(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_remote_source_view *remote,
    const q3n_client_settings *settings,
    uint32_t index, qa_error *error)
{
    if (index>=64) return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client registration index is outside physical rows");
    if (!begin(owner,app,cut,remote,settings,error)) return false;
    const char *text; uint64_t revision;
    bool ok=source_string(owner,544u+index,&text,&revision,error);
    if (ok) {
        owner->active_info=true; owner->active_client=index; owner->active_configstring_revision=revision;
        ok=new_info(owner,index,text,revision,owner->max_clients,owner->game_type,settings,error);
    }
    return end(owner,ok);
}
static bool sync(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_remote_source_view *remote,
    const q3n_client_settings *settings,
    bool reload, qa_error *error)
{
    if (!begin(owner, app, cut, remote, settings, error)) return false;
    bool ok = true;
    for (uint32_t i = 0; i < 64 && ok; ++i) {
        const char *text; uint64_t revision = 0;
        ok = source_string(owner, 544u + i, &text, &revision, error);
        if (ok && (!owner->clients[i].observed || owner->clients[i].configstring_revision != revision || (reload && *text))) {
            owner->active_info = true; owner->active_client = i; owner->active_configstring_revision = revision;
            ok = new_info(owner, i, text, revision, owner->max_clients, owner->game_type, settings, error);
            owner->active_info = false;
        }
    }
    /* A registration callback may reach a different earlier client row.
     * Admit the complete reached client table before rendering. */
    if (ok) ok = qualify_table(owner, error);
    return end(owner, ok);
}
bool q3n_clients_sync(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_client_settings *settings, qa_error *error)
{ return sync(owner, app, cut, NULL, settings, false, error); }
bool q3n_clients_register_one(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_client_settings *settings,
    uint32_t index, qa_error *error)
{ return register_one(owner, app, cut, NULL, settings, index, error); }
bool q3n_clients_initialize(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_client_settings *settings,
    uint32_t seat, qa_error *error)
{
    uint32_t physical; qa_actor_id actor; qa_q3_player player; bool found;
    if (!qa_application_native_q3_presentation_local(app,cut,seat,&physical,&actor,&player,&found,error)) return false;
    if (!found) return q3n_client_fail(error,QA_ERROR_ARGUMENT,"Native Q3 client initialization requires its actual local viewing seat");
    qa_native_q3_wire_basis basis;
    if (!owner || !qa_native_q3_wire_reader_basis(owner->options.reader,&basis,error)) return false;
    if (basis.seat!=seat || basis.physical_client!=physical || !qa_actor_id_equal(basis.actor,actor))
        return q3n_client_fail(error,QA_ERROR_ARGUMENT,"Native Q3 client initialization has another wire recipient");
    if (!q3n_clients_register_one(owner,app,cut,settings,physical,error) ||
        !q3n_clients_sync(owner,app,cut,settings,error)) return false;
    uint32_t after; qa_actor_id after_actor;
    if (!qa_application_native_q3_presentation_local(app,cut,seat,&after,&after_actor,&player,&found,error)) return false;
    return found && after==physical && qa_actor_id_equal(actor,after_actor) ? true :
        q3n_client_fail(error,QA_ERROR_ARGUMENT,"Native Q3 local client actor changed during initialization");
}
bool q3n_clients_reload(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_client_settings *settings, qa_error *error)
{ return sync(owner, app, cut, NULL, settings, true, error); }
static bool reset(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_remote_source_view *remote, qa_error *error)
{
    if (!q3n_clients_idle(owner) || (!remote && !owner->compiled_cut && (!app || !cut)) || !qa_q3_assets_idle(owner->options.assets))
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client reset requires its idle genuine media owner");
    if (!(remote ? remote_current(owner, remote, error) : source_current(owner, app, cut, error))) return false;
    owner->busy = true; uint64_t revisions[64]; bool ok = true;
    for (uint32_t i = 0; i < 64 && ok; ++i) {
        const char *text;
        ok = source_string(owner, 544u + i, &text, &revisions[i], error);
    }
    if (ok) ok = remote ? remote_current(owner, remote, error) : source_current(owner, app, cut, error);
    for (uint32_t i = 0; i < 64 && ok; ++i) {
        const char *text; uint64_t revision;
        ok = source_string(owner, 544u + i, &text, &revision, error);
        if (ok && revision != revisions[i])
            ok = q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 reached client table changed during reset admission");
    }
    if (ok) ok = remote ? remote_current(owner, remote, error) : source_current(owner, app, cut, error);
    if (ok) {
        for (uint32_t i = 0; i < 64; ++i) {
            uint64_t media_revision = owner->clients[i].media_revision;
            q3n_animation_dispose(&owner->holders[i]);
            owner->clients[i] = (q3n_client_info){.observed = true, .physical_client = i,
                .configstring_revision = revisions[i], .media_revision = media_revision};
        }
    }
    owner->busy = false; return ok;
}
static bool load_deferred(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_remote_source_view *remote,
    const q3n_client_settings *settings, qa_error *error)
{
    if (!begin(owner, app, cut, remote, settings, error)) return false;
    bool ok = true;
    for (uint32_t i = 0; i < owner->max_clients && ok; ++i) {
        q3n_client_info *slot = &owner->clients[i]; if (!slot->info_valid || !slot->deferred) continue;
        const char *text; uint64_t revision = 0;
        ok = source_string(owner, 544u + i, &text, &revision, error);
        if (ok && revision != slot->configstring_revision) ok = q3n_client_fail(error, QA_ERROR_ARGUMENT, "Deferred Q3 client-info publication changed");
        owner->active_info = ok; owner->active_client = i; owner->active_configstring_revision = revision;
        if (!ok) break;
        if (settings->memory_remaining < 4000000) {
            ok = print(owner, error, "Memory is low.  Using deferred model.\n");
            if (ok) slot->deferred = false;
            owner->active_info = false; continue;
        }
        q3n_client_info next = *slot; q3n_animation_holder holder = {0};
        if (ok) ok = load(owner, &next, &holder, owner->game_type, settings, error);
        if (ok && owner->next_media_revision == UINT64_MAX) ok = q3n_client_fail(error, QA_ERROR_MEMORY, "Q3 client media revision exhausted");
        if (ok) { next.media_revision = ++owner->next_media_revision; q3n_animation_dispose(&owner->holders[i]);
            owner->holders[i] = holder; holder = (q3n_animation_holder){0}; *slot = next; }
        q3n_animation_dispose(&holder);
        owner->active_info = false;
    }
    if (ok) ok = qualify_table(owner, error);
    return end(owner, ok);
}
bool q3n_clients_custom_sound(q3n_clients *owner, int32_t index, const char *name, int32_t *out, qa_error *error)
{
    if (!q3n_clients_idle(owner) || !name || !out) return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Invalid native Q3 custom sound request");
    if (*name != '*') {
        owner->busy = true;
        bool ok = qa_q3_register_sound(owner->options.assets, name, false, out, error);
        owner->busy = false; return ok;
    }
    if (index < 0 || index >= 64) index = 0;
    for (size_t i = 0; i < sizeof(custom_sounds) / sizeof(*custom_sounds); ++i)
        if (!strcmp(name, custom_sounds[i])) { *out = owner->clients[index].sounds[i]; return true; }
    return q3n_client_fail(error, QA_ERROR_FORMAT, "Unknown Q3 custom sound");
}
static bool dynamic_write(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_remote_source_view *remote, uint32_t index,
    uint64_t configstring_revision, uint64_t media_revision, const q3n_client_dynamic *value, qa_error *error)
{
    if (!q3n_clients_idle(owner) || !value || index >= 64 || (!remote && !owner->compiled_cut && (!app || !cut)) ||
        (!remote && !owner->compiled_cut && cut->product != owner->options.product) || !owner->clients[index].observed ||
        owner->clients[index].configstring_revision != configstring_revision ||
        owner->clients[index].media_revision != media_revision)
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 dynamic client state requires its exact observed physical row");
    const char *text; uint64_t revision;
    if (!(remote ? remote_current(owner, remote, error) : source_current(owner, app, cut, error)) ||
        !source_string(owner, 544u + index, &text, &revision, error)) return false;
    if (revision != configstring_revision)
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 dynamic client publication was superseded");
    owner->clients[index].dynamic = *value; return true;
}
bool q3n_clients_reset(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, qa_error *error)
{ return reset(owner, app, cut, NULL, error); }
bool q3n_clients_load_deferred(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, const q3n_client_settings *settings, qa_error *error)
{ return load_deferred(owner, app, cut, NULL, settings, error); }
bool q3n_clients_dynamic_write(q3n_clients *owner, qa_application *app,
    const qa_application_native_q3_presentation *cut, uint32_t index,
    uint64_t config_revision, uint64_t media_revision, const q3n_client_dynamic *value, qa_error *error)
{ return dynamic_write(owner, app, cut, NULL, index, config_revision, media_revision, value, error); }
bool q3n_clients_remote_register_one(q3n_clients *owner, const q3n_remote_source_view *source,
    const q3n_client_settings *settings, uint32_t index, qa_error *error)
{ return register_one(owner, NULL, NULL, source, settings, index, error); }
bool q3n_clients_remote_sync(q3n_clients *owner, const q3n_remote_source_view *source,
    const q3n_client_settings *settings, qa_error *error)
{ return sync(owner, NULL, NULL, source, settings, false, error); }
bool q3n_clients_remote_reload(q3n_clients *owner, const q3n_remote_source_view *source,
    const q3n_client_settings *settings, qa_error *error)
{ return sync(owner, NULL, NULL, source, settings, true, error); }
bool q3n_clients_remote_initialize(q3n_clients *owner, const q3n_remote_source_view *source,
    const q3n_client_settings *settings, qa_error *error)
{
    if (!source || !owner) return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Remote initialization requires its actual source owner");
    if (!remote_current(owner, source, error)) return false;
    return q3n_clients_remote_register_one(owner, source, settings, source->basis.physical_client, error) &&
        q3n_clients_remote_sync(owner, source, settings, error) && remote_current(owner, source, error);
}
bool q3n_clients_remote_reset(q3n_clients *owner, const q3n_remote_source_view *source, qa_error *error)
{ return reset(owner, NULL, NULL, source, error); }
bool q3n_clients_remote_load_deferred(q3n_clients *owner, const q3n_remote_source_view *source,
    const q3n_client_settings *settings, qa_error *error)
{ return load_deferred(owner, NULL, NULL, source, settings, error); }
bool q3n_clients_remote_dynamic_write(q3n_clients *owner, const q3n_remote_source_view *source,
    uint32_t index, uint64_t config_revision, uint64_t media_revision,
    const q3n_client_dynamic *value, qa_error *error)
{ return dynamic_write(owner, NULL, NULL, source, index, config_revision, media_revision, value, error); }
bool q3n_clients_compiled_current(const q3n_clients *o,const q3n_compiled_source_view *v,qa_error *e)
{ return q3n_clients_idle(o) && compiled_current(o,v,e); }
static bool compiled_enter(q3n_clients *o,const q3n_compiled_source_view *v,qa_error *e)
{
    if (!q3n_clients_idle(o) || o->compiled_cut || !compiled_current(o,v,e))
        return q3n_client_fail(e,QA_ERROR_ARGUMENT,"Compiled client operation requires its returned real source");
    o->compiled_cut=v; return true;
}
static bool compiled_leave(q3n_clients *o,bool result)
{ o->compiled_cut=NULL; return result; }
bool q3n_clients_compiled_sync(q3n_clients *o,const q3n_compiled_source_view *v,const q3n_client_settings *s,qa_error *e)
{ return compiled_enter(o,v,e) && compiled_leave(o,sync(o,NULL,NULL,NULL,s,false,e)); }
bool q3n_clients_compiled_register_one(q3n_clients *o,const q3n_compiled_source_view *v,const q3n_client_settings *s,uint32_t n,qa_error *e)
{ return compiled_enter(o,v,e) && compiled_leave(o,register_one(o,NULL,NULL,NULL,s,n,e)); }
bool q3n_clients_compiled_reload(q3n_clients *o,const q3n_compiled_source_view *v,const q3n_client_settings *s,qa_error *e)
{ return compiled_enter(o,v,e) && compiled_leave(o,sync(o,NULL,NULL,NULL,s,true,e)); }
bool q3n_clients_compiled_reset(q3n_clients *o,const q3n_compiled_source_view *v,qa_error *e)
{ return compiled_enter(o,v,e) && compiled_leave(o,reset(o,NULL,NULL,NULL,e)); }
bool q3n_clients_compiled_load_deferred(q3n_clients *o,const q3n_compiled_source_view *v,const q3n_client_settings *s,qa_error *e)
{ return compiled_enter(o,v,e) && compiled_leave(o,load_deferred(o,NULL,NULL,NULL,s,e)); }
bool q3n_clients_compiled_dynamic_write(q3n_clients *o,const q3n_compiled_source_view *v,uint32_t n,
    uint64_t config,uint64_t media,const q3n_client_dynamic *value,qa_error *e)
{ return compiled_enter(o,v,e) && compiled_leave(o,dynamic_write(o,NULL,NULL,NULL,n,config,media,value,e)); }
bool q3n_clients_compiled_initialize(q3n_clients *o,const q3n_compiled_source_view *v,const q3n_client_settings *s,qa_error *e)
{
    if (!v || v->basis.client_number<0) return q3n_client_fail(e,QA_ERROR_ARGUMENT,"Compiled initialization requires its actual physical viewing client");
    return q3n_clients_compiled_register_one(o,v,s,(uint32_t)v->basis.client_number,e) &&
        q3n_clients_compiled_sync(o,v,s,e) && compiled_current(o,v,e);
}
bool q3n_clients_animation_holder(const q3n_clients *owner, uint32_t index,
    const qa_resource **resource, const qa_vfs_acquisition **receipt, qa_error *error)
{
    if (!q3n_clients_idle(owner) || index >= 64 || !resource || !receipt)
        return q3n_client_fail(error, QA_ERROR_ARGUMENT, "Native Q3 animation inventory requires its idle physical owner");
    *resource = owner->holders[index].resource; *receipt = *resource ? &owner->holders[index].receipt : NULL; return true;
}
