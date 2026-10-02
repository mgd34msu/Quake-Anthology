#ifndef QA_KEX_SAVE_INTERNAL_H
#define QA_KEX_SAVE_INTERNAL_H

#include "qa/network_kex_save.h"

bool qa_kex_save_address_valid(const qa_net_address *, bool peer);
bool qa_kex_save_address_write(qa_net_writer *, const qa_net_address *);
bool qa_kex_save_address_read(qa_net_reader *, qa_net_address *);

#endif
