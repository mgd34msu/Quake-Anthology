#include "source_call.h"
#include "internal.h"
#include "qa/q3_host.h"

bool application_source_qvm_arguments(const qa_qvm_call *call,
    const qa_native_value_type *types, size_t count, qa_native_value *values, qa_error *error)
{
    for (size_t i = 0; i < count; ++i) {
        int32_t word;
        if (!qa_qvm_call_argument(call, i, &word, error)) return false;
        values[i].type = types ? types[i] : QA_NATIVE_ADDRESS;
        if (!qa_q3_source_word_to_value(values + i, word, error)) return false;
    }
    return true;
}

bool application_source_qvm_proceed(void *context, const qa_native_value *values,
    size_t count, qa_error *error)
{
    application_source_qvm_call *source = context;
    int32_t prior[QA_QVM_ARGUMENTS];
    size_t saved = 0;
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        int32_t word;
        if (!qa_q3_source_value_to_word(values + i, &word, error) ||
            !qa_qvm_call_argument(source->call, i, prior + i, error)) {
            ok = false;
            break;
        }
        ++saved;
        if (word != prior[i]) ok = qa_qvm_call_set_argument(source->call, i, word, error);
    }
    if (ok) ok = qa_qvm_proceed(source->call, source->result, error);
    qa_error first = error ? *error : (qa_error){0};
    for (size_t i = 0; i < saved; ++i) {
        qa_error restore = {0};
        if (!qa_qvm_call_set_argument(source->call, i, prior[i], &restore)) {
            if (ok) first = restore;
            ok = false;
        }
    }
    if (!ok && error) *error = first;
    return ok;
}

bool application_source_qvm_cancel(void *context, qa_error *error)
{ return qa_qvm_cancel(((application_source_qvm_call *)context)->call, error); }

bool application_source_qvm_cancelled(void *context, bool *out, qa_error *error)
{ return qa_qvm_call_cancelled(((application_source_qvm_call *)context)->call, out, error); }

static bool native_accepts(void *context, const qa_error *error)
{
    application_source_native_call *call = context;
    return call->cancelling && qa_error_same(error, &call->cancellation);
}

bool application_source_native_cancel(void *context, qa_error *error)
{
    application_source_native_call *call = context;
    application_fail(&call->cancellation, QA_ERROR_ARGUMENT,
        "Original source actor retired during its callback");
    call->cancelling = true;
    if (error) *error = call->cancellation;
    return false;
}

bool application_source_native_cancelled(void *context, bool *out, qa_error *error)
{
    (void)error;
    *out = ((application_source_native_call *)context)->cancelling;
    return true;
}

bool application_source_native_proceed(void *context, const qa_native_value *values,
    size_t count, qa_error *error)
{
    application_source_native_call *call = context;
    if (call->result->type != QA_NATIVE_VOID)
        return qa_native_invoke_original(call->binding, values, count, call->result, error);
    bool cancelled;
    return qa_native_invoke_original_cancellable(call->binding, values, count, call->result,
        native_accepts, call, &cancelled, error);
}
