#include "selected_media_internal.h"

static bool model_handle(const q3n_selected_media *o, const char *path, int32_t handle,
    bool required, qa_error *e)
{
    qa_q3_presentation_assets *a = o->options.assets;
    q3p_name *name = q3p_find_name(a, Q3P_MODEL, path);
    if (!name || name->handle != handle || handle < 0 || (size_t)handle > a->model_count || (required && !handle))
        return q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 model handle has no exact retained registration");
    if (!handle) return true;
    const q3p_model *model = a->models[handle - 1];
    return model && !model->world && model->resource && model->provider.mounts == o->options.content &&
        model->provider.images == a->options.provider.images && model->provider.materials == a->options.provider.materials ? true :
        q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 model holder leaves its actual content namespace");
}
static bool shader_handle(const q3n_selected_media *o, const char *path, int32_t handle, qa_error *e)
{
    qa_q3_presentation_assets *a = o->options.assets;
    if (handle < 0 || (size_t)handle > a->shader_count || (handle && !a->shaders[handle - 1]))
        return q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 shader holder is absent");
    q3p_name *name = q3p_find_name(a, Q3P_SHADER, path);
    if (!handle) return !name || !name->handle ? true :
        q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 powerup shader lost its exact authored registration");
    return name && name->handle == handle ? true :
        q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 powerup shader lost its exact authored registration");
}
static bool animation_valid(const q3n_selected_media *o, qa_error *e)
{
    if (!o->animation_resource) return !o->animation_content && !o->character ? true :
        q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 animation has no retained resource");
    if (!o->animation_content || o->animation_receipt.resource_id != qa_resource_id(o->animation_resource) ||
        qa_resource_pool_find(qa_vfs_resources(o->animation_content), o->animation_receipt.resource_id) != o->animation_resource ||
        (!o->character && (o->animation_content != o->options.content || !o->animation_receipt.path ||
            strcmp(o->animation_receipt.path, "models/players/sarge/animation.cfg"))))
        return q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 animation holder lost its true acquisition provenance");
    const qa_player_animation_config *config = &o->animation_config;
    if (config->footsteps < QA_FOOTSTEP_NORMAL || config->footsteps > QA_FOOTSTEP_ENERGY ||
        config->gender < QA_MODEL_MALE || config->gender > QA_MODEL_NEUTER)
        return q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 animation header is outside its source enums");
    for (unsigned i = 0; i < QA_PLAYER_ANIMATION_COUNT; ++i)
        if (i != 31 && !config->animations[i].present)
            return q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 animation has an absent required source cell");
    return qa_vfs_acquisition_retained(o->animation_content, &o->animation_receipt, e);
}
bool q3n_selected_media_valid(const q3n_selected_media *o, bool capture, qa_error *e)
{
    const qa_q3_presentation_assets *a = o ? o->options.assets : NULL;
    if (!a || a->options.provider.mounts != o->options.content || a->options.provider.family != QA_SCENE_Q3 ||
        a->options.select || a->codec_busy ||
        (capture ? !a->capturing || a->busy != 1 : a->capturing || a->busy != 0))
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 media qualification requires its actual registry lease");
    if (o->shaders_ready) {
        if (!shader_handle(o, "powerups/invisibility", o->invisibility, e) ||
            !shader_handle(o, "powerups/battleWeapon", o->battle_weapon, e) ||
            !shader_handle(o, "powerups/quadWeapon", o->quad_weapon, e)) return false;
    } else if (o->invisibility || o->battle_weapon || o->quad_weapon)
        return q3p_fail(e, QA_ERROR_FORMAT, "Unadmitted selected Q3 shaders contain live handles");
    for (unsigned i = 0; i < 14; ++i) {
        const q3n_selected_media_row *row = &o->rows[i];
        if (!row->world_ready) {
            if (row->view_ready || row->hands_fallback || row->gun || row->hands || row->barrel || row->flash)
                return q3p_fail(e, QA_ERROR_FORMAT, "Unadmitted selected Q3 row contains live media");
            continue;
        }
        const qa_q3_item *item = q3n_selected_media_item(o->options.product, (int32_t)i); char path[128];
        if (!item || !item->model || !o->shaders_ready ||
            !model_handle(o, item->model, row->gun, true, e) ||
            !q3n_selected_media_path(item->model, "_barrel.md3", path, e) || !model_handle(o, path, row->barrel, false, e) ||
            !q3n_selected_media_path(item->model, "_flash.md3", path, e) || !model_handle(o, path, row->flash, false, e)) return false;
        if (row->view_ready) {
            if (!o->animation_resource || !q3n_selected_media_path(item->model, "_hand.md3", path, e)) return false;
            if (row->hands_fallback) {
                if (!model_handle(o, path, 0, false, e)) return false;
                if (!model_handle(o, "models/weapons2/shotgun/shotgun_hand.md3", row->hands, true, e)) return false;
            } else if (!model_handle(o, path, row->hands, true, e)) return false;
        } else if (row->hands || row->hands_fallback)
            return q3p_fail(e, QA_ERROR_FORMAT, "World-only selected Q3 row contains unadmitted hands");
    }
    return animation_valid(o, e);
}
static bool animation_fields(qa_source_save_io *io, qa_player_animation_config *c)
{
    uint32_t footsteps = c->footsteps, gender = c->gender;
    if (!qa_source_save_u32(io, &footsteps) || footsteps > QA_FOOTSTEP_ENERGY ||
        !qa_source_save_u32(io, &gender) || gender > QA_MODEL_NEUTER ||
        !qa_source_save_bool(io, &c->fixed_legs) || !qa_source_save_bool(io, &c->fixed_torso)) return false;
    c->footsteps = (qa_model_footstep)footsteps; c->gender = (qa_model_gender)gender;
    for (unsigned i = 0; i < 3; ++i) if (!qa_source_save_f32(io, &c->head_offset[i])) return false;
    for (unsigned i = 0; i < QA_PLAYER_ANIMATION_COUNT; ++i) {
        qa_player_animation *a = &c->animations[i];
        if (!qa_source_save_i32(io, &a->first_frame) || !qa_source_save_i32(io, &a->num_frames) ||
            !qa_source_save_i32(io, &a->loop_frames) || !qa_source_save_i32(io, &a->frame_lerp) ||
            !qa_source_save_i32(io, &a->initial_lerp) || !qa_source_save_bool(io, &a->reversed) ||
            !qa_source_save_bool(io, &a->flipflop) || !qa_source_save_bool(io, &a->present)) return false;
    }
    return true;
}
static bool text(qa_source_save_io *io, char **value)
{
    bool present = *value != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    size_t size = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*value) : 0;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *value = malloc(size + 1);
        if (!*value) return q3p_fail(io->error, QA_ERROR_MEMORY, "Restoring selected Q3 animation receipt");
        (*value)[size] = 0;
    }
    return qa_source_save_bytes(io, *value, size) && !memchr(*value, 0, size);
}
static bool view(qa_source_save_io *io, const q3n_selected_media_refs *refs, qa_vfs **value, bool exact)
{
    uint64_t id = 0;
    if ((io->direction == QA_SOURCE_SAVE_WRITE && !refs->view_encode(refs->context, *value, &id, io->error)) ||
        !qa_source_save_u64(io, &id) || !id) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        qa_vfs *resolved = NULL;
        if (!refs->view_decode(refs->context, id, &resolved, io->error) || !resolved || (exact && resolved != *value)) return false;
        *value = resolved;
    }
    return true;
}
static bool fields(qa_source_save_io *io, q3n_selected_media *o, const q3n_selected_media_refs *refs)
{
    uint8_t magic[4] = {'Q','3','S','R'}; uint32_t schema = 1, product = o->options.product;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "Q3SR", 4) ||
        !qa_source_save_u32(io, &schema) || schema != 1 || !qa_source_save_u32(io, &product) || product != (uint32_t)o->options.product ||
        !view(io, refs, &o->options.content, true) || !qa_source_save_bool(io, &o->shaders_ready) ||
        !qa_source_save_i32(io, &o->invisibility) || !qa_source_save_i32(io, &o->battle_weapon) ||
        !qa_source_save_i32(io, &o->quad_weapon)) return false;
    for (unsigned i = 0; i < 14; ++i) {
        q3n_selected_media_row *row = &o->rows[i];
        if (!qa_source_save_bool(io, &row->world_ready) || !qa_source_save_bool(io, &row->view_ready) ||
            !qa_source_save_bool(io, &row->hands_fallback) || !qa_source_save_i32(io, &row->gun) ||
            !qa_source_save_i32(io, &row->hands) || !qa_source_save_i32(io, &row->barrel) ||
            !qa_source_save_i32(io, &row->flash)) return false;
    }
    bool present = o->animation_resource != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (present) {
        uint64_t pool = 0, resource = 0;
        if (!view(io, refs, &o->animation_content, false) || !qa_source_save_bool(io, &o->character) ||
            (io->direction == QA_SOURCE_SAVE_WRITE && !refs->resource_encode(refs->context, o->animation_resource, &pool, &resource, io->error)) ||
            !qa_source_save_u64(io, &pool) || !pool || !qa_source_save_u64(io, &resource) || !resource ||
            !qa_source_save_u64(io, &o->animation_receipt.mount) || !qa_source_save_u64(io, &o->animation_receipt.resource_id) ||
            !text(io, &o->animation_receipt.path) || !text(io, &o->animation_receipt.lookup_path) ||
            !text(io, &o->animation_receipt.link_source) || !text(io, &o->animation_receipt.link_target) ||
            !animation_fields(io, &o->animation_config)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            const qa_resource *resolved = NULL;
            if (!refs->resource_decode(refs->context, pool, resource, &resolved, io->error) || !resolved) return false;
            o->animation_resource = (qa_resource *)resolved; qa_resource_retain(o->animation_resource);
        }
    }
    return q3n_selected_media_valid(o, true, io->error);
}
static bool ready(const q3n_selected_media *o, const q3n_selected_media_refs *refs, qa_error *e)
{
    const qa_q3_presentation_assets *a = o ? o->options.assets : NULL;
    return q3n_selected_media_idle(o) && refs && refs->view_encode && refs->view_decode &&
        refs->resource_encode && refs->resource_decode && a && a->capturing && a->busy == 1 && !a->codec_busy ? true :
        q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 media codec requires its real registry capture and aggregate dictionaries");
}
bool q3n_selected_media_checkpoint(const q3n_selected_media *borrowed, const q3n_selected_media_refs *refs,
    qa_buffer *out, qa_error *e)
{
    if (!out || out->data || out->size || !ready(borrowed, refs, e)) return false;
    q3n_selected_media *o = (q3n_selected_media *)borrowed; o->busy = true;
    q3n_selected_media copy = *o; qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, e) && fields(&io, &copy, refs) && qa_source_save_finish(&io, out);
    if (!ok && e && e->code == QA_OK) q3p_fail(e, QA_ERROR_FORMAT, "Selected Q3 media checkpoint is inconsistent");
    qa_source_save_dispose(&io); o->busy = false; return ok;
}
bool q3n_selected_media_restore(q3n_selected_media *o, const q3n_selected_media_refs *refs, qa_bytes bytes, qa_error *e)
{
    if (!ready(o, refs, e)) return false;
    if (o->shaders_ready || o->animation_resource) return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 media restore requires an empty cache");
    for (unsigned i = 0; i < 14; ++i) if (o->rows[i].world_ready)
        return q3p_fail(e, QA_ERROR_ARGUMENT, "Selected Q3 media restore requires empty weapon rows");
    q3n_selected_media *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return q3p_fail(e, QA_ERROR_MEMORY, "Allocating selected Q3 media restore candidate");
    candidate->options = o->options; o->busy = true; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, e) && fields(&io, candidate, refs) && qa_source_save_finish(&io, NULL);
    if (ok) { *o = *candidate; o->busy = true; candidate->animation_resource = NULL;
        candidate->animation_receipt = (qa_vfs_acquisition){0}; }
    else if (e && e->code == QA_OK) q3p_fail(e, QA_ERROR_FORMAT, "Saved selected Q3 media leaves its genuine holder inventory");
    qa_source_save_dispose(&io); q3n_selected_media_destroy(candidate); o->busy = false; return ok;
}
