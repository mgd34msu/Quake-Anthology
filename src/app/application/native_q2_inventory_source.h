#ifndef QA_APPLICATION_NATIVE_Q2_INVENTORY_SOURCE_H
#define QA_APPLICATION_NATIVE_Q2_INVENTORY_SOURCE_H
#include "qa/native.h"
#include "qa/inventory.h"
struct application_native_q2;
typedef struct application_native_q2_inventory_source {
    qa_actor_id actor;
    uint32_t slot, inventory_offset, count, cursor_offset, client_bytes;
    int32_t empty;
    qa_native_address entity, client;
} application_native_q2_inventory_source;
/* Reads the actual declared original client, including while its synchronous
 * source scanner is entered. The caller retains the engine around the borrow. */
bool application_native_q2_inventory_source_read(struct application_native_q2 *,
    qa_actor_id, application_native_q2_inventory_source *, qa_error *);
bool application_native_q2_inventory_source_current(struct application_native_q2 *,
    const application_native_q2_inventory_source *, qa_error *);
bool application_native_q2_inventory_index(struct application_native_q2 *,
    qa_item_id, uint32_t *, qa_error *);
bool application_native_q2_inventory_item(struct application_native_q2 *,
    uint32_t, qa_item_id *, qa_error *);
#endif
