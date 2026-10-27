/* Port of src/compat/q2/classic/inventory.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_CLASSIC_INVENTORY_H
#define QA_COMPAT_Q2_CLASSIC_INVENTORY_H
#include <stdbool.h>
#include "qa/compat/q2/classic/host.h"
#include "qa/compat/q2/classic/world_profile.h"
#include "qa/world/gameplay/inventory.h"

typedef struct qa_compat_q2_classic_source_inventory qa_compat_q2_classic_source_inventory;
/* declared NULL selects by digest; has_declared false with a NULL
 * profile is the TS declared=null. NULL return is the TS null. */
qa_compat_q2_classic_source_inventory *qa_compat_q2_classic_source_inventory_create(
    qa_compat_q2_classic_guest_host *host, qa_guest_address image,
    const qa_compat_q2_classic_world_profile *declared, bool has_declared);
void qa_compat_q2_classic_source_inventory_destroy(qa_compat_q2_classic_source_inventory *inv);
/* Returned binding ctx is owned by inv; entries scratch is inv-owned. */
qa_inventory_state_binding qa_compat_q2_classic_source_inventory_bind(qa_compat_q2_classic_source_inventory *inv, qa_raw_entity_view record);
#endif
