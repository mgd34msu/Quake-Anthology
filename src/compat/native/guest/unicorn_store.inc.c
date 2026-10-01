/* Include in Unicorn 2.1.4 qemu/accel/tcg/cputlb.c immediately after its
 * store_memop definition. Replace its three RAM store_memop call sites with
 * qa_unicorn_ram_store(env, paddr, haddr, val, op), preserving each op expression.
 * All types and the actual store operation come from that dependency unit. */
#include "unicorn_state.h"

_Static_assert(UC_API_MAJOR == 2 && UC_API_MINOR == 1 && UC_API_PATCH == 4 &&
    UC_API_EXTRA == 255, "native guest stores require Unicorn 2.1.4");

enum { QA_UNICORN_STORE_HOOK_MARK = 0x51415354 };

UNICORN_EXPORT unsigned qa_unicorn_store_revision(void)
{
    return QA_UNICORN_STORE_REVISION;
}

UNICORN_EXPORT uc_err qa_unicorn_store_bind(uc_engine *uc, uc_hook identity)
{
    if (!uc || uc->arch != UC_ARCH_X86) return UC_ERR_ARG;
    struct hook *hook;
    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(uc, hook, UC_HOOK_MEM_WRITE) {
        if ((uc_hook)hook == identity && !hook->to_delete &&
            hook->type == UC_HOOK_MEM_WRITE) {
            /* op_flags is used by TCG opcode hooks, never memory hooks. */
            hook->op_flags = QA_UNICORN_STORE_HOOK_MARK;
            return UC_ERR_OK;
        }
    }
    return UC_ERR_HOOK;
}

static bool qa_unicorn_store_event(struct uc_struct *uc, int event,
    uint64_t address, int size, uint64_t value)
{
    struct hook *hook;
    HOOK_FOREACH_VAR_DECLARE;
    HOOK_FOREACH(uc, hook, UC_HOOK_MEM_WRITE) {
        if (hook->op_flags != QA_UNICORN_STORE_HOOK_MARK ||
            !HOOK_BOUND_CHECK(hook, address)) continue;
        JIT_CALLBACK_GUARD(((uc_cb_hookmem_t)hook->callback)(uc,
            (uc_mem_type)event, address, size, (int64_t)value, hook->user_data));
    }
    return !uc->stop_request;
}

static inline void qa_unicorn_ram_store(CPUArchState *env, target_ulong address,
    void *host, uint64_t value, MemOp operation)
{
    struct uc_struct *uc = env->uc;
    int size = 1 << (operation & MO_SIZE);
    /* This runs after mapping, protection, watchpoint and CoW checks. A journal
     * allocation failure cancels this store before touching owned RAM. */
    if (uc->stop_request || !qa_unicorn_store_event(uc,
        QA_UNICORN_MEM_WRITE_PREPARE, address, size, value)) return;
    store_memop(host, value, operation);
    /* Even same-value stores and a prefix preceding a later fault are real. */
    qa_unicorn_store_event(uc, QA_UNICORN_MEM_WRITE_COMMITTED, address, size, value);
}
