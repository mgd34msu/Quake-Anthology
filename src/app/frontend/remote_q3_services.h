#ifndef QA_FRONTEND_REMOTE_Q3_SERVICES_H
#define QA_FRONTEND_REMOTE_Q3_SERVICES_H

#include "remote_q3_client.h"
#include "../../presentation/q3_native/remote_frame.h"
#include "../../presentation/q3_native/media.h"
#include "../../presentation/q3_native/client_info.h"

typedef struct frontend_remote_q3_services frontend_remote_q3_services;
typedef struct frontend_remote_q3_services_view {
    frontend_remote_q3_resources resources;
    qa_native_q3_remote_client_service *client;
    q3n_remote_source *source;
    q3n_media *media;
    q3n_clients *clients;
} frontend_remote_q3_services_view;

/* Retains the actual constructor CHARACTER declaration without a player actor.
 * The owners are empty constructor children, without media registration or Init. */
bool frontend_remote_q3_services_create(frontend_remote_q3 *, qa_error *);
/* Genuine empty child reconstruction under the parent's staged graph domain.
 * Client/source continuation precedes runtime construction; numeric media and
 * immutable animation holders import after the actual asset dictionary. */
bool frontend_remote_q3_services_prepare_restored(frontend_remote_q3 *,qa_bytes client,qa_bytes source,qa_error *);
bool frontend_remote_q3_services_finish_restore(frontend_remote_q3 *,qa_bytes media,
    const q3n_client_refs *,qa_bytes clients,qa_error *);
bool frontend_remote_q3_services_read(const frontend_remote_q3 *,
    frontend_remote_q3_services_view *, qa_error *);
bool frontend_remote_q3_services_bind(frontend_remote_q3 *, qa_error *);
bool frontend_remote_q3_services_idle(const frontend_remote_q3_services *);
bool frontend_remote_q3_services_destroy(frontend_remote_q3_services **, qa_error *);
/* Returned compiled video scope retains the true client and Source reader
 * while reconstructing only registered presentation children. */
bool frontend_remote_q3_services_video_read(const frontend_remote_q3 *,frontend_remote_q3_services_view *,qa_error *);
bool frontend_remote_q3_services_video_close(frontend_remote_q3 *,qa_error *);
bool frontend_remote_q3_services_video_reopen(frontend_remote_q3 *,qa_error *);

#endif
