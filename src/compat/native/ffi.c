#include "internal.h"
#include "guest/internal.h"

void native_latch_error(qa_native_instance *instance, const qa_error *error) {
    if (instance->failed) return;
    instance->failure = error ? *error : (qa_error){.code = QA_ERROR_ARGUMENT};
    if (!instance->failure.message[0])
        snprintf(instance->failure.message, sizeof(instance->failure.message),
                 "native import callback failed");
    if (instance->guest && guest_callback_failure(instance->guest, &instance->failure)) return;
    instance->failed = true;
}

bool native_dispatch_import(qa_native_instance *instance, const native_signature_spec *spec,
                            const qa_native_value *arguments, size_t count,
                            qa_native_value *result) {
    qa_error error = {0};
    if (instance->module->info.profile == QA_NATIVE_QUAKE_LIVE_GAME_API10 && spec->slot == 22) {
        if (count != 5 || arguments[0].type != QA_NATIVE_ADDRESS ||
            arguments[1].type != QA_NATIVE_I32 || arguments[2].type != QA_NATIVE_I32 ||
            arguments[3].type != QA_NATIVE_ADDRESS || arguments[4].type != QA_NATIVE_I32 ||
            arguments[1].as.i32 < 0 || arguments[2].as.i32 <= 0 || arguments[4].as.i32 <= 0) {
            qa_error_set(&error, QA_ERROR_ARGUMENT, spec->slot,
                         "Quake Live LocateGameData arguments are invalid");
            native_latch_error(instance, &error);
            return false;
        }
        qa_native_entity_table table = {.base = arguments[0].as.address,
                                        .stride = (size_t)arguments[2].as.i32,
                                        .count = (uint32_t)arguments[1].as.i32,
                                        .capacity = (uint32_t)arguments[1].as.i32};
        if (!native_entity_table_store(instance, table, &error)) {
            native_latch_error(instance, &error);
            return false;
        }
    }
    if (spec->dispatch == QA_NATIVE_Q2_IMPORT_TagMalloc && count == 2) {
        uint64_t requested;
        if (arguments[0].type == QA_NATIVE_I32) {
            if (arguments[0].as.i32 < 0) {
                qa_error_set(&error, QA_ERROR_ARGUMENT, 0, "native TagMalloc size is negative");
                native_latch_error(instance, &error);
                return false;
            }
            requested = (uint32_t)arguments[0].as.i32;
        } else {
            requested = arguments[0].as.u64;
        }
#if SIZE_MAX < UINT64_MAX
        if (requested > (uint64_t)SIZE_MAX) {
            qa_error_set(&error, QA_ERROR_MEMORY, 0, "native TagMalloc size exceeds the host");
            native_latch_error(instance, &error);
            return false;
        }
#endif
        qa_native_address address;
        if (!qa_native_allocate(instance, (size_t)requested, arguments[1].as.i32, &address,
                                &error)) {
            native_latch_error(instance, &error);
            return false;
        }
        *result = (qa_native_value){.type = QA_NATIVE_ADDRESS, .as.address = address};
        return true;
    }
    if (spec->dispatch == QA_NATIVE_Q2_IMPORT_TagFree && count == 1) {
        if (!qa_native_free(instance, arguments[0].as.address, &error)) {
            native_latch_error(instance, &error);
            return false;
        }
        *result = (qa_native_value){.type = QA_NATIVE_VOID};
        return true;
    }
    if (spec->dispatch == QA_NATIVE_Q2_IMPORT_FreeTags && count == 1) {
        qa_native_free_tag(instance, arguments[0].as.i32);
        *result = (qa_native_value){.type = QA_NATIVE_VOID};
        return true;
    }
    if (!instance->options.import) {
        qa_error_set(&error, QA_ERROR_UNSUPPORTED, spec->slot, "native import %s is not bound",
                     spec->name);
        native_latch_error(instance, &error);
        return false;
    }
    qa_native_import_call call = {.profile = instance->module->info.profile,
                                  .slot = spec->slot,
                                  .name = spec->name,
                                  .signature = &spec->signature,
                                  .arguments = arguments,
                                  .argument_count = count};
    ++instance->callback_depth;
    bool ok = instance->options.import(instance->options.context, instance, &call, result, &error);
    --instance->callback_depth;
    if (!ok)
        native_latch_error(instance, &error);
    return ok;
}

bool native_import_bind(native_import_binding *binding, qa_native_instance *instance,
                        const native_signature_spec *spec, qa_error *error) {
    return native_process_import_bind(binding, instance, spec, error);
}

void native_import_unbind(native_import_binding *binding) {
    if (!binding) return;
    guest_abi_plan_destroy(binding->guest_plan);
    memset(binding, 0, sizeof(*binding));
}
