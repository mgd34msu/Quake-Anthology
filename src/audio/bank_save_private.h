#ifndef QA_AUDIO_BANK_SAVE_PRIVATE_H
#define QA_AUDIO_BANK_SAVE_PRIVATE_H
#include "bank_private.h"
struct asset_row { qa_audio_asset *asset; size_t holders; };
size_t qa_bank_asset_index(const struct asset_row *, size_t, const qa_audio_asset *);
bool qa_bank_add_asset(struct asset_row **, size_t *, qa_audio_asset *, qa_error *);
bool qa_bank_cache_valid(const qa_audio_bank *, qa_error *);
#endif
