#ifndef QA_NATIVE_HOOK_CONTROL_H
#define QA_NATIVE_HOOK_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

#define NATIVE_HOOK_CONTROL_MAGIC UINT64_C(0x51414e4f42534552)
#define NATIVE_HOOK_MAX_WATCH_BYTES 65536u

enum native_hook_operation {
    NATIVE_HOOK_ENTRY_ADD = 1,
    NATIVE_HOOK_ENTRY_REMOVE,
    NATIVE_HOOK_WATCH_ADD,
    NATIVE_HOOK_WATCH_REMOVE,
    NATIVE_HOOK_BYPASS_ARM,
    NATIVE_HOOK_BYPASS_CLEAR,
    NATIVE_HOOK_DEPTH,
    NATIVE_HOOK_REGION_SERVICE,
    NATIVE_HOOK_IMAGE
};

/* Fixed-width process-local marker packet. Both producer and DR client use the
 * same target pointer width; addresses still have an explicit 64-bit extent. */
typedef struct native_hook_control {
    uint64_t magic, operation, id, address, size, replacement, status;
} native_hook_control;

void qa_native_runner_hook_control(native_hook_control *control);
bool native_hooks_control(native_hook_control *control);
void native_hooks_depth(uint32_t depth);

#endif
