#ifndef QA_APPLICATION_NATIVE_Q2_INVENTORY_ROWS_H
#define QA_APPLICATION_NATIVE_Q2_INVENTORY_ROWS_H
#include "native_q2_inventory_scanner.h"
typedef struct application_native_q2_inventory_rows application_native_q2_inventory_rows;
bool application_native_q2_inventory_rows_create(struct application_native_q2 *,
    application_native_q2_inventory_rows **, qa_error *);
/* Original descriptors are acquired after the genuine native item table is
 * initialized, before scanner activation. No Source callback is replayed. */
bool application_native_q2_inventory_rows_prepare(application_native_q2_inventory_rows *, qa_error *);
bool application_native_q2_inventory_rows_destroy(application_native_q2_inventory_rows *, qa_error *);
bool application_native_q2_inventory_rows_idle(const application_native_q2_inventory_rows *);
application_native_q2_inventory_scanner_options application_native_q2_inventory_rows_options(
    application_native_q2_inventory_rows *);
#endif
