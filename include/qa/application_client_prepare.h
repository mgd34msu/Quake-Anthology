#ifndef QA_APPLICATION_CLIENT_PREPARE_H
#define QA_APPLICATION_CLIENT_PREPARE_H
#include "qa/application_client.h"

typedef struct qa_application_client_preparation qa_application_client_preparation;
typedef enum qa_application_client_prepare_phase {
    QA_CLIENT_PREPARE_CONFIGURATION,
    QA_CLIENT_PREPARE_RELEASE,
    QA_CLIENT_PREPARE_RESOURCES,
    QA_CLIENT_PREPARE_CONSUMING,
    QA_CLIENT_PREPARE_CLEANUP
} qa_application_client_prepare_phase;
/* A standalone physical CLIENT owns this transaction. The retained launch is
 * the unchanged installed resource baseline, never a candidate descriptor. */
bool qa_application_client_prepare_begin(qa_application *,const qa_application_client_source *,
    qa_application_client_preparation **,qa_error *);
bool qa_application_client_prepare_current(const qa_application_client_preparation *);
bool qa_application_client_prepare_active(const qa_application *);
bool qa_application_client_prepare_associated(const qa_application *,
    const qa_application_client_preparation *);
bool qa_application_client_prepare_entered(const qa_application_client_preparation *,
    qa_application_client_prepare_phase);
bool qa_application_client_prepare_phase_is(const qa_application_client_preparation *,
    qa_application_client_prepare_phase);
const qa_application_client_source *qa_application_client_prepare_source(
    const qa_application_client_preparation *);
qa_application *qa_application_client_prepare_application(const qa_application_client_preparation *);
const qa_launch_snapshot *qa_application_client_prepare_launch(
    const qa_application_client_preparation *);
/* complete is supplied by the real retained programme/device/resource child.
 * A WAIT leaves this exact phase and its CLIENT lease installed. */
bool qa_application_client_prepare_advance(qa_application_client_preparation *,
    bool (*advance)(void *,qa_application_client_preparation *,bool *,qa_error *),void *,
    bool *complete,qa_error *);
/* ready is a pure proof of every prepared child. consume performs only their
 * admitted publication and scalar handoff. Cleanup remains checked/retryable. */
bool qa_application_client_prepare_consume(qa_application_client_preparation *,
    bool (*ready)(void *,const qa_application_client_preparation *),
    void (*consume)(void *,qa_application_client_preparation *),void *,qa_error *);
bool qa_application_client_prepare_abort(qa_application_client_preparation *,qa_error *);
/* Cancellation advances only an already retained release history. It leaves
 * RELEASE installed until that history reports completion; no new work starts. */
bool qa_application_client_prepare_cancel_advance(qa_application_client_preparation *,
    bool (*cleanup)(void *,qa_application_client_preparation *,bool *,qa_error *),void *,
    bool *complete,qa_error *);
bool qa_application_client_prepare_cancel_entered(const qa_application_client_preparation *);
bool qa_application_client_prepare_finish(qa_application_client_preparation **,
    bool (*cleanup)(void *,qa_application_client_preparation *,bool *,qa_error *),void *,
    bool *complete,qa_error *);
/* Initial and post-config Q2 +set use the same actual routed CLIENT console.
 * Q1/QW startup rows remain stuffed commands, as their source startup rules. */
bool qa_application_client_prepare_initial(qa_application_client_preparation *,qa_error *);
bool qa_application_client_prepare_replay(qa_application_client_preparation *,qa_error *);
/* Only the genuine retained remote-request owner can claim original startup
 * operands. current is a pure full physical CLIENT/request identity proof. */
bool qa_application_client_prepare_startup_claim(qa_application_client_preparation *,void *,
    bool (*current)(void *,const qa_application_client_source *),qa_error *);
bool qa_application_client_prepare_startup_current(const qa_application_client_preparation *);
bool qa_application_client_prepare_safe_mode(const qa_application_client_preparation *,bool *,qa_error *);
bool qa_application_client_prepare_startup_ready(const qa_application_client_preparation *);
void qa_application_client_prepare_startup_publish(qa_application_client_preparation *);
bool qa_application_client_prepare_holds(const qa_application *,const qa_application_client_source *);
#endif
