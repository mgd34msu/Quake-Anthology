#ifndef QA_NATIVE_UNICORN_STATE_H
#define QA_NATIVE_UNICORN_STATE_H

#include "qa/native_guest.h"
#include <unicorn/unicorn.h>

enum { QA_UNICORN_STATE_REVISION = 1 };
enum { QA_UNICORN_STORE_REVISION = 1,
    QA_UNICORN_MEM_WRITE_PREPARE = 0x514100,
    QA_UNICORN_MEM_WRITE_COMMITTED = 0x514101 };
enum { QA_UNICORN_MAP_REVISION = 1 };
unsigned qa_unicorn_map_revision(void);
bool qa_unicorn_memory_owned(const uc_engine *);
uc_err qa_unicorn_memory_bind(uc_engine *);
uc_err qa_unicorn_memory_map(uc_engine *, uint64_t, size_t, uint32_t, void *);
uc_err qa_unicorn_memory_change(uc_engine *, uint64_t, size_t, uint32_t, bool);
uc_err qa_unicorn_memory_release(uc_engine *);

/* Bodies are compiled inside the pinned dependency's actual x86 source unit.
 * Public Unicorn selector operations cannot transfer cached segment state. */
unsigned qa_unicorn_state_revision(void);
uc_err qa_unicorn_x86_state(uc_engine *, qa_native_guest_cpu *, bool);
unsigned qa_unicorn_store_revision(void);
uc_err qa_unicorn_store_bind(uc_engine *, uc_hook);

#endif
