/*
 * Port of src/persistence/provider-ownership.ts. Plain TS Errors
 * abort with the identical message. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_PROVIDER_OWNERSHIP_H
#define QA_PERSISTENCE_PROVIDER_OWNERSHIP_H

#include "qa/contracts/session.h"

extern const qa_provider_checkpoint_ref qa_campaign_unit_checkpoint;

qa_provider_checkpoint_ref qa_save_provider_contract(const char *schema, qa_provider_id source);
void qa_validate_save_provider_owner(qa_provider_checkpoint record, qa_provider_id source);

#endif
