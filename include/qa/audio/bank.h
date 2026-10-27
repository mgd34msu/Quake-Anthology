/*
 * Port of src/audio/bank.ts. C accommodations:
 *  - The async TS methods are synchronous, as elsewhere. Decodes the
 *    TS catches (register/openMusic catch -> null) go through the
 *    quiet qa_try_* variants in wav.h/streams.h; QA_*_TRY_INVALID is
 *    the TS null, and these bank functions return false only on OOM
 *    with *out NULL already set.
 *  - MountedContent is the landed qa_content_mounted (TS owner
 *    src/content/mounts/index.ts), borrowed by the bank. Assets are
 *    owned by the bank and stay valid until end-registration
 *    eviction, clear, or destroy. Each asset holds its own copy of
 *    the opened reference (its strings included): the TS retains
 *    the reference object by GC identity, while the mounts port
 *    replaces its stored record on a repeated open of one path, so
 *    borrowing that record would dangle. Opened bytes are freed
 *    after decode, per the mounts port's caller-frees contract.
 *  - TS default arguments (family "q3", source null) are passed
 *    explicitly, as in music.h.
 * GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_BANK_H
#define QA_AUDIO_BANK_H

#include <stdbool.h>

#include "qa/audio/streams.h"
#include "qa/audio/types.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"

typedef struct qa_sound_bank qa_sound_bank;

/* NULL only on OOM. `content` is borrowed and must outlive the bank. */
qa_sound_bank *qa_sound_bank_create(qa_content_mounted *content);
void qa_sound_bank_destroy(qa_sound_bank *bank);
qa_content_mounted *qa_sound_bank_content(const qa_sound_bank *bank);

void qa_sound_bank_begin_registration(qa_sound_bank *bank);
/* False only on OOM; *out NULL is the TS null. */
bool qa_sound_bank_register(qa_sound_bank *bank, const char *name,
                            qa_sound_family family,
                            const qa_sound_asset **out);
bool qa_sound_bank_register_sexed_sound(qa_sound_bank *bank, const char *base,
                                        const char *model,
                                        const qa_sound_asset **out);
/* NULL is the TS undefined. */
const qa_sound_asset *qa_sound_bank_get(const qa_sound_bank *bank,
                                        qa_resource_id resource,
                                        qa_sound_family family);
void qa_sound_bank_end_registration(qa_sound_bank *bank);
/* source NULL is the TS null. False only on OOM; *out NULL the TS
 * null. The caller owns a non-NULL stream. */
bool qa_sound_bank_open_music(qa_sound_bank *bank, const char *path,
                              qa_content_id source, qa_pcm_stream **out);
void qa_sound_bank_clear(qa_sound_bank *bank);

#endif
