/*
 * Port of tests/content/catalog/archive-mutation.test.ts: seeded byte
 * mutants of a small PK3 and PAK either read back or fail with a caught
 * error. The TS "foreign" class (a non-Error throw or a TypeError from a
 * missed bounds check) has no C counterpart; its C analogue is a crash or an
 * uncaught abort, which ends this process, so surviving every mutant with only
 * named engine errors is the check. The generator and mutator follow
 * tests/support/mutation.ts operation for operation. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "qa/content/archive.h"
#include "qa/foundation/support.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

typedef struct buffer {
  uint8_t *bytes;
  size_t length;
} buffer;

static void append(buffer *out, const void *bytes, size_t length) {
  out->bytes = realloc(out->bytes, out->length + length + 1);
  if (out->bytes == NULL) abort();
  if (length > 0) memcpy(out->bytes + out->length, bytes, length);
  out->length += length;
}

static void put16(uint8_t *at, uint32_t value) {
  at[0] = (uint8_t)value;
  at[1] = (uint8_t)(value >> 8);
}
static void put32(uint8_t *at, uint32_t value) {
  put16(at, value & 0xffffu);
  put16(at + 2, value >> 16);
}

/* tests/support/mutation.ts seededRandom (mulberry32). */
typedef struct seeded_random {
  uint32_t state;
} seeded_random;

static double next_random(seeded_random *random) {
  random->state += 0x6d2b79f5u;
  uint32_t value = random->state;
  value = (value ^ (value >> 15)) * (value | 1u);
  value ^= value + (value ^ (value >> 7)) * (value | 61u);
  return (double)(value ^ (value >> 14)) / 4294967296.0;
}

static size_t pick(seeded_random *random, size_t limit) {
  return (size_t)(next_random(random) * (double)limit);
}

/* tests/support/mutation.ts mutate. */
static buffer mutate(const buffer *source, seeded_random *random) {
  buffer bytes = {0};
  append(&bytes, source->bytes, source->length);
  switch (pick(random, 6)) {
    case 0:
      if (bytes.length > 0) {
        size_t at = pick(random, bytes.length);
        bytes.bytes[at] ^= (uint8_t)(1u << pick(random, 8));
      }
      return bytes;
    case 1:
      for (size_t count = 1 + pick(random, 4); count > 0 && bytes.length > 0; count--) {
        size_t at = pick(random, bytes.length);
        bytes.bytes[at] = (uint8_t)pick(random, 256);
      }
      return bytes;
    case 2:
      bytes.length = pick(random, bytes.length + 1);
      return bytes;
    case 3: {
      size_t at = pick(random, bytes.length + 1);
      size_t extra_count = 1 + pick(random, 8);
      uint8_t extra[8];
      for (size_t i = 0; i < extra_count; i++) extra[i] = (uint8_t)pick(random, 256);
      buffer out = {0};
      append(&out, bytes.bytes, at);
      append(&out, extra, extra_count);
      append(&out, bytes.bytes + at, bytes.length - at);
      free(bytes.bytes);
      return out;
    }
    case 4: {
      size_t at = pick(random, bytes.length + 1);
      size_t room = bytes.length - at < 8 ? bytes.length - at : 8;
      size_t cut = pick(random, room + 1);
      memmove(bytes.bytes + at, bytes.bytes + at + cut, bytes.length - at - cut);
      bytes.length -= cut;
      return bytes;
    }
    default:
      if (bytes.length >= 4) {
        static const uint32_t words[5] = {0xffffffffu, 0x7fffffffu, 0x80000000u, 0, 0x00010000u};
        size_t at = pick(random, bytes.length - 3);
        put32(bytes.bytes + at, words[pick(random, 5)]);
      }
      return bytes;
  }
}

typedef struct member {
  const char *name;
  const uint8_t *data;
  size_t length;
  bool deflate;
} member;

static buffer deflate_raw(const uint8_t *data, size_t length) {
  z_stream stream;
  memset(&stream, 0, sizeof stream);
  if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
    abort();
  buffer out = {0};
  out.bytes = malloc(compressBound((uLong)length) + 16);
  if (out.bytes == NULL) abort();
  stream.next_in = (Bytef *)(uintptr_t)data;
  stream.avail_in = (uInt)length;
  stream.next_out = out.bytes;
  stream.avail_out = (uInt)(compressBound((uLong)length) + 16);
  if (deflate(&stream, Z_FINISH) != Z_STREAM_END) abort();
  out.length = stream.total_out;
  deflateEnd(&stream);
  return out;
}

/* archive-mutation.test.ts zip(). */
static buffer build_zip(const member *members, size_t count) {
  buffer parts = {0}, central = {0};
  uint32_t offset = 0;
  for (size_t i = 0; i < count; i++) {
    size_t name_length = strlen(members[i].name);
    buffer body = members[i].deflate ? deflate_raw(members[i].data, members[i].length) : (buffer){0};
    if (!members[i].deflate) append(&body, members[i].data, members[i].length);
    uint32_t method = members[i].deflate ? 8 : 0;
    uint32_t crc = (uint32_t)crc32(0, members[i].data, (uInt)members[i].length);
    uint8_t local[30] = {0};
    put32(local, 0x04034b50u);
    put16(local + 4, 20);
    put16(local + 8, method);
    put32(local + 14, crc);
    put32(local + 18, (uint32_t)body.length);
    put32(local + 22, (uint32_t)members[i].length);
    put16(local + 26, (uint32_t)name_length);
    uint8_t header[46] = {0};
    put32(header, 0x02014b50u);
    put16(header + 4, 20);
    put16(header + 6, 20);
    put16(header + 10, method);
    put32(header + 16, crc);
    put32(header + 20, (uint32_t)body.length);
    put32(header + 24, (uint32_t)members[i].length);
    put16(header + 28, (uint32_t)name_length);
    put32(header + 42, offset);
    append(&parts, local, sizeof local);
    append(&parts, members[i].name, name_length);
    append(&parts, body.bytes, body.length);
    append(&central, header, sizeof header);
    append(&central, members[i].name, name_length);
    offset += (uint32_t)(30 + name_length + body.length);
    free(body.bytes);
  }
  uint8_t end[22] = {0};
  put32(end, 0x06054b50u);
  put16(end + 8, (uint32_t)count);
  put16(end + 10, (uint32_t)count);
  put32(end + 12, (uint32_t)central.length);
  put32(end + 16, offset);
  append(&parts, central.bytes, central.length);
  append(&parts, end, sizeof end);
  free(central.bytes);
  return parts;
}

/* archive-mutation.test.ts pak(). */
static buffer build_pak(const member *members, size_t count) {
  buffer body = {0};
  uint8_t *directory = calloc(count, 64);
  if (directory == NULL) abort();
  uint32_t offset = 12;
  for (size_t i = 0; i < count; i++) {
    memcpy(directory + i * 64, members[i].name, strlen(members[i].name));
    put32(directory + i * 64 + 56, offset);
    put32(directory + i * 64 + 60, (uint32_t)members[i].length);
    append(&body, members[i].data, members[i].length);
    offset += (uint32_t)members[i].length;
  }
  uint8_t header[12] = {'P', 'A', 'C', 'K'};
  put32(header + 4, (uint32_t)(12 + body.length));
  put32(header + 8, (uint32_t)(64 * count));
  buffer out = {0};
  append(&out, header, sizeof header);
  append(&out, body.bytes, body.length);
  append(&out, directory, 64 * count);
  free(body.bytes);
  free(directory);
  return out;
}

typedef struct read_job {
  const buffer *bytes;
  qa_archive_handle *archive;
  size_t total;
} read_job;

static void read_all_run(void *raw) {
  read_job *job = raw;
  job->archive = qa_decode_archive(job->bytes->bytes, job->bytes->length, false,
                                   QA_ARCHIVE_FORMAT_PAK, NULL);
  if (job->archive == NULL) abort(); /* OOM */
  for (size_t i = 0; i < job->archive->entry_count; i++) {
    size_t length = 0;
    uint8_t *data = qa_archive_read_entry_sync_ordinal(job->archive, job->archive->entries[i].ordinal, &length);
    free(data);
    job->total += length;
  }
}

/* readAll: the summed entry lengths, or -1 with *caught filled. */
static int64_t read_all(const buffer *bytes, qa_caught_error *caught) {
  read_job job = {bytes, NULL, 0};
  bool ok = qa_try(read_all_run, &job, caught);
  if (job.archive != NULL) qa_archive_destroy(job.archive);
  return ok ? (int64_t)job.total : -1;
}

static bool engine_error(const char *name) {
  return strcmp(name, "ArchiveError") == 0 || strcmp(name, "BinaryError") == 0 ||
         strcmp(name, "RangeError") == 0 || strcmp(name, "Error") == 0;
}

static void run_mutations(uint32_t seed, int count, const buffer *valid) {
  seeded_random random = {seed};
  int rejected = 0, foreign = 0;
  for (int index = 0; index < count; index++) {
    buffer mutant = mutate(valid, &random);
    qa_caught_error caught;
    if (read_all(&mutant, &caught) < 0) {
      rejected++;
      if (!engine_error(caught.name)) {
        foreign++;
        fprintf(stderr, "seed %u mutant %d: %s: %s\n", seed, index, caught.name, caught.message);
      }
    }
    free(mutant.bytes);
  }
  CHECK(foreign == 0);
  CHECK(rejected > 0);
}

int main(void) {
  static const char sentence[] = "a pk3 member with enough repeated text to deflate ";
  size_t sentence_length = sizeof sentence - 1;
  uint8_t text[8 * 64];
  for (int i = 0; i < 8; i++) memcpy(text + (size_t)i * sentence_length, sentence, sentence_length);
  size_t text_length = 8 * sentence_length;

  member zip_members[2] = {{"maps/a.bsp", text, text_length, true},
                           {"scripts/b.shader", text, 40, false}};
  member pak_members[2] = {{"maps/a.bsp", text, text_length, false},
                           {"progs.dat", text, 9, false}};
  buffer zip = build_zip(zip_members, 2);
  buffer pak = build_pak(pak_members, 2);

  /* "valid fixture archives read back before any mutation". */
  qa_caught_error caught;
  CHECK(read_all(&zip, &caught) == (int64_t)(text_length + 40));
  CHECK(read_all(&pak, &caught) == (int64_t)(text_length + 9));

  /* "seeded mutants of a pk3 / PAK either read or fail with a typed error". */
  for (uint32_t seed = 21; seed <= 23; seed++) run_mutations(seed, 500, &zip);
  for (uint32_t seed = 31; seed <= 32; seed++) run_mutations(seed, 500, &pak);

  free(zip.bytes);
  free(pak.bytes);
  printf("test_archive_mutation: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
