/* Text-include before do_tb_flush in pinned translate-all.c, x86_64-softmmu.
 * Its actual flush path must not allocate while committing RAM or closing. */
#include "unicorn_state.h"

static void qa_unicorn_memory_tb_reset(CPUState *cpu)
{
    if (qa_unicorn_memory_owned(cpu->uc))
        qht_reset(&cpu->uc->tcg_ctx->tb_ctx.htable);
    else
        qht_reset_size(cpu->uc, &cpu->uc->tcg_ctx->tb_ctx.htable, CODE_GEN_HTABLE_SIZE);
}
