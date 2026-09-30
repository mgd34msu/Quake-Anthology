#include "internal.h"
#include "qa/hash.h"

static bool signature(qa_source_save_io *io)
{
    uint8_t magic[4] = {'Q','3','A','S'}; uint32_t version = 1;
    return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "Q3AS", 4) &&
        qa_source_save_u32(io, &version) && version == 1;
}
static const q3p_name *handle_name(const qa_q3_presentation_assets *assets, q3p_resource_kind kind, int32_t handle)
{
    const q3p_name *result = NULL;
    for (size_t i = 0; i < assets->name_capacity; ++i)
        for (const q3p_name *entry = assets->names[i]; entry; entry = entry->next)
            if (entry->kind == kind && entry->handle == handle &&
                (!result || (entry->generated && !result->generated) ||
                 (entry->generated == result->generated && strcmp(entry->name, result->name) < 0))) result = entry;
    return result;
}
static bool digest_fields(qa_source_save_io *io, const qa_resource *resource)
{
    bool present = resource != NULL;
    if (!qa_source_save_bool(io, &present) || present != (resource != NULL)) return false;
    if (!present) return true;
    const qa_sha256_digest *actual = qa_resource_digest(resource);
    if (!actual) return false;
    qa_sha256_digest digest = *actual;
    return qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) &&
        !memcmp(digest.bytes, actual->bytes, sizeof(digest.bytes));
}
static bool model_fields(qa_source_save_io *io, const q3p_model *model)
{
    bool world = model->world != NULL, owned = model->owns_world, lods = model->has_lods;
    if (!qa_source_save_bool(io, &world) || world != (model->world != NULL) ||
        !qa_source_save_bool(io, &owned) || owned != model->owns_world ||
        !qa_source_save_bool(io, &lods) || lods != model->has_lods || !digest_fields(io, model->resource)) return false;
    uint32_t inline_model = model->inline_model;
    if (!qa_source_save_u32(io, &inline_model) || inline_model != model->inline_model) return false;
    if (lods) {
        uint32_t load_count = model->lods.load_count, lod_count = model->lods.lod_count;
        uint64_t byte_length = model->lods.byte_length;
        if (!qa_source_save_u32(io, &load_count) || load_count != model->lods.load_count ||
            !qa_source_save_u32(io, &lod_count) || lod_count != model->lods.lod_count ||
            !qa_source_save_u64(io, &byte_length) || byte_length != model->lods.byte_length) return false;
        for (uint32_t i = 0; i < 3; ++i) {
            uint32_t state = model->lods.states[i], alias = model->lods.aliases[i], order = model->lods.load_order[i];
            const char *path = model->lods.paths[i];
            if (!qa_source_save_u32(io, &state) || state != (uint32_t)model->lods.states[i] ||
                !qa_source_save_u32(io, &alias) || alias != model->lods.aliases[i] ||
                !qa_source_save_u32(io, &order) || order != model->lods.load_order[i] || !qa_source_save_text(io, &path) ||
                ((path == NULL) != (model->lods.paths[i] == NULL)) || (path && strcmp(path, model->lods.paths[i]))) return false;
        }
    }
    if (lods) for (uint32_t i = 0; i < 3; ++i) {
        const qa_model *lod = qa_model_at_lod(&model->lods, i); bool present = lod != NULL;
        if (!qa_source_save_bool(io, &present) || present != (lod != NULL)) return false;
        if (lod) {
            qa_sha256_digest actual, expected;
            qa_sha256((qa_bytes){lod->source.data, lod->source.size}, &actual); expected = actual;
            if (!qa_source_save_bytes(io, expected.bytes, sizeof(expected.bytes)) ||
                memcmp(actual.bytes, expected.bytes, sizeof(expected.bytes))) return false;
        }
    }
    return true;
}
static bool image_fields(qa_source_save_io *io, const qa_q3_asset_checkpoint_refs *refs,
    const qa_scene_image *image, qa_scene_image **candidate)
{
    qa_buffer encoded = {0}; size_t count = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        if (!image || !refs || !refs->image_encode || !refs->image_encode(refs->context, image, &encoded, io->error)) {
            if (!refs || !refs->image_encode) q3p_fail(io->error, QA_ERROR_ARGUMENT, "Generated Q3 picture encoder is absent");
            qa_buffer_free(&encoded); return false;
        }
        count = encoded.size;
    }
    bool ok = qa_source_save_count(io, &count, io->direction == QA_SOURCE_SAVE_WRITE ? SIZE_MAX : io->input.size);
    if (ok && io->direction == QA_SOURCE_SAVE_READ) {
        encoded.data = count ? malloc(count) : NULL; encoded.size = count;
        if (count && !encoded.data) ok = q3p_fail(io->error, QA_ERROR_MEMORY, "Restoring generated Q3 picture descriptor");
    }
    if (ok) ok = qa_source_save_bytes(io, encoded.data, count);
    if (ok && io->direction == QA_SOURCE_SAVE_READ) {
        ok = refs && refs->image_decode && refs->image_decode(refs->context, (qa_bytes){encoded.data, count}, candidate, io->error) && *candidate;
        if (!refs || !refs->image_decode) q3p_fail(io->error, QA_ERROR_ARGUMENT, "Generated Q3 candidate picture resolver is absent");
    }
    qa_buffer_free(&encoded); return ok;
}
static bool table_fields(qa_source_save_io *io, qa_q3_presentation_assets *assets, q3p_resource_kind kind,
    const qa_q3_asset_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = kind == Q3P_MODEL ? assets->model_count : kind == Q3P_SKIN ? assets->skin_count :
        kind == Q3P_SHADER ? assets->shader_count : assets->sound_count;
    size_t maximum = reading && io->input.size / 8 < INT32_MAX ? io->input.size / 8 : INT32_MAX;
    if (!qa_source_save_count(io, &count, maximum)) return false;
    for (size_t i = 0; i < count; ++i) {
        bool present = kind != Q3P_MODEL || (!reading && assets->models[i] != NULL);
        if (!qa_source_save_bool(io, &present) || (!present && kind != Q3P_MODEL)) return false;
        if (!present) {
            if (reading) {
                if (!q3p_reserve((void **)&assets->models, &assets->model_capacity, i + 1, sizeof(*assets->models), io->error)) return false;
                assets->models[assets->model_count++] = NULL;
            }
            continue;
        }
        const q3p_name *registration = reading ? NULL : handle_name(assets, kind, (int32_t)i + 1);
        const char *name = registration ? registration->name : NULL;
        bool option = registration && registration->option, generated = registration && registration->generated;
        if ((!reading && !registration) || !qa_source_save_text(io, &name) || !name ||
            !qa_source_save_bool(io, &option) || !qa_source_save_bool(io, &generated) || (generated && kind != Q3P_SHADER)) return false;
        int32_t handle = (int32_t)i + 1;
        if (kind == Q3P_SHADER && generated) {
            qa_scene_image *image = NULL;
            const qa_material *material = reading ? NULL : assets->shaders[i];
            const qa_scene_image *source = material && material->stage_count == 1 && material->stages[0].image_count == 1 ?
                material->stages[0].images[0] : NULL;
            bool ok = image_fields(io, refs, source, &image);
            if (ok && reading) {
                ok = qa_q3_register_picture_image(assets, image, &handle, io->error);
                const q3p_name *rebound = ok ? q3p_find_name(assets, Q3P_SHADER, name) : NULL;
                ok = ok && rebound && rebound->handle == handle;
            }
            qa_scene_image_release(image); if (!ok) return false;
        } else if (reading) {
            bool ok = kind == Q3P_MODEL ? qa_q3_register_model(assets, name, &handle, io->error) :
                kind == Q3P_SKIN ? qa_q3_register_skin(assets, name, &handle, io->error) :
                kind == Q3P_SHADER ? qa_q3_register_shader(assets, name, option, &handle, io->error) :
                qa_q3_register_sound(assets, name, option, &handle, io->error);
            if (!ok) return false;
        }
        if (handle != (int32_t)i + 1) return false;
        if (kind == Q3P_MODEL && !model_fields(io, assets->models[i])) return false;
        if (kind == Q3P_SKIN && !digest_fields(io, assets->skins[i]->resource)) return false;
        if (kind == Q3P_SOUND && !digest_fields(io, qa_audio_asset_resource(assets->sounds[i]))) return false;
    }
    return true;
}
static bool asset_fields(qa_source_save_io *io, qa_q3_presentation_assets *assets, const qa_q3_asset_checkpoint_refs *refs)
{
    if (!signature(io) || !digest_fields(io, qa_audio_asset_resource(assets->options.zero_sound))) return false;
    for (q3p_resource_kind kind = Q3P_MODEL; kind <= Q3P_SOUND; ++kind)
        if (!table_fields(io, assets, kind, refs)) return false;
    bool reading = io->direction == QA_SOURCE_SAVE_READ; size_t count = assets->name_count;
    if (!qa_source_save_count(io, &count, reading ? io->input.size / 8 : SIZE_MAX)) return false;
    if (!reading) {
        for (size_t i = 0; i < assets->name_capacity; ++i)
            for (q3p_name *entry = assets->names[i]; entry; entry = entry->next) {
                uint32_t kind = entry->kind; int32_t handle = entry->handle;
                const char *name = entry->name; bool option = entry->option, generated = entry->generated;
                if (!qa_source_save_u32(io, &kind) || !qa_source_save_text(io, &name) || !qa_source_save_i32(io, &handle) ||
                    !qa_source_save_bool(io, &option) || !qa_source_save_bool(io, &generated)) return false;
            }
    } else {
        for (size_t i = 0; i < assets->name_capacity; ++i) {
            q3p_name *entry = assets->names[i];
            while (entry) { q3p_name *next = entry->next; free(entry); entry = next; }
            assets->names[i] = NULL;
        }
        assets->name_count = 0;
        for (size_t i = 0; i < count; ++i) {
            uint32_t kind = 0; const char *name = NULL; int32_t handle = 0; bool option = false, generated = false;
            if (!qa_source_save_u32(io, &kind) || kind > Q3P_SOUND || !qa_source_save_text(io, &name) || !name ||
                !qa_source_save_i32(io, &handle) || handle < 0 || !qa_source_save_bool(io, &option) ||
                !qa_source_save_bool(io, &generated) || (generated && kind != Q3P_SHADER)) return false;
            size_t maximum = kind == Q3P_MODEL ? assets->model_count : kind == Q3P_SKIN ? assets->skin_count :
                kind == Q3P_SHADER ? assets->shader_count : assets->sound_count;
            if ((size_t)handle > maximum || (kind == Q3P_MODEL && handle && !assets->models[handle - 1]) ||
                q3p_find_name(assets, (q3p_resource_kind)kind, name) ||
                !q3p_add_name(assets, (q3p_resource_kind)kind, name, handle, option, io->error)) return false;
            q3p_find_name(assets, (q3p_resource_kind)kind, name)->generated = generated;
        }
        for (q3p_resource_kind kind = Q3P_MODEL; kind <= Q3P_SOUND; ++kind) {
            size_t maximum = kind == Q3P_MODEL ? assets->model_count : kind == Q3P_SKIN ? assets->skin_count :
                kind == Q3P_SHADER ? assets->shader_count : assets->sound_count;
            for (size_t i = 0; i < maximum; ++i)
                if (!(kind == Q3P_MODEL && !assets->models[i]) && !handle_name(assets, kind, (int32_t)i + 1)) return false;
        }
    }
    return true;
}
bool qa_q3_presentation_assets_checkpoint(qa_q3_presentation_assets *assets, qa_session *session,
    const qa_q3_asset_checkpoint_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!assets || assets->busy || !session || !out) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 handle checkpoint requires an idle owner");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, session, error) && asset_fields(&io, assets, refs) && qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_FORMAT, "Invalid retained Q3 handle table");
    qa_source_save_dispose(&io); return ok;
}
bool qa_q3_presentation_assets_restore(qa_q3_presentation_assets *assets, qa_session *session,
    const qa_q3_asset_checkpoint_refs *refs, qa_bytes bytes, qa_error *error)
{
    if (!assets || assets->busy || !session || assets->name_count || assets->model_count || assets->skin_count ||
        assets->shader_count || assets->sound_count) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 handle restore requires an empty candidate");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, session, bytes, error) && asset_fields(&io, assets, refs) && qa_source_save_finish(&io, NULL);
    if (!ok && (!error || error->code == QA_OK)) q3p_fail(error, QA_ERROR_FORMAT, "Saved Q3 handle identity or content differs");
    qa_source_save_dispose(&io); return ok;
}
