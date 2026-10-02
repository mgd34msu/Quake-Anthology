#ifndef QA_APPLICATION_GUEST_Q3_PICKUPS_H
#define QA_APPLICATION_GUEST_Q3_PICKUPS_H
#include "guest_q3_pickups_profile.h"
#include "qa/inventory.h"
#include "qa/qvm_save.h"

typedef struct application_q3_pickups application_q3_pickups;
bool application_q3_pickups_create(struct q3g_role *,const application_q3_pickup_profile *,
    application_q3_pickups **,qa_error *);
bool application_q3_pickups_idle(const application_q3_pickups *);
/* True only when a retained frame can unwind at the returned Source boundary
 * without crossing another owner's actual projected-word lease. */
bool application_q3_pickups_cleanup_ready(const application_q3_pickups *);
/* Retry admitted frames before the parent's VM destroy-ready preflight.
 * Success may leave frames blocked by another owner; idle proves completion. */
bool application_q3_pickups_cleanup(application_q3_pickups *,qa_error *);
bool application_q3_pickups_destroy(application_q3_pickups **,qa_error *);
/* Shared combat-free composition calls this after the real Source free and
 * actor retirement. It never calls the Source free a second time. */
bool application_q3_pickups_after_free(application_q3_pickups *,const qa_qvm_call *,int32_t pointer,qa_error *);
/* Borrowed only inside the exact replacement grant's take callback. */
bool application_q3_pickups_supply(application_q3_pickups *,const qa_pickup_offer *,
    qa_supply_offer *,qa_supply_options *,qa_error *);
size_t application_q3_pickups_descriptor_count(const application_q3_pickups *);
bool application_q3_pickups_descriptors(const application_q3_pickups *,qa_qvm_saved_function *,size_t,qa_error *);
void application_q3_pickups_adopt(application_q3_pickups *,const qa_qvm_binding *);
/* Only closed/returned continuation is durable; live frames are not encoded. */
bool application_q3_pickups_checkpoint(const application_q3_pickups *,qa_buffer *,qa_error *);
bool application_q3_pickups_restore(application_q3_pickups *,qa_bytes,qa_error *);
#endif
