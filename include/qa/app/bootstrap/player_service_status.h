/* Port of src/app/bootstrap/player-service-status.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PLAYER_SERVICE_STATUS_H
#define QA_APP_BOOTSTRAP_PLAYER_SERVICE_STATUS_H
#include <stdbool.h>
#include "qa/network/common/endpoint.h"
/* TS PlayerServiceStatus union on kind. reason/endpoint valid per kind;
 * reason for "unconfigured" is written into caller scratch by the fn. */
typedef struct qa_app_player_service_status {
  const char *kind;    /* "unconfigured"|"configured"|"unavailable" */
  const char *service; /* "authorization"|"rankings" */
  const char *reason;  /* unconfigured/unavailable, else NULL */
  bool has_endpoint;
  qa_net_address endpoint; /* borrowed copy (no owned strings retained) */
} qa_app_player_service_status;
/* endpoint NULL is TS null. scratch holds the unconfigured reason. */
qa_app_player_service_status qa_app_player_service_status_make(const char *service,
    const qa_net_address *endpoint, char *scratch, size_t scratch_size);
#endif
