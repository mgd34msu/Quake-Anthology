#ifndef QA_Q3_MODEL_OPENING_H
#define QA_Q3_MODEL_OPENING_H
#include "qa/q3_presentation.h"

#define QA_Q3_MODEL_PRIMARY_OPENING UINT32_MAX
#define QA_Q3_MODEL_MD4_OPENING 3u
typedef struct qa_q3_model_opening {
    bool present;
    const char *first_requested_path;
    qa_q3_presentation_provider provider;
    const qa_resource *resource;
    const qa_vfs_acquisition *receipt;
    int64_t rank; /* Actual opening order at admission; -1 denotes a link. */
    const qa_mount_id *order;
    size_t order_count;
    const char *prefix; /* NULL for default order or a link. */
    bool user_overlay;
} qa_q3_model_opening;

/* Borrow the first admitted holder's provenance while idle or under its asset
 * capture lease, outside a codec. PRIMARY selects the dispatch opening. Slots
 * 0..2 select the actual decoded source, resolving MD3 aliases to their opening.
 * Inline models have no resource/receipt. Missing slots and retired holders
 * return an empty observation. No acquisition or validation callback occurs.
 * All borrows end before the registry or its provider is retired. */
bool qa_q3_assets_model_opening(const qa_q3_presentation_assets *, size_t ordinal,
    uint32_t slot, qa_q3_model_opening *, qa_error *);
#endif
