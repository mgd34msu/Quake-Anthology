/* Port of src/app/bootstrap/player-service-status.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/player_service_status.h"
#include <stdio.h>
#include <string.h>
qa_app_player_service_status qa_app_player_service_status_make(const char *service,
    const qa_net_address *endpoint, char *scratch, size_t scratch_size) {
  qa_app_player_service_status out;
  memset(&out, 0, sizeof out);
  out.service = service;
  if (endpoint == NULL) {
    out.kind = "unconfigured";
    snprintf(scratch, scratch_size,
        "%s requires an explicitly configured compatible endpoint; local records are not native service submission.",
        service);
    out.reason = scratch;
  } else {
    out.kind = "configured";
    out.has_endpoint = true;
    out.endpoint = *endpoint;
    out.endpoint.host = NULL; /* borrowed view: do not retain owned host */
  }
  return out;
}
