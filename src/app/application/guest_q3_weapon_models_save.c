#include "guest_q3_weapon_models_private.h"
#include "internal.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>

static bool signature(qa_source_save_io *io, const application_q3_weapon_models *owner)
{
    uint8_t magic[8] = {'Q','A','G','3','W','M',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','W','M',0,0};
    uint32_t version = 1, abi = owner->module.profile->abi;
    qa_sha256_digest digest = owner->module.profile->artifact;
    qa_sha256_digest game_digest = owner->module.profile->game_artifact;
    uint32_t game_abi = owner->module.profile->game_abi;
    bool present = owner->module.profile->present;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || !qa_source_save_u32(io, &version) ||
        !qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) || !qa_source_save_u32(io, &abi) ||
        !qa_source_save_bytes(io, game_digest.bytes, sizeof(game_digest.bytes)) || !qa_source_save_u32(io, &game_abi) ||
        !qa_source_save_bool(io, &present)) return false;
    return (!memcmp(magic, expected, sizeof(magic)) && version == 1 && abi == (uint32_t)owner->module.profile->abi &&
        qa_sha256_equal(&digest, &owner->module.profile->artifact) &&
        qa_sha256_equal(&game_digest, &owner->module.profile->game_artifact) &&
        game_abi == (uint32_t)owner->module.profile->game_abi && present == owner->module.profile->present) ||
        application_fail(io->error, QA_ERROR_FORMAT, "CG weapon model continuation differs from its actual artifact");
}
static bool record_fields(qa_source_save_io *io, const application_q3_weapon_models_profile *profile,
    application_q3_weapon_model_record *record)
{
    if (!qa_source_save_i32(io, &record->weapon) || !qa_source_save_u32(io, &record->row)) return false;
    for (size_t i = 0; i < APPLICATION_Q3_WEAPON_MODEL_FIELDS; ++i)
        if (!qa_source_save_i32(io, record->handles + i)) return false;
    if (record->row >= profile->count || record->handles[APPLICATION_Q3_WEAPON_GUN] < 0 ||
        (profile->indexed && (int64_t)profile->index_base + record->row != record->weapon))
        return application_fail(io->error, QA_ERROR_FORMAT, "Saved CG weapon receipt leaves its actual table identity");
    for (size_t i = 0; i < APPLICATION_Q3_WEAPON_MODEL_FIELDS; ++i)
        if (record->handles[i] < 0 || (!profile->fields[i] && record->handles[i]))
            return application_fail(io->error, QA_ERROR_FORMAT, "Saved CG weapon receipt invents an undeclared registry field");
    return true;
}
bool application_q3_weapon_models_checkpoint(const application_q3_weapon_models *owner,
    qa_buffer *out, qa_error *error)
{
    if (!owner || !out || out->data || out->size || !application_q3_weapon_models_idle(owner) ||
        !qa_qvm_source_returned(owner->module.vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG model capture needs its actual returned source and registry");
    if (!application_q3_weapon_models_qualify((application_q3_weapon_models *)owner, error)) return false;
    qa_qvm_saved_function descriptor = {0};
    if (!application_q3_weapon_models_descriptors(owner, &descriptor,
        application_q3_weapon_models_descriptor_count(owner), error)) return false;
    qa_source_save_io io = {0}; qa_qvm_binding binding = owner->binding; size_t count = owner->count;
    bool ok = qa_source_save_writer(&io, owner->module.session, error) && signature(&io, owner) &&
        qa_source_save_u64(&io, &binding) && qa_source_save_count(&io, &count, owner->module.profile->count);
    for (size_t i = 0; ok && i < count; ++i) {
        application_q3_weapon_model_record record = owner->records[i];
        ok = record_fields(&io, owner->module.profile, &record);
    }
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_weapon_models_restore(application_q3_weapon_models *owner, qa_bytes bytes,
    qa_qvm_binding *saved_binding, qa_error *error)
{
    if (!owner || !saved_binding || !owner->module.importing || owner->imported || owner->count ||
        !application_q3_weapon_models_idle(owner) || !qa_qvm_source_returned(owner->module.vm))
        return application_fail(error, QA_ERROR_ARGUMENT, "CG receipt import needs its untouched isolated constructor");
    qa_source_save_io io = {0}; qa_qvm_binding binding = 0; size_t count = 0;
    bool ok = qa_source_save_reader(&io, owner->module.session, bytes, error) && signature(&io, owner) &&
        qa_source_save_u64(&io, &binding) && qa_source_save_count(&io, &count, owner->module.profile->count);
    if (ok && ((binding != 0) != owner->module.profile->present || (!owner->module.profile->present && count)))
        ok = application_fail(error, QA_ERROR_FORMAT, "Saved CG model bindings differ from their declared constructor");
    if (ok && count > (io.input.size - io.offset) / (8 + 4 * APPLICATION_Q3_WEAPON_MODEL_FIELDS))
        ok = application_fail(error, QA_ERROR_FORMAT, "Truncated CG weapon model receipt inventory");
    application_q3_weapon_model_record *records = NULL;
    if (ok && count > SIZE_MAX / sizeof(*records))
        ok = application_fail(error, QA_ERROR_MEMORY, "Saved CG model receipt inventory exceeds address space");
    if (ok && count) {
        records = calloc(count, sizeof(*records));
        if (!records) ok = application_fail(error, QA_ERROR_MEMORY, "Restoring actual CG weapon model receipts");
    }
    for (size_t i = 0; ok && i < count; ++i) {
        ok = record_fields(&io, owner->module.profile, records + i);
        for (size_t j = 0; ok && j < i; ++j)
            if (records[i].weapon == records[j].weapon || records[i].row == records[j].row)
                ok = application_fail(error, QA_ERROR_FORMAT, "Saved CG model receipts repeat a source weapon or table row");
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!ok) { free(records); return false; }
    owner->records = records; owner->count = count; owner->imported = true;
    *saved_binding = binding; return true;
}
