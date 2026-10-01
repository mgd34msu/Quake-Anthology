#include "selected_authored_media_internal.h"

static char *copy(const char *text, qa_error *error)
{
    size_t length = strlen(text);
    if (length == SIZE_MAX) { q3p_fail(error, QA_ERROR_MEMORY, "Authored Q3 path exceeds address space"); return NULL; }
    char *out = malloc(length + 1);
    if (!out) { q3p_fail(error, QA_ERROR_MEMORY, "Retaining authored Q3 media declaration"); return NULL; }
    memcpy(out, text, length + 1); return out;
}
static char *part_path(const char *gun, const char *suffix, qa_error *error)
{
    const char *slash = strrchr(gun, '/'), *dot = strrchr(gun, '.');
    size_t stem = dot && (!slash || dot > slash) ? (size_t)(dot - gun) : strlen(gun);
    size_t tail = strlen(suffix);
    if (stem > SIZE_MAX - tail - 1) { q3p_fail(error, QA_ERROR_MEMORY, "Authored Q3 part path exceeds address space"); return NULL; }
    char *out = malloc(stem + tail + 1);
    if (!out) { q3p_fail(error, QA_ERROR_MEMORY, "Retaining authored Q3 part path"); return NULL; }
    memcpy(out, gun, stem); memcpy(out + stem, suffix, tail + 1); return out;
}
bool q3n_selected_authored_idle(const q3n_selected_authored_media *owner)
{ return owner && !owner->busy; }
void q3n_selected_authored_destroy(q3n_selected_authored_media *owner)
{
    if (!q3n_selected_authored_idle(owner)) return;
    for (size_t i = 0; i < owner->options.attachment_count; ++i) {
        free((char *)owner->attachments[i].path); free((char *)owner->attachments[i].tag);
    }
    free(owner->attachments); free(owner->attachment_models);
    free(owner->gun); free(owner->anchor); free(owner->anchor_tag);
    free(owner->barrel); free(owner->flash); free(owner);
}
bool q3n_selected_authored_create(const q3n_selected_authored_options *options,
    q3n_selected_authored_media **out, qa_error *error)
{
    if (!options || !out || *out || !options->content || !options->assets ||
        !options->gun || !*options->gun || !options->anchor || !*options->anchor ||
        !options->anchor_tag || !*options->anchor_tag ||
        (options->attachment_count && !options->attachments) ||
        options->attachment_count > SIZE_MAX / sizeof(q3n_selected_authored_attachment) ||
        options->attachment_count > SIZE_MAX / sizeof(int32_t) ||
        options->assets->options.provider.mounts != options->content ||
        options->assets->options.provider.family != QA_SCENE_Q3 || options->assets->options.select)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 media requires its genuine declaration and selected registry");
    for (size_t i = 0; i < options->attachment_count; ++i)
        if (!options->attachments[i].path || !*options->attachments[i].path ||
            !options->attachments[i].tag || !*options->attachments[i].tag)
            return q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 attachment lacks its model or tag");
    q3n_selected_authored_media *owner = calloc(1, sizeof(*owner));
    if (!owner) return q3p_fail(error, QA_ERROR_MEMORY, "Allocating authored Q3 media owner");
    owner->options = *options; owner->options.attachments = NULL; owner->options.attachment_count = 0;
    owner->gun = copy(options->gun, error); owner->anchor = copy(options->anchor, error);
    owner->anchor_tag = copy(options->anchor_tag, error);
    owner->barrel = part_path(options->gun, "_barrel.md3", error);
    owner->flash = part_path(options->gun, "_flash.md3", error);
    bool okay = owner->gun && owner->anchor && owner->anchor_tag && owner->barrel && owner->flash;
    if (okay && options->attachment_count) {
        owner->attachments = calloc(options->attachment_count, sizeof(*owner->attachments));
        owner->attachment_models = calloc(options->attachment_count, sizeof(*owner->attachment_models));
        if (!owner->attachments || !owner->attachment_models)
            okay = q3p_fail(error, QA_ERROR_MEMORY, "Retaining authored Q3 attachments");
    }
    for (size_t i = 0; okay && i < options->attachment_count; ++i) {
        owner->attachments[i].path = copy(options->attachments[i].path, error);
        owner->attachments[i].tag = copy(options->attachments[i].tag, error);
        owner->options.attachment_count = i + 1;
        okay = owner->attachments[i].path && owner->attachments[i].tag;
    }
    if (!okay) { q3n_selected_authored_destroy(owner); return false; }
    owner->options.gun = owner->gun; owner->options.anchor = owner->anchor;
    owner->options.anchor_tag = owner->anchor_tag; owner->options.attachments = owner->attachments;
    *out = owner; return true;
}
bool q3n_selected_authored_options_read(const q3n_selected_authored_media *owner,
    q3n_selected_authored_options *out, qa_error *error)
{
    if (!q3n_selected_authored_idle(owner) || !out)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 options require their idle retained owner");
    *out = owner->options; return true;
}
static bool current(void *context, bool (*read)(void *), qa_error *error)
{ return read(context) || q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 gear observation was superseded"); }
static bool model(q3n_selected_authored_media *owner, const char *path, bool required,
    int32_t *out, void *context, bool (*read)(void *), qa_error *error)
{
    return qa_q3_register_model(owner->options.assets, path, out, error) && current(context, read, error) &&
        (!required || *out || q3p_fail(error, QA_ERROR_NOT_FOUND, "Authored Q3 gear model is absent"));
}
static q3n_selected_weapon_media tuple(const q3n_selected_authored_media *owner, bool view)
{
    return (q3n_selected_weapon_media){.assets = owner->options.assets, .gun = owner->gun_model,
        .hands = view ? owner->hands : 0, .barrel = view ? 0 : owner->barrel_model,
        .flash = view ? 0 : owner->flash_model, .invisibility = view ? 0 : owner->invisibility,
        .battle_weapon = view ? 0 : owner->battle_weapon, .quad_weapon = view ? 0 : owner->quad_weapon};
}
bool q3n_selected_authored_prepare(q3n_selected_authored_media *owner, bool view,
    void *context, bool (*read)(void *), q3n_selected_weapon_media *out, qa_error *error)
{
    if (!q3n_selected_authored_idle(owner) || !read || !out || !qa_q3_assets_idle(owner->options.assets))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 admission requires its actual idle observation and registry");
    if (!current(context, read, error)) return false;
    int32_t *models = owner->options.attachment_count ?
        calloc(owner->options.attachment_count, sizeof(*models)) : NULL;
    if (owner->options.attachment_count && !models)
        return q3p_fail(error, QA_ERROR_MEMORY, "Preparing authored Q3 attachment handles");
    if (models) memcpy(models, owner->attachment_models, owner->options.attachment_count * sizeof(*models));
    q3n_selected_authored_media candidate = *owner;
    candidate.attachment_models = models;
    owner->busy = true; bool okay = true;
    if (!candidate.gun_ready) {
        okay = model(&candidate, candidate.gun, true, &candidate.gun_model, context, read, error);
        if (okay) candidate.gun_ready = true;
    }
    if (okay && view && !candidate.view_ready) {
        okay = model(&candidate, candidate.anchor, false, &candidate.hands, context, read, error);
        if (okay && !candidate.hands) {
            candidate.hands_fallback = true;
            okay = model(&candidate, "models/weapons2/shotgun/shotgun_hand.md3", true, &candidate.hands, context, read, error);
        }
        for (size_t i = 0; okay && i < candidate.options.attachment_count; ++i)
            okay = model(&candidate, candidate.attachments[i].path, true, &candidate.attachment_models[i], context, read, error);
        if (okay) candidate.view_ready = true;
    }
    if (okay && !view && !candidate.world_ready) {
        okay = model(&candidate, candidate.barrel, false, &candidate.barrel_model, context, read, error) &&
            model(&candidate, candidate.flash, false, &candidate.flash_model, context, read, error) &&
            qa_q3_register_shader(candidate.options.assets, "powerups/invisibility", true, &candidate.invisibility, error) && current(context, read, error) &&
            qa_q3_register_shader(candidate.options.assets, "powerups/battleWeapon", true, &candidate.battle_weapon, error) && current(context, read, error) &&
            qa_q3_register_shader(candidate.options.assets, "powerups/quadWeapon", true, &candidate.quad_weapon, error) && current(context, read, error);
        if (okay) candidate.world_ready = true;
    }
    if (okay) okay = current(context, read, error);
    if (okay) {
        free(owner->attachment_models); *owner = candidate; models = NULL;
        *out = tuple(owner, view);
    }
    owner->busy = false;
    free(models); return okay;
}
bool q3n_selected_authored_read(const q3n_selected_authored_media *owner, bool view,
    q3n_selected_weapon_media *out, qa_error *error)
{
    if (!q3n_selected_authored_idle(owner) || !out || !owner->gun_ready ||
        (view ? !owner->view_ready : !owner->world_ready) || !q3n_selected_authored_valid(owner, false, error))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 media has no completed admitted route");
    *out = tuple(owner, view); return true;
}
size_t q3n_selected_authored_attachment_count(const q3n_selected_authored_media *owner)
{ return owner ? owner->options.attachment_count : 0; }
bool q3n_selected_authored_attachment_read(const q3n_selected_authored_media *owner, size_t index,
    q3n_selected_authored_attachment_view *out, qa_error *error)
{
    if (!q3n_selected_authored_idle(owner) || !owner->view_ready || !out || index >= owner->options.attachment_count)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 attachment requires its completed view admission");
    *out = (q3n_selected_authored_attachment_view){owner->attachments[index].tag, owner->attachment_models[index]}; return true;
}
