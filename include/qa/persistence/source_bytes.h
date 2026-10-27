/*
 * Port of src/persistence/source-bytes.ts. C accommodations: TS byte
 * strings are char* with explicit length where interior bytes matter;
 * read results are malloc'd (NULL only on OOM). textBytes takes
 * ptr+length and also returns its length (same as input length).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_SOURCE_BYTES_H
#define QA_PERSISTENCE_SOURCE_BYTES_H

#include <stddef.h>
#include <stdint.h>

#include "qa/core/binary/index.h"

/* Malloc'd byte string (length bytes, NUL-terminated for convenience;
 * interior NULs preserved in the length). NULL only on OOM. */
char *qa_byte_text(const uint8_t *bytes, size_t length);
/* Malloc'd bytes; charCodes above 255 abort (impossible for C bytes,
 * kept for parity with the TS check on unsigned-char input). The
 * input is ptr+length of byte values 0..255. NULL only on OOM. */
uint8_t *qa_text_bytes(const char *text, size_t length);
char *qa_read_cstring(qa_binary_reader *reader);
void qa_write_cstring(qa_binary_writer *writer, const char *value, size_t length);
char *qa_read_fixed(qa_binary_reader *reader, int64_t width);
void qa_write_fixed(qa_binary_writer *writer, const char *value, size_t length, int64_t width);

#endif
