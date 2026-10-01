#ifndef QA_APPLICATION_Q3_ROUND_H
#define QA_APPLICATION_Q3_ROUND_H

#include "qa/application.h"
#include "qa/network_q3_round.h"

/* The application owns these observations until the cut is disposed. Each
 * row names an actual admitted source client and its previous generation. */
typedef struct qa_application_q3_round_client {
    uint32_t seat, source_slot;
    qa_actor_id previous_actor;
    qa_net_client_id remote_client;
    qa_net_seat_id remote_seat;
    const char *userinfo;
    qa_q3_usercmd last_command;
    bool remote, bot;
} qa_application_q3_round_client;

/* The frontend owns a cut over its actual local clients, presentation, input
 * and optional native network owner. It is not a world or source replacement. */
typedef struct qa_application_q3_round_cut qa_application_q3_round_cut;
typedef void (*qa_application_q3_round_mutation_fn)(void *);
typedef struct qa_application_q3_round_services {
    bool (*prepare)(void *, qa_application *, qa_actor_owner,
        const qa_application_q3_round_client *, size_t,
        qa_application_q3_round_cut **empty, qa_error *);
    /* Deliver actual retained event projections before resetting their
     * owners, and after each source frame/client admission. */
    bool (*deliver)(qa_application_q3_round_cut *, qa_error *);
    bool (*network_owned)(qa_application_q3_round_cut *, bool *, qa_error *);
    /* Call mark immediately before the first real mutation, before source or
     * service callbacks can observe the new epoch. */
    bool (*begin)(qa_application_q3_round_cut *,
        qa_application_q3_round_mutation_fn mark, void *mark_context, qa_error *);
    bool (*bind)(qa_application_q3_round_cut *, qa_error *);
    /* Queue map_restart on the actual transport/local reliable owner before
     * Connect(false)/Begin. The source client itself remains application-owned. */
    bool (*queue_client)(qa_application_q3_round_cut *,
        const qa_application_q3_round_client *, qa_error *);
    bool (*admit_client)(qa_application_q3_round_cut *,
        const qa_application_q3_round_client *, qa_actor_id, qa_error *);
    bool (*reject_client)(qa_application_q3_round_cut *,
        const qa_application_q3_round_client *, const char *, qa_error *);
    bool (*finish)(qa_application_q3_round_cut *, qa_error *);
    void (*dispose)(qa_application_q3_round_cut *);
} qa_application_q3_round_services;

/* Readonly qualification for the actual retained source/frontend callback
 * boundary. An authorized round may be between completed settlement frames. */
bool qa_application_q3_round_callback_ready(qa_application *, qa_actor_owner,
    qa_error *);
/* Read the real full-generation physical GAME client binding independently of
 * which provider owns the canonical character. This performs no admission. */
bool qa_application_q3_source_client_slot(qa_application *, qa_actor_owner,
    qa_actor_id, uint32_t *, qa_error *);

#endif
