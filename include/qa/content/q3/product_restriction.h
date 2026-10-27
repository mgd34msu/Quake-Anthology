/* Port of src/content/q3/product-restriction.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_PRODUCT_RESTRICTION_H
#define QA_CONTENT_Q3_PRODUCT_RESTRICTION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/core/q3_product_policy.h"
/* read_product_id: false is TS null; on true the out bytes are borrowed. */
typedef bool (*qa_q3_read_product_id_fn)(void *ctx, const uint8_t **out, size_t *out_length);
qa_q3_mount_restriction qa_q3_resolve_mount_restriction(qa_q3_product_policy policy, bool fs_restrict,
    qa_q3_read_product_id_fn read_product_id, void *read_product_id_ctx);
#endif
