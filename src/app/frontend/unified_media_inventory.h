#ifndef QA_FRONTEND_UNIFIED_MEDIA_INVENTORY_H
#define QA_FRONTEND_UNIFIED_MEDIA_INVENTORY_H
#include "remote_unified_presentation.h"

/* Each real replica has two structural slots: installed then pending. Empty
 * slots remain in this ordinal domain; no compacted ready-owner inventory. */
bool frontend_unified_media_inventory_count(const qa_frontend *,size_t *,qa_error *);
bool frontend_unified_media_inventory_at(const qa_frontend *,size_t,frontend_unified_media **,qa_error *);
bool frontend_unified_media_bank_key(size_t,size_t,uint64_t *);
bool frontend_unified_media_bank_key_read(uint64_t,size_t *,size_t *);
#endif
