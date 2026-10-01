#ifndef QA_FRONTEND_NETWORK_Q3_PACKAGES_H
#define QA_FRONTEND_NETWORK_Q3_PACKAGES_H
#include "qa/application_network.h"

typedef struct frontend_q3_packages frontend_q3_packages;
bool frontend_q3_packages_create(qa_application *, qa_actor_owner, uint32_t checksum_feed,
    frontend_q3_packages **, qa_error *);
void frontend_q3_packages_destroy(frontend_q3_packages *);
bool frontend_q3_packages_collect(frontend_q3_packages *, qa_error *);
bool frontend_q3_packages_view(frontend_q3_packages *, qa_application_network_q3_package_view *, qa_error *);
void frontend_q3_packages_rebind(frontend_q3_packages *, qa_application *);
bool frontend_q3_packages_checkpoint(const frontend_q3_packages *, qa_buffer *, qa_error *);
bool frontend_q3_packages_restore_receipt(frontend_q3_packages *, qa_bytes, qa_error *);
bool frontend_q3_packages_prepare(frontend_q3_packages *, bool restoring, qa_error *);
bool frontend_q3_packages_check(frontend_q3_packages *, qa_error *);
bool frontend_q3_packages_pure(frontend_q3_packages *, int32_t checksum_feed_server_id,
    qa_q3_pure_server *, qa_error *);
bool frontend_q3_packages_download(frontend_q3_packages *, const char *name,
    qa_bytes *, const qa_sha256_digest **, qa_error *);
#endif
