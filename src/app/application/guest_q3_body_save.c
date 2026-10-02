#include "guest_q3_body_save.h"
#include "guest_q3_body_private.h"
#include "internal.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>

static bool signature(qa_source_save_io *io, const char expected[8])
{
    char magic[8]; memcpy(magic, expected, 8);
    uint32_t version = 1;
    return qa_source_save_bytes(io, magic, 8) && !memcmp(magic, expected, 8) &&
        qa_source_save_u32(io, &version) && (version == 1 ||
            application_fail(io->error, QA_ERROR_FORMAT, "Unknown CGAME body continuation version"));
}
static bool text(qa_source_save_io *io, char **out)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t length = !reading && *out ? strlen(*out) : 0;
    if (!qa_source_save_count(io, &length, reading ? io->input.size - io->offset : SIZE_MAX - 1) ||
        length == SIZE_MAX) return false;
    if (!reading) return *out && qa_source_save_bytes(io, *out, length);
    if (*out) return application_fail(io->error, QA_ERROR_ARGUMENT, "Body profile import has an existing path owner");
    char *copy = malloc(length + 1);
    if (!copy) return application_fail(io->error, QA_ERROR_MEMORY, "Retaining imported body artifact path");
    if (!qa_source_save_bytes(io, copy, length) || memchr(copy, 0, length)) { free(copy); return false; }
    copy[length] = 0; *out = copy; return true;
}
static bool profile_fields(qa_source_save_io *io, const qa_qvm_image *image,
    application_q3_body_profile *profile)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t abi = profile->abi;
    size_t instructions = 0; qa_qvm_image_instructions(image, &instructions);
    if (!signature(io, "QAG3BP\0\0") || !text(io, &profile->artifact_path) ||
        !qa_source_save_bytes(io, profile->artifact.bytes, sizeof(profile->artifact.bytes)) ||
        !qa_source_save_u32(io, &abi) || abi > QA_QVM_Q3_116N ||
        !qa_source_save_bool(io, &profile->present) ||
        !qa_source_save_count(io, &profile->count, instructions)) return false;
    if (reading) {
        profile->abi = (qa_qvm_abi)abi;
        if (profile->count > (io->input.size - io->offset) / 59)
            return application_fail(io->error, QA_ERROR_FORMAT, "Truncated body declaration inventory");
        if (profile->count > SIZE_MAX / sizeof(*profile->submissions))
            return application_fail(io->error, QA_ERROR_MEMORY, "Imported body declaration exceeds address space");
        profile->submissions = profile->count ? calloc(profile->count, sizeof(*profile->submissions)) : NULL;
        if (profile->count && !profile->submissions)
            return application_fail(io->error, QA_ERROR_MEMORY, "Retaining imported body declarations");
    }
    for (size_t i = 0; i < profile->count; ++i) {
        application_q3_body_submission *row = profile->submissions + i;
        if (!qa_source_save_u32(io, &row->entry) || !qa_source_save_u32(io, &row->actor_argument) ||
            !qa_source_save_u64(io, &row->entity_number_offset) ||
            !qa_source_save_bool(io, &row->argument_reference) || !qa_source_save_u32(io, &row->reference_argument) ||
            !qa_source_save_bool(io, &row->conditional) || !qa_source_save_u32(io, &row->condition_argument) ||
            !qa_source_save_i64(io, &row->condition_value) || !qa_source_save_bool(io, &row->mesh) ||
            !qa_source_save_u32(io, &row->mesh_entry) || !qa_source_save_u32(io, &row->mesh_entity_argument) ||
            !qa_source_save_u32(io, &row->mesh_state_argument) || !qa_source_save_u32(io, &row->mesh_shader_offset) ||
            !qa_source_save_count(io, &row->call_count, instructions)) return false;
        if (reading) {
            if (row->call_count > (io->input.size - io->offset) / 8)
                return application_fail(io->error, QA_ERROR_FORMAT, "Truncated body mesh call inventory");
            if (row->call_count > SIZE_MAX / sizeof(*row->calls))
                return application_fail(io->error, QA_ERROR_MEMORY, "Imported body mesh calls exceed address space");
            row->calls = row->call_count ? calloc(row->call_count, sizeof(*row->calls)) : NULL;
            if (row->call_count && !row->calls)
                return application_fail(io->error, QA_ERROR_MEMORY, "Retaining imported body mesh calls");
        }
        for (size_t j = 0; j < row->call_count; ++j) {
            uint32_t part = row->calls[j].part;
            if (!qa_source_save_u32(io, &row->calls[j].instruction) || !qa_source_save_u32(io, &part) ||
                part > QA_APPLICATION_Q3_BODY_HEAD) return false;
            if (reading) row->calls[j].part = (qa_application_q3_body_part)part;
        }
    }
    return true;
}

bool application_q3_body_profile_checkpoint(const qa_qvm_image *image, qa_qvm_abi abi,
    const char *path, const application_q3_body_profile *profile, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !application_q3_body_profile_qualify(image, abi, path, profile, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Body profile capture requires its immutable actual owner");
    application_q3_body_profile value = *profile;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) && profile_fields(&io, image, &value) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}
bool application_q3_body_profile_restore(const qa_qvm_image *image, qa_qvm_abi abi,
    const char *path, qa_bytes bytes, application_q3_body_profile *out, qa_error *error)
{
    if (!image || !path || !out || out->artifact_path || out->submissions || out->count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Body profile import requires an empty candidate owner");
    application_q3_body_profile profile = {0}; qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && profile_fields(&io, image, &profile) &&
        qa_source_save_finish(&io, NULL) && application_q3_body_profile_qualify(image, abi, path, &profile, error);
    qa_source_save_dispose(&io);
    if (!okay) { application_q3_body_profile_free(&profile); return false; }
    *out = profile; return true;
}

static bool body_fields(qa_source_save_io *io, const application_q3_body_profile *profile,
    application_q3_body_saved *saved)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_sha256_digest digest = {{0}};
    if (!reading && profile) digest = profile->artifact;
    if (!signature(io, "QAG3BD\0\0") || !qa_source_save_bool(io, &saved->present) ||
        !qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) ||
        !qa_source_save_bool(io, &saved->enabled)) return false;
    if (saved->present != (profile != NULL) ||
        (profile ? !qa_sha256_equal(&digest, &profile->artifact) :
            memcmp(digest.bytes, (uint8_t[32]){0}, 32) != 0) ||
        (saved->enabled && (!profile || !profile->present)))
        return application_fail(io->error, QA_ERROR_FORMAT, "Body continuation differs from its artifact owner");
    size_t expected = saved->enabled ? application_q3_body_profile_hooks(profile) : 0;
    if (!qa_source_save_count(io, &saved->count, expected) || saved->count != expected)
        return application_fail(io->error, QA_ERROR_FORMAT, "Body continuation has a different enabled callback set");
    if (reading) {
        if (saved->count > (io->input.size - io->offset) / 8)
            return application_fail(io->error, QA_ERROR_FORMAT, "Truncated body binding identity inventory");
        if (saved->count > SIZE_MAX / sizeof(*saved->bindings)) return false;
        saved->bindings = saved->count ? calloc(saved->count, sizeof(*saved->bindings)) : NULL;
        if (saved->count && !saved->bindings)
            return application_fail(io->error, QA_ERROR_MEMORY, "Retaining body callback identities");
    }
    for (size_t i = 0; i < saved->count; ++i) {
        if (!qa_source_save_u64(io, &saved->bindings[i]) || !saved->bindings[i]) return false;
        for (size_t j = 0; j < i; ++j) if (saved->bindings[i] == saved->bindings[j])
            return application_fail(io->error, QA_ERROR_FORMAT, "Body continuation repeats a callback identity");
    }
    return true;
}
bool application_q3_body_checkpoint(const application_q3_body *owner, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !application_q3_body_idle(owner) ||
        !application_q3_body_bindings_complete(owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Body capture requires its idle complete source owner");
    application_q3_body_saved saved = {.present = owner != NULL, .enabled = owner && owner->enabled,
        .count = application_q3_body_descriptor_count(owner)};
    if (saved.count > SIZE_MAX / sizeof(*saved.bindings)) return false;
    saved.bindings = saved.count ? calloc(saved.count, sizeof(*saved.bindings)) : NULL;
    if (saved.count && !saved.bindings)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining captured body callback identities");
    for (size_t i = 0; i < saved.count; ++i) saved.bindings[i] = owner->hooks[i].binding;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) &&
        body_fields(&io, owner ? owner->module.profile : NULL, &saved) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); application_q3_body_saved_free(&saved); return okay;
}
bool application_q3_body_saved_read(const application_q3_body_profile *profile, qa_bytes bytes,
    application_q3_body_saved *out, qa_error *error)
{
    if (!out || out->bindings || out->count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Body continuation import requires an empty candidate");
    application_q3_body_saved saved = {0}; qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && body_fields(&io, profile, &saved) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay) { application_q3_body_saved_free(&saved); return false; }
    *out = saved; return true;
}
void application_q3_body_saved_free(application_q3_body_saved *saved)
{
    if (!saved) return;
    free(saved->bindings); *saved = (application_q3_body_saved){0};
}
