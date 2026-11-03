/* Port of src/media/ogg.ts. GPL-2.0-or-later. */
#include "qa/media/ogg.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS plain `throw new Error(msg)`. */
static noreturn void media_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static uint32_t checksum_table[256];
static bool checksum_ready = false;
static void checksum_init(void) {
  if (checksum_ready) return;
  for (uint32_t index = 0; index < 256; index++) {
    uint32_t value = index << 24;
    for (int bit = 0; bit < 8; bit++) {
      value = value & 0x80000000u ? (value << 1) ^ 0x04c11db7u : value << 1;
    }
    checksum_table[index] = value;
  }
  checksum_ready = true;
}

typedef struct page_ref {
  const uint8_t *data; /* borrowed from the input while decoding */
  size_t length;
} page_ref;

typedef struct logical_stream {
  uint32_t serial;
  uint32_t sequence;
  qa_ogg_packet *packets;
  size_t packet_count;
  size_t packet_capacity;
  page_ref *pages;
  size_t page_count;
  size_t page_capacity;
  uint8_t *pieces; /* owned pending packet bytes */
  size_t pieces_length;
  size_t pieces_capacity;
  size_t piece_count;
  bool ended;
} logical_stream;

void qa_ogg_movie_dispose(qa_ogg_movie *movie) {
  for (size_t i = 0; i < movie->video_count; i++) free(movie->video[i].data);
  free(movie->video);
  free(movie->audio);
  memset(movie, 0, sizeof *movie);
}

static void stream_dispose(logical_stream *stream) {
  for (size_t i = 0; i < stream->packet_count; i++) {
    free(stream->packets[i].data);
  }
  free(stream->packets);
  free(stream->pages);
  free(stream->pieces);
  memset(stream, 0, sizeof *stream);
}

static bool grow_packets(logical_stream *stream) {
  if (stream->packet_count < stream->packet_capacity) return true;
  size_t grown = stream->packet_capacity == 0 ? 8 : stream->packet_capacity * 2;
  qa_ogg_packet *packets =
      (qa_ogg_packet *)realloc(stream->packets, grown * sizeof *packets);
  if (packets == NULL) return false;
  stream->packets = packets;
  stream->packet_capacity = grown;
  return true;
}

static bool grow_pages(logical_stream *stream) {
  if (stream->page_count < stream->page_capacity) return true;
  size_t grown = stream->page_capacity == 0 ? 8 : stream->page_capacity * 2;
  page_ref *pages = (page_ref *)realloc(stream->pages, grown * sizeof *pages);
  if (pages == NULL) return false;
  stream->pages = pages;
  stream->page_capacity = grown;
  return true;
}

static bool append_piece(logical_stream *stream, const uint8_t *data,
                         size_t length) {
  if (stream->pieces_length + length > stream->pieces_capacity) {
    size_t grown = stream->pieces_capacity == 0 ? 4096
                                                : stream->pieces_capacity;
    while (grown < stream->pieces_length + length) grown *= 2;
    uint8_t *pieces = (uint8_t *)realloc(stream->pieces, grown);
    if (pieces == NULL) return false;
    stream->pieces = pieces;
    stream->pieces_capacity = grown;
  }
  memcpy(stream->pieces + stream->pieces_length, data, length);
  stream->pieces_length += length;
  stream->piece_count++;
  return true;
}

static uint32_t read_u32_be(const uint8_t *bytes, size_t offset) {
  return ((uint32_t)bytes[offset] << 24) | ((uint32_t)bytes[offset + 1] << 16) |
         ((uint32_t)bytes[offset + 2] << 8) | bytes[offset + 3];
}
static uint32_t read_u32_le(const uint8_t *bytes, size_t offset) {
  return (uint32_t)bytes[offset] | ((uint32_t)bytes[offset + 1] << 8) |
         ((uint32_t)bytes[offset + 2] << 16) |
         ((uint32_t)bytes[offset + 3] << 24);
}
static int64_t read_i64_le(const uint8_t *bytes, size_t offset) {
  uint64_t value = 0;
  for (int i = 7; i >= 0; i--) value = (value << 8) | bytes[offset + (size_t)i];
  int64_t result;
  memcpy(&result, &value, sizeof result);
  return result;
}

bool qa_decode_ogg_movie(const uint8_t *bytes, size_t length,
                         qa_ogg_movie *out) {
  memset(out, 0, sizeof *out);
  if (length > QA_MAX_OGG_MOVIE_BYTES) {
    qa_range_error("Ogg movie exceeds 512 MiB");
  }
  checksum_init();
  logical_stream streams[8];
  memset(streams, 0, sizeof streams);
  size_t stream_count = 0;
  bool oom = false;
  size_t offset = 0;
  while (offset < length && !oom) {
    if (offset + 27 > length || read_u32_be(bytes, offset) != 0x4f676753u ||
        bytes[offset + 4] != 0) {
      media_error("Invalid or truncated Ogg page");
    }
    uint8_t flags = bytes[offset + 5];
    uint8_t segments = bytes[offset + 26];
    if ((flags & ~7) != 0 || offset + 27 + segments > length) {
      media_error("Invalid Ogg page flags/lacing");
    }
    const uint8_t *lacing = bytes + offset + 27;
    size_t payload_start = offset + 27 + segments;
    size_t payload_bytes = 0;
    for (int i = 0; i < segments; i++) payload_bytes += lacing[i];
    size_t end = payload_start + payload_bytes;
    if (end > length) media_error("Truncated Ogg page payload");
    uint32_t crc = 0;
    for (size_t position = offset; position < end; position++) {
      uint8_t byte = position >= offset + 22 && position < offset + 26
                         ? 0
                         : bytes[position];
      crc = (crc << 8) ^ checksum_table[((crc >> 24) ^ byte) & 255u];
    }
    if (crc != read_u32_le(bytes, offset + 22)) {
      media_error("Ogg page checksum mismatch");
    }
    uint32_t serial = read_u32_le(bytes, offset + 14);
    uint32_t sequence = read_u32_le(bytes, offset + 18);
    logical_stream *stream = NULL;
    for (size_t i = 0; i < stream_count; i++) {
      if (streams[i].serial == serial) stream = &streams[i];
    }
    if (stream == NULL) {
      if ((flags & 2) == 0 || sequence != 0 || stream_count >= 8) {
        media_error("Invalid Ogg logical stream start");
      }
      stream = &streams[stream_count++];
      stream->serial = serial;
    } else if ((flags & 2) != 0 || stream->ended) {
      media_error("Ogg logical stream restarted after admission");
    }
    if (sequence != stream->sequence++ ||
        ((flags & 1) != 0) != (stream->piece_count != 0)) {
      media_error("Ogg packet sequence discontinuity");
    }
    if (!grow_pages(stream)) {
      oom = true;
      break;
    }
    stream->pages[stream->page_count].data = bytes + offset;
    stream->pages[stream->page_count].length = end - offset;
    stream->page_count++;
    size_t cursor = payload_start;
    int64_t last_completed = -1;
    for (int i = 0; i < segments && !oom; i++) {
      size_t part = lacing[i];
      if (!append_piece(stream, bytes + cursor, part)) {
        oom = true;
        break;
      }
      cursor += part;
      if (stream->pieces_length > (size_t)16 * 1024 * 1024) {
        media_error("Ogg packet exceeds 16 MiB");
      }
      if (part < 255) {
        if (!grow_packets(stream)) {
          oom = true;
          break;
        }
        qa_ogg_packet *packet = &stream->packets[stream->packet_count];
        packet->data = stream->pieces;
        packet->length = stream->pieces_length;
        packet->first = stream->packet_count == 0;
        packet->last = false;
        packet->granule = -1;
        packet->index = (int64_t)stream->packet_count;
        stream->packet_count++;
        last_completed = packet->index;
        stream->pieces = NULL;
        stream->pieces_length = 0;
        stream->pieces_capacity = 0;
        stream->piece_count = 0;
      }
    }
    if (oom) break;
    if (last_completed >= 0) {
      qa_ogg_packet *packet = &stream->packets[last_completed];
      packet->granule = read_i64_le(bytes, offset + 6);
      packet->last = (flags & 4) != 0;
    }
    if ((flags & 4) != 0) {
      if (stream->piece_count != 0) {
        media_error("Ogg stream ends inside a packet");
      }
      stream->ended = true;
    }
    offset = end;
  }
  int video_stream = -1;
  int audio_stream = -1;
  if (!oom) {
    for (size_t s = 0; s < stream_count; s++) {
      logical_stream *stream = &streams[s];
      if (!stream->ended) media_error("Truncated Ogg logical stream");
      if (stream->packet_count == 0) {
        media_error("Empty Ogg logical stream");
      }
      const uint8_t *first = stream->packets[0].data;
      size_t first_length = stream->packets[0].length;
      bool is_theora = first_length >= 7 && first[0] == 0x80 &&
                       memcmp(first + 1, "theora", 6) == 0;
      bool is_vorbis = first_length >= 7 && first[0] == 1 &&
                       memcmp(first + 1, "vorbis", 6) == 0;
      if (is_theora) {
        if (video_stream >= 0) {
          media_error("Multiple Theora video streams are unsupported");
        }
        video_stream = (int)s;
      } else if (is_vorbis) {
        if (audio_stream >= 0) {
          media_error("Multiple Vorbis soundtracks are unsupported");
        }
        audio_stream = (int)s;
      }
    }
    if (video_stream < 0 || streams[video_stream].packet_count < 4) {
      media_error("Ogg movie has no complete Theora video");
    }
  }
  if (!oom && audio_stream >= 0) {
    logical_stream *stream = &streams[audio_stream];
    size_t total = 0;
    for (size_t i = 0; i < stream->page_count; i++) {
      total += stream->pages[i].length;
    }
    out->audio = (uint8_t *)malloc(total == 0 ? 1 : total);
    if (out->audio == NULL) {
      oom = true;
    } else {
      size_t at = 0;
      for (size_t i = 0; i < stream->page_count; i++) {
        memcpy(out->audio + at, stream->pages[i].data,
               stream->pages[i].length);
        at += stream->pages[i].length;
      }
      out->audio_length = total;
    }
  }
  if (!oom) {
    out->video = streams[video_stream].packets;
    out->video_count = streams[video_stream].packet_count;
    streams[video_stream].packets = NULL;
    streams[video_stream].packet_count = 0;
  }
  for (size_t s = 0; s < stream_count; s++) stream_dispose(&streams[s]);
  if (oom) {
    qa_ogg_movie_dispose(out);
    return false;
  }
  return true;
}
