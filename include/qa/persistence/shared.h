/*
 * Port of src/persistence/shared.ts: boundary readers for digests,
 * times, vectors, frames, clocks, numeric profiles, random state,
 * and frame ordering. Returned structs borrow strings/bytes from
 * the checkpoint tree; list fields (words, providers) are malloc'd
 * arrays the caller frees (elements stay borrowed). NULL array with
 * nonzero count is impossible: allocation failure aborts via the
 * reader. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_SHARED_H
#define QA_PERSISTENCE_SHARED_H

#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/time.h"
#include "qa/persistence/value.h"

qa_content_digest qa_read_digest(const qa_save_reader *reader);
qa_source_time qa_read_time(const qa_save_reader *reader);
qa_vec3 qa_read_vector(const qa_save_reader *reader);
qa_bounds qa_read_bounds(const qa_save_reader *reader);
qa_frame_context qa_read_frame(const qa_save_reader *reader);
qa_clock_profile qa_read_clock(const qa_save_reader *reader);
qa_arithmetic_profile qa_read_arithmetic(const qa_save_reader *reader);
qa_numeric_profile qa_read_numeric(const qa_save_reader *reader);
qa_random_state qa_read_random(const qa_save_reader *reader);
qa_frame_ordering qa_read_ordering(const qa_save_reader *reader);

#endif
