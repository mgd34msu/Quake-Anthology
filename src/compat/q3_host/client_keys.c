#include "internal.h"
#include "qa/q3_key.h"

q3_service_result q3_client_keys(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_UI ||
        (call->service != 53 && call->service != 54 && call->service != 81)) return Q3_UNHANDLED;
    if (call->service == 81) {
        qa_bytes key = {0}, checksum = {0};
        bool ok = q3_string(call, call->arguments[0], &key, error);
        if (ok && strlen((const char *)key.data) == 16) {
            if (call->arguments[1]) ok = q3_string(call, call->arguments[1], &checksum, error);
            if (ok) *result = qa_q3_key_valid((const char *)key.data, (const char *)checksum.data);
        }
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    qa_q3_key *keys = call->host->options.keys;
    if (!keys) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 product key owner is unbound"); return Q3_FAILED; }
    int32_t unique;
    if (call->vm) {
        int32_t words[10] = {10};
        if (!qa_qvm_invoke(call->vm, 0, words, 10, &unique, error)) return Q3_FAILED;
    } else {
        int32_t words[9] = {0};
        if (!qa_native_host_q3_vm_call(call->native_host, 10, words, 9, &unique, error)) return Q3_FAILED;
    }
    const char *directory = call->host->options.game_directory;
    uint8_t bytes[17];
    if (call->service == 53) {
        q3_record admitted;
        if (!q3_record_open(call, call->arguments[0], 17, &admitted, error)) return Q3_FAILED;
        qa_q3_key_read_ui(keys, unique, directory, bytes);
        if (!q3_write(call, call->arguments[0], (qa_bytes){bytes, 16}, error) ||
            !q3_write(call, call->arguments[0] + 16, (qa_bytes){bytes + 16, 1}, error)) return Q3_FAILED;
    } else {
        if (!q3_read(call, call->arguments[0], bytes, 16, error)) return Q3_FAILED;
        if (!qa_q3_key_write_ui_stored(keys,unique,directory,bytes,error)) return Q3_FAILED;
    }
    return Q3_COMPLETED;
}
