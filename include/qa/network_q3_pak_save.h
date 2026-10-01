#ifndef QA_NETWORK_Q3_PAK_SAVE_H
#define QA_NETWORK_Q3_PAK_SAVE_H

#include "qa/network_q3.h"

/* Retains the actual creation slots, search permutation, reference flags and
 * loose checksum cell. Immutable package text/checksums qualify each slot;
 * process-local pointers and random callbacks are never serialized. */
bool qa_q3_pak_references_checkpoint(const qa_q3_pak_references *, qa_buffer *, qa_error *);
/* The enclosing content owner supplies its restored genuine inventory in the
 * same creation-slot order, independently of its reordered VFS search paths.
 * Complete decode precedes construction; no acquisition or random callback. */
bool qa_q3_pak_references_restore(qa_bytes, const qa_q3_pak_entry *const *, size_t,
    uint32_t checksum_feed, qa_q3_pak_random_fn, void *, qa_q3_pak_references **empty, qa_error *);

#endif
