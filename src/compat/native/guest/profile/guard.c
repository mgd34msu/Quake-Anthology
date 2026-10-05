#include "guard.h"
#include <stdatomic.h>

#if defined(__linux__) && defined(__x86_64__)
__attribute__((noinline, visibility("default")))
void qa_guest_profile_control(guest_profile_guard_control *control)
{
    (void)control;
    atomic_signal_fence(memory_order_seq_cst);
}
/* A rejecting guard redirects the original app context here after preserving
 * FP state. The installed Linux signal bridge captures the real stopped CPU;
 * the guard receipt supplies the observed original instruction address. */
__asm__(".text\n.globl qa_guest_profile_fault\n.type qa_guest_profile_fault,@function\n"
    "qa_guest_profile_fault:\n ud2\n.size qa_guest_profile_fault,.-qa_guest_profile_fault\n"
    ".globl qa_guest_profile_import\n.type qa_guest_profile_import,@function\n"
    "qa_guest_profile_import:\n int3\n.size qa_guest_profile_import,.-qa_guest_profile_import\n");
#else
void qa_guest_profile_control(guest_profile_guard_control *control) { (void)control; }
void qa_guest_profile_fault(void) { }
void qa_guest_profile_import(void) { }
#endif

bool guest_profile_guard_control_call(guest_profile_guard_control *control, qa_error *error)
{
    if (!control || control->operation > GUEST_PROFILE_GUARD_CAPTURE ||
        (control->mapping_count && !control->mappings) || (control->callback_count && !control->callbacks)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "profile control requires actual child observations"); return false;
    }
    control->magic = GUEST_PROFILE_GUARD_MAGIC;
    control->status = 0; control->reserved = 0;
    control->installed = (guest_profile_guard_receipt){0};
    qa_guest_profile_control(control);
    atomic_signal_fence(memory_order_seq_cst);
    if (control->status != 1 || control->installed.policy != GUEST_PROFILE_GUARD_SOURCE_X64) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, (size_t)control->scope,
            "actual instruction guard did not admit the child control transfer"); return false;
    }
    return true;
}
