#ifndef APPLICATION_GUEST_QC_BOT_ORDERS_H
#define APPLICATION_GUEST_QC_BOT_ORDERS_H
#include "guest_qc_internal.h"
bool application_qc_bot_order(struct application_qc_state *,qa_qc_instance *,
    bool follow,qa_error *);
#endif
