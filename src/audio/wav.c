/* Port of src/audio/wav.ts. See include/qa/audio/wav.h.
 * GPL-2.0-or-later. */
#include "qa/audio/wav.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct chunk_range {
  int64_t offset;
  int64_t length;
} chunk_range;

typedef struct wav_format {
  int64_t sample_rate;
  int channels;
  int bytes_per_sample;
  int64_t block_align;
} wav_format;

static noreturn void reject(const char *source, int64_t offset,
                            const char *format, ...) {
  va_list args;
  va_start(args, format);
  /* qa_binary_error is variadic; forward via a small buffer. */
  char message[512];
  vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_binary_error(source, offset, "%s", message);
}

static void four_cc(qa_binary_reader *reader, char out[5]) {
  for (int i = 0; i < 4; i++) out[i] = (char)qa_binary_reader_u8(reader);
  out[4] = '\0';
}

static void expect_four_cc(qa_binary_reader *reader, const char *expected) {
  size_t offset = qa_binary_reader_offset(reader);
  char actual[5];
  four_cc(reader, actual);
  if (strcmp(actual, expected) != 0)
    reject(reader->source, (int64_t)offset,
           "expected \"%s\", got \"%s\"", expected, actual);
}

static bool matches_four_cc(qa_binary_reader *reader, int64_t offset,
                            const char *expected) {
  qa_binary_reader_seek(reader, offset);
  for (int i = 0; i < 4; i++)
    if (qa_binary_reader_u8(reader) != (uint8_t)expected[i]) return false;
  return true;
}

static bool find_chunk(qa_binary_reader *reader, int64_t start, int64_t length,
                       const char *name, chunk_range *out) {
  int64_t offset = start;
  while (offset < length) {
    if (offset + 8 > (int64_t)reader->length)
      reject(reader->source, offset + 4, "truncated WAV chunk header");
    qa_binary_reader_seek(reader, offset + 4);
    int32_t chunk_length = qa_binary_reader_i32(reader);
    if (chunk_length < 0) return false;
    if (chunk_length == 0x7fffffff)
      reject(reader->source, offset + 4,
             "WAV chunk alignment overflows a signed int");
    if (matches_four_cc(reader, offset, name)) {
      out->offset = offset + 8;
      out->length = chunk_length;
      return true;
    }
    offset += 8 + ((chunk_length + 1) & ~1);
  }
  return false;
}

static int64_t pcm_sample(qa_binary_reader *reader, int64_t data_offset,
                          int64_t bytes_per_sample, int64_t index) {
  qa_binary_reader_seek(reader,
                        data_offset + index * (bytes_per_sample == 2 ? 2 : 1));
  return bytes_per_sample == 2
             ? qa_binary_reader_i16(reader)
             : ((int64_t)qa_binary_reader_u8(reader) - 128) * 256; /* TS (u8 - 128) << 8 without shifting a negative */
}

static bool pcm_samples(qa_binary_reader *reader, int64_t data_offset,
                        int64_t bytes_per_sample, int64_t sample_count,
                        int16_t **out) {
  if (sample_count < 0)
    reject(reader->source, data_offset, "negative WAV sample allocation");
  int16_t *samples = NULL;
  if (sample_count > 0) {
    samples = malloc((size_t)sample_count * sizeof *samples);
    if (samples == NULL) return false;
  }
  for (int64_t i = 0; i < sample_count; i++)
    samples[i] = (int16_t)pcm_sample(reader, data_offset, bytes_per_sample, i);
  *out = samples;
  return true;
}

void qa_pcm_sound_dispose(qa_pcm_sound *sound) {
  if (sound->samples_owned) free(sound->samples);
  sound->samples = NULL;
  sound->samples_owned = false;
  sound->sample_count = 0;
}

void qa_decoded_wav_dispose(qa_decoded_wav *sound) {
  qa_pcm_sound_dispose(&sound->pcm);
}

bool qa_read_wav_info(qa_wav_info *info, uint8_t *bytes, size_t byte_length,
                      int64_t length, const char *source,
                      qa_wav_print_fn print_fn, void *print_ctx) {
  memset(info, 0, sizeof *info);
  if (!qa_binary_reader_init(&info->reader, bytes, byte_length, source))
    return false;
  info->source = info->reader.source;
  const char *src = info->reader.source;
  if (length < 0 || length > (int64_t)info->reader.length)
    reject(src, 0, "WAV file length %lld exceeds its physical allocation",
           (long long)length);
  chunk_range riff;
  if (!find_chunk(&info->reader, 0, length, "RIFF", &riff) ||
      !matches_four_cc(&info->reader, riff.offset, "WAVE")) {
    print_fn("Missing RIFF/WAVE chunks\n", print_ctx);
    return true;
  }
  int64_t chunk_start = riff.offset + 4;
  chunk_range format;
  if (!find_chunk(&info->reader, chunk_start, length, "fmt ", &format)) {
    print_fn("Missing fmt chunk\n", print_ctx);
    return true;
  }
  qa_binary_reader_seek(&info->reader, format.offset);
  int64_t encoding = qa_binary_reader_i16(&info->reader);
  info->channels = qa_binary_reader_i16(&info->reader);
  info->sample_rate = qa_binary_reader_i32(&info->reader);
  qa_binary_reader_skip(&info->reader, 6);
  info->source_bytes_per_sample =
      (int64_t)(qa_binary_reader_i16(&info->reader) / 8);
  if (encoding != 1) {
    print_fn("Microsoft PCM format only\n", print_ctx);
    return true;
  }
  chunk_range data;
  if (!find_chunk(&info->reader, chunk_start, length, "data", &data)) {
    print_fn("Missing data chunk\n", print_ctx);
    return true;
  }
  if (info->source_bytes_per_sample == 0)
    reject(src, data.offset - 4, "WAV sample count divides by zero source width");
  info->frame_count = data.length / info->source_bytes_per_sample;
  info->data_offset = data.offset;
  return true;
}

void qa_wav_info_destroy(qa_wav_info *info) {
  qa_binary_reader_destroy(&info->reader);
  info->source = NULL;
}

int64_t qa_wav_info_sample(qa_wav_info *info, int64_t index) {
  return pcm_sample(&info->reader, info->data_offset,
                    info->source_bytes_per_sample, index);
}

bool qa_wav_info_decode(qa_wav_info *info, qa_pcm_sound *out) {
  if (info->channels != 1)
    reject(info->reader.source, info->data_offset,
           "source WAV sound decoding requires mono channels");
  memset(out, 0, sizeof *out);
  if (!pcm_samples(&info->reader, info->data_offset,
                   info->source_bytes_per_sample, info->frame_count,
                   &out->samples))
    return false;
  out->samples_owned = true;
  out->sample_rate = info->sample_rate;
  out->channels = (int)info->channels;
  out->frame_count = info->frame_count;
  out->sample_count = (size_t)info->frame_count;
  out->loop_start = -1;
  return true;
}

static void parse_format(qa_binary_reader *reader, const char *source,
                         int64_t chunk_offset, wav_format *out) {
  if (reader->length < 16)
    reject(source, chunk_offset, "fmt chunk is %zu bytes, expected at least 16",
           reader->length);
  uint32_t encoding = qa_binary_reader_u16(reader);
  if (encoding != 1)
    reject(source, chunk_offset,
           "unsupported WAV encoding %u; only PCM is supported", encoding);
  uint32_t channels = qa_binary_reader_u16(reader);
  if (channels != 1 && channels != 2)
    reject(source, chunk_offset + 2, "unsupported WAV channel count %u", channels);
  uint32_t sample_rate = qa_binary_reader_u32(reader);
  if (sample_rate == 0)
    reject(source, chunk_offset + 4, "WAV sample rate must be positive");
  uint32_t byte_rate = qa_binary_reader_u32(reader);
  uint32_t block_align = qa_binary_reader_u16(reader);
  uint32_t bits = qa_binary_reader_u16(reader);
  if (bits != 8 && bits != 16 && bits != 24)
    reject(source, chunk_offset + 14, "unsupported WAV sample width %u bits", bits);
  int bytes_per_sample = bits == 8 ? 1 : bits == 16 ? 2 : 3;
  uint32_t expected_align = channels * (uint32_t)bytes_per_sample;
  if (block_align != expected_align)
    reject(source, chunk_offset + 12,
           "WAV block alignment %u does not match %u", block_align, expected_align);
  uint32_t expected_rate = sample_rate * block_align;
  if (byte_rate != expected_rate)
    reject(source, chunk_offset + 8, "WAV byte rate %u does not match %u",
           byte_rate, expected_rate);
  out->sample_rate = sample_rate;
  out->channels = (int)channels;
  out->bytes_per_sample = bytes_per_sample;
  out->block_align = block_align;
}

static int64_t parse_cue_loop(qa_binary_reader *reader, const char *source,
                              int64_t chunk_offset, bool *has) {
  *has = false;
  if (reader->length < 4) reject(source, chunk_offset, "truncated WAV cue chunk");
  uint32_t cue_count = qa_binary_reader_u32(reader);
  uint64_t required = 4 + (uint64_t)cue_count * 24;
  if (required > reader->length)
    reject(source, chunk_offset, "truncated WAV cue-point records");
  if (cue_count == 0) return 0;
  qa_binary_reader_skip(reader, 20);
  *has = true;
  return (int64_t)qa_binary_reader_u32(reader);
}

static int64_t parse_sampler_loop(qa_binary_reader *reader, const char *source,
                                  int64_t chunk_offset, bool source_sentinel,
                                  bool *has) {
  *has = false;
  if (reader->length < 36)
    reject(source, chunk_offset, "truncated WAV sampler chunk");
  qa_binary_reader_seek(reader, 28);
  uint32_t loop_count = qa_binary_reader_u32(reader);
  uint64_t required = 36 + (uint64_t)loop_count * 24;
  if (required > reader->length)
    reject(source, chunk_offset, "truncated WAV sampler-loop records");
  if (loop_count == 0) return 0;
  qa_binary_reader_seek(reader, 40);
  uint32_t type = qa_binary_reader_u32(reader);
  uint32_t start = qa_binary_reader_u32(reader);
  uint32_t end = qa_binary_reader_u32(reader);
  if (source_sentinel && type == 255 && start == 0xffffffffu &&
      end == 0xffffffffu)
    return 0;
  *has = true;
  return (int64_t)start;
}

static bool decode_pcm(const uint8_t *bytes, size_t length, const char *source,
                       bool source_signed_chunks, bool read_loops,
                       qa_decoded_wav *out) {
  memset(out, 0, sizeof *out);
  qa_binary_reader reader;
  /* Reader borrows mutable storage; decoding never writes through it. */
  if (!qa_binary_reader_init(&reader, (uint8_t *)(uintptr_t)bytes, length,
                             source))
    return false;
  const char *src = reader.source;
  bool ok = false;
  if (reader.length < 12) reject(src, 0, "truncated RIFF/WAVE header");
  expect_four_cc(&reader, "RIFF");
  uint32_t riff_size = qa_binary_reader_u32(&reader);
  if (riff_size < 4) reject(src, 4, "invalid RIFF size %u", riff_size);
  uint64_t riff_end =
      source_signed_chunks ? reader.length : 8ull + riff_size;
  if (!source_signed_chunks && riff_end > reader.length)
    reject(src, 4, "RIFF size %u exceeds %zu-byte input", riff_size,
           reader.length);
  expect_four_cc(&reader, "WAVE");
  bool have_format = false, have_data = false;
  wav_format format = {0};
  chunk_range data = {0};
  bool has_cue = false, has_sampler = false;
  int64_t cue_loop = 0, sampler_loop = 0;
  while ((uint64_t)qa_binary_reader_offset(&reader) < riff_end) {
    int64_t header_offset = (int64_t)qa_binary_reader_offset(&reader);
    if (riff_end - (uint64_t)header_offset < 8)
      reject(src, header_offset, "truncated WAV chunk header");
    char chunk_id[5];
    four_cc(&reader, chunk_id);
    uint32_t chunk_length = qa_binary_reader_u32(&reader);
    if (source_signed_chunks && chunk_length > 0x7fffffffu) break;
    int64_t chunk_offset = (int64_t)qa_binary_reader_offset(&reader);
    if ((uint64_t)chunk_offset > riff_end - chunk_length) {
      if (source_signed_chunks && have_format && have_data &&
          strcmp(chunk_id, "LIST") == 0 && riff_end - (uint64_t)chunk_offset >= 4 &&
          matches_four_cc(&reader, chunk_offset, "INFO"))
        break;
      reject(src, header_offset + 4, "WAV chunk \"%s\" exceeds RIFF bounds",
             chunk_id);
    }
    uint64_t chunk_end = (uint64_t)chunk_offset + chunk_length;
    uint64_t next = chunk_end < riff_end ? chunk_end + (chunk_length & 1) : chunk_end;
    if (strcmp(chunk_id, "fmt ") == 0 && !have_format) {
      qa_binary_reader section;
      if (!qa_binary_reader_section(&reader, chunk_offset, chunk_length, &section))
        goto done;
      parse_format(&section, src, chunk_offset, &format);
      qa_binary_reader_destroy(&section);
      have_format = true;
    } else if (strcmp(chunk_id, "data") == 0 && !have_data) {
      data.offset = chunk_offset;
      data.length = chunk_length;
      have_data = true;
    } else if (read_loops && strcmp(chunk_id, "cue ") == 0 && !has_cue) {
      qa_binary_reader section;
      if (!qa_binary_reader_section(&reader, chunk_offset, chunk_length, &section))
        goto done;
      cue_loop = parse_cue_loop(&section, src, chunk_offset, &has_cue);
      qa_binary_reader_destroy(&section);
    } else if (read_loops && strcmp(chunk_id, "smpl") == 0 && !has_sampler) {
      qa_binary_reader section;
      if (!qa_binary_reader_section(&reader, chunk_offset, chunk_length, &section))
        goto done;
      sampler_loop =
          parse_sampler_loop(&section, src, chunk_offset, source_signed_chunks,
                             &has_sampler);
      qa_binary_reader_destroy(&section);
    }
    qa_binary_reader_seek(&reader, (int64_t)next);
  }
  if (!have_format) reject(src, 12, "missing WAV fmt chunk");
  if (!have_data) reject(src, 12, "missing WAV data chunk");
  if (data.length % format.block_align != 0)
    reject(src, data.offset,
           "WAV data length %lld is not a multiple of block alignment %lld",
           (long long)data.length, (long long)format.block_align);
  int64_t frame_count = data.length / format.block_align;
  int64_t sample_count = frame_count * format.channels;
  bool has_loop = has_cue || has_sampler;
  int64_t loop_start = has_cue ? cue_loop : sampler_loop;
  int16_t *samples = NULL;
  if (format.bytes_per_sample == 3) {
    if (sample_count > 0) {
      samples = malloc((size_t)sample_count * sizeof *samples);
      if (samples == NULL) goto done;
    }
    for (int64_t i = 0; i < sample_count; i++) {
      qa_binary_reader_seek(&reader, data.offset + i * 3 + 1);
      samples[i] = qa_binary_reader_i16(&reader);
    }
  } else if (!pcm_samples(&reader, data.offset, format.bytes_per_sample,
                          sample_count, &samples)) {
    goto done;
  }
  if (has_loop && loop_start >= frame_count) {
    free(samples);
    reject(src, data.offset, "WAV loop start %lld is outside %lld frames",
           (long long)loop_start, (long long)frame_count);
  }
  out->source_bytes_per_sample = format.bytes_per_sample;
  out->pcm.sample_rate = format.sample_rate;
  out->pcm.channels = format.channels;
  out->pcm.samples = samples;
  out->pcm.samples_owned = true;
  out->pcm.sample_count = (size_t)sample_count;
  out->pcm.frame_count = frame_count;
  out->pcm.loop_start = has_loop ? loop_start : -1;
  ok = true;
done:
  qa_binary_reader_destroy(&reader);
  return ok;
}

bool qa_decode_wav(const uint8_t *bytes, size_t length, const char *source,
                   qa_decoded_wav *out) {
  return decode_pcm(bytes, length, source, false, true, out);
}

bool qa_decode_q3_wav(const uint8_t *bytes, size_t length, const char *source,
                      qa_decoded_wav *out) {
  return decode_pcm(bytes, length, source, true, false, out);
}

bool qa_decode_quake_wav(const uint8_t *bytes, size_t length,
                         const char *source, qa_decoded_wav *out) {
  if (!decode_pcm(bytes, length, source, true, true, out)) return false;
  if (out->pcm.loop_start < 0) return true;
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, (uint8_t *)(uintptr_t)bytes, length,
                             source)) {
    qa_decoded_wav_dispose(out);
    return false;
  }
  const char *src = reader.source;
  bool cue_seen = false, has_loop_end = false;
  int64_t loop_end = 0;
  qa_binary_reader_seek(&reader, 12);
  uint32_t riff_size = 0;
  memcpy(&riff_size, bytes + 4, 4); /* host is little-endian (DataView LE) */
  /* Re-read LE explicitly to stay endian-correct. */
  riff_size = (uint32_t)bytes[4] | ((uint32_t)bytes[5] << 8) |
              ((uint32_t)bytes[6] << 16) | ((uint32_t)bytes[7] << 24);
  uint64_t riff_end = 8ull + riff_size;
  while ((uint64_t)qa_binary_reader_offset(&reader) + 8 <= riff_end) {
    char name[5];
    four_cc(&reader, name);
    uint32_t chunk_length = qa_binary_reader_u32(&reader);
    int64_t start = (int64_t)qa_binary_reader_offset(&reader);
    if (chunk_length > 0x7fffffffu) break;
    if ((uint64_t)start > riff_end - chunk_length &&
        strcmp(name, "LIST") == 0 && riff_end - (uint64_t)start >= 4 &&
        matches_four_cc(&reader, start, "INFO"))
      break;
    if (strcmp(name, "cue ") == 0) cue_seen = true;
    if (cue_seen && strcmp(name, "LIST") == 0 && chunk_length >= 24) {
      qa_binary_reader list;
      if (!qa_binary_reader_section(&reader, start, chunk_length, &list)) {
        qa_binary_reader_destroy(&reader);
        qa_decoded_wav_dispose(out);
        return false;
      }
      qa_binary_reader_seek(&list, 20);
      char mark[5];
      four_cc(&list, mark);
      if (strcmp(mark, "mark") == 0) {
        qa_binary_reader_seek(&list, 16);
        loop_end = out->pcm.loop_start + (int64_t)qa_binary_reader_u32(&list);
        has_loop_end = true;
        qa_binary_reader_destroy(&list);
        break;
      }
      qa_binary_reader_destroy(&list);
    }
    uint64_t next = (uint64_t)start + chunk_length + (chunk_length & 1);
    qa_binary_reader_seek(&reader,
                          (int64_t)(next < riff_end ? next : riff_end));
  }
  int64_t end_offset = (int64_t)qa_binary_reader_offset(&reader);
  qa_binary_reader_destroy(&reader);
  if (!has_loop_end) return true;
  if (loop_end <= out->pcm.loop_start || loop_end > out->pcm.frame_count)
    qa_binary_error(src, end_offset, "Sound Forge loop end outside WAV frames");
  out->pcm.frame_count = loop_end;
  out->pcm.sample_count = (size_t)(loop_end * out->pcm.channels);
  return true;
}

/* ---- Quiet validation for the qa_try_decode_* entry points ----
 * The aborting decoder's reader cannot be caught, so decodability is
 * decided by replaying decode_pcm's checks over the raw bytes with
 * explicit bounds. A passing input is then decoded by the aborting
 * entry point, which succeeds on it (false there only means OOM). */

static uint16_t load_u16le(const uint8_t *bytes, uint64_t offset) {
  return (uint16_t)(bytes[offset] | (uint16_t)bytes[offset + 1] << 8);
}

static uint32_t load_u32le(const uint8_t *bytes, uint64_t offset) {
  return (uint32_t)bytes[offset] | (uint32_t)bytes[offset + 1] << 8 |
         (uint32_t)bytes[offset + 2] << 16 |
         (uint32_t)bytes[offset + 3] << 24;
}

static bool tag_at(const uint8_t *bytes, uint64_t offset, const char *tag) {
  return memcmp(bytes + offset, tag, 4) == 0;
}

/* matches_four_cc as the reader runs it: a byte is only read while
 * every earlier byte matched, so running out of input mid-match of a
 * matching prefix is a decode failure, not a mismatch. */
typedef enum tag_match { TAG_MATCH, TAG_MISMATCH, TAG_TRUNCATED } tag_match;

static tag_match tag_match_at(const uint8_t *bytes, size_t length,
                              uint64_t offset, const char *tag) {
  if (offset > length) return TAG_TRUNCATED; /* the seek itself fails */
  for (int i = 0; i < 4; i++) {
    if (offset + (uint64_t)i >= length) return TAG_TRUNCATED;
    if (bytes[offset + (uint64_t)i] != (uint8_t)tag[i]) return TAG_MISMATCH;
  }
  return TAG_MATCH;
}

typedef struct valid_pcm {
  bool has_loop;
  int64_t loop_start;
  int64_t frame_count;
} valid_pcm;

/* parse_format's checks over the section at offset (length bytes are
 * inside the input). False mirrors its rejects. */
static bool valid_format(const uint8_t *bytes, uint64_t offset,
                         uint32_t length, wav_format *out) {
  if (length < 16) return false;
  if (load_u16le(bytes, offset) != 1) return false;
  uint32_t channels = load_u16le(bytes, offset + 2);
  if (channels != 1 && channels != 2) return false;
  uint32_t sample_rate = load_u32le(bytes, offset + 4);
  if (sample_rate == 0) return false;
  uint32_t byte_rate = load_u32le(bytes, offset + 8);
  uint32_t block_align = load_u16le(bytes, offset + 12);
  uint32_t bits = load_u16le(bytes, offset + 14);
  if (bits != 8 && bits != 16 && bits != 24) return false;
  int bytes_per_sample = bits == 8 ? 1 : bits == 16 ? 2 : 3;
  if (block_align != channels * (uint32_t)bytes_per_sample) return false;
  if (byte_rate != sample_rate * block_align) return false;
  out->sample_rate = sample_rate;
  out->channels = (int)channels;
  out->bytes_per_sample = bytes_per_sample;
  out->block_align = block_align;
  return true;
}

static bool valid_cue_loop(const uint8_t *bytes, uint64_t offset,
                           uint32_t length, bool *has, int64_t *out) {
  *has = false;
  if (length < 4) return false;
  uint32_t cue_count = load_u32le(bytes, offset);
  if (4ull + (uint64_t)cue_count * 24 > length) return false;
  if (cue_count == 0) return true;
  *has = true;
  *out = (int64_t)load_u32le(bytes, offset + 24);
  return true;
}

static bool valid_sampler_loop(const uint8_t *bytes, uint64_t offset,
                               uint32_t length, bool source_sentinel,
                               bool *has, int64_t *out) {
  *has = false;
  if (length < 36) return false;
  uint32_t loop_count = load_u32le(bytes, offset + 28);
  if (36ull + (uint64_t)loop_count * 24 > length) return false;
  if (loop_count == 0) return true;
  uint32_t type = load_u32le(bytes, offset + 40);
  uint32_t start = load_u32le(bytes, offset + 44);
  uint32_t end = load_u32le(bytes, offset + 48);
  if (source_sentinel && type == 255 && start == 0xffffffffu &&
      end == 0xffffffffu)
    return true;
  *has = true;
  *out = (int64_t)start;
  return true;
}

/* decode_pcm's decision procedure. The samples themselves always
 * decode once the data range passes, as in the aborting code. */
static bool validate_decode_pcm(const uint8_t *bytes, size_t length,
                                bool source_signed_chunks, bool read_loops,
                                valid_pcm *out) {
  if (length < 12) return false;
  if (!tag_at(bytes, 0, "RIFF")) return false;
  uint32_t riff_size = load_u32le(bytes, 4);
  if (riff_size < 4) return false;
  uint64_t riff_end = source_signed_chunks ? length : 8ull + riff_size;
  if (!source_signed_chunks && riff_end > length) return false;
  if (!tag_at(bytes, 8, "WAVE")) return false;
  bool have_format = false, have_data = false;
  wav_format format = {0};
  chunk_range data = {0};
  bool has_cue = false, has_sampler = false;
  int64_t cue_loop = 0, sampler_loop = 0;
  uint64_t offset = 12;
  while (offset < riff_end) {
    if (riff_end - offset < 8) return false;
    uint32_t chunk_length = load_u32le(bytes, offset + 4);
    if (source_signed_chunks && chunk_length > 0x7fffffffu) break;
    uint64_t chunk_offset = offset + 8;
    if (chunk_offset > riff_end - chunk_length) {
      if (source_signed_chunks && have_format && have_data &&
          tag_at(bytes, offset, "LIST") && riff_end - chunk_offset >= 4) {
        tag_match info = tag_match_at(bytes, length, chunk_offset, "INFO");
        if (info == TAG_TRUNCATED) return false;
        if (info == TAG_MATCH) break;
      }
      return false;
    }
    uint64_t chunk_end = chunk_offset + chunk_length;
    uint64_t next =
        chunk_end < riff_end ? chunk_end + (chunk_length & 1) : chunk_end;
    if (tag_at(bytes, offset, "fmt ") && !have_format) {
      if (!valid_format(bytes, chunk_offset, chunk_length, &format))
        return false;
      have_format = true;
    } else if (tag_at(bytes, offset, "data") && !have_data) {
      data.offset = (int64_t)chunk_offset;
      data.length = chunk_length;
      have_data = true;
    } else if (read_loops && tag_at(bytes, offset, "cue ") && !has_cue) {
      if (!valid_cue_loop(bytes, chunk_offset, chunk_length, &has_cue,
                          &cue_loop))
        return false;
    } else if (read_loops && tag_at(bytes, offset, "smpl") && !has_sampler) {
      if (!valid_sampler_loop(bytes, chunk_offset, chunk_length,
                              source_signed_chunks, &has_sampler,
                              &sampler_loop))
        return false;
    }
    offset = next;
  }
  if (!have_format || !have_data) return false;
  if (data.length % format.block_align != 0) return false;
  out->frame_count = data.length / format.block_align;
  out->has_loop = has_cue || has_sampler;
  out->loop_start = has_cue ? cue_loop : sampler_loop;
  if (out->has_loop && out->loop_start >= out->frame_count) return false;
  return true;
}

/* decodeQuakeWav's second LIST-mark pass, as the aborting reader runs
 * it (including its seeks and section bounds). */
static bool validate_quake_mark(const uint8_t *bytes, size_t length,
                                int64_t loop_start, int64_t frame_count) {
  uint64_t riff_end = 8ull + load_u32le(bytes, 4);
  bool cue_seen = false, has_loop_end = false;
  int64_t loop_end = 0;
  uint64_t offset = 12;
  while (offset + 8 <= riff_end) {
    if (offset + 8 > length) return false;
    uint32_t chunk_length = load_u32le(bytes, offset + 4);
    uint64_t start = offset + 8;
    if (chunk_length > 0x7fffffffu) break;
    if (start > riff_end - chunk_length && tag_at(bytes, offset, "LIST") &&
        riff_end - start >= 4) {
      tag_match info = tag_match_at(bytes, length, start, "INFO");
      if (info == TAG_TRUNCATED) return false;
      if (info == TAG_MATCH) break;
    }
    if (tag_at(bytes, offset, "cue ")) cue_seen = true;
    if (cue_seen && tag_at(bytes, offset, "LIST") && chunk_length >= 24) {
      if (start > length || chunk_length > length - start) return false;
      if (tag_at(bytes, start + 20, "mark")) {
        loop_end = loop_start + (int64_t)load_u32le(bytes, start + 16);
        has_loop_end = true;
        break;
      }
    }
    uint64_t next = start + chunk_length + (chunk_length & 1);
    if (next > riff_end) next = riff_end;
    if (next > length) return false; /* the seek to next fails */
    offset = next;
  }
  if (has_loop_end && (loop_end <= loop_start || loop_end > frame_count))
    return false;
  return true;
}

qa_wav_try_result qa_try_decode_wav(const uint8_t *bytes, size_t length,
                                    const char *source,
                                    qa_decoded_wav *out) {
  valid_pcm valid;
  if (!validate_decode_pcm(bytes, length, false, true, &valid)) {
    memset(out, 0, sizeof *out);
    return QA_WAV_TRY_INVALID;
  }
  return qa_decode_wav(bytes, length, source, out) ? QA_WAV_TRY_OK
                                                    : QA_WAV_TRY_OOM;
}

qa_wav_try_result qa_try_decode_q3_wav(const uint8_t *bytes, size_t length,
                                       const char *source,
                                       qa_decoded_wav *out) {
  valid_pcm valid;
  if (!validate_decode_pcm(bytes, length, true, false, &valid)) {
    memset(out, 0, sizeof *out);
    return QA_WAV_TRY_INVALID;
  }
  return qa_decode_q3_wav(bytes, length, source, out) ? QA_WAV_TRY_OK
                                                       : QA_WAV_TRY_OOM;
}

qa_wav_try_result qa_try_decode_quake_wav(const uint8_t *bytes, size_t length,
                                          const char *source,
                                          qa_decoded_wav *out) {
  valid_pcm valid;
  if (!validate_decode_pcm(bytes, length, true, true, &valid) ||
      (valid.has_loop &&
       !validate_quake_mark(bytes, length, valid.loop_start,
                            valid.frame_count))) {
    memset(out, 0, sizeof *out);
    return QA_WAV_TRY_INVALID;
  }
  return qa_decode_quake_wav(bytes, length, source, out) ? QA_WAV_TRY_OK
                                                          : QA_WAV_TRY_OOM;
}
