/* Text-include before tlb_flush_one_mmuidx_locked in pinned cputlb.c,
 * x86_64-softmmu only. Owned flushes retain their already allocated tables. */
#include "unicorn_state.h"

static void qa_unicorn_memory_tlb_resize(CPUArchState *env, CPUTLBDesc *desc,
    CPUTLBDescFast *fast, int64_t now)
{
    if (!qa_unicorn_memory_owned(env->uc))
        tlb_mmu_resize_locked(env->uc, desc, fast, now);
}
