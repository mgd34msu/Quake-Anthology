#ifndef QA_APPLICATION_GUEST_Q3_CATALOG_H
#define QA_APPLICATION_GUEST_Q3_CATALOG_H

#include "qa/gameplay.h"
#include "qa/qvm.h"
#include "qa/strings.h"

typedef struct application_q3_catalog application_q3_catalog;
struct q3g_role;
struct guest_inventory_weapon;
typedef struct application_q3_catalog_record {
    uint32_t index;
    uint64_t address;
    int32_t type, tag;
    const char *class_name, *pickup_name;
} application_q3_catalog_record;
typedef struct application_q3_catalog_weapon {
    int32_t weapon;
    qa_item_id item, ammo;
    const char *label;
} application_q3_catalog_weapon;

/* Both declarations are retained artifact receipts, already qualified against
 * image. A complete primary takes precedence over the optional items receipt.
 * known_missionpack is an admitted executable profile, not a content family. */
bool application_q3_catalog_create(qa_qvm_image *, qa_qvm *, qa_qvm_abi,
    qa_strings *, qa_bytes primary, qa_bytes items_declaration,
    bool known_missionpack, application_q3_catalog **, qa_error *);
/* The native receipt names this module's actual item RVAs, pointer fields and
 * source type values. Absence is represented by no owner, never a QVM roster. */
bool application_q3_catalog_create_native(struct q3g_role *, qa_bytes,
    application_q3_catalog **, qa_error *);
bool application_q3_catalog_role_current(const application_q3_catalog *,
    const struct q3g_role *);
void application_q3_catalog_destroy(application_q3_catalog *);
/* Outputs borrow this owner's latest admitted records until its next read. */
bool application_q3_catalog_records(application_q3_catalog *,
    const application_q3_catalog_record **, size_t *, qa_error *);
bool application_q3_catalog_weapons(application_q3_catalog *,
    const application_q3_catalog_weapon **, size_t *, qa_error *);
bool application_q3_catalog_ammo_label(application_q3_catalog *, qa_item_id,
    const char **, qa_error *);
/* True only for the actual default roster constructor, independently of
 * labels or item identities read from a declared original table. */
bool application_q3_catalog_standard(const application_q3_catalog *);
bool application_q3_catalog_current(const application_q3_catalog *,
    const qa_qvm_image *, const qa_qvm *, qa_qvm_abi);
/* Direct checked projection callback; context is this retained catalog. */
bool application_q3_catalog_inventory_read(void *, struct q3g_role *,
    const struct guest_inventory_weapon **, size_t *, qa_error *);

#endif
