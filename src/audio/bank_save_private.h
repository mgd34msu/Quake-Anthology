#ifndef QA_AUDIO_BANK_SAVE_PRIVATE_H
#define QA_AUDIO_BANK_SAVE_PRIVATE_H
#include "bank_private.h"
#include "checkpoint_internal.h"
#include "qa/audio_bank_save.h"
struct asset_row { qa_audio_asset *asset; size_t holders; };
struct sample_row { qa_audio_sample *sample; size_t holders; };
size_t qa_bank_asset_index(const struct asset_row *, size_t, const qa_audio_asset *);
size_t qa_bank_sample_index(const struct sample_row *, size_t, const qa_audio_sample *);
bool qa_bank_add_asset(struct asset_row **, size_t *, qa_audio_asset *, qa_error *);
bool qa_bank_add_sample(struct sample_row **, size_t *, qa_audio_sample *, qa_error *);
bool qa_bank_asset_valid(const qa_audio_asset *, qa_error *);
bool qa_bank_cache_valid(const qa_audio_bank *, qa_error *);
bool qa_bank_write_extent(qa_source_save_io *, size_t, size_t);
bool qa_bank_read_extent(qa_source_save_io *, uint64_t, uint64_t);
bool qa_bank_write_asset(qa_source_save_io *, const struct asset_row *, const struct sample_row *, size_t,
    const qa_audio_bank_checkpoint_refs *);
bool qa_bank_read_asset(qa_source_save_io *, struct asset_row *, struct sample_row *, size_t,
    const qa_audio_bank_checkpoint_refs *);
#endif
