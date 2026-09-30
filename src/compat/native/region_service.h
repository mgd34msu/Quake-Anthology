#ifndef QA_NATIVE_REGION_SERVICE_H
#define QA_NATIVE_REGION_SERVICE_H

#include <stdint.h>

#define NATIVE_REGION_SERVICE_MAGIC UINT64_C(0x5141524547494f4e)
#define NATIVE_REGION_SERVICE_LIMIT 64u

typedef struct native_region_state {
    uint64_t registers[16];
    uint8_t simd[16][16];
    uint64_t flags, instruction;
} native_region_state;

/* Process-local owned frame. No pointer or machine context is sent on wire. */
typedef struct native_region_service {
    uint64_t magic, cookie;
    uint32_t id, phase, depth, action, replace_state, completed;
    native_region_state before, after;
} native_region_service;

void qa_native_runner_region_service(native_region_service *packet);
void qa_native_runner_region_resume(native_region_service *packet);

#endif
