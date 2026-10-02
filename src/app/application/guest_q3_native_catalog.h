#ifndef QA_APPLICATION_GUEST_Q3_NATIVE_CATALOG_H
#define QA_APPLICATION_GUEST_Q3_NATIVE_CATALOG_H

#include "guest_q3_catalog.h"

typedef struct application_q3_native_catalog application_q3_native_catalog;
bool application_q3_native_catalog_create(struct q3g_role *, qa_bytes,
    application_q3_native_catalog **, qa_error *);
void application_q3_native_catalog_destroy(application_q3_native_catalog *);
bool application_q3_native_catalog_current(const application_q3_native_catalog *,
    const struct q3g_role *);
bool application_q3_native_catalog_records(application_q3_native_catalog *,
    const application_q3_catalog_record **, size_t *, qa_error *);
bool application_q3_native_catalog_weapons(application_q3_native_catalog *,
    const application_q3_catalog_weapon **, size_t *, qa_error *);
bool application_q3_native_catalog_ammo_label(application_q3_native_catalog *,
    qa_item_id, const char **, qa_error *);

#endif
