#include "bot_records.h"
#include "qa/bot_runtime.h"

static bool rank(void *context, int32_t index, float *out, qa_error *error)
{
    q3_call *call = context;
    q3_bot_memory memory = {call, call->arguments[1]};
    int32_t bits;
    if (!q3_bot_inventory_read(&memory, index, &bits, error)) return false;
    memcpy(out, &bits, sizeof(*out)); return true;
}

static bool publish(void *context, qa_bot_genetic_target target, int32_t value, qa_error *error)
{
    q3_call *call = context;
    size_t index = target == QA_BOT_GENETIC_PARENT1 ? 2 : target == QA_BOT_GENETIC_PARENT2 ? 3 : 4;
    return q3_write_word(call, call->arguments[index], (uint32_t)value, error);
}

static void warning(void *context, const char *text)
{
    q3_call *call = context;
    if (call->host->options.common.print)
        call->host->options.common.print(call->host->options.common.context, text);
}

q3_service_result q3_bot_genetic(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_GAME || call->service != 564) return Q3_UNHANDLED;
    qa_bot_runtime *runtime = q3_bot_runtime(call);
    if (!runtime) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot runtime is unbound"); return Q3_FAILED;
    }
    qa_bot_genetic_source source = {.context = call, .read = rank, .write = publish, .warning = warning};
    qa_bot_random_source random = qa_bot_runtime_random_source(runtime);
    qa_bot_genetic_selection selection;
    if (!qa_bot_genetic_select_from(q3_integer(call, 0), &source, &random, &selection, error)) return Q3_FAILED;
    *result = selection.status == QA_BOT_GENETIC_SELECTED; return Q3_COMPLETED;
}
