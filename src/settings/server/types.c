/* Port of src/settings/server/types.ts. GPL-2.0-or-later. */
#include "qa/settings/server/types.h"

#include <stdlib.h>

const char *qa_server_apply_at_name(qa_server_apply_at apply_at) {
  switch (apply_at) {
    case QA_SERVER_APPLY_LIVE: return "live";
    case QA_SERVER_APPLY_NEXT_MATCH: return "next-match";
    case QA_SERVER_APPLY_NEXT_MAP: return "next-map";
    case QA_SERVER_APPLY_RESTART: return "restart";
  }
  return "live";
}

void qa_server_setting_values_free(qa_server_setting_values *values) {
  free(values->desired);
  free(values->effective);
  values->desired = NULL;
  values->effective = NULL;
}

void qa_server_setting_status_free(qa_server_setting_status *status) {
  free(status->desired);
  free(status->effective);
  status->desired = NULL;
  status->effective = NULL;
}
