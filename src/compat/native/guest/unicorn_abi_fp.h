#ifndef QA_NATIVE_UNICORN_ABI_FP_H
#define QA_NATIVE_UNICORN_ABI_FP_H
#include <unicorn/unicorn.h>
#include <stdbool.h>
#include <stdint.h>
enum { QA_UNICORN_ABI_FP_REVISION = 1 };
unsigned qa_unicorn_abi_fp_revision(void);
uc_err qa_unicorn_abi_fp(uc_engine *, bool, uint64_t *, bool);
#endif
