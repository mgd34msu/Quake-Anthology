/* Port of src/compat/q2/rerelease/pickup-protection.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_PICKUP_PROTECTION_H
#define QA_COMPAT_Q2_RERELEASE_PICKUP_PROTECTION_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/host.h"

/* TS withRereleasePrimaryProtection<T>: operation receives the
 * consume callback (which runs its execute callback between the
 * primary-current checks) and returns an opaque result. committed
 * NULL is the TS default () => true. */
void *qa_compat_q2_with_rerelease_primary_protection(
    qa_compat_q2_rerelease_q2_guest_host *host, qa_raw_entity_view recipient, qa_protection_channel channel,
    void *ctx, void (*current)(void *ctx),
    void *(*operation)(void *ctx, void *consume_ctx, void (*consume)(void *consume_ctx, void *execute_ctx, void (*execute)(void *))),
    bool (*committed)(void *ctx));
#endif
