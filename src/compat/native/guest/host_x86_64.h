#ifndef QA_NATIVE_GUEST_HOST_X86_64_H
#define QA_NATIVE_GUEST_HOST_X86_64_H

#ifndef __ASSEMBLER__
#include "qa/native_guest.h"

/* Hardware user state is distinct from an emulated privileged CPU. XSAVE owns
 * every enabled physical component, including components beyond x87/SSE. The
 * XSAVE buffer is a transient hardware transfer, never a cold wire format. */
typedef struct guest_host_x86_64_state {
    uint64_t registers[16], instruction, flags, fs_base, gs_base;
    uint16_t selectors[6];
    uint64_t xfeatures;
    qa_buffer xsave;
} guest_host_x86_64_state;
typedef struct guest_host_x86_64_capabilities {
    uint64_t xfeatures;
    uint32_t xsave_bytes, mxcsr_mask;
} guest_host_x86_64_capabilities;
typedef struct guest_host_x86_64_component {
    uint32_t index, offset, bytes;
    bool supervisor, compact_align64;
} guest_host_x86_64_component;

bool guest_host_x86_64_capability(guest_host_x86_64_capabilities *, qa_error *);
bool guest_host_x86_64_capture(guest_host_x86_64_state *, qa_error *);
bool guest_host_x86_64_component_read(uint32_t, guest_host_x86_64_component *, qa_error *);
bool guest_host_x86_64_state_copy(const guest_host_x86_64_state *,
    guest_host_x86_64_state *, qa_error *);
void guest_host_x86_64_state_free(guest_host_x86_64_state *);
bool guest_host_x86_64_state_valid(const guest_host_x86_64_state *,
    const guest_host_x86_64_capabilities *, qa_error *);
bool guest_host_x86_64_from_cpu(const qa_native_guest_cpu *,
    const guest_host_x86_64_state *, guest_host_x86_64_state *, qa_error *);
bool guest_host_x86_64_to_cpu(const guest_host_x86_64_state *,
    qa_native_guest_cpu *, qa_error *);

/* Child-private bridge; its signal wrapper restores actual host FS/GS and full
 * floating context before C, then restores guest state on signal return.
 * Actual flat selector identities must match; bases remain separately owned. */
typedef bool (*guest_host_x86_64_stop_fn)(void *, int, void *,
    guest_host_x86_64_state *, bool *, qa_error *);
bool guest_host_x86_64_bridge_open(guest_host_x86_64_stop_fn, void *, qa_error *);
bool guest_host_x86_64_enter(const guest_host_x86_64_state *, qa_error *);
void guest_host_x86_64_bridge_close(void);

#endif
#endif
