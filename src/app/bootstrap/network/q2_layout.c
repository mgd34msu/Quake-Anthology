/* Port of src/app/bootstrap/network/q2-layout.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q2_layout.h"
#include <string.h>
qa_app_q2_layout qa_app_q2_application_layout(qa_q2_protocol_identity protocol) {
  const char *kind = protocol.kind ? protocol.kind : "";
  if (strcmp(kind, "q2-rerelease") == 0 || strcmp(kind, "q2-kex") == 0 || strcmp(kind, "q2-kex-demo") == 0) {
    qa_app_q2_layout layout = {62, 8254, 10302, 10814, 11326, 11582, 8192, 2048, 512, 12448, 61, 60, 59, true, 12103};
    return layout;
  }
  qa_app_q2_layout layout = {32, 288, 544, 800, 1056, 1312, 256, 256, 256, 2080, 31, 30, 29, false, 0};
  return layout;
}
