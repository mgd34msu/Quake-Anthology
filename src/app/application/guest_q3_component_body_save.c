#include "guest_q3_component_body_private.h"
#include "internal.h"
#include "qa/source_save.h"

#include <string.h>

static bool fields(qa_source_save_io *io, const application_q3_body_profile *profile,
    qa_qvm_binding bindings[2])
{
    uint8_t magic[8] = {'Q','A','G','3','C','B',0,0};
    uint32_t version = 1, player = profile->submissions[0].entry,
        mesh = profile->submissions[0].mesh_entry;
    qa_sha256_digest digest = profile->artifact;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QAG3CB\0\0", sizeof(magic)) ||
        !qa_source_save_u32(io, &version) || version != 1 ||
        !qa_source_save_bytes(io, digest.bytes, sizeof(digest.bytes)) ||
        !qa_sha256_equal(&digest, &profile->artifact) ||
        !qa_source_save_u32(io, &player) || player != profile->submissions[0].entry ||
        !qa_source_save_u32(io, &mesh) || mesh != profile->submissions[0].mesh_entry ||
        !qa_source_save_u64(io, bindings) || !qa_source_save_u64(io, bindings + 1) ||
        !bindings[0] || !bindings[1] || bindings[0] == bindings[1])
        return application_fail(io->error, QA_ERROR_FORMAT, "Component body callback state differs from its real declaration");
    return true;
}
bool application_q3_component_body_checkpoint(const application_q3_component_body *owner,
    qa_buffer *out, qa_error *error)
{
    qa_qvm_saved_function descriptors[2];
    if (!out || out->data || out->size ||
        !application_q3_component_body_descriptors(owner, descriptors, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body capture needs its complete idle owner");
    qa_qvm_binding bindings[2] = {descriptors[0].binding, descriptors[1].binding};
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, NULL, error) &&
        fields(&io, owner->options.profile, bindings) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}
bool application_q3_component_body_saved_read(const application_q3_body_profile *profile,
    qa_bytes bytes, qa_qvm_binding out[2], qa_error *error)
{
    if (!profile || !profile->present || profile->count != 1 || !profile->submissions ||
        !profile->submissions[0].mesh || !out || out[0] || out[1])
        return application_fail(error, QA_ERROR_ARGUMENT, "Component body import needs its qualified declaration and empty identities");
    qa_qvm_binding bindings[2] = {0}; qa_source_save_io io = {0};
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && fields(&io, profile, bindings) &&
        qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (okay) { out[0] = bindings[0]; out[1] = bindings[1]; }
    return okay;
}
