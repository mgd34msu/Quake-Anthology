#ifndef QA_APPLICATION_CLIENT_H
#define QA_APPLICATION_CLIENT_H
#include "qa/application_startup_prepare.h"
#include "qa/network_runtime.h"

/* This namespace belongs to a physical compiled CLIENT. It grants no GAME,
 * CHARACTER, source player or local simulation authority. */
typedef struct qa_application_client_context {
    qa_session *session;
    qa_actor_owner receiver, entity_owner;
    qa_actor_definition entity_definition;
    uint32_t seat, physical_seat;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command;
    const void *lifetime;
} qa_application_client_context;
typedef struct qa_application_client_source {
    const qa_launch_instance *descriptor;
    qa_application_client_context context;
    qa_network_runtime *runtime;
    qa_net_client_id client;
    qa_net_seat_id network_seat;
    uint64_t connection_epoch, configuration_generation;
} qa_application_client_source;
typedef struct qa_application_client_owner {
    void *context;
    bool (*retain)(void *, qa_error *);
    bool (*release)(void *, qa_error *);
    /* Pure physical graph checks. These callbacks must not call the
     * application CLIENT facade recursively. */
    bool (*current)(void *, const qa_launch_instance *, qa_console *, qa_cvars *,
        const qa_command_context *);
    bool (*idle)(void *);
    /* Proves the actual attached connection. Pending constructors have no
     * connection and never call this callback. */
    bool (*connection_current)(void *, const qa_application_client_source *);
    /* Optional decoded-entity proof. Only received entities can acquire a
     * canonical observer ID; their source number is never cast to an actor. */
    bool (*entity_current)(void *, const qa_application_client_source *, uint32_t,
        uint64_t *map_generation);
} qa_application_client_owner;
typedef struct qa_application_client_options {
    const qa_launch_instance *descriptor;
    qa_actor_owner receiver;
    uint32_t seat, physical_seat;
    uint64_t configuration_generation;
    qa_network_runtime *runtime;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command;
    qa_application_client_owner owner;
} qa_application_client_options;

/* Admit a physical CLIENT provider over its actual prepared metadata. This
 * creates no GAME, component, player or world. The receiver ID is interned
 * from that descriptor's real instance; existing GAME owners cannot alias it. */
bool qa_application_client_provider_prepare(qa_application *, const qa_launch_instance *,
    qa_actor_owner *, qa_error *);
bool qa_application_client_provider_release(qa_application *, qa_actor_owner, qa_error *);
/* Capture the actual local input origin after metadata admission. Pending
 * CLIENT commands remain actorless; decoded viewers have separate receipts. */
bool qa_application_client_provider_command(qa_application *, qa_actor_owner, uint32_t seat,
    const qa_command_context *input, qa_command_context *, uint64_t *configuration_generation, qa_error *);

/* Retains metadata and the actual frontend registry callback owner. The
 * supplied heap and console remain owned by that physical frontend owner.
 * On failure no application row or owner reference is installed. */
bool qa_application_client_create(qa_application *, const qa_application_client_options *,
    qa_application_client_source *, qa_error *);
bool qa_application_client_read(qa_application *, qa_actor_owner, uint32_t,
    qa_application_client_source *, qa_error *);
bool qa_application_client_current(qa_application *, const qa_application_client_source *);
/* Called only with the genuine successful Network attach receipt. The owner
 * callback authenticates the complete full-generation connection tuple. */
bool qa_application_client_bind(qa_application *, const qa_application_client_source *,
    qa_net_client_id, qa_net_seat_id, uint64_t epoch, qa_application_client_source *, qa_error *);
/* After the physical owner has admitted its real replacement descriptor and
 * captured command tuple, requalify this same registry/connection namespace.
 * The callback proves the supplied graph directly; no cache or clock replay. */
bool qa_application_client_rebind(qa_application *, const qa_application_client_source *,
    const qa_launch_instance *, const qa_command_context *, uint64_t configuration_generation,
    qa_application_client_source *, qa_error *);
bool qa_application_client_entity_read(qa_application *, const qa_application_client_source *,
    uint32_t source_number, qa_actor_id *, qa_error *);
bool qa_application_client_idle(qa_application *, const qa_application_client_source *);
/* Retires observer actors before releasing the physical owner lease. A
 * rejected release retains a retryable row and all remaining ownership. */
bool qa_application_client_retire(qa_application *, const qa_application_client_source *, qa_error *);
#endif
