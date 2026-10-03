#ifndef QA_NATIVE_GUEST_NATIVE_CPU_CODEC_H
#define QA_NATIVE_GUEST_NATIVE_CPU_CODEC_H

#include "host_x86_64.h"

/* QAHC records architectural fields, never an XSAVE/ucontext byte image.
 * The destination's actual CPUID layout constructs a new aligned transient
 * transfer. Unsupported enabled user components require a different genuine
 * execution profile; they cannot be silently dropped, even when inactive.
 * Capture/admission of the surrounding stopped process belongs to its owner. */
bool guest_native_cpu_checkpoint(const guest_host_x86_64_state *,
    const guest_host_x86_64_capabilities *, qa_buffer *, qa_error *);
bool guest_native_cpu_restore(qa_bytes,
    const guest_host_x86_64_capabilities *, guest_host_x86_64_state *, qa_error *);

#endif
