#ifndef QA_Q3_PRODUCT_POLICY_H
#define QA_Q3_PRODUCT_POLICY_H

#include "qa/common.h"

typedef struct qa_application qa_application;
typedef struct qa_cvars qa_cvars;

/* Captured from startup before any Q3 recipe opens its artifacts. Build/UI
 * policy stays independent from the media restriction resolved by files.c. */
typedef struct qa_q3_product_policy {
    bool prerelease_demo;
    bool prerelease_team_arena_demo;
    bool fs_restrict;
    bool restriction_resolved;
    bool filesystem_restricted;
} qa_q3_product_policy;

/* A copy of the retained owner, independent of later forced cvar writes.
 * False means the application has not resolved its initial Q3 media policy. */
bool qa_application_q3_product_policy_read(const qa_application *, qa_q3_product_policy *);
/* Fresh physical Q3 GAME/CLIENT registries receive the retained build flags
 * and resolved media restriction before configuration. Reset defaults remain
 * the original zero-valued INIT declarations. Pure restore imports QACV. */
bool qa_q3_product_policy_register_source(const qa_q3_product_policy *,
    qa_cvars *, uint64_t owner, qa_error *);

#endif
