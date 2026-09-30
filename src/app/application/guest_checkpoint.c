#include "guest_checkpoint.h"
#include "guest_qc_internal.h"
#include "guest_q3_save.h"
#include "qa/binary.h"

enum { GUEST_CHECKPOINT_HEADER = 120 };

static void identity(const application_provider *provider, uint8_t *out)
{
    memcpy(out, "QAGC", 4);
    qa_store_u32le(out + 4, 1);
    qa_store_u32le(out + 8, provider->kind);
    memcpy(out + 16, provider->launch->identity.bytes, 32);
    const qa_sha256_digest *artifact = qa_resource_digest(provider->launch->artifact);
    const qa_sha256_digest *declaration = qa_resource_digest(provider->launch->declaration);
    if (artifact) memcpy(out + 48, artifact->bytes, 32);
    if (declaration) memcpy(out + 80, declaration->bytes, 32);
}

static bool checkpoint_owner(application_provider *provider, qa_error *error)
{
    if (!provider || !provider->application || !provider->constructed || !provider->launch ||
        !qa_session_safe(provider->application->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Guest checkpoint requires a prepared idle provider");
    if (provider->kind == APPLICATION_PROVIDER_QC)
        return (qa_qc_idle(provider->state.qc.instance) && application_qc_input_idle(provider)) ||
            application_fail(error, QA_ERROR_ARGUMENT, "QuakeC continuation has an active source/input scope");
    if (provider->kind == APPLICATION_PROVIDER_QVM)
        return application_q3_guest_idle(provider) ||
            application_fail(error, QA_ERROR_ARGUMENT, "Q3 continuation has an active source/input scope");
    return application_fail(error, QA_ERROR_UNSUPPORTED,
        "Q3 guest checkpoint requires coupled role, client and shared bot continuation");
}

bool application_guest_checkpoint_capture(application_provider *provider,
                                             qa_buffer *out, qa_error *error)
{
    if (!out || !checkpoint_owner(provider, error)) return false;
    qa_qc_checkpoint *snapshot = NULL;
    qa_buffer encoded = {0};
    bool ok = provider->kind == APPLICATION_PROVIDER_QVM ?
        application_guest_q3_save_capture(provider, &encoded, error) :
        (qa_qc_checkpoint_capture(provider->state.qc.instance, &snapshot, error) &&
         qa_qc_checkpoint_encode(snapshot, &encoded, error));
    qa_qc_checkpoint_destroy(snapshot);
    if (!ok) return false;
    if (encoded.size > SIZE_MAX - GUEST_CHECKPOINT_HEADER) {
        qa_buffer_free(&encoded);
        return application_fail(error, QA_ERROR_MEMORY, "Guest checkpoint extent overflow");
    }
    qa_buffer bytes = {.size = GUEST_CHECKPOINT_HEADER + encoded.size};
    bytes.data = calloc(1, bytes.size);
    if (!bytes.data) {
        qa_buffer_free(&encoded);
        return application_fail(error, QA_ERROR_MEMORY, "Allocating guest continuation");
    }
    identity(provider, bytes.data);
    qa_store_u64le(bytes.data + 112, encoded.size);
    if (encoded.size) memcpy(bytes.data + GUEST_CHECKPOINT_HEADER, encoded.data, encoded.size);
    qa_buffer_free(&encoded);
    *out = bytes;
    return true;
}

bool application_guest_checkpoint_body(const application_provider *provider,
                                         qa_bytes bytes, qa_bytes *out, qa_error *error)
{
    if (!provider || !provider->launch || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing guest checkpoint identity");
    uint8_t expected[GUEST_CHECKPOINT_HEADER] = {0};
    identity(provider, expected);
    if (!bytes.data || bytes.size < GUEST_CHECKPOINT_HEADER ||
        memcmp(bytes.data, expected, 112) ||
        qa_load_u64le(bytes.data + 112) != bytes.size - GUEST_CHECKPOINT_HEADER)
        return application_fail(error, QA_ERROR_FORMAT, "Guest backend/content checkpoint identity differs");
    *out = (qa_bytes){bytes.data + GUEST_CHECKPOINT_HEADER, bytes.size - GUEST_CHECKPOINT_HEADER};
    return true;
}

bool application_guest_checkpoint_restore(application_provider *provider,
                                             qa_bytes bytes, qa_error *error)
{
    qa_bytes body;
    if (!checkpoint_owner(provider, error) || !application_guest_checkpoint_body(provider, bytes, &body, error))
        return false;
    if (provider->kind == APPLICATION_PROVIDER_QVM)
        return application_guest_q3_save_restore(provider, body, error);
    qa_qc_checkpoint *snapshot = NULL;
    bool ok = qa_qc_checkpoint_decode(body, &snapshot, error) &&
        qa_qc_checkpoint_restore(provider->state.qc.instance, snapshot, error);
    qa_qc_checkpoint_destroy(snapshot);
    return ok;
}
