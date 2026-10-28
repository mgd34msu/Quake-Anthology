/* Port of src/app/bootstrap/network/gtv-source.ts. prepare pumps
 * the landed connection until the first DATA header lands; poll
 * also pumps the socket once before consuming pending DATA.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_GTV_SOURCE_H
#define QA_APP_BOOTSTRAP_NETWORK_GTV_SOURCE_H
#include <stdbool.h>
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/app/bootstrap/network/q2_demo.h"
#include "qa/network/q2/gtv_transport.h"
#include "qa/network/q2/mvd_profile.h"
typedef struct qa_app_gtv_source_options {
  const char *host; double port;
  qa_q2_gtv_identity identity;
  const bool *aborted; /* TS signal, may be NULL */
} qa_app_gtv_source_options;
typedef struct qa_app_gtv_remote_source qa_app_gtv_remote_source;
/* NULL only on OOM; admission failures abort with the TS messages. */
qa_app_gtv_remote_source *qa_app_gtv_remote_source_prepare(qa_app_gtv_source_options options);
void qa_app_gtv_remote_source_destroy(qa_app_gtv_remote_source *source);
const qa_q2_mvd_header *qa_app_gtv_remote_source_header(qa_app_gtv_remote_source *source);
const char *qa_app_gtv_remote_source_phase(qa_app_gtv_remote_source *source);
bool qa_app_gtv_remote_source_time(qa_app_gtv_remote_source *source, double *out);
void qa_app_gtv_remote_source_bind(qa_app_gtv_remote_source *source,
    qa_app_q2_client_host host, qa_app_q2_mvd_presentation presentation);
void qa_app_gtv_remote_source_poll(qa_app_gtv_remote_source *source);
void qa_app_gtv_remote_source_select_player(qa_app_gtv_remote_source *source, double clientnum);
void qa_app_gtv_remote_source_close(qa_app_gtv_remote_source *source);
#endif
