#ifndef QA_APPLICATION_GUEST_NATIVE_Q2_PRIMARY_PICKUPS_H
#define QA_APPLICATION_GUEST_NATIVE_Q2_PRIMARY_PICKUPS_H
#include "qa/inventory.h"
struct application_native_q2;
struct application_native_q2_primary_pickups;
bool application_native_q2_primary_pickups_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_primary_pickups_activate(struct application_native_q2 *, qa_error *);
bool application_native_q2_primary_pickups_suspend(struct application_native_q2 *, qa_error *);
bool application_native_q2_primary_pickups_close(struct application_native_q2 *, qa_error *);
bool application_native_q2_primary_pickups_idle(const struct application_native_q2_primary_pickups *);
bool application_native_q2_primary_pickups_returned(const struct application_native_q2_primary_pickups *);
bool application_native_q2_primary_pickups_supply(struct application_native_q2_primary_pickups *,
    const qa_pickup_offer *, qa_supply_offer *, qa_supply_options *, qa_error *);
#endif
