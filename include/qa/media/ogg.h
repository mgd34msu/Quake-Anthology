/*
 * Port of src/media/ogg.ts: Ogg framing (RFC 3533) packet lacing
 * and logical stream sequence ownership. Packet data and the
 * joined Vorbis page bytes are owned malloc'd buffers.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_OGG_H
#define QA_MEDIA_OGG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_ogg_packet {
  uint8_t *data; /* owned */
  size_t length;
  bool first;
  bool last;
  int64_t granule;
  int64_t index;
} qa_ogg_packet;

typedef struct qa_ogg_movie {
  qa_ogg_packet *video; /* owned array */
  size_t video_count;
  uint8_t *audio; /* owned joined Vorbis pages; NULL when absent */
  size_t audio_length;
} qa_ogg_movie;
void qa_ogg_movie_dispose(qa_ogg_movie *movie);

/* False only on OOM; malformed input aborts as thrown. */
/* MAX_OGG_MOVIE_BYTES: checked against the input size before the whole
 * file is read into memory. */
#define QA_MAX_OGG_MOVIE_BYTES ((size_t)512 * 1024 * 1024)
bool qa_decode_ogg_movie(const uint8_t *bytes, size_t length,
                         qa_ogg_movie *out);

#endif
