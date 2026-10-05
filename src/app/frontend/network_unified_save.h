#ifndef QA_FRONTEND_NETWORK_UNIFIED_SAVE_H
#define QA_FRONTEND_NETWORK_UNIFIED_SAVE_H

#include "network_unified.h"

bool frontend_network_unified_server(const frontend_network_unified *);
bool frontend_network_unified_restore_dispose(frontend_network_unified **, qa_error *);

#endif
