#ifndef QA_APPLICATION_GUEST_Q3_WEAPONS_SAVE_H
#define QA_APPLICATION_GUEST_Q3_WEAPONS_SAVE_H

#include "guest_q3_weapons.h"

typedef struct application_q3_weapons_saved {
    qa_qvm_binding bindings[6];
    size_t count;
    bool present;
} application_q3_weapons_saved;
/* All mutable original selection, timers, counters and powerups belong to VM
 * RAM. This owner persists only its actual callback identities. Transient
 * dispatch/effect/drop/preparation scopes must be finished before capture.
 * The whole executor composer qualifies these IDs together with all other
 * owner descriptors, adopts once, and then imports executor RAM. */
bool application_q3_weapons_checkpoint(const application_q3_weapons *, qa_buffer *, qa_error *);
bool application_q3_weapons_prepare_restore(const application_q3_weapons *, qa_bytes,
    application_q3_weapons_saved *, qa_error *);

#endif
