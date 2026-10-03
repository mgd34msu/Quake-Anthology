#include "guest_q3_weapon_models_profile.h"
#include "internal.h"
#include "qa/json.h"
#include "qa/source_save.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool number(const qa_json_document *doc, qa_json_id object, const char *name,
    uint32_t *out, qa_error *error)
{
    double value;
    if (!qa_json_number(doc, qa_json_get(doc, object, name), &value, error)) return false;
    if (!isfinite(value) || value < 0 || value > UINT32_MAX || trunc(value) != value)
        return application_fail(error, QA_ERROR_FORMAT, "CG weapon model field is outside its source word domain");
    *out = (uint32_t)value; return true;
}
void application_q3_weapon_models_profile_free(application_q3_weapon_models_profile *profile)
{
    if (!profile) return;
    free(profile->artifact_path); free(profile->game_artifact_path);
    *profile = (application_q3_weapon_models_profile){0};
}
bool application_q3_weapon_models_profile_namespace(const application_q3_weapon_models_profile *profile,
    const qa_qvm_image *image, qa_qvm_abi abi, const char *path, qa_error *error)
{
    if (!profile || !profile->present || !profile->game_artifact_path || !image || !path ||
        profile->game_abi != abi || !qa_sha256_equal(&profile->game_artifact, qa_qvm_image_digest(image)))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon receipt has a different actual GAME namespace");
    char *normalized = qa_vfs_normalize_path(path, error);
    bool same = normalized && !strcmp(normalized, profile->game_artifact_path);
    free(normalized);
    return same || application_fail(error, QA_ERROR_FORMAT, "CG weapon receipt belongs to a different actual GAME artifact");
}
bool application_q3_weapon_models_profile_qualify(const qa_qvm_image *image, qa_qvm_abi abi,
    const char *path, const application_q3_weapon_models_profile *profile, qa_error *error)
{
    if (!image || !path || !profile || !profile->artifact_path || (unsigned)abi > QA_QVM_Q3_116N ||
        profile->abi != abi || !qa_sha256_equal(&profile->artifact, qa_qvm_image_digest(image)))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon model profile lost its actual artifact");
    char *normalized = qa_vfs_normalize_path(path, error);
    bool same = normalized && !strcmp(normalized, profile->artifact_path);
    free(normalized);
    if (!same) return application_fail(error, QA_ERROR_FORMAT, "CG weapon models name different artifact bytes");
    if (!profile->present) {
        if (profile->game_artifact_path || profile->game_abi ||
            memcmp(profile->game_artifact.bytes, (uint8_t[32]){0}, 32) ||
            profile->registration || profile->weapon_argument || profile->base || profile->count ||
            profile->stride || profile->weapon_offset || profile->registered_offset ||
            profile->index_base || profile->indexed)
            return application_fail(error, QA_ERROR_FORMAT, "Absent CG weapon models retain undeclared source state");
        for (size_t i = 0; i < APPLICATION_Q3_WEAPON_MODEL_FIELDS; ++i)
            if (profile->fields[i] || profile->offsets[i])
                return application_fail(error, QA_ERROR_FORMAT, "Absent CG weapon models retain undeclared table fields");
        return true;
    }
    if (!profile->game_artifact_path || (unsigned)profile->game_abi > QA_QVM_Q3_116N)
        return application_fail(error, QA_ERROR_FORMAT, "CG weapon model declaration has no retained GAME namespace");
    normalized = qa_vfs_normalize_path(profile->game_artifact_path, error);
    same = normalized && !strcmp(normalized, profile->game_artifact_path);
    free(normalized);
    if (!same) return application_fail(error, QA_ERROR_FORMAT, "CG weapon model GAME path is not canonical");
    size_t count;
    const qa_qvm_instruction *code = qa_qvm_image_instructions(image, &count);
    uint64_t end = (uint64_t)profile->base + (uint64_t)profile->count * profile->stride;
    if (profile->registration >= count || code[profile->registration].opcode != QA_QVM_ENTER ||
        code[profile->registration].operand < 8 || profile->weapon_argument >= 62 ||
        !profile->count || profile->stride < 8 || (profile->base & 3) || (profile->stride & 3) ||
        end > qa_qvm_image_memory_size(image) || !profile->fields[APPLICATION_Q3_WEAPON_GUN] ||
        (!profile->indexed && (profile->index_base || (profile->weapon_offset & 3) || profile->weapon_offset > profile->stride - 4 ||
            profile->weapon_offset == profile->registered_offset)) ||
        (profile->indexed && (profile->weapon_offset || (int64_t)profile->index_base + profile->count - 1 > INT32_MAX)) ||
        (profile->registered_offset & 3) || profile->registered_offset > profile->stride - 4)
        return application_fail(error, QA_ERROR_FORMAT, "CG weapon registration or table leaves its actual source ABI");
    if (!qa_qvm_qualify_source_span(image, profile->base,
        (size_t)((uint64_t)profile->count * profile->stride), error)) return false;
    for (size_t i = 0; i < APPLICATION_Q3_WEAPON_MODEL_FIELDS; ++i) {
        if (!profile->fields[i]) {
            if (profile->offsets[i])
                return application_fail(error, QA_ERROR_FORMAT, "CG weapon models retain an undeclared table offset");
            continue;
        }
        uint32_t offset = profile->offsets[i];
        if ((offset & 3) || offset > profile->stride - 4 || (!profile->indexed && offset == profile->weapon_offset) ||
            offset == profile->registered_offset)
            return application_fail(error, QA_ERROR_FORMAT, "CG weapon model aliases unrelated table storage");
        for (size_t j = 0; j < i; ++j)
            if (profile->fields[j] && offset == profile->offsets[j])
                return application_fail(error, QA_ERROR_FORMAT, "CG weapon model fields repeat table storage");
    }
    return true;
}
bool application_q3_weapon_models_profile_read(const qa_qvm_image *image, qa_qvm_role role,
    qa_qvm_abi abi, const char *path, const qa_bytes *bytes,
    application_q3_weapon_models_profile *out, qa_error *error)
{
    if (!image || role != QA_QVM_CGAME || !path || !out || out->artifact_path || out->game_artifact_path ||
        (unsigned)abi > QA_QVM_Q3_116N || (bytes && (!bytes->data || !bytes->size)))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon model declaration needs its actual bytecode opening");
    application_q3_weapon_models_profile profile = {.artifact = *qa_qvm_image_digest(image), .abi = abi,
        .present = bytes != NULL};
    profile.artifact_path = qa_vfs_normalize_path(path, error);
    if (!profile.artifact_path) return false;
    qa_json_document *doc = NULL;
    bool ok = !bytes || qa_json_parse(*bytes, &doc, error);
    if (ok && bytes) {
        qa_json_id root = qa_json_root(doc), table = qa_json_get(doc, root, "table"),
            models = qa_json_get(doc, table, "models");
        uint32_t version = 0;
        qa_buffer declared_path = {0}; char *normalized = NULL;
        char hex[65], digest[72]; qa_sha256_hex(&profile.artifact, hex);
        memcpy(digest, "sha256:", 7); memcpy(digest + 7, hex, 65);
        ok = number(doc, root, "version", &version, error) && version == 1 &&
            qa_json_string(doc, qa_json_get(doc, root, "artifactPath"), &declared_path, error);
        if (ok && memchr(declared_path.data, 0, declared_path.size)) ok = false;
        if (ok) normalized = qa_vfs_normalize_path((const char *)declared_path.data, error);
        if (ok) ok = normalized && !strcmp(normalized, profile.artifact_path) &&
            qa_json_string_equal(doc, qa_json_get(doc, root, "artifactDigest"), digest);
        free(normalized); qa_buffer_free(&declared_path);
        if (!ok && (!error || error->code == QA_OK))
            application_fail(error, QA_ERROR_FORMAT, "CG weapon models belong to a different artifact opening");
        qa_json_id game = qa_json_get(doc, root, "game");
        qa_buffer game_path = {0}, game_digest = {0};
        if (ok) ok = qa_json_string(doc, qa_json_get(doc, game, "artifactPath"), &game_path, error) &&
            qa_json_string(doc, qa_json_get(doc, game, "artifactDigest"), &game_digest, error);
        if (ok) ok = !memchr(game_path.data, 0, game_path.size) && !memchr(game_digest.data, 0, game_digest.size) &&
            qa_sha256_parse((const char *)game_digest.data, &profile.game_artifact, error);
        if (ok) profile.game_artifact_path = qa_vfs_normalize_path((const char *)game_path.data, error);
        if (ok) ok = profile.game_artifact_path != NULL;
        if (ok) {
            qa_json_id game_abi = qa_json_get(doc, game, "abiProfile");
            if (qa_json_string_equal(doc, game_abi, "q3-modern")) profile.game_abi = QA_QVM_Q3_MODERN;
            else if (qa_json_string_equal(doc, game_abi, "q3-1.16n-base")) profile.game_abi = QA_QVM_Q3_116N;
            else ok = application_fail(error, QA_ERROR_FORMAT, "CG weapon namespace declares an unknown GAME ABI");
        }
        qa_buffer_free(&game_path); qa_buffer_free(&game_digest);
        if (ok) ok = number(doc, root, "registrationEntry", &profile.registration, error) &&
            number(doc, root, "weaponArgument", &profile.weapon_argument, error) &&
            number(doc, table, "base", &profile.base, error) && number(doc, table, "count", &profile.count, error) &&
            number(doc, table, "stride", &profile.stride, error) &&
            number(doc, table, "registeredOffset", &profile.registered_offset, error);
        profile.indexed = qa_json_get(doc, table, "indexBase") != QA_JSON_NONE;
        if (ok && profile.indexed) {
            double value;
            ok = qa_json_number(doc, qa_json_get(doc, table, "indexBase"), &value, error);
            if (ok && (!isfinite(value) || value < INT32_MIN || value > INT32_MAX || trunc(value) != value))
                ok = application_fail(error, QA_ERROR_FORMAT, "CG weapon index base leaves its source int32 identity");
            if (ok) profile.index_base = (int32_t)value;
            if (ok && qa_json_get(doc, table, "weaponOffset") != QA_JSON_NONE)
                ok = application_fail(error, QA_ERROR_FORMAT, "CG weapon table declares two different identity layouts");
        } else if (ok) ok = number(doc, table, "weaponOffset", &profile.weapon_offset, error);
        static const char *const names[] = {"gun", "hands", "barrel", "flash", "invisibility", "battle", "quad"};
        for (size_t i = 0; ok && i < APPLICATION_Q3_WEAPON_MODEL_FIELDS; ++i) {
            profile.fields[i] = qa_json_get(doc, models, names[i]) != QA_JSON_NONE;
            if (profile.fields[i]) ok = number(doc, models, names[i], &profile.offsets[i], error);
        }
    }
    qa_json_destroy(doc);
    if (ok) ok = application_q3_weapon_models_profile_qualify(image, abi, path, &profile, error);
    if (!ok) { application_q3_weapon_models_profile_free(&profile); return false; }
    *out = profile; return true;
}

static bool profile_text(qa_source_save_io *io, char **path)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t length = !reading && *path ? strlen(*path) : 0;
    if (!qa_source_save_count(io, &length, reading ? io->input.size - io->offset : SIZE_MAX - 1))
        return false;
    if (!length || length == SIZE_MAX)
        return application_fail(io->error, QA_ERROR_FORMAT, "CG weapon model profile has an empty artifact path");
    if (!reading) return qa_source_save_bytes(io, *path, length);
    if (*path) return application_fail(io->error, QA_ERROR_ARGUMENT, "CG weapon profile import already owns a path");
    char *copy = malloc(length + 1);
    if (!copy) return application_fail(io->error, QA_ERROR_MEMORY, "Retaining CG weapon model artifact path");
    if (!qa_source_save_bytes(io, copy, length)) { free(copy); return false; }
    if (memchr(copy, 0, length)) {
        free(copy);
        return application_fail(io->error, QA_ERROR_FORMAT, "CG weapon model artifact path contains a NUL");
    }
    copy[length] = 0; *path = copy; return true;
}

static bool profile_fields(qa_source_save_io *io, application_q3_weapon_models_profile *profile)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    char magic[8] = "QAG3MP\0";
    uint32_t abi = profile->abi, game_abi = profile->game_abi;
    if (!qa_source_save_bytes(io, magic, sizeof(magic))) return false;
    if (memcmp(magic, "QAG3MP\0\0", sizeof(magic)))
        return application_fail(io->error, QA_ERROR_FORMAT, "Unknown CG weapon model profile signature");
    if (!profile_text(io, &profile->artifact_path) ||
        !qa_source_save_bytes(io, profile->artifact.bytes, sizeof(profile->artifact.bytes)) ||
        !qa_source_save_u32(io, &abi) || !qa_source_save_bool(io, &profile->present)) return false;
    if (abi > QA_QVM_Q3_116N)
        return application_fail(io->error, QA_ERROR_FORMAT, "CG weapon model profile has an unknown ABI");
    if (reading) profile->abi = (qa_qvm_abi)abi;
    if (!profile->present) return true;
    if (!profile_text(io, &profile->game_artifact_path) ||
        !qa_source_save_bytes(io, profile->game_artifact.bytes, sizeof(profile->game_artifact.bytes)) ||
        !qa_source_save_u32(io, &game_abi)) return false;
    if (game_abi > QA_QVM_Q3_116N)
        return application_fail(io->error, QA_ERROR_FORMAT, "CG weapon model profile has an unknown GAME ABI");
    if (reading) profile->game_abi = (qa_qvm_abi)game_abi;
    if (!qa_source_save_u32(io, &profile->registration) || !qa_source_save_u32(io, &profile->weapon_argument) ||
        !qa_source_save_u32(io, &profile->base) || !qa_source_save_u32(io, &profile->count) ||
        !qa_source_save_u32(io, &profile->stride) || !qa_source_save_u32(io, &profile->weapon_offset) ||
        !qa_source_save_u32(io, &profile->registered_offset) || !qa_source_save_i32(io, &profile->index_base) ||
        !qa_source_save_bool(io, &profile->indexed)) return false;
    for (size_t i = 0; i < APPLICATION_Q3_WEAPON_MODEL_FIELDS; ++i)
        if (!qa_source_save_bool(io, &profile->fields[i]) || !qa_source_save_u32(io, &profile->offsets[i]))
            return false;
    return true;
}

bool application_q3_weapon_models_profile_checkpoint(const qa_qvm_image *image, qa_qvm_abi abi,
    const char *path, const application_q3_weapon_models_profile *profile, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size)
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon model profile capture requires empty owned output");
    if (!application_q3_weapon_models_profile_qualify(image, abi, path, profile, error)) return false;
    application_q3_weapon_models_profile value = *profile;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) && profile_fields(&io, &value) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}

bool application_q3_weapon_models_profile_restore(const qa_qvm_image *image, qa_qvm_abi abi,
    const char *path, qa_bytes bytes, application_q3_weapon_models_profile *out, qa_error *error)
{
    if (!image || !path || !out || out->artifact_path || out->game_artifact_path || out->present)
        return application_fail(error, QA_ERROR_ARGUMENT, "CG weapon model profile import requires an empty candidate owner");
    application_q3_weapon_models_profile profile = {0};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && profile_fields(&io, &profile) &&
        qa_source_save_finish(&io, NULL) &&
        application_q3_weapon_models_profile_qualify(image, abi, path, &profile, error);
    qa_source_save_dispose(&io);
    if (!ok) { application_q3_weapon_models_profile_free(&profile); return false; }
    *out = profile; return true;
}
