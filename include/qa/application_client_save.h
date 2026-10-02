#ifndef QA_APPLICATION_CLIENT_SAVE_H
#define QA_APPLICATION_CLIENT_SAVE_H
#include "qa/application_client.h"

/* References belong to the enclosing restored session/string domain. The
 * frontend codec owns descriptor, registry, connection and callback imports;
 * this state retains only the physical CLIENT's private observer namespace. */
typedef struct qa_application_client_state {
    qa_sha256_digest descriptor_identity;
    qa_actor_owner receiver, entity_owner;
    uint32_t seat, physical_seat;
    uint64_t configuration_generation, connection_epoch, entity_generation;
    qa_net_client_id client;
    qa_net_seat_id network_seat;
    qa_saved_actor_id *actors;
    size_t actor_count;
} qa_application_client_state;
bool qa_application_client_capture(qa_application *, const qa_application_client_source *,
    qa_application_client_state *, qa_error *);
void qa_application_client_state_free(qa_application_client_state *);
/* No network attach, source entity allocation, command dispatch or registry
 * replay. Every observer resolves through the genuine imported actor domain. */
bool qa_application_client_create_restored(qa_application *, const qa_application_client_options *,
    const qa_application_client_state *, qa_application_client_source *, qa_error *);
#endif
