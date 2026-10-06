#include "guest_checkpoint.h"
#include "guest_qc_internal.h"
#include "guest_q3_save.h"
#include "qa/binary.h"
#include "qa/source_save.h"

static bool identity(const application_provider *provider, qa_buffer *out, qa_error *error)
{
    const qa_launch_instance *launch = provider->launch;
    uint32_t kind = provider->kind, product = launch->selection.product;
    uint64_t artifact_bytes = qa_resource_bytes(launch->artifact).size;
    uint64_t declaration_bytes = qa_resource_bytes(launch->declaration).size;
    uint8_t magic[4] = {'Q','A','G','C'};
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, NULL, error) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) &&
        qa_source_save_u32(&io, &kind) && qa_source_save_u32(&io, &product) &&
        qa_source_save_text_assert(&io, launch->selection.instance) &&
        qa_source_save_text_assert(&io, launch->selection.artifact) &&
        qa_source_save_text_assert(&io, launch->selection.component) &&
        qa_source_save_text_assert(&io, qa_resource_path(launch->declaration)) &&
        qa_source_save_u64(&io, &artifact_bytes) && qa_source_save_u64(&io, &declaration_bytes) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

static bool checkpoint_owner(application_provider *provider, qa_error *error)
{
    if (!provider || !provider->application || !provider->constructed || !provider->launch ||
        !qa_session_safe(provider->application->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest checkpoint requires a prepared idle provider");
    if (provider->kind == APPLICATION_PROVIDER_QC)
        return (qa_qc_idle(provider->state.qc.instance) && application_qc_input_idle(provider)) ||
            application_fail(error, QA_ERROR_ARGUMENT, "QuakeC continuation has an active source/input scope");
    if (provider->kind == APPLICATION_PROVIDER_QVM ||
        (provider->kind == APPLICATION_PROVIDER_NATIVE && provider->state.native.engine))
        return application_q3_guest_idle(provider) ||
            application_fail(error, QA_ERROR_ARGUMENT, "Q3 continuation has an active source/input scope");
    return application_fail(error, QA_ERROR_UNSUPPORTED,
        "Q3 guest checkpoint requires its actual GAME and shared bot continuation");
}

bool application_guest_checkpoint_capture(application_provider *provider,
    const struct qa_application_native_resource_refs *resources, qa_buffer *out, qa_error *error)
{
    if (!out || !checkpoint_owner(provider, error)) return false;
    qa_qc_checkpoint *snapshot = NULL;
    qa_buffer encoded = {0};
    bool ok = provider->kind != APPLICATION_PROVIDER_QC ?
        application_guest_q3_save_capture(provider, resources, &encoded, error) :
        (qa_qc_checkpoint_capture(provider->state.qc.instance, &snapshot, error) &&
         qa_qc_checkpoint_encode(snapshot, &encoded, error));
    qa_qc_checkpoint_destroy(snapshot);
    if (!ok) return false;
    qa_buffer owner = {0};
    if (!identity(provider, &owner, error)) { qa_buffer_free(&encoded); return false; }
    if (owner.size > SIZE_MAX - sizeof(uint64_t) ||
        encoded.size > SIZE_MAX - owner.size - sizeof(uint64_t)) {
        qa_buffer_free(&owner);
        qa_buffer_free(&encoded);
        return application_fail(error, QA_ERROR_MEMORY, "Guest checkpoint extent overflow");
    }
    size_t header = owner.size + sizeof(uint64_t);
    qa_buffer bytes = {.size = header + encoded.size};
    bytes.data = calloc(1, bytes.size);
    if (!bytes.data) {
        qa_buffer_free(&owner);
        qa_buffer_free(&encoded);
        return application_fail(error, QA_ERROR_MEMORY, "Allocating guest continuation");
    }
    memcpy(bytes.data, owner.data, owner.size);
    qa_store_u64le(bytes.data + owner.size, encoded.size);
    if (encoded.size) memcpy(bytes.data + header, encoded.data, encoded.size);
    qa_buffer_free(&owner);
    qa_buffer_free(&encoded);
    *out = bytes;
    return true;
}

bool application_guest_checkpoint_body(const application_provider *provider,
                                         qa_bytes bytes, qa_bytes *out, qa_error *error)
{
    if (!provider || !provider->launch || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing guest checkpoint identity");
    qa_buffer expected = {0};
    if (!identity(provider, &expected, error)) return false;
    size_t header = expected.size + sizeof(uint64_t);
    bool ok = bytes.data && bytes.size >= header &&
        !memcmp(bytes.data, expected.data, expected.size) &&
        qa_load_u64le(bytes.data + expected.size) == bytes.size - header;
    qa_buffer_free(&expected);
    if (!ok)
        return application_fail(error, QA_ERROR_FORMAT, "Guest backend/content checkpoint identity differs");
    *out = (qa_bytes){bytes.data + header, bytes.size - header};
    return true;
}

bool application_guest_checkpoint_restore(application_provider *provider,
                                             qa_bytes bytes, qa_error *error)
{
    qa_bytes body;
    if (!checkpoint_owner(provider, error) || !application_guest_checkpoint_body(provider, bytes, &body, error))
        return false;
    if (provider->kind != APPLICATION_PROVIDER_QC)
        return application_guest_q3_save_restore(provider, body, error);
    qa_qc_checkpoint *snapshot = NULL;
    bool ok = qa_qc_checkpoint_decode(body, &snapshot, error) &&
        qa_qc_checkpoint_restore(provider->state.qc.instance, snapshot, error);
    qa_qc_checkpoint_destroy(snapshot);
    return ok;
}
