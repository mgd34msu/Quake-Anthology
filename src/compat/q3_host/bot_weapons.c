#include "bot_records.h"
#include "qa/bot_runtime.h"
#include "qa/bot_weapons_source.h"

#include <stdio.h>

typedef struct weight_path { q3_call *call; qa_buffer text; } weight_path;

static bool path_read(void *context, const char **out, qa_error *error)
{
    weight_path *path = context;
    if (!q3_string(path->call, path->call->arguments[1], &path->text, error)) return false;
    *out = (const char *)path->text.data; return true;
}

static bool state(q3_call *call, qa_bot_runtime *runtime, int32_t handle)
{
    if (handle > 0 && qa_bot_runtime_weapon_has_handle(runtime, (uint32_t)handle)) return true;
    char text[96];
    snprintf(text, sizeof(text), handle < 1 ? "move state handle %d out of range" :
                                                         "invalid move state %d", handle);
    if (call->host->options.common.print)
        call->host->options.common.print(call->host->options.common.context, text);
    return false;
}

q3_service_result q3_bot_weapons(q3_call *call, int32_t *result, qa_error *error)
{
    if (call->host->options.role != QA_QVM_GAME || call->service < 558 || call->service > 563)
        return Q3_UNHANDLED;
    qa_bot_runtime *runtime = q3_bot_runtime(call);
    if (!runtime) { q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 bot weapon owner is unbound"); return Q3_FAILED; }
    uint32_t handle = (uint32_t)q3_integer(call, 0); bool ok;
    if (call->service == 561) {
        ok = qa_bot_runtime_weapon_allocate(runtime, &handle, error);
        if (ok) *result = (int32_t)handle;
    } else if (call->service == 559) {
        qa_bytes weapon; bool found;
        ok = qa_bot_runtime_weapon_source_info(runtime, handle, (uint32_t)q3_integer(call, 1),
                                                &weapon, &found, error);
        if (ok && found) ok = q3_write(call, call->arguments[2], weapon, error);
    } else {
        if (call->service == 560) *result = 11;
        if (!state(call, runtime, (int32_t)handle)) return Q3_COMPLETED;
        if (call->service == 558) {
            q3_bot_memory memory = {call, call->arguments[1]};
            qa_bot_inventory_view inventory = {.context = &memory, .read = q3_bot_inventory_read};
            uint32_t weapon;
            ok = qa_bot_runtime_weapon_choose_view(runtime, handle, &inventory, &weapon, error);
            if (ok) *result = (int32_t)weapon;
        } else if (call->service == 560) {
            weight_path path = {.call = call};
            ok = qa_bot_runtime_weapon_weights_from(runtime, handle, &path, path_read, result, error);
            qa_buffer_free(&path.text);
        } else if (call->service == 562) ok = qa_bot_runtime_weapon_free(runtime, handle, error);
        else ok = qa_bot_runtime_weapon_reset(runtime, handle, error);
    }
    return ok ? Q3_COMPLETED : Q3_FAILED;
}
