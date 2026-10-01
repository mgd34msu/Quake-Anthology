#ifndef QA_NETWORK_Q3_PAK_ROLE_H
#define QA_NETWORK_Q3_PAK_ROLE_H
#include "qa/network_q3.h"
#include "qa/qvm.h"

/* The actual client constructor qualifies its retained CGAME/UI artifact
 * receipt and selected pack before calling. This marks that real module role
 * independently of requested-path accounting; no file is opened or recorded. */
bool qa_q3_pak_record_client_role(qa_q3_pak_references *, const qa_q3_pak_entry *,
    qa_qvm_role, qa_error *);
#endif
