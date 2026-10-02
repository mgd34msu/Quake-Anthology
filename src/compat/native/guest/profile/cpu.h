#ifndef QA_NATIVE_GUEST_PROFILE_CPU_H
#define QA_NATIVE_GUEST_PROFILE_CPU_H

#include "../host_x86_64.h"

/* Actual physical constructor baseline. These named values join the lower
 * named CPU checkpoint; they are not a signature-derived safety assertion. */
typedef struct guest_profile_cpu_domain {
    uint64_t xfeatures;
    uint32_t pkru;
    bool has_pkru;
} guest_profile_cpu_domain;

bool guest_profile_cpu_domain_read(const guest_host_x86_64_state *,
    const guest_host_x86_64_capabilities *, guest_profile_cpu_domain *, qa_error *);
/* Fresh source construction only, before IFUNC/TLS/DllMain/Init. The caller
 * installs this real state once. No restore/continuation caller may reset it. */
bool guest_profile_cpu_initialize(const guest_profile_cpu_domain *,
    const guest_host_x86_64_capabilities *, guest_host_x86_64_state *, qa_error *);
/* Pure fresh/cold/entered/stopped qualification. The source advertises only
 * x87/SSE; its instruction guard excludes writes to the other user components.
 * Their actual named registers must have init values, whether their captured
 * BV bits are clear or were materialized by real signal delivery. The physical
 * domain remains distinct from the actual kernel transfer mask, which may omit
 * unused components. Qualification never enlarges that transfer mask.
 * PKRU must match the physical controller baseline. Neither this predicate nor
 * the installed monitor receipt proves a third-party engine's preservation. */
bool guest_profile_cpu_current(const guest_profile_cpu_domain *,
    const guest_host_x86_64_capabilities *, const guest_host_x86_64_state *, qa_error *);

#endif
