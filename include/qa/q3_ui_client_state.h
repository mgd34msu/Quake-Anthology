#ifndef QA_Q3_UI_CLIENT_STATE_H
#define QA_Q3_UI_CLIENT_STATE_H

#include "qa/common.h"

enum { QA_Q3_UI_CLIENT_STRING_BYTES = 1024, QA_Q3_UI_CLIENT_STATE_BYTES = 3084 };

/* Owned copy of the actual CLIENT connection observation. Phase is the source
 * connstate_t value; every string is terminated within its original extent.
 * This receipt has no borrowed network text or guest-memory pointers. */
typedef struct qa_q3_ui_client_state {
    int32_t phase, connect_packet_count, client_number;
    char server_name[QA_Q3_UI_CLIENT_STRING_BYTES];
    char update_info[QA_Q3_UI_CLIENT_STRING_BYTES];
    char message[QA_Q3_UI_CLIENT_STRING_BYTES];
} qa_q3_ui_client_state;

#endif
