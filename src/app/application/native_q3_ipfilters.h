#ifndef QA_APPLICATION_NATIVE_Q3_IPFILTERS_H
#define QA_APPLICATION_NATIVE_Q3_IPFILTERS_H

#include "internal.h"

/* Allocation is separate from genuine source Init and continuation import.
 * Init replaces the GameSource command state on both ordinary and fast loads,
 * then runs G_ProcessIPBans from the actual cached g_banIPs snapshot. */
bool application_native_q3_ipfilters_create(application_provider *, qa_error *);
bool application_native_q3_ipfilters_destroy(application_provider *, qa_error *);
bool application_native_q3_ipfilters_idle(const application_provider *);
bool application_native_q3_ipfilters_initialized(const application_provider *);
bool application_native_q3_ipfilters_init(application_provider *, qa_error *);

/* True reject is a source ClientConnect denial, not an operation failure.
 * This reads retained filters and cached g_filterBan without parsing bans. */
bool application_native_q3_ipfilters_filter(application_provider *, const char *address,
    bool *reject, qa_error *);
bool application_native_q3_ipfilters_console(application_provider *,
    const qa_command_invocation *, bool *handled, qa_error *);

/* The QAGI continuation includes the initialized bit, high-water rows including
 * removal holes,
 * and the separately mutated ban VM string with its modification count.
 * Import targets an empty owner and invokes no source effects or parsers. */
bool application_native_q3_ipfilters_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q3_ipfilters_restore(application_provider *, qa_bytes, qa_error *);

#endif
