#include "selected_authored_media_internal.h"

static bool model_handle(const q3n_selected_authored_media *owner, const char *path,
    int32_t handle, bool required, qa_error *error)
{
    qa_q3_presentation_assets *assets = owner->options.assets;
    q3p_name *name = q3p_find_name(assets, Q3P_MODEL, path);
    if (!name || name->handle != handle || handle < 0 ||
        (size_t)handle > assets->model_count || (required && !handle))
        return q3p_fail(error, QA_ERROR_FORMAT, "Authored Q3 model lost its exact retained registration");
    if (!handle) return true;
    const q3p_model *model = assets->models[handle - 1];
    return model && !model->world && model->resource && model->provider.mounts == owner->options.content &&
        model->provider.images == assets->options.provider.images &&
        model->provider.materials == assets->options.provider.materials ? true :
        q3p_fail(error, QA_ERROR_FORMAT, "Authored Q3 model leaves its actual selected content namespace");
}
static bool shader_handle(const q3n_selected_authored_media *owner, const char *path,
    int32_t handle, qa_error *error)
{
    qa_q3_presentation_assets *assets = owner->options.assets;
    if (handle < 0 || (size_t)handle > assets->shader_count || (handle && !assets->shaders[handle - 1]))
        return q3p_fail(error, QA_ERROR_FORMAT, "Authored Q3 shader holder is absent");
    q3p_name *name = q3p_find_name(assets, Q3P_SHADER, path);
    if (!handle) return !name || !name->handle ? true :
        q3p_fail(error, QA_ERROR_FORMAT, "Absent authored Q3 shader conflicts with its retained registration");
    return name && name->handle == handle ? true :
        q3p_fail(error, QA_ERROR_FORMAT, "Authored Q3 shader lost its source registration");
}
bool q3n_selected_authored_valid(const q3n_selected_authored_media *owner,
    bool capture, qa_error *error)
{
    const qa_q3_presentation_assets *assets = owner ? owner->options.assets : NULL;
    if (!assets || assets->options.provider.mounts != owner->options.content ||
        assets->options.provider.family != QA_SCENE_Q3 || assets->options.select || assets->codec_busy ||
        (capture ? !assets->capturing || assets->busy != 1 : assets->capturing || assets->busy != 0))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 qualification requires its real registry lease");
    if (owner->gun_ready) {
        if (!model_handle(owner, owner->gun, owner->gun_model, true, error)) return false;
    } else if (owner->gun_model || owner->view_ready || owner->world_ready)
        return q3p_fail(error, QA_ERROR_FORMAT, "Unadmitted authored Q3 gun has live media");
    if (owner->view_ready) {
        if (owner->hands_fallback) {
            if (!model_handle(owner, owner->anchor, 0, false, error) ||
                !model_handle(owner, "models/weapons2/shotgun/shotgun_hand.md3", owner->hands, true, error)) return false;
        } else if (!model_handle(owner, owner->anchor, owner->hands, true, error)) return false;
        for (size_t i = 0; i < owner->options.attachment_count; ++i)
            if (!model_handle(owner, owner->attachments[i].path, owner->attachment_models[i], true, error)) return false;
    } else {
        if (owner->hands || owner->hands_fallback)
            return q3p_fail(error, QA_ERROR_FORMAT, "Unadmitted authored Q3 view has live hands");
        for (size_t i = 0; i < owner->options.attachment_count; ++i)
            if (owner->attachment_models[i])
                return q3p_fail(error, QA_ERROR_FORMAT, "Unadmitted authored Q3 view has live attachments");
    }
    if (owner->world_ready) {
        if (!model_handle(owner, owner->barrel, owner->barrel_model, false, error) ||
            !model_handle(owner, owner->flash, owner->flash_model, false, error) ||
            !shader_handle(owner, "powerups/invisibility", owner->invisibility, error) ||
            !shader_handle(owner, "powerups/battleWeapon", owner->battle_weapon, error) ||
            !shader_handle(owner, "powerups/quadWeapon", owner->quad_weapon, error)) return false;
    } else if (owner->barrel_model || owner->flash_model || owner->invisibility || owner->battle_weapon || owner->quad_weapon)
        return q3p_fail(error, QA_ERROR_FORMAT, "Unadmitted authored Q3 held route has live media");
    return true;
}
static bool exact_text(qa_source_save_io *io, const char *expected)
{
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(expected) : 0;
    if (!qa_source_save_count(io, &length, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX) ||
        length != strlen(expected)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)expected, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset ||
        memcmp(io->input.data + io->offset, expected, length)) return false;
    io->offset += length; return true;
}
static bool fields(qa_source_save_io *io, q3n_selected_authored_media *owner,
    const q3n_selected_media_refs *refs)
{
    uint8_t magic[4] = {'Q','3','S','A'}; uint32_t version = 1; uint64_t view = 0;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "Q3SA", 4) ||
        !qa_source_save_u32(io, &version) || version != 1 ||
        (io->direction == QA_SOURCE_SAVE_WRITE && !refs->view_encode(refs->context, owner->options.content, &view, io->error)) ||
        !qa_source_save_u64(io, &view) || !view) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        qa_vfs *resolved = NULL;
        if (!refs->view_decode(refs->context, view, &resolved, io->error) || resolved != owner->options.content) return false;
    }
    size_t count = owner->options.attachment_count;
    if (!exact_text(io, owner->gun) || !exact_text(io, owner->anchor) || !exact_text(io, owner->anchor_tag) ||
        !qa_source_save_count(io, &count, owner->options.attachment_count) || count != owner->options.attachment_count)
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!exact_text(io, owner->attachments[i].path) || !exact_text(io, owner->attachments[i].tag) ||
            !qa_source_save_i32(io, &owner->attachment_models[i])) return false;
    return qa_source_save_bool(io, &owner->gun_ready) && qa_source_save_bool(io, &owner->view_ready) &&
        qa_source_save_bool(io, &owner->world_ready) && qa_source_save_bool(io, &owner->hands_fallback) &&
        qa_source_save_i32(io, &owner->gun_model) && qa_source_save_i32(io, &owner->hands) &&
        qa_source_save_i32(io, &owner->barrel_model) && qa_source_save_i32(io, &owner->flash_model) &&
        qa_source_save_i32(io, &owner->invisibility) && qa_source_save_i32(io, &owner->battle_weapon) &&
        qa_source_save_i32(io, &owner->quad_weapon) && q3n_selected_authored_valid(owner, true, io->error);
}
static bool ready(const q3n_selected_authored_media *owner,
    const q3n_selected_media_refs *refs, qa_error *error)
{
    const qa_q3_presentation_assets *assets = owner ? owner->options.assets : NULL;
    return q3n_selected_authored_idle(owner) && refs && refs->view_encode && refs->view_decode &&
        assets && assets->capturing && assets->busy == 1 && !assets->codec_busy ? true :
        q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 codec requires its actual capture and content dictionary");
}
bool q3n_selected_authored_checkpoint(const q3n_selected_authored_media *borrowed,
    const q3n_selected_media_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !ready(borrowed, refs, error)) return false;
    q3n_selected_authored_media *owner = (q3n_selected_authored_media *)borrowed;
    owner->busy = true;
    q3n_selected_authored_media copy = *owner; qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && fields(&io, &copy, refs) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); owner->busy = false;
    if (!okay && error && error->code == QA_OK) q3p_fail(error, QA_ERROR_FORMAT, "Authored Q3 checkpoint lost its real media");
    return okay;
}
bool q3n_selected_authored_restore(q3n_selected_authored_media *owner,
    const q3n_selected_media_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!ready(owner, refs, error) || owner->gun_ready || owner->view_ready || owner->world_ready)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Authored Q3 import requires its empty constructor cache");
    q3n_selected_authored_media candidate = *owner;
    candidate.attachment_models = owner->options.attachment_count ?
        calloc(owner->options.attachment_count, sizeof(*candidate.attachment_models)) : NULL;
    if (owner->options.attachment_count && !candidate.attachment_models)
        return q3p_fail(error, QA_ERROR_MEMORY, "Retaining authored Q3 imported attachments");
    qa_source_save_io io = {0}; owner->busy = true;
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, &candidate, refs) && qa_source_save_finish(&io, NULL);
    if (okay) {
        free(owner->attachment_models); *owner = candidate; owner->busy = true;
        candidate.attachment_models = NULL;
    } else if (error && error->code == QA_OK) q3p_fail(error, QA_ERROR_FORMAT, "Saved authored Q3 media differs from its declared registry");
    qa_source_save_dispose(&io); free(candidate.attachment_models); owner->busy = false; return okay;
}
