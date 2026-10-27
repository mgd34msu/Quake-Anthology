/*
 * Port of src/core/binary/index.ts (BinaryError, BinaryReader,
 * BinaryWriter). This index.ts is the module itself, not a re-export
 * barrel, so it ports as a plain file pair. C accommodations:
 *  - The thrown BinaryError prints "BinaryError: <source>:<offset>:
 *    <message>" (the TS Error string form) and aborts; RangeError
 *    throws use qa_range_error, as elsewhere in the port.
 *  - Offsets and lengths are int64_t so the TS safe-integer range
 *    checks (including negative inputs) reproduce exactly.
 *  - Uint8Array copies become malloc'd buffers the caller frees;
 *    dataView becomes a borrowed pointer into the reader's storage
 *    (sections share storage, byte reads copy, as in the TS).
 *  - The reader borrows its byte storage (mutable, like a Uint8Array)
 *    and owns a copy of its source name; destroy frees only the name.
 *  - fixedString validates UTF-8 like the TS fatal TextDecoder; the
 *    decoder's TypeError aborts with the oracle runtime's message.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_BINARY_INDEX_H
#define QA_CORE_BINARY_INDEX_H

#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

noreturn void qa_binary_error(const char *source, int64_t offset,
                              const char *format, ...);

typedef struct qa_binary_reader {
  uint8_t *data; /* borrowed */
  size_t length;
  char *source; /* owned */
  size_t cursor;
} qa_binary_reader;

/* `source` NULL means "<buffer>". Returns 0 only on allocation failure. */
int qa_binary_reader_init(qa_binary_reader *reader, uint8_t *data,
                          size_t length, const char *source);
void qa_binary_reader_destroy(qa_binary_reader *reader);

size_t qa_binary_reader_offset(const qa_binary_reader *reader);
size_t qa_binary_reader_length(const qa_binary_reader *reader);
size_t qa_binary_reader_remaining(const qa_binary_reader *reader);

void qa_binary_reader_seek(qa_binary_reader *reader, int64_t offset);
void qa_binary_reader_skip(qa_binary_reader *reader, int64_t length);
uint8_t qa_binary_reader_u8(qa_binary_reader *reader);
int8_t qa_binary_reader_i8(qa_binary_reader *reader);
uint16_t qa_binary_reader_u16(qa_binary_reader *reader);
int16_t qa_binary_reader_i16(qa_binary_reader *reader);
uint32_t qa_binary_reader_u32(qa_binary_reader *reader);
int32_t qa_binary_reader_i32(qa_binary_reader *reader);
float qa_binary_reader_f32(qa_binary_reader *reader);
double qa_binary_reader_f64(qa_binary_reader *reader);
float qa_binary_reader_finite_f32(qa_binary_reader *reader);
/* Malloc'd copy of the next `length` bytes; caller frees. Length 0
 * may return NULL. Allocation failure also returns NULL. */
uint8_t *qa_binary_reader_bytes(qa_binary_reader *reader, int64_t length);
/* Malloc'd NUL-terminated strings; caller frees, NULL on OOM. */
char *qa_binary_reader_fixed_string(qa_binary_reader *reader, int64_t length);
char *qa_binary_reader_fixed_byte_string(qa_binary_reader *reader,
                                         int64_t length);
/* Borrowed checked view into the reader's storage. */
uint8_t *qa_binary_reader_data_view(qa_binary_reader *reader, int64_t offset,
                                    int64_t length);
/* Fills `out` with a sub-reader sharing storage. 0 only on OOM. */
int qa_binary_reader_section(const qa_binary_reader *reader, int64_t offset,
                             int64_t length, qa_binary_reader *out);
void qa_binary_reader_expect_magic(qa_binary_reader *reader, const char *magic);
int qa_binary_reader_records(const qa_binary_reader *reader, int64_t offset,
                             int64_t length, int64_t stride,
                             qa_binary_reader *out);

typedef struct qa_binary_writer {
  uint8_t *data; /* owned */
  size_t capacity;
  size_t cursor;
} qa_binary_writer;

/* Returns 0 only on allocation failure; invalid capacity aborts. */
int qa_binary_writer_init(qa_binary_writer *writer, int64_t capacity);
void qa_binary_writer_destroy(qa_binary_writer *writer);
size_t qa_binary_writer_offset(const qa_binary_writer *writer);

void qa_binary_writer_u8(qa_binary_writer *writer, int64_t value);
void qa_binary_writer_i8(qa_binary_writer *writer, int64_t value);
void qa_binary_writer_u16(qa_binary_writer *writer, int64_t value);
void qa_binary_writer_i16(qa_binary_writer *writer, int64_t value);
void qa_binary_writer_u32(qa_binary_writer *writer, int64_t value);
void qa_binary_writer_i32(qa_binary_writer *writer, int64_t value);
void qa_binary_writer_f32(qa_binary_writer *writer, double value);
void qa_binary_writer_f64(qa_binary_writer *writer, double value);
void qa_binary_writer_bytes(qa_binary_writer *writer, const uint8_t *value,
                            size_t length);
/* Malloc'd copy of the written prefix; caller frees. *out_length set. */
uint8_t *qa_binary_writer_finish(const qa_binary_writer *writer,
                                 size_t *out_length);

#endif
