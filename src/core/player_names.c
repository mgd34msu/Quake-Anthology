#include "qa/player_names.h"
#include <stdio.h>
#include <string.h>

qa_hud_q2_player_names qa_hud_q2_appearance_names_read(const char *appearance, bool rerelease)
{
    const char *slash = strchr(appearance, '/');
    const char *end = strchr(appearance, '\\');
    if (slash && end && slash > end) slash = NULL;
    if (!slash && !rerelease) slash = end;
    const char *skin = slash ? slash + 1 : "grunt";
    const char *skin_end = strchr(skin, '\\');
    const char *appearance_end = slash ? skin_end : end;
    const char *tag = appearance_end && appearance_end[1] ? appearance_end + 1 : "default";
    const char *tag_end = strchr(tag, '\\');
    return (qa_hud_q2_player_names){
        .appearance = {(const uint8_t *)appearance, appearance_end ? (size_t)(appearance_end - appearance) : strlen(appearance)},
        .tag = {(const uint8_t *)tag, tag_end ? (size_t)(tag_end - tag) : strlen(tag)},
        .model = slash && slash != appearance ? (qa_bytes){(const uint8_t *)appearance, (size_t)(slash - appearance)} :
            (qa_bytes){(const uint8_t *)"male", 4},
        .skin = {(const uint8_t *)skin, skin_end ? (size_t)(skin_end - skin) : strlen(skin)}};
}
qa_hud_q2_player_names qa_hud_q2_player_names_read(const char *info, bool rerelease)
{
    const char *first = strchr(info, '\\');
    qa_hud_q2_player_names value = qa_hud_q2_appearance_names_read(first && first[1] ? first + 1 : "male/grunt", rerelease);
    value.name = (qa_bytes){(const uint8_t *)info, first ? (size_t)(first - info) : 0};
    return value;
}
bool qa_player_skin_bind(qa_strings *strings, const char *text, bool rerelease,
    qa_string_id *skin, qa_string_id *model, qa_error *error)
{
    qa_hud_q2_player_names names = qa_hud_q2_appearance_names_read(text, rerelease);
    qa_string_id skin_id, model_id;
    if (!qa_strings_intern_cstr(strings, text, &skin_id, error) ||
        !qa_strings_intern(strings, names.model, &model_id, error)) return false;
    *skin = skin_id; *model = model_id;
    return true;
}

bool qa_hud_q2_player_model_path(qa_bytes model, qa_bytes member, const char *suffix,
    char *path, size_t capacity, qa_error *error)
{
    int n = snprintf(path, capacity, "players/%.*s/%.*s%s", (int)model.size, (const char *)model.data,
        (int)member.size, (const char *)member.data, suffix);
    if (n < 0 || (size_t)n >= capacity) { qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 player model path is too long"); return false; }
    return true;
}
bool qa_hud_q2_player_icon_path(qa_bytes appearance,char *path,size_t capacity,qa_error *error)
{
    int n=snprintf(path,capacity,"/players/%.*s_i.pcx",(int)appearance.size,(const char *)appearance.data);
    if(n<0||(size_t)n>=capacity) {qa_error_set(error,QA_ERROR_FORMAT,0,"Q2 player icon path is too long");return false;}
    return true;
}
bool qa_hud_q2_player_icon(qa_strings *strings,const char *appearance,size_t length,qa_string_id *icon,qa_error *error)
{
    char path[4096];
    if (!qa_hud_q2_player_icon_path((qa_bytes){(const uint8_t *)appearance,length},path,sizeof(path),error)) return false;
    return qa_strings_intern_cstr(strings,path,icon,error);
}
bool qa_hud_q2_player_ids_bind(qa_strings *strings, const char *info, bool rerelease,
    qa_hud_q2_player_ids *out, qa_error *error)
{
    qa_hud_q2_player_names names = qa_hud_q2_player_names_read(info, rerelease);
    qa_hud_q2_player_ids value = {0};
    char path[4096];
    if (!qa_strings_intern(strings, names.name, &value.name, error) ||
        !qa_strings_intern(strings, names.appearance, &value.appearance, error) ||
        !qa_strings_intern(strings, names.model, &value.model, error) ||
        !qa_strings_intern(strings, names.skin, &value.skin, error) ||
        !qa_hud_q2_player_icon(strings, (const char *)names.appearance.data, names.appearance.size, &value.icon, error)) return false;
    int n = snprintf(path, sizeof(path), "/tags/%.*s.pcx", (int)names.tag.size, (const char *)names.tag.data);
    if (n < 0 || (size_t)n >= sizeof(path) || !qa_strings_intern_cstr(strings, path, &value.tag, error)) return false;
    n = snprintf(path, sizeof(path), "%.*s.pcx", (int)names.tag.size, (const char *)names.tag.data);
    if (n < 0 || (size_t)n >= sizeof(path) || !qa_strings_intern_cstr(strings, path, &value.native_tag, error)) return false;
    *out = value;
    return true;
}
bool qa_hud_q2_player_picture_names(qa_strings *strings, const char *info, qa_string_id *icon, qa_string_id *dogtag, qa_error *error)
{
    qa_hud_q2_player_ids names;
    if (!qa_hud_q2_player_ids_bind(strings, info, true, &names, error)) return false;
    *icon = names.icon; *dogtag = names.tag;
    return true;
}

static bool component(qa_bytes text)
{
    if (!text.size || text.size >= 64 || (text.size <= 2 && text.data[0] == '.' &&
        (text.size == 1 || text.data[1] == '.'))) return false;
    for (size_t i = 0; i < text.size; ++i) {
        unsigned char c = text.data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || strchr("_+.-", c))) return false;
    }
    return true;
}
static bool model_is(qa_bytes model, const char *name)
{
    size_t i = 0;
    for (; i < model.size && name[i]; ++i) {
        unsigned char c = model.data[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)name[i]) return false;
    }
    return i == model.size && !name[i];
}
static bool media_path(qa_strings *strings, qa_bytes model, qa_bytes member, const char *suffix,
    qa_q2_player_media_kind kind, qa_q2_player_media_probe probe, void *context,
    qa_string_id *out, bool *found, qa_error *error)
{
    char path[4096];
    if (kind == QA_Q2_PLAYER_MEDIA_ICON) {
        char appearance[128];
        int n = snprintf(appearance, sizeof(appearance), "%.*s/%.*s", (int)model.size,
            (const char *)model.data, (int)member.size, (const char *)member.data);
        if (n < 0 || (size_t)n >= sizeof(appearance)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 player appearance is too long"); return false;
        }
        if (!qa_hud_q2_player_icon_path((qa_bytes){(const uint8_t *)appearance, (size_t)n}, path, sizeof(path), error)) return false;
    } else if (!qa_hud_q2_player_model_path(model, member, suffix, path, sizeof(path), error)) return false;
    return qa_strings_intern_cstr(strings, path, out, error) && probe(context, *out, kind, found, error);
}
bool qa_q2_player_media_resolve(qa_strings *strings, const qa_hud_q2_player_ids *names,
    const qa_string_id *weapons, size_t count, bool rerelease, double noskins, bool base_client, bool body,
    qa_q2_player_media_probe probe, void *context, qa_q2_player_media *out, qa_error *error)
{
    static const qa_bytes male = {(const uint8_t *)"male", 4}, female = {(const uint8_t *)"female", 6},
        grunt = {(const uint8_t *)"grunt", 5}, athena = {(const uint8_t *)"athena", 6},
        tris = {(const uint8_t *)"tris.md2", 8}, default_weapon = {(const uint8_t *)"weapon.md2", 10};
    qa_q2_player_media value = *out;
    qa_bytes model = qa_strings_text(strings, body ? names->model : value.model_name);
    if (body) {
        qa_bytes skin = qa_strings_text(strings, names->skin);
        if (rerelease && (noskins == 2 || !component(skin))) {
            if (model_is(model, "female")) { model = female; skin = athena; }
            else { model = male; skin = grunt; }
        } else if (noskins != 0 || !component(model) || !component(skin)) { model = male; skin = grunt; }
        bool model_found, skin_found, icon_found;
        if (!media_path(strings, model, tris, "", QA_Q2_PLAYER_MEDIA_MODEL, probe, context, &value.model, &model_found, error)) return false;
        if (!model_found && !model_is(model, "male")) {
            model = male;
            if (!media_path(strings, model, tris, "", QA_Q2_PLAYER_MEDIA_MODEL, probe, context, &value.model, &model_found, error)) return false;
        }
        if (!media_path(strings, model, skin, ".pcx", QA_Q2_PLAYER_MEDIA_SKIN, probe, context, &value.skin, &skin_found, error)) return false;
        if (!skin_found && rerelease && model_is(model, "female")) {
            skin = athena;
            if (!media_path(strings, model, skin, ".pcx", QA_Q2_PLAYER_MEDIA_SKIN, probe, context, &value.skin, &skin_found, error)) return false;
        }
        if (!skin_found && !model_is(model, "male")) {
            model = male;
            if (!media_path(strings, model, tris, "", QA_Q2_PLAYER_MEDIA_MODEL, probe, context, &value.model, &model_found, error) ||
                !media_path(strings, model, skin, ".pcx", QA_Q2_PLAYER_MEDIA_SKIN, probe, context, &value.skin, &skin_found, error)) return false;
        }
        if (!skin_found) {
            skin = grunt;
            if (!media_path(strings, model, skin, ".pcx", QA_Q2_PLAYER_MEDIA_SKIN, probe, context, &value.skin, &skin_found, error)) return false;
        }
        if (!media_path(strings, model, skin, "_i.pcx", QA_Q2_PLAYER_MEDIA_ICON, probe, context, &value.icon, &icon_found, error) ||
            !qa_strings_intern(strings, model, &value.model_name, error)) return false;
        value.body_valid = model_found && skin_found && (rerelease || icon_found);
    }
    memset(value.weapons, 0, sizeof(value.weapons)); value.weapon_valid = false;
    if (count > 32) count = 32;
    for (size_t i = 0; i < count; ++i) {
        qa_bytes weapon = qa_strings_text(strings, weapons[i]); bool found;
        if (!media_path(strings, model, weapon, "", QA_Q2_PLAYER_MEDIA_MODEL, probe, context, value.weapons + i, &found, error)) return false;
        if (!found && model_is(model, "cyborg"))
            if (!media_path(strings, male, weapon, "", QA_Q2_PLAYER_MEDIA_MODEL, probe, context, value.weapons + i, &found, error)) return false;
        if (!found && i) { value.weapons[i] = value.weapons[0]; found = value.weapon_valid; }
        if (!found && !base_client)
            if (!media_path(strings, male, default_weapon, "", QA_Q2_PLAYER_MEDIA_MODEL, probe, context, value.weapons + i, &found, error)) return false;
        if (!i) value.weapon_valid = found;
    }
    *out = value;
    return true;
}
